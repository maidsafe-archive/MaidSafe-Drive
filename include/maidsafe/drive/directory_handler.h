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
  //typedef std::shared_ptr<DirectoryListing> DirectoryListingPtr;
  //typedef encrypt::SelfEncryptor<Storage> SelfEncryptor;
  //typedef encrypt::DataMap DataMap;

  DirectoryHandler(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
                   const Identity& root_parent_id, bool create);
  virtual ~DirectoryHandler() {}

  void Add(const boost::filesystem::path& relative_path, FileContext&& file_context);
  Directory* Get(const boost::filesystem::path& relative_path);
  // Puts a new version of 'relative_path' (which must be a directory) to storage.
  void PutVersion(const boost::filesystem::path& relative_path);
  void FlushAll();









  void Delete(const boost::filesystem::path& relative_path);
  void Rename(const boost::filesystem::path& old_relative_path,
              const boost::filesystem::path& new_relative_path, MetaData& meta_data);
  void UpdateParent(const boost::filesystem::path& parent_path, const MetaData& meta_data);
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
                        const boost::filesystem::path& new_relative_path, MetaData& meta_data);
  void RenameDifferentParent(const boost::filesystem::path& old_relative_path,
                             const boost::filesystem::path& new_relative_path,
                             MetaData& meta_data);
  Directory Get(const ParentId& parent_id, const DirectoryId& directory_id) const;
  void Put(const Directory& directory, const boost::filesystem::path& relative_path);
  void Delete(const Directory& directory);
  encrypt::DataMap GetDataMap(const ParentId& parent_id, const DirectoryId& directory_id) const;

  std::shared_ptr<Storage> storage_;
  Identity unique_user_id_, root_parent_id_;
  mutable std::mutex cache_mutex_;
  std::map<boost::filesystem::path, Directory> cache_;
};

// ==================== Implementation details ====================================================
template <typename Storage>
DirectoryHandler<Storage>::DirectoryHandler(std::shared_ptr<Storage> storage,
                                            const Identity& unique_user_id,
                                            const Identity& root_parent_id, bool create)
    : storage_(storage),
      unique_user_id_(unique_user_id),
      root_parent_id_(root_parent_id),
      cache_mutex_(),
      cache_() {
  if (!unique_user_id.IsInitialised())
    ThrowError(CommonErrors::uninitialised);
  if (!root_parent_id.IsInitialised())
    ThrowError(CommonErrors::uninitialised);
  if (create) {
    MetaData root_meta_data(kRoot, true);
    Directory root_parent, root;
    DirectoryListingPtr root_parent_listing(std::make_shared<DirectoryListing>(root_parent_id_)),
                        root_listing(std::make_shared<DirectoryListing>(
                            *root_meta_data.directory_id));
    root_parent.parent_id = unique_user_id_;
    root_parent.listing = root_parent_listing;
    root_parent.listing->AddChild(root_meta_data);
    root.parent_id = root_parent_id_;
    root.listing = root_listing;
    Put(root_parent, kRoot.parent_path());
    Put(root, kRoot);
  }
}

template <typename Storage>
void DirectoryHandler<Storage>::Add(const boost::filesystem::path& relative_path,
                                    FileContext&& file_context) {
  SCOPED_PROFILE
  auto parent(GetParent(relative_path));
  assert(parent.first && parent.second);

  if (IsDirectory(file_context)) {
    Directory directory(parent.first->directory_id(),
                        std::make_shared<DirectoryListing>(*meta_data.directory_id));
    Put(directory, relative_path);
  }

  parent.second->UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent.second->attributes.st_ctime = parent.second->attributes.st_mtime;
  if (IsDirectory(file_context))
    ++parent.second->attributes.st_nlink;
#endif

  // TODO(Fraser#5#): 2013-11-28 - Use on_scope_exit or similar to undo changes if AddChild throws.
  parent.first->AddChild(std::move(file_context));
  Put(parent, relative_path.parent_path());
}

template <typename Storage>
Directory* DirectoryHandler<Storage>::Get(const boost::filesystem::path& relative_path) {
  SCOPED_PROFILE
  {  // NOLINT
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto itr(cache_.find(relative_path));
    if (itr != std::end(cache_))
      return &(itr->second);
  }
  Directory directory(Get(unique_user_id_, root_parent_id_));
  MetaData meta_data;
  bool found_root = false;
  auto end(std::end(relative_path));
  for (auto itr(std::begin(relative_path)); itr != end; ++itr) {
    if (itr == std::begin(relative_path)) {
      directory.listing->GetChild(kRoot, meta_data);
      found_root = true;
    } else {
      directory.listing->GetChild(*itr, meta_data);
    }

    if (!meta_data.directory_id)
      ThrowError(CommonErrors::invalid_parameter);
    directory = Get(directory.listing->directory_id(), *meta_data.directory_id);
    if (found_root)
      found_root = false;
  }

  std::lock_guard<std::mutex> lock(cache_mutex_);
  auto insertion_result(cache_.emplace(relative_path, directory));
  assert(insertion_result.second);
  return &(insertion_result.first->second);
}

template <typename Storage>
void DirectoryHandler<Storage>::PutVersion(const boost::filesystem::path& relative_path) {
  auto directory(Get(relative_path));
  assert(directory);
  Put(*directory, relative_path);
}

template <typename Storage>
void DirectoryHandler<Storage>::FlushAll() {
  SCOPED_PROFILE
  bool error(false);
  std::lock_guard<std::mutex> lock(cache_mutex_);
  for (const auto& dir : cache_) {
    dir.second.ResetChildrenIterator();
    auto child(dir.second.GetChildAndIncrementItr());
    while (child) {
      if (child->self_encryptor && !child->self_encryptor->Flush()) {
        error = true;
        LOG(kError) << "Failed to flush " << (dir.first / child->meta_data.name);
      }
      child = dir.second.GetChildAndIncrementItr();
    }
    Put(dir.second, dir.first);
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
    Delete(directory);here!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    {  // NOLINT
      std::lock_guard<std::mutex> lock(cache_mutex_);
      cache_.erase(relative_path);
    }
  } else {
    //SelfEncryptor(meta_data.data_map, *storage_).DeleteAllChunks();
  }

  parent.first->RemoveChild(meta_data);
  parent.second->UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent.second->attributes.st_ctime = parent.second->attributes.st_mtime;
  if (IsDirectory(file_context))
    --parent.second->attributes.st_nlink;
#endif

  try {
    if (grandparent.listing)
      grandparent.listing->UpdateChild(parent_meta_data);
  }
  catch (...) {/*Non-critical*/
  }

#ifndef MAIDSAFE_WIN32
  Put(grandparent, relative_path.parent_path().parent_path());
#endif
  Put(parent, relative_path.parent_path());
}

template <typename Storage>
void DirectoryHandler<Storage>::Rename(const boost::filesystem::path& old_relative_path,
                                       const boost::filesystem::path& new_relative_path,
                                       MetaData& meta_data) {
  SCOPED_PROFILE
  if (old_relative_path == new_relative_path)
    return;

  if (old_relative_path.parent_path() == new_relative_path.parent_path()) {
    RenameSameParent(old_relative_path, new_relative_path, meta_data);
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto itr(cache_.find(old_relative_path.parent_path()));
    if (itr != std::end(cache_))
      cache_.erase(old_relative_path.parent_path());
  } else {
    RenameDifferentParent(old_relative_path, new_relative_path, meta_data);
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto itr1(cache_.find(old_relative_path.parent_path()));
    if (itr1 != std::end(cache_))
      cache_.erase(old_relative_path.parent_path());
    auto itr2(cache_.find(new_relative_path.parent_path()));
    if (itr2 != std::end(cache_))
      cache_.erase(new_relative_path.parent_path());
  }
  if (IsDirectory(file_context)) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto itr(cache_.find(old_relative_path));
    if (itr != std::end(cache_))
      cache_.erase(old_relative_path);
  }
}

template <typename Storage>
void DirectoryHandler<Storage>::UpdateParent(const boost::filesystem::path& parent_path,
                                             const MetaData& meta_data) {
  SCOPED_PROFILE
  Directory parent = Get(parent_path);
  parent.first->UpdateChild(meta_data);
  Put(parent, parent_path);
}

template <typename Storage>
bool DirectoryHandler<Storage>::IsDirectory(const FileContext& file_context) const {
  return static_cast<bool>(file_context.meta_data.directory_id);
}

template <typename Storage>
std::pair<Directory*, FileContext*> DirectoryHandler<Storage>::GetParent(
    const boost::filesystem::path& relative_path) {
  auto grandparent(Get(relative_path.parent_path().parent_path()));
  auto parent_context(grandparent->GetChild(relative_path.parent_path().filename()));
  if (!(parent_context.meta_data.directory_id))
    ThrowError(CommonErrors::invalid_parameter);
  return std::make_pair(Get(relative_path.parent_path()), parent_context);
}

template <typename Storage>
void DirectoryHandler<Storage>::RenameSameParent(const boost::filesystem::path& old_relative_path,
                                                 const boost::filesystem::path& new_relative_path,
                                                 MetaData& meta_data) {
  Directory parent;
  MetaData parent_meta_data;
  GetParent(old_relative_path, parent, parent_meta_data);

#ifndef MAIDSAFE_WIN32
  struct stat old;
  old.st_ctime = meta_data.attributes.st_ctime;
  old.st_mtime = meta_data.attributes.st_mtime;
  time(&meta_data.attributes.st_mtime);
  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
#endif

  assert(parent.listing);
  if (!parent.first->HasChild(new_relative_path.filename())) {
    parent.first->RemoveChild(meta_data);
    meta_data.name = new_relative_path.filename();
    parent.first->AddChild(meta_data);
  } else {
    MetaData old_meta_data;
    try {
      parent.first->GetChild(new_relative_path.filename(), old_meta_data);
    }
    catch (const std::exception& exception) {
#ifndef MAIDSAFE_WIN32
      meta_data.attributes.st_ctime = old.st_ctime;
      meta_data.attributes.st_mtime = old.st_mtime;
#endif
      boost::throw_exception(exception);
    }
    parent.first->RemoveChild(old_meta_data);
    parent.first->RemoveChild(meta_data);
    meta_data.name = new_relative_path.filename();
    parent.first->AddChild(meta_data);
  }

#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&parent.second->last_write_time);
#else
  parent.second->attributes.st_ctime = parent.second->attributes.st_mtime =
      meta_data.attributes.st_mtime;
//   if (!same_parent && IsDirectory(file_context)) {
//     --parent.second->attributes.st_nlink;
//     ++new_parent_meta_data.attributes.st_nlink;
//     new_parent_meta_data.attributes.st_ctime =
//         new_parent_meta_data.attributes.st_mtime =
//         parent.second->attributes.st_mtime;
//   }
#endif
  Put(parent, old_relative_path.parent_path());

// #ifndef MAIDSAFE_WIN32
//   try {
//     assert(grandparent.listing);
//     grandparent.listing->UpdateChild(parent_meta_data, true);
//   }
//   catch(...) { /*Non-critical*/ }
//   Put(grandparent, old_relative_path.parent_path().parent_path());
// #endif
}

template <typename Storage>
void DirectoryHandler<Storage>::RenameDifferentParent(
    const boost::filesystem::path& old_relative_path,
    const boost::filesystem::path& new_relative_path,
    MetaData& meta_data) {
  Directory old_parent, new_parent;
  MetaData old_parent_meta_data, new_parent_meta_data;

  GetParent(old_relative_path, old_parent, old_parent_meta_data);
  GetParent(new_relative_path, new_parent, new_parent_meta_data);
#ifndef MAIDSAFE_WIN32
  struct stat old;
  old.st_ctime = meta_data.attributes.st_ctime;
  old.st_mtime = meta_data.attributes.st_mtime;
  time(&meta_data.attributes.st_mtime);
  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
#endif
  assert(old_parent.listing);
  assert(new_parent.listing);

  if (IsDirectory(file_context)) {
    Directory directory(Get(old_relative_path));
    Delete(directory);
    directory.parent_id = new_parent.listing->directory_id();
    Put(directory, new_relative_path);
  }

  old_parent.listing->RemoveChild(meta_data);

  if (!new_parent.listing->HasChild(new_relative_path.filename())) {
    meta_data.name = new_relative_path.filename();
    new_parent.listing->AddChild(meta_data);
  } else {
    MetaData old_meta_data;
    try {
      new_parent.listing->GetChild(new_relative_path.filename(), old_meta_data);
    }
    catch(const std::exception& exception) {
#ifndef MAIDSAFE_WIN32
      meta_data.attributes.st_ctime = old.st_ctime;
      meta_data.attributes.st_mtime = old.st_mtime;
#endif
      boost::throw_exception(exception);
    }
    new_parent.listing->RemoveChild(old_meta_data);
    meta_data.name = new_relative_path.filename();
    new_parent.listing->AddChild(meta_data);
  }

#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&old_parent_meta_data.last_write_time);
#else
  old_parent_meta_data.attributes.st_ctime = old_parent_meta_data.attributes.st_mtime =
      meta_data.attributes.st_mtime;
  if (IsDirectory(file_context)) {
    --old_parent_meta_data.attributes.st_nlink;
    ++new_parent_meta_data.attributes.st_nlink;
    new_parent_meta_data.attributes.st_ctime = new_parent_meta_data.attributes.st_mtime =
        old_parent_meta_data.attributes.st_mtime;
  }
#endif
  Put(old_parent, old_relative_path.parent_path());
  Put(new_parent, new_relative_path.parent_path());

#ifndef MAIDSAFE_WIN32
  if (new_relative_path.parent_path() != old_relative_path.parent_path().parent_path()) {
    try {
      if (old_grandparent.listing)
        old_grandparent.listing->UpdateChild(old_parent_meta_data);
    }
    catch (...) {/*Non-critical*/
    }
    Put(old_grandparent, old_relative_path.parent_path().parent_path());
  }
#endif
}

template <typename Storage>
void DirectoryHandler<Storage>::Put(const Directory& directory,
                                    const boost::filesystem::path& relative_path) {
  if (!directory.contents_changed())
    return;

  std::string serialised_directory_listing(directory.listing->Serialise());

  try {
    Delete(directory);
  }
  catch(...) {}

  DataMap data_map;
  {
    SelfEncryptor self_encryptor(data_map, *storage_);
    assert(serialised_directory_listing.size() <= std::numeric_limits<uint32_t>::max());
    if (!self_encryptor.Write(serialised_directory_listing.c_str(),
                              static_cast<uint32_t>(serialised_directory_listing.size()),
                              0)) {
      ThrowError(CommonErrors::invalid_parameter);
    }
  }

  auto encrypted_data_map = encrypt::EncryptDataMap(
      directory.parent_id, directory.listing->directory_id(), data_map);

  MutableData::Name name = MutableData::Name(directory.listing->directory_id());
  MutableData mutable_data(name, encrypted_data_map);
  storage_->template Put<MutableData>(mutable_data);

  {  // NOLINT
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_[relative_path] = directory;
  }
}

template <typename Storage>
Directory DirectoryHandler<Storage>::Get(const ParentId& parent_id,
                                         const DirectoryId& directory_id) const {
  auto data_map(GetDataMap(parent_id, directory_id));
  SelfEncryptor self_encryptor(data_map, *storage_);
  uint32_t data_map_size(static_cast<uint32_t>(data_map->size()));
  std::string serialised_listing(data_map_size, 0);

  if (!self_encryptor.Read(const_cast<char*>(serialised_listing.c_str()), data_map_size, 0))
    ThrowError(CommonErrors::invalid_parameter);

  Directory directory(parent_id, std::make_shared<DirectoryListing>(serialised_listing));
  assert(directory.listing->directory_id() == directory_id);
  return directory;
}

template <typename Storage>
void DirectoryHandler<Storage>::Delete(const Directory* directory) {
  auto data_map(GetDataMap(directory.parent_id, directory.listing->directory_id()));
  SelfEncryptor self_encryptor(data_map, *storage_);
  //self_encryptor.DeleteAllChunks();
  MutableData::Name name(directory.listing->directory_id());
  storage_->Delete(name);
}

template <typename Storage>
encrypt::DataMap DirectoryHandler<Storage>::GetDataMap(
    const ParentId& parent_id, const DirectoryId& directory_id) const {
  MutableData::Name name(directory_id);
  MutableData directory(storage_->Get(name).get());
  return encrypt::DecryptDataMap(parent_id, directory_id, directory.data().string());
}

template <typename Storage>
void DirectoryHandler<Storage>::HandleDataPoppedFromBuffer(
    const boost::filesystem::path& relative_path, const std::string& name,
    const NonEmptyString& content) const {
  // NOTE, This will be executed on a different thread to the one writing to the encryptor which has
  // triggered this call.  We therefore can't safely access any non-threadsafe class members here.
  LOG(kWarning) << "Data has been popped from the buffer for " << relative_path;
  // TODO(Fraser#5#): 2013-11-27 - Handle mutex-protecting the file_contexts, so we can log that
  // we're storing this chunk to the network.  If it's only a temporary chunk (i.e. it's not still
  // listed in the datamap when we get the next flush/close call), we should delete it from the
  // network again.
  ImmutableData data(ImmutableData::Name(Identity(name)), ImmutableData::serialised_type(content));
  storage_->Put(data);
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_HANDLER_H_
