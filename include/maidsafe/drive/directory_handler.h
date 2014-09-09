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

#include "boost/asio/io_service.hpp"
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
#include "maidsafe/common/data_types/data_type_values.h"
#include "maidsafe/common/data_types/mutable_data.h"

#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/file.h"

namespace maidsafe {

namespace drive {

namespace detail {

namespace test { class DirectoryHandlerTest; }

template <typename Storage>
class DirectoryHandler
    : public std::enable_shared_from_this<DirectoryHandler<Storage>>,
      public Path::Listener {
 public:
  template <typename... Types>
  static std::shared_ptr<DirectoryHandler<Storage>> Create(Types&&... args);
  ~DirectoryHandler();

  void Add(const boost::filesystem::path& relative_path, std::shared_ptr<Path> path);
  template <typename T = Path>
  typename std::enable_if<std::is_base_of<detail::Path, T>::value, std::shared_ptr<T>>::type
  Get(const boost::filesystem::path& relative_path);
  void FlushAll();
  void Delete(const boost::filesystem::path& relative_path);
  void Rename(const boost::filesystem::path& old_relative_path,
              const boost::filesystem::path& new_relative_path);
  void HandleDataPoppedFromBuffer(const boost::filesystem::path& relative_path,
                                  const std::string& name, const NonEmptyString& content) const;

  Identity root_parent_id() const { return root_parent_id_; }

  friend class test::DirectoryHandlerTest;

 private:
  DirectoryHandler() = delete;
  DirectoryHandler(const DirectoryHandler&) = delete;
  DirectoryHandler(DirectoryHandler&&) = delete;
  DirectoryHandler& operator=(const DirectoryHandler) = delete;
  DirectoryHandler(std::shared_ptr<Storage> storage,
                   const Identity& unique_user_id,
                   const Identity& root_parent_id,
                   const boost::filesystem::path& disk_buffer_path,
                   bool create,
                   boost::asio::io_service& asio_service);

  void Initialise(std::shared_ptr<Storage> storage,
                  const Identity& unique_user_id,
                  const Identity& root_parent_id,
                  const boost::filesystem::path& disk_buffer_path,
                  bool create,
                  boost::asio::io_service& asio_service);

  bool IsDirectory(std::shared_ptr<const Path> path) const;
  std::pair<std::shared_ptr<Directory>, std::shared_ptr<Path>>
    GetParent(const boost::filesystem::path& relative_path);
  void PrepareNewPath(const boost::filesystem::path& new_relative_path, Directory* new_parent);
  void RenameDifferentParent(const boost::filesystem::path& old_relative_path,
                             const boost::filesystem::path& new_relative_path,
                             std::shared_ptr<Directory> new_parent);
  void Put(std::shared_ptr<Path> path);
  ImmutableData SerialiseDirectory(std::shared_ptr<Directory> directory) const;
  std::shared_ptr<Directory> GetFromStorage(const boost::filesystem::path& relative_path,
      const ParentId& parent_id, const DirectoryId& directory_id);
  std::shared_ptr<Directory> ParseDirectory(
      const boost::filesystem::path& relative_path, const ImmutableData& encrypted_data_map,
      const ParentId& parent_id, const DirectoryId& directory_id,
      std::vector<StructuredDataVersions::VersionName> versions);
  void DeleteOldestVersion(Path* path);
  void DeleteAllVersions(Path* path);
  NonEmptyString GetChunkFromStore(const std::string& name) const;


  std::shared_ptr<Path::Listener> GetListener();

  // Path::Listener
  virtual void PathPut(std::shared_ptr<Path>);
  virtual void PathPutChunk(const ImmutableData&);
  virtual void PathIncrementChunks(const std::vector<ImmutableData::Name>&);

  std::shared_ptr<Storage> storage_;
  Identity unique_user_id_, root_parent_id_;
  mutable detail::File::Buffer disk_buffer_;
  mutable std::mutex cache_mutex_;
  boost::asio::io_service& asio_service_;
  std::map<boost::filesystem::path, std::shared_ptr<Path>> cache_;
};

// ==================== Implementation details ====================================================
template <typename Storage>
template <typename... Types>
std::shared_ptr<DirectoryHandler<Storage>>
DirectoryHandler<Storage>::Create(Types&&... args) {
  std::shared_ptr<DirectoryHandler<Storage>> self
    (new DirectoryHandler<Storage>{std::forward<Types>(args)...});
  self->Initialise(std::forward<Types>(args)...);
  return self;
}

template <typename Storage>
DirectoryHandler<Storage>::DirectoryHandler(std::shared_ptr<Storage> storage,
                                            const Identity& unique_user_id,
                                            const Identity& root_parent_id,
                                            const boost::filesystem::path& disk_buffer_path,
                                            bool,
                                            boost::asio::io_service& asio_service)
    : storage_(storage),
      unique_user_id_(unique_user_id),
      root_parent_id_(root_parent_id),
      // All chunks of serialised dirs should comfortably have been stored well before being popped
      // out of buffer, so allow pop_functor to be a no-op.
      disk_buffer_(MemoryUsage(Concurrency() * 1024 * 1024), DiskUsage(30 * 1024 * 1024),
                   [](const std::string&, const NonEmptyString&) {}, disk_buffer_path, true),
      cache_mutex_(),
      asio_service_(asio_service),
      cache_() {
  if (!unique_user_id.IsInitialised())
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::uninitialised));
  if (!root_parent_id.IsInitialised())
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::uninitialised));
}

template <typename Storage>
DirectoryHandler<Storage>::~DirectoryHandler() {
  FlushAll();
}

template <typename Storage>
std::shared_ptr<Path::Listener> DirectoryHandler<Storage>::GetListener() {
    return std::static_pointer_cast<Path::Listener>(this->shared_from_this());
}

template <typename Storage>
void DirectoryHandler<Storage>::Initialise(std::shared_ptr<Storage>,
                                           const Identity&,
                                           const Identity&,
                                           const boost::filesystem::path&,
                                           bool create,
                                           boost::asio::io_service&) {
  if (!create) {
    try {
      cache_[""] = GetFromStorage("", ParentId(unique_user_id_), root_parent_id_);
    } catch (...) {
      create = true;
    }
  }
  if (create) {
    // TODO(Fraser#5#): 2013-12-05 - Fill 'root_file' attributes appropriately.
    auto root_file(File::Create(kRoot, true));
    std::shared_ptr<Directory>
        root_parent(Directory::Create(ParentId(unique_user_id_),
                                      root_parent_id_,
                                      asio_service_,
                                      GetListener(),
                                      ""));
    std::shared_ptr<Directory>
        root(Directory::Create(ParentId(root_parent_id_),
                               *root_file->meta_data.directory_id,
                               asio_service_,
                               GetListener(),
                               kRoot));
    root_file->SetParent(root_parent);
    root_parent->AddChild(root_file);
    root->ScheduleForStoring();
    cache_[""] = root_parent;
    cache_[kRoot] = root;
  }
}

template <typename Storage>
void DirectoryHandler<Storage>::Add(const boost::filesystem::path& relative_path,
                                    std::shared_ptr<Path> path) {
  SCOPED_PROFILE
  auto parent(GetParent(relative_path));
  assert(parent.first && parent.second);

  if (IsDirectory(path)) {
    std::shared_ptr<Directory>
        directory(Directory::Create(ParentId(parent.first->directory_id()),
                                    *path->meta_data.directory_id,
                                    asio_service_,
                                    GetListener(),
                                    relative_path));
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_[relative_path] = directory;
  }

  parent.second->meta_data.UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent.second->meta_data.creation_time = parent.second->meta_data.last_write_time;
  if (IsDirectory(path)) {
    // FIXME: Determine how to handle hard links
    // ++parent.second->meta_data.attributes.st_nlink;
    parent.second->ScheduleForStoring();
  }
#endif

  // TODO(Fraser#5#): 2013-11-28 - Use on_scope_exit or similar to undo changes if AddChild throws.
  parent.first->AddChild(path);
}

template <typename Storage>
template <typename T>
typename std::enable_if<std::is_base_of<detail::Path, T>::value, std::shared_ptr<T>>::type
DirectoryHandler<Storage>::Get(const boost::filesystem::path& relative_path) {
  SCOPED_PROFILE
  std::shared_ptr<Directory> parent;
  boost::filesystem::path antecedent;
  {  // NOLINT
    std::lock_guard<std::mutex> lock(cache_mutex_);
    // Try to find the exact directory
    auto itr(cache_.find(relative_path));
    if (itr != std::end(cache_))
      return std::dynamic_pointer_cast<T>(itr->second);

    // Locate the first antecedent in cache
    antecedent = relative_path;
    while (itr == std::end(cache_) && !antecedent.empty()) {
      antecedent = antecedent.parent_path();
      itr = cache_.find(antecedent);
    }
    assert(itr != std::end(cache_));
    parent = std::static_pointer_cast<Directory>(itr->second);
  }

  // Recover the decendent directories until we reach the target
  std::shared_ptr<const Path> file;
  auto path_itr(std::begin(relative_path));
  std::advance(path_itr, std::distance(std::begin(antecedent), std::end(antecedent)));
  while (path_itr != std::end(relative_path)) {
    if (path_itr == std::begin(relative_path)) {
      file = parent->GetChild(kRoot);
      antecedent = kRoot;
    } else {
      file = parent->GetChild(*path_itr);
      antecedent = (antecedent / *path_itr).make_preferred();
    }

    if (!file->meta_data.directory_id)
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
    auto directory(GetFromStorage(antecedent, ParentId(parent->directory_id()),
                                  *file->meta_data.directory_id));
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      parent = directory;
      auto insertion_result(cache_.emplace(antecedent, std::move(directory)));
      assert(insertion_result.second);
      static_cast<void>(insertion_result);
    }
    ++path_itr;
  }
  return std::dynamic_pointer_cast<T>(parent);
}

template <typename Storage>
void DirectoryHandler<Storage>::FlushAll() {
  SCOPED_PROFILE
  bool error(false);
  std::lock_guard<std::mutex> lock(cache_mutex_);
  for (auto& dir : cache_) {
    auto current = std::static_pointer_cast<Directory>(dir.second);
    current->ResetChildrenCounter();
    auto child(current->GetChildAndIncrementCounter());
    while (child) {
      if (child->self_encryptor && !child->self_encryptor->Flush()) {
        error = true;
        LOG(kError) << "Failed to flush " << (dir.first / child->meta_data.name);
      }
      child = current->GetChildAndIncrementCounter();
    }
    current->ResetChildrenCounter();
    current->StoreImmediatelyIfPending();
  }
  if (error)
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::unknown));
}

template <typename Storage>
void DirectoryHandler<Storage>::Delete(const boost::filesystem::path& relative_path) {
  SCOPED_PROFILE
  auto parent(GetParent(relative_path));
  assert(parent.first && parent.second);

  auto file(parent.first->GetChild(relative_path.filename()));
  bool is_directory(IsDirectory(file));
  if (is_directory) {
    auto directory(Get(relative_path));
    DeleteAllVersions(directory.get());
    {  // NOLINT
      std::lock_guard<std::mutex> lock(cache_mutex_);
      cache_.erase(relative_path);
    }
  }

  parent.first->RemoveChild(relative_path.filename());
  parent.second->meta_data.UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent.second->meta_data.creation_time = parent.second->meta_data.last_write_time;
  // FIXME: Determine how to handle hard links
  // if (is_directory) {
  //   --parent.second->meta_data.attributes.st_nlink;
  // }
#endif
}

template <typename Storage>
void DirectoryHandler<Storage>::Rename(const boost::filesystem::path& old_relative_path,
                                       const boost::filesystem::path& new_relative_path) {
  SCOPED_PROFILE
  assert(old_relative_path != new_relative_path);

  auto new_parent(Get<Directory>(new_relative_path.parent_path()));
  PrepareNewPath(new_relative_path, new_parent.get());

  if (old_relative_path.parent_path() == new_relative_path.parent_path())
    new_parent->RenameChild(old_relative_path.filename(), new_relative_path.filename());
  else
    RenameDifferentParent(old_relative_path, new_relative_path, new_parent);

  if (IsDirectory(File::Create(old_relative_path, true))) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    // Fix old entry (if it's still there) and any children entries in the cache (effectively
    // renaming the key part of each such entry).
    auto old_path_size(old_relative_path.string().size());
    auto itr(cache_.lower_bound(old_relative_path));
    while (itr != std::end(cache_)) {
      if (itr->first.string().substr(0, old_path_size) == old_relative_path.string()) {
        boost::filesystem::path new_path(
            new_relative_path.string() + itr->first.string().substr(old_path_size));
        cache_.insert(std::make_pair(new_path, std::move(itr->second)));
        itr = cache_.erase(itr);
      } else {
        break;
      }
    }
  }
}

template <typename Storage>
bool DirectoryHandler<Storage>::IsDirectory(std::shared_ptr<const Path> path) const {
  return static_cast<bool>(path->meta_data.directory_id);
}

template <typename Storage>
std::pair<std::shared_ptr<Directory>, std::shared_ptr<Path>>
DirectoryHandler<Storage>::GetParent(const boost::filesystem::path& relative_path) {
  auto grandparent(Get<Directory>(relative_path.parent_path().parent_path()));
  auto parent_context(grandparent->GetMutableChild(relative_path.parent_path().filename()));
  if (!(parent_context->meta_data.directory_id))
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
  return std::make_pair(Get<Directory>(relative_path.parent_path()), parent_context);
}

template <typename Storage>
void DirectoryHandler<Storage>::PrepareNewPath(const boost::filesystem::path& new_relative_path,
                                               Directory* new_parent) {
  // From http://www.boost.org/doc/libs/release/libs/filesystem/doc/reference.html#rename -
  // "If old_p and new_p resolve to the same existing file, no action is taken. Otherwise, if new_p
  // resolves to an existing non-directory file, it is removed, while if new_p resolves to an
  // existing directory, it is removed if empty on ISO/IEC 9945 but is an error on Windows. A
  // symbolic link is itself renamed, rather than the file it resolves to being renamed."
  try {
    if (IsDirectory(new_parent->GetChild(new_relative_path.filename()))) {
#ifdef MAIDSAFE_WIN32
      BOOST_THROW_EXCEPTION(MakeError(DriveErrors::file_exists));
#else
      auto existing_directory(Get<Directory>(new_relative_path));
      if (existing_directory->empty()) {
        new_parent->RemoveChild(new_relative_path.filename());
        DeleteAllVersions(existing_directory.get());
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_.erase(new_relative_path);
      } else {
        BOOST_THROW_EXCEPTION(MakeError(DriveErrors::file_exists));
      }
#endif
    } else {
      new_parent->RemoveChild(new_relative_path.filename());
    }
  }
  catch (const drive_error& error) {
    if (error.code() != make_error_code(DriveErrors::no_such_file))
      throw;
  }
}

template <typename Storage>
void DirectoryHandler<Storage>::RenameDifferentParent(
    const boost::filesystem::path& old_relative_path,
    const boost::filesystem::path& new_relative_path,
    std::shared_ptr<Directory> new_parent) {
  auto old_parent(GetParent(old_relative_path));
  assert(old_parent.first && old_parent.second && new_parent);
  auto file(old_parent.first->RemoveChild(old_relative_path.filename()));

// #ifndef MAIDSAFE_WIN32
//   struct stat old;
//   old.st_ctime = meta_data.attributes.st_ctime;
//   old.st_mtime = meta_data.attributes.st_mtime;
//   time(&meta_data.attributes.st_mtime);
//   meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
// #endif
  if (IsDirectory(file)) {
    auto directory(Get<Directory>(old_relative_path));
    DeleteAllVersions(directory.get());
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto itr(cache_.find(old_relative_path));
      assert(itr != std::end(cache_));
      std::shared_ptr<Directory> temp = std::static_pointer_cast<Directory>(itr->second);
      temp->SetNewParent(ParentId(new_parent->directory_id()), new_relative_path);
      while (directory->HasPending()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }
      cache_.erase(itr);
      auto insertion_result(cache_.emplace(new_relative_path, temp));
      assert(insertion_result.second);
      directory = std::static_pointer_cast<Directory>(insertion_result.first->second);
    }
    directory->ScheduleForStoring();
  }

  file->meta_data.name = new_relative_path.filename();
  file->SetParent(new_parent);
  new_parent->AddChild(file);

#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&old_parent.second->meta_data.last_write_time);
  // if (new_relative_path.parent_path() != old_relative_path.parent_path().parent_path()) {
  //   try {
  //     if (old_grandparent.listing)
  //       old_grandparent.listing->UpdateChild(old_parent_meta_data);
  //   }
  //   catch (const std::exception&) {}  // Non-critical
  //   Put(old_grandparent, old_relative_path.parent_path().parent_path());
  // }
#else
  // old_parent_meta_data.attributes.st_ctime = old_parent_meta_data.attributes.st_mtime =
  //     meta_data.attributes.st_mtime;
  // if (IsDirectory(file)) {
  //   --old_parent_meta_data.attributes.st_nlink;
  //   ++new_parent_meta_data.attributes.st_nlink;
  //   new_parent_meta_data.attributes.st_ctime = new_parent_meta_data.attributes.st_mtime =
  //       old_parent_meta_data.attributes.st_mtime;
  // }
#endif
}

template <typename Storage>
void DirectoryHandler<Storage>::Put(std::shared_ptr<Path> path) {
  auto directory(std::static_pointer_cast<Directory>(path));
  ImmutableData encrypted_data_map(SerialiseDirectory(directory));
  storage_->Put(encrypted_data_map);
  if (directory->VersionsCount() == 0) {
    auto result(directory->InitialiseVersions(encrypted_data_map.name()));
    MutableData::Name hash_directory_id(crypto::Hash<crypto::SHA512>(std::get<0>(result)));
    auto future(storage_->CreateVersionTree(hash_directory_id,
                                            std::get<1>(result), kMaxVersions, 2));
    future.get();
  } else {
    auto result(directory->AddNewVersion(encrypted_data_map.name()));
    MutableData::Name hash_directory_id(crypto::Hash<crypto::SHA512>(std::get<0>(result)));
    storage_->PutVersion(hash_directory_id, std::get<1>(result), std::get<2>(result));
  }
}

template <typename Storage>
ImmutableData
DirectoryHandler<Storage>::SerialiseDirectory(std::shared_ptr<Directory> directory) const {
  std::string serialised_directory(directory->Serialise());
  encrypt::DataMap data_map;
  {
    encrypt::SelfEncryptor self_encryptor(data_map,
                                          disk_buffer_,
                                          std::bind(&DirectoryHandler<Storage>::GetChunkFromStore,
                                                    this->shared_from_this(),
                                                    std::placeholders::_1));
    assert(serialised_directory.size() <= std::numeric_limits<uint32_t>::max());
    if (!self_encryptor.Write(serialised_directory.c_str(),
                              static_cast<uint32_t>(serialised_directory.size()), 0)) {
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
    }
  }
  for (const auto& chunk : data_map.chunks) {
    auto content(disk_buffer_.Get(chunk.hash));
    storage_->Put(ImmutableData(content));
  }
  auto encrypted_data_map_contents(encrypt::EncryptDataMap(directory->parent_id(),
                                                           directory->directory_id(),
                                                           data_map));
  return ImmutableData(encrypted_data_map_contents);
}

template <typename Storage>
std::shared_ptr<Directory> DirectoryHandler<Storage>::GetFromStorage(
    const boost::filesystem::path& relative_path, const ParentId& parent_id,
    const DirectoryId& directory_id) {
  MutableData::Name hash_directory_id(crypto::Hash<crypto::SHA512>(directory_id));
  auto version_tip_of_trees(storage_->GetVersions(hash_directory_id).get());
  assert(!version_tip_of_trees.empty());
  if (version_tip_of_trees.size() != 1U) {
    // TODO(Fraser#5#): 2013-12-05 - Handle multiple branches (resolve conflicts if possible or
    //                  display all versions parsed as dirs to user and get them to choose a single
    //                  one to keep)
    version_tip_of_trees.resize(1);
  }
  auto versions(storage_->GetBranch(hash_directory_id, version_tip_of_trees.front()).get());
  assert(!versions.empty());
  try {
    ImmutableData encrypted_data_map(storage_->Get(versions.front().id).get());
    return ParseDirectory(relative_path, encrypted_data_map, parent_id, directory_id,
                          std::move(versions));
  }
  catch (const std::exception& e) {
    LOG(kError) << "Failed to get directory from storage: " << e.what();
    throw;
  }
}

template <typename Storage>
std::shared_ptr<Directory> DirectoryHandler<Storage>::ParseDirectory(
    const boost::filesystem::path& relative_path, const ImmutableData& encrypted_data_map,
    const ParentId& parent_id, const DirectoryId& directory_id,
    std::vector<StructuredDataVersions::VersionName> versions) {
  auto data_map(encrypt::DecryptDataMap(parent_id.data, directory_id,
                                        encrypted_data_map.data().string()));
  encrypt::SelfEncryptor self_encryptor(data_map,
                                        disk_buffer_,
                                        std::bind(&DirectoryHandler<Storage>::GetChunkFromStore,
                                                  this->shared_from_this(),
                                                  std::placeholders::_1));
  uint32_t data_map_size(static_cast<uint32_t>(data_map.size()));
  std::string serialised_listing(data_map_size, 0);

  if (!self_encryptor.Read(const_cast<char*>(serialised_listing.c_str()), data_map_size, 0))
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));

  std::shared_ptr<Directory>
      directory(Directory::Create(parent_id,
                                  serialised_listing,
                                  std::move(versions),
                                  asio_service_,
                                  GetListener(),
                                  relative_path));
  assert(directory->directory_id() == directory_id);
  return directory;
}

template <typename Storage>
void DirectoryHandler<Storage>::DeleteOldestVersion(Path* /*path*/) {
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
void DirectoryHandler<Storage>::DeleteAllVersions(Path* /*path*/) {
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
  BOOST_THROW_EXCEPTION(MakeError(CommonErrors::file_too_large));
}

template <typename Storage>
NonEmptyString DirectoryHandler<Storage>::GetChunkFromStore(const std::string& name) const {
  try {
    auto chunk(storage_->Get(ImmutableData::Name(Identity(name))).get());
    return chunk.data();
  }
  catch (const std::exception& e) {
    LOG(kError) << "Failed to get chunk from storage: " << e.what();
    throw;
  }
}

template <typename Storage>
void DirectoryHandler<Storage>::PathPut(std::shared_ptr<Path> path) {
  Put(path);
}

template <typename Storage>
void DirectoryHandler<Storage>::PathPutChunk(const ImmutableData& chunk) {
  storage_->Put(chunk);
}

template <typename Storage>
void DirectoryHandler<Storage>::PathIncrementChunks(
  const std::vector<ImmutableData::Name>& names) {
  storage_->IncrementReferenceCount(names);
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_HANDLER_H_
