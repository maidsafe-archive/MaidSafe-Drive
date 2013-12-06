/*  Copyright 2011 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#ifndef MAIDSAFE_DRIVE_DIRECTORY_HANDLER_H_
#define MAIDSAFE_DRIVE_DIRECTORY_HANDLER_H_

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "boost/algorithm/string/find.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/fstream.hpp"
#include "boost/filesystem/path.hpp"

#include "maidsafe/common/error.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/profiler.h"
#include "maidsafe/common/rsa.h"
#include "maidsafe/common/types.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/data_types/data_type_values.h"
#include "maidsafe/data_types/mutable_data.h"

#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/file_context.h"

namespace maidsafe {

namespace drive {

namespace detail {

namespace test { class DirectoryHandlerTest; }

template <typename Storage>
class DirectoryHandler {
 public:
  DirectoryHandler(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
                   const Identity& root_parent_id, const boost::filesystem::path& disk_buffer_path,
                   bool create);
  ~DirectoryHandler();

  void Add(const boost::filesystem::path& relative_path, FileContext&& file_context);
  Directory* Get(const boost::filesystem::path& relative_path);
  void FlushAll();
  void Delete(const boost::filesystem::path& relative_path);
  void Rename(const boost::filesystem::path& old_relative_path,
              const boost::filesystem::path& new_relative_path);
  void HandleDataPoppedFromBuffer(const boost::filesystem::path& relative_path,
                                  const std::string& name, const NonEmptyString& content) const;

  Identity root_parent_id() const { return root_parent_id_; }

  friend class test::DirectoryHandlerTest;

 private:
  DirectoryHandler();
  DirectoryHandler(const DirectoryHandler&);
  DirectoryHandler(DirectoryHandler&&);
  DirectoryHandler& operator=(const DirectoryHandler);

  bool IsDirectory(const FileContext& file_context) const;
  std::pair<Directory*, FileContext*> GetParent(const boost::filesystem::path& relative_path);
  void RenameSameParent(const boost::filesystem::path& old_relative_path,
                        const boost::filesystem::path& new_relative_path);
  void RenameDifferentParent(const boost::filesystem::path& old_relative_path,
                             const boost::filesystem::path& new_relative_path);
  void Put(Directory* directory, StoreDelay delay = StoreDelay::kApply);
  ImmutableData SerialiseDirectory(Directory* directory) const;
  std::unique_ptr<Directory> GetFromStorage(const ParentId& parent_id,
                                            const DirectoryId& directory_id) const;
  std::unique_ptr<Directory> ParseDirectory(
      const ImmutableData& encrypted_data_map, const ParentId& parent_id,
      const DirectoryId& directory_id,
      std::vector<StructuredDataVersions::VersionName> versions) const;
  void DeleteOldestVersion(Directory* directory);
  void DeleteAllVersions(Directory* directory);

  std::shared_ptr<Storage> storage_;
  Identity unique_user_id_, root_parent_id_;
  mutable detail::FileContext::Buffer disk_buffer_;
  std::function<NonEmptyString(const std::string&)> get_chunk_from_store_;
  mutable std::mutex cache_mutex_;
  std::map<boost::filesystem::path, std::unique_ptr<Directory>> cache_;
};

// ==================== Implementation details ====================================================
template <typename Storage>
DirectoryHandler<Storage>::DirectoryHandler(std::shared_ptr<Storage> storage,
                                            const Identity& unique_user_id,
                                            const Identity& root_parent_id,
                                            const boost::filesystem::path& disk_buffer_path,
                                            bool create)
    : storage_(storage),
      unique_user_id_(unique_user_id),
      root_parent_id_(root_parent_id),
      // All chunks of serialised dirs should comfortably have been stored well before being popped
      // out of buffer, so allow pop_functor to be a no-op.
      disk_buffer_(MemoryUsage(Concurrency() * 1024 * 1024), DiskUsage(30 * 1024 * 1024),
                   [](const std::string&, const NonEmptyString&) {}, disk_buffer_path),
      get_chunk_from_store_(),
      cache_mutex_(),
      cache_() {
  if (!unique_user_id.IsInitialised())
    ThrowError(CommonErrors::uninitialised);
  if (!root_parent_id.IsInitialised())
    ThrowError(CommonErrors::uninitialised);
  get_chunk_from_store_ = [this](const std::string& name)->NonEmptyString {
    auto chunk(storage_->Get(ImmutableData::Name(Identity(name))).get());
    return chunk.data();
  };
  if (create) {
    // TODO(Fraser#5#): 2013-12-05 - Fill 'root_file_context' attributes appropriately.
    FileContext root_file_context(kRoot, true);
    std::unique_ptr<Directory> root_parent(
        new Directory(ParentId(unique_user_id_), root_parent_id));
    std::unique_ptr<Directory> root(
        new Directory(ParentId(root_parent_id), *root_file_context.meta_data.directory_id));
    root_file_context.parent = root_parent.get();
    root_parent->AddChild(std::move(root_file_context));
    root->MarkAsChanged();
    Put(root_parent.get(), StoreDelay::kIgnore);
    Put(root.get(), StoreDelay::kIgnore);
    cache_[""] = std::move(root_parent);
    cache_[kRoot] = std::move(root);
  } else {
    cache_[""] = GetFromStorage(ParentId(unique_user_id_), root_parent_id_);
  }
}

template <typename Storage>
DirectoryHandler<Storage>::~DirectoryHandler() {
  FlushAll();
}

template <typename Storage>
void DirectoryHandler<Storage>::Add(const boost::filesystem::path& relative_path,
                                    FileContext&& file_context) {
  SCOPED_PROFILE
  auto parent(GetParent(relative_path));
  assert(parent.first && parent.second);

  if (IsDirectory(file_context)) {
    std::unique_ptr<Directory> directory(new Directory(ParentId(parent.first->directory_id()),
                                                       *file_context.meta_data.directory_id));
    Put(directory.get());
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_[relative_path] = std::move(directory);
  }

  parent.second->meta_data.UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent.second->meta_data.attributes.st_ctime = parent.second->meta_data.attributes.st_mtime;
  if (IsDirectory(file_context)) {
    ++parent.second->meta_data.attributes.st_nlink;
    parent.second->parent->MarkAsChanged();
  }
#endif

  // TODO(Fraser#5#): 2013-11-28 - Use on_scope_exit or similar to undo changes if AddChild throws.
  parent.first->AddChild(std::move(file_context));
  Put(parent.first);
}

template <typename Storage>
Directory* DirectoryHandler<Storage>::Get(const boost::filesystem::path& relative_path) {
  SCOPED_PROFILE
  Directory* parent(nullptr);
  boost::filesystem::path antecedent;
  {  // NOLINT
    std::lock_guard<std::mutex> lock(cache_mutex_);
    // Try to find the exact directory
    auto itr(cache_.find(relative_path));
    if (itr != std::end(cache_))
      return itr->second.get();

    // Locate the first antecedent in cache
    antecedent = relative_path;
    while (itr == std::end(cache_) && !antecedent.empty()) {
      antecedent = antecedent.parent_path();
      itr = cache_.find(antecedent);
    }
    assert(itr != std::end(cache_));
    parent = itr->second.get();
  }

  // Recover the decendent directories until we reach the target
  const FileContext* file_context(nullptr);
  auto path_itr(std::begin(relative_path));
  std::advance(path_itr, std::distance(std::begin(antecedent), std::end(antecedent)));
  while (path_itr != std::end(relative_path)) {
    if (path_itr == std::begin(relative_path)) {
      file_context = parent->GetChild(kRoot);
      antecedent = kRoot;
    } else {
      file_context = parent->GetChild(*path_itr);
      antecedent = (antecedent / *path_itr).make_preferred();
    }

    if (!file_context->meta_data.directory_id)
      ThrowError(CommonErrors::invalid_parameter);
    auto directory(GetFromStorage(ParentId(parent->directory_id()),
                                  *file_context->meta_data.directory_id));
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      parent = directory.get();
      auto insertion_result(cache_.emplace(antecedent, std::move(directory)));
      assert(insertion_result.second);
    }
    ++path_itr;
  }
  return parent;
}

//template <typename Storage>
//void DirectoryHandler<Storage>::PutVersion(const boost::filesystem::path& relative_path) {
//  auto directory(Get(relative_path));
//  assert(directory);
//  Put(directory);
//}

template <typename Storage>
void DirectoryHandler<Storage>::FlushAll() {
  SCOPED_PROFILE
  bool error(false);
  std::lock_guard<std::mutex> lock(cache_mutex_);
  for (auto& dir : cache_) {
    dir.second->ResetChildrenIterator();
    auto child(dir.second->GetChildAndIncrementItr());
    while (child) {
      if (child->self_encryptor && !child->self_encryptor->Flush()) {
        error = true;
        LOG(kError) << "Failed to flush " << (dir.first / child->meta_data.name);
      }
      child = dir.second->GetChildAndIncrementItr();
    }
    Put(dir.second.get(), StoreDelay::kIgnore);
  }
  if (error)
    ThrowError(CommonErrors::unknown);
}

template <typename Storage>
void DirectoryHandler<Storage>::Delete(const boost::filesystem::path& relative_path) {
  SCOPED_PROFILE
  auto parent(GetParent(relative_path));
  assert(parent.first && parent.second);

  auto file_context(parent.first->GetChild(relative_path.filename()));

  if (IsDirectory(*file_context)) {
    auto directory(Get(relative_path));
    DeleteAllVersions(directory);
    {  // NOLINT
      std::lock_guard<std::mutex> lock(cache_mutex_);
      cache_.erase(relative_path);
    }
  }

  parent.first->RemoveChild(relative_path.filename());
  parent.second->meta_data.UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent.second->meta_data.attributes.st_ctime = parent.second->meta_data.attributes.st_mtime;
  if (IsDirectory(*file_context))
    --parent.second->meta_data.attributes.st_nlink;
#endif

  Put(parent.first);
}

template <typename Storage>
void DirectoryHandler<Storage>::Rename(const boost::filesystem::path& old_relative_path,
                                       const boost::filesystem::path& new_relative_path) {
  SCOPED_PROFILE
  assert(old_relative_path != new_relative_path);

  if (old_relative_path.parent_path() == new_relative_path.parent_path()) {
    RenameSameParent(old_relative_path, new_relative_path);
    //std::lock_guard<std::mutex> lock(cache_mutex_);
    //auto itr(cache_.find(old_relative_path.parent_path()));
    //if (itr != std::end(cache_))
    //  cache_.erase(old_relative_path.parent_path());
  } else {
    RenameDifferentParent(old_relative_path, new_relative_path);
    //std::lock_guard<std::mutex> lock(cache_mutex_);
    //auto itr1(cache_.find(old_relative_path.parent_path()));
    //if (itr1 != std::end(cache_))
    //  cache_.erase(old_relative_path.parent_path());
    //auto itr2(cache_.find(new_relative_path.parent_path()));
    //if (itr2 != std::end(cache_))
    //  cache_.erase(new_relative_path.parent_path());
  }
  //if (IsDirectory(file_context)) {
  //  std::lock_guard<std::mutex> lock(cache_mutex_);
  //  auto itr(cache_.find(old_relative_path));
  //  if (itr != std::end(cache_))
  //    cache_.erase(old_relative_path);
  //}
}

template <typename Storage>
bool DirectoryHandler<Storage>::IsDirectory(const FileContext& file_context) const {
  return static_cast<bool>(file_context.meta_data.directory_id);
}

template <typename Storage>
std::pair<Directory*, FileContext*> DirectoryHandler<Storage>::GetParent(
    const boost::filesystem::path& relative_path) {
  auto grandparent(Get(relative_path.parent_path().parent_path()));
  auto parent_context(grandparent->GetMutableChild(relative_path.parent_path().filename()));
  if (!(parent_context->meta_data.directory_id))
    ThrowError(CommonErrors::invalid_parameter);
  return std::make_pair(Get(relative_path.parent_path()), parent_context);
}

template <typename Storage>
void DirectoryHandler<Storage>::RenameSameParent(const boost::filesystem::path& old_relative_path,
                                                 const boost::filesystem::path& new_relative_path) {
  auto parent(GetParent(old_relative_path));
  assert(parent.first && parent.second);
  parent.first->RenameChild(old_relative_path.filename(), new_relative_path.filename());

//#ifndef MAIDSAFE_WIN32
//  struct stat old;
//  old.st_ctime = meta_data.attributes.st_ctime;
//  old.st_mtime = meta_data.attributes.st_mtime;
//  time(&meta_data.attributes.st_mtime);
//  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
//#endif
//
//  if (!parent.first->HasChild(new_relative_path.filename())) {
//    parent.first->RemoveChild(meta_data);
//    meta_data.name = new_relative_path.filename();
//    parent.first->AddChild(meta_data);
//  } else {
//    MetaData old_meta_data;
//    try {
//      parent.first->GetChild(new_relative_path.filename(), old_meta_data);
//    }
//    catch (const std::exception& exception) {
//#ifndef MAIDSAFE_WIN32
//      meta_data.attributes.st_ctime = old.st_ctime;
//      meta_data.attributes.st_mtime = old.st_mtime;
//#endif
//      boost::throw_exception(exception);
//    }
//    parent.first->RemoveChild(old_meta_data);
//    parent.first->RemoveChild(meta_data);
//    meta_data.name = new_relative_path.filename();
//    parent.first->AddChild(meta_data);
//  }

#ifdef MAIDSAFE_WIN32
  //GetSystemTimeAsFileTime(&parent.second->meta_data.last_write_time);
  //parent.second->parent->MarkAsChanged();
#else
  //parent.second->attributes.st_ctime = parent.second->attributes.st_mtime =
  //    meta_data.attributes.st_mtime;
#endif
  Put(parent.first);
}

template <typename Storage>
void DirectoryHandler<Storage>::RenameDifferentParent(
    const boost::filesystem::path& old_relative_path,
    const boost::filesystem::path& new_relative_path) {
  auto old_parent(GetParent(old_relative_path));
  auto new_parent(GetParent(new_relative_path));
  assert(old_parent.first && old_parent.second && new_parent.first && new_parent.second);
  auto file_context(old_parent.first->RemoveChild(old_relative_path.filename()));

//#ifndef MAIDSAFE_WIN32
//  struct stat old;
//  old.st_ctime = meta_data.attributes.st_ctime;
//  old.st_mtime = meta_data.attributes.st_mtime;
//  time(&meta_data.attributes.st_mtime);
//  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
//#endif
  if (IsDirectory(file_context)) {
    auto directory(Get(old_relative_path));
    DeleteAllVersions(directory);
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto itr(cache_.find(old_relative_path));
      assert(itr != std::end(cache_));
      std::unique_ptr<Directory> temp(std::move(itr->second));
      temp->parent_id_ = ParentId(new_parent.first->directory_id());
      cache_.erase(itr);
      auto insertion_result(cache_.emplace(new_relative_path, std::move(temp)));
      assert(insertion_result.second);
      directory = insertion_result.first->second.get();
    }
    Put(directory);
  }

  new_parent.first->AddChild(std::move(file_context));

//  if (!new_parent.listing->HasChild(new_relative_path.filename())) {
//    meta_data.name = new_relative_path.filename();
//    new_parent.listing->AddChild(meta_data);
//  } else {
//    MetaData old_meta_data;
//    try {
//      new_parent.listing->GetChild(new_relative_path.filename(), old_meta_data);
//    }
//    catch(const std::exception& exception) {
//#ifndef MAIDSAFE_WIN32
//      meta_data.attributes.st_ctime = old.st_ctime;
//      meta_data.attributes.st_mtime = old.st_mtime;
//#endif
//      boost::throw_exception(exception);
//    }
//    new_parent.listing->RemoveChild(old_meta_data);
//    meta_data.name = new_relative_path.filename();
//    new_parent.listing->AddChild(meta_data);
//  }

#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&old_parent.second->meta_data.last_write_time);
#else
  //old_parent_meta_data.attributes.st_ctime = old_parent_meta_data.attributes.st_mtime =
  //    meta_data.attributes.st_mtime;
  //if (IsDirectory(file_context)) {
  //  --old_parent_meta_data.attributes.st_nlink;
  //  ++new_parent_meta_data.attributes.st_nlink;
  //  new_parent_meta_data.attributes.st_ctime = new_parent_meta_data.attributes.st_mtime =
  //      old_parent_meta_data.attributes.st_mtime;
  //}
#endif
  Put(old_parent.first);
  Put(new_parent.first);

#ifndef MAIDSAFE_WIN32
  //if (new_relative_path.parent_path() != old_relative_path.parent_path().parent_path()) {
  //  try {
  //    if (old_grandparent.listing)
  //      old_grandparent.listing->UpdateChild(old_parent_meta_data);
  //  }
  //  catch (const std::exception&) {}  // Non-critical
  //  Put(old_grandparent, old_relative_path.parent_path().parent_path());
  //}
#endif
}

template <typename Storage>
void DirectoryHandler<Storage>::Put(Directory* directory, StoreDelay delay) {
  if (!directory->NeedsToBeSaved(delay))
    return;

  // Should delete oldest version only when told to do so by return message from PutVersion.
  //try {
  //  DeleteOldestVersion(directory);
  //}
  //catch(const std::exception& e) {
  //  LOG(kError) << "Failed deleting oldest version of " << relative_path << ": " << e.what();
  //  assert(false);  // Should never fail.
  //}

  ImmutableData encrypted_data_map(SerialiseDirectory(directory));
  storage_->Put(encrypted_data_map);
  // TODO(Fraser#5#): 2013-12-03 - Change versions to not be typed.
  if (directory->versions_.empty()) {
    directory->versions_.emplace_back(0, encrypted_data_map.name());
    storage_->template PutVersion<MutableData>(MutableData::Name(directory->directory_id()),
                                               StructuredDataVersions::VersionName(),
                                               directory->versions_[0]);
  } else {
    directory->versions_.emplace_front(directory->versions_.front().index + 1,
                                       encrypted_data_map.name());
    auto itr(std::begin(directory->versions_));
    storage_->template PutVersion<MutableData>(MutableData::Name(directory->directory_id()),
                                              *(itr + 1), *itr);
  }
}

template <typename Storage>
ImmutableData DirectoryHandler<Storage>::SerialiseDirectory(Directory* directory) const {
  std::string serialised_directory(directory->Serialise());
  encrypt::DataMap data_map;
  {
    encrypt::SelfEncryptor self_encryptor(data_map, disk_buffer_, get_chunk_from_store_);
    assert(serialised_directory.size() <= std::numeric_limits<uint32_t>::max());
    if (!self_encryptor.Write(serialised_directory.c_str(),
                              static_cast<uint32_t>(serialised_directory.size()), 0)) {
      ThrowError(CommonErrors::invalid_parameter);
    }
  }
  for (const auto& chunk : data_map.chunks) {
    auto content(disk_buffer_.Get(chunk.hash));
    storage_->Put(ImmutableData(content));
  }
  auto encrypted_data_map_contents(encrypt::EncryptDataMap(directory->parent_id_,
                                                           directory->directory_id(), data_map));
  return ImmutableData(encrypted_data_map_contents);
}

template <typename Storage>
std::unique_ptr<Directory> DirectoryHandler<Storage>::GetFromStorage(
    const ParentId& parent_id, const DirectoryId& directory_id) const {
  auto version_tip_of_trees(
      storage_->template GetVersions<MutableData>(MutableData::Name(directory_id)).get());
  assert(!version_tip_of_trees.empty());
  if (version_tip_of_trees.size() != 1U) {
    // TODO(Fraser#5#): 2013-12-05 - Handle multiple branches (resolve conflicts if possible or
    //                  display all versions parsed as dirs to user and get them to choose a single
    //                  one to keep)
    version_tip_of_trees.resize(1);
  }
  auto versions(storage_->template GetBranch<MutableData>(MutableData::Name(directory_id),
                                                          version_tip_of_trees.front()).get());
  assert(!versions.empty());
  ImmutableData encrypted_data_map(storage_->Get(versions.front().id).get());
  return ParseDirectory(encrypted_data_map, parent_id, directory_id, std::move(versions));
}

template <typename Storage>
std::unique_ptr<Directory> DirectoryHandler<Storage>::ParseDirectory(
    const ImmutableData& encrypted_data_map, const ParentId& parent_id,
    const DirectoryId& directory_id,
    std::vector<StructuredDataVersions::VersionName> versions) const {
  auto data_map(encrypt::DecryptDataMap(parent_id.data, directory_id,
                                        encrypted_data_map.data().string()));
  encrypt::SelfEncryptor self_encryptor(data_map, disk_buffer_, get_chunk_from_store_);
  uint32_t data_map_size(static_cast<uint32_t>(data_map.size()));
  std::string serialised_listing(data_map_size, 0);

  if (!self_encryptor.Read(const_cast<char*>(serialised_listing.c_str()), data_map_size, 0))
    ThrowError(CommonErrors::parsing_error);

  std::unique_ptr<Directory> directory(new Directory(parent_id, serialised_listing,
                                                     std::move(versions)));
  assert(directory->directory_id() == directory_id);
  return std::move(directory);
}

template <typename Storage>
void DirectoryHandler<Storage>::DeleteOldestVersion(Directory* /*directory*/) {
  // storage_->GetBranch
  // if versions > directory->max_versions_ {
  //   dir_data = storage_->Get(ImmutableData::Name(oldest_version))
  //   data_map = encrypt::DecryptDataMap(parent_id, directory_id, dir_data);
  //   use SelfEncryptor to decrypt data_map into a new temp directory
  //   iterate all children, deleting all chunks from all data_maps
  //   delete all chunks from dir data_map
  // }
}

template <typename Storage>
void DirectoryHandler<Storage>::DeleteAllVersions(Directory* /*directory*/) {
}

template <typename Storage>
void DirectoryHandler<Storage>::HandleDataPoppedFromBuffer(
    const boost::filesystem::path& relative_path, const std::string& name,
    const NonEmptyString& content) const {
  // NOTE, This will be executed on a different thread to the one writing to the encryptor which has
  // triggered this call.  We therefore can't safely access any non-threadsafe class members here.
  LOG(kWarning) << "Chunk " << HexSubstr(name) << " has been popped from the buffer for "
                << relative_path;
  // TODO(Fraser#5#): 2013-11-27 - Handle mutex-protecting the file_contexts, so we can log that
  // we're storing this chunk to the network.  If it's only a temporary chunk (i.e. it's not still
  // listed in the datamap when we get the next flush/close call), we should delete it from the
  // network again.
  ImmutableData data(content);
  assert(data.name()->string() == name);
//  storage_->Put(data);
  ThrowError(CommonErrors::file_too_large);
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_HANDLER_H_
