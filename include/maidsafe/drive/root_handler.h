/*  Copyright 2013 MaidSafe.net limited

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

#ifndef MAIDSAFE_DRIVE_ROOT_HANDLER_H_
#define MAIDSAFE_DRIVE_ROOT_HANDLER_H_

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "boost/optional.hpp"

#include "maidsafe/data_store/surefile_store.h"
#include "maidsafe/data_types/data_type_values.h"
#include "maidsafe/nfs/client/maid_node_nfs.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/directory_listing.h"

namespace maidsafe {

namespace drive {

namespace detail {

template <typename Storage>
class RootHandler {
 public:
  typedef std::unique_ptr<DirectoryHandler<Storage>> DirectoryHandlerPtr;

  RootHandler(std::shared_ptr<nfs_client::MaidNodeNfs> maid_node_nfs,
              const Identity& unique_user_id, const Identity& drive_root_id,
              OnServiceAdded on_service_added);

  RootHandler(const Identity& drive_root_id, OnServiceAdded on_service_added,
              OnServiceRemoved on_service_removed, OnServiceRenamed on_service_renamed);

  void AddService(const boost::filesystem::path& service_alias,
                  const boost::filesystem::path& store_path, const Identity& service_root_id);
  void RemoveService(const boost::filesystem::path& service_alias);

  // Returns nullptr if there isn't a subdir containig the given path (most likely path == kRoot)
  std::unique_ptr<DirectoryHandler<Storage>> GetHandler(const boost::filesystem::path& path) const;
  std::unique_ptr<DirectoryHandler<Storage>> GetHandler(const boost::filesystem::path& path);

  Storage* GetStorage(const boost::filesystem::path& path) const;

  DataTagValue GetDirectoryType(const boost::filesystem::path& path) const;

  FileContext<Storage> GetFileContext(const boost::filesystem::path& path) const;

  void AddElement(const boost::filesystem::path& path, const MetaData& meta_data,
                  DirectoryId& grandparent_id, DirectoryId& parent_id);

  bool CanDelete(const boost::filesystem::path& path) const;
  void DeleteElement(const boost::filesystem::path& path, MetaData& meta_data);

  void RenameElement(const boost::filesystem::path& old_path,
                     const boost::filesystem::path& new_path, MetaData& meta_data);

  void UpdateParentDirectoryListing(const boost::filesystem::path& parent_path, MetaData meta_data);
  Directory GetFromPath(const boost::filesystem::path& path) const;

  Identity drive_root_id() const { return root_.listing->directory_id(); }
  std::shared_ptr<Storage> default_storage() const { return default_storage_; }

 private:
  RootHandler(const RootHandler&);
  RootHandler(RootHandler&&);
  RootHandler& operator=(RootHandler);

  // Called on the first run ever for this Drive (creating new user account)
  void CreateRoot(const Identity& unique_user_id);
  // Called when starting up a new session (user logging back in)
  void InitRoot(const Identity& unique_user_id, const Identity& drive_root_id);

  boost::optional<boost::filesystem::path> GetAlias(const boost::filesystem::path& path) const;
  void GetParentAndGrandparent(const boost::filesystem::path& path, Directory& grandparent,
                               Directory& parent, MetaData& parent_meta_data) const;

  bool CanAdd(const boost::filesystem::path& path) const;
  bool CanRename(const boost::filesystem::path& from_path,
                 const boost::filesystem::path& to_path) const;
  void RenameSameParent(const boost::filesystem::path& old_path,
                        const boost::filesystem::path& new_path, MetaData& meta_data);
  void RenameDifferentParent(const boost::filesystem::path& old_path,
                             const boost::filesystem::path& new_path, MetaData& meta_data);
  void ReStoreDirectories(const boost::filesystem::path& old_path,
                          const boost::filesystem::path& new_path);

  void ReStoreDirectory(DirectoryHandlerPtr& old_handler,
                        const boost::filesystem::path& old_path,
                        DirectoryHandlerPtr& new_handler,
                        const boost::filesystem::path& new_path);
  void ReStoreFile(DirectoryHandlerPtr& old_handler,
                   DirectoryHandlerPtr& new_handler,
                   const MetaData& meta_data);

  void Put(const boost::filesystem::path& path, Directory& directory);
  void Delete(const boost::filesystem::path& path, const Directory& directory);

  std::shared_ptr<Storage> default_storage_;  // MaidNodeNfs or nullptr
  mutable std::mutex root_mutex_;             // protects root_ & root_meta_data_
  mutable std::mutex handlers_mutex_;         // protects directory_handlers_
  mutable std::mutex directories_mutex_;      // protects recent_directories_
  Directory root_;
  MetaData root_meta_data_;
  std::map<boost::filesystem::path, DirectoryHandler<Storage>> directory_handlers_;
  std::map<boost::filesystem::path, Directory> recent_directories_;
  OnServiceAdded on_service_added_;
  OnServiceRemoved on_service_removed_;
  OnServiceRenamed on_service_renamed_;
};

// ==================== Implementation =============================================================
template <typename Storage>
struct Default {
  typedef std::pair<boost::filesystem::path, DataTagValue> PathAndType;
  static const std::vector<PathAndType> kValues;
  static bool IsDefault(const boost::filesystem::path& path) {
    return std::any_of(std::begin(kValues), std::end(kValues),
                       [&path](const PathAndType & value) { return path == value.first; });
  }
};

template <typename Storage>
RootHandler<Storage>::RootHandler(std::shared_ptr<nfs_client::MaidNodeNfs> maid_node_nfs,
                                  const Identity& unique_user_id, const Identity& drive_root_id,
                                  OnServiceAdded on_service_added)
    : default_storage_(maid_node_nfs),
      root_mutex_(),
      handlers_mutex_(),
      directories_mutex_(),
      root_(),
      root_meta_data_(kRoot, true),
      directory_handlers_(),
      recent_directories_(),
      on_service_added_(std::move(on_service_added)),
      on_service_removed_(nullptr),
      on_service_renamed_(nullptr) {
  if (!unique_user_id.IsInitialised())
    ThrowError(CommonErrors::uninitialised);
  drive_root_id.IsInitialised() ? InitRoot(unique_user_id, drive_root_id)
                                : CreateRoot(unique_user_id);
}

template <typename Storage>
RootHandler<Storage>::RootHandler(const Identity& drive_root_id, OnServiceAdded on_service_added,
                                  OnServiceRemoved on_service_removed,
                                  OnServiceRenamed on_service_renamed)
    : default_storage_(),
      root_mutex_(),
      handlers_mutex_(),
      directories_mutex_(),
      root_(),
      root_meta_data_(kRoot, true),
      directory_handlers_(),
      recent_directories_(),
      on_service_added_(std::move(on_service_added)),
      on_service_removed_(std::move(on_service_removed)),
      on_service_renamed_(std::move(on_service_renamed)) {
  drive_root_id.IsInitialised() ? InitRoot(Identity(), drive_root_id) : CreateRoot(Identity());
}

template <>
void RootHandler<data_store::SureFileStore>::AddService(
    const boost::filesystem::path& service_alias, const boost::filesystem::path& store_path,
    const Identity& service_root_id);

template <>
void RootHandler<data_store::SureFileStore>::RemoveService(
    const boost::filesystem::path& service_alias);

template <typename Storage>
std::unique_ptr<DirectoryHandler<Storage>> RootHandler<Storage>::GetHandler(
    const boost::filesystem::path& path) const {
  if (path.empty())
    return nullptr;
  auto alias(*(++std::begin(path)));
  std::lock_guard<std::mutex> handlers_lock(handlers_mutex_);
  auto itr(directory_handlers_.find(alias));
  return itr == std::end(directory_handlers_) ? nullptr
                                              : std::unique_ptr<DirectoryHandler<Storage>>(
                                                    new DirectoryHandler<Storage>(itr->second));
}

template <typename Storage>
std::unique_ptr<DirectoryHandler<Storage>> RootHandler<Storage>::GetHandler(
    const boost::filesystem::path& path) {
  if (path.empty())
    return nullptr;
  auto alias(*(++std::begin(path)));
  std::lock_guard<std::mutex> handlers_lock(handlers_mutex_);
  auto itr(directory_handlers_.find(alias));
  return itr == std::end(directory_handlers_) ? nullptr
                                              : std::unique_ptr<DirectoryHandler<Storage>>(
                                                    new DirectoryHandler<Storage>(itr->second));
}

template <>
nfs_client::MaidNodeNfs* RootHandler<nfs_client::MaidNodeNfs>::GetStorage(
    const boost::filesystem::path& path) const;

template <>
data_store::SureFileStore* RootHandler<data_store::SureFileStore>::GetStorage(
    const boost::filesystem::path& path) const;

template <>
DataTagValue RootHandler<nfs_client::MaidNodeNfs>::GetDirectoryType(
    const boost::filesystem::path& path) const;

template <>
DataTagValue RootHandler<data_store::SureFileStore>::GetDirectoryType(
    const boost::filesystem::path& path) const;

template <typename Storage>
FileContext<Storage> RootHandler<Storage>::GetFileContext(
    const boost::filesystem::path& path) const {
  FileContext<Storage> file_context;
  Directory parent;
  {
    std::lock_guard<std::mutex> root_lock(root_mutex_);
    parent = root_;
    *file_context.meta_data = root_meta_data_;
  }
  bool found(true);
  {
    std::lock_guard<std::mutex> dir_lock(directories_mutex_);
    auto itr(recent_directories_.find(path.parent_path()));
    if (itr == std::end(recent_directories_))
      found = false;
    else
      parent = itr->second;
  }

  if (!found) {
    auto dir_handler(GetHandler(path));
    if (dir_handler)
      parent = dir_handler->GetFromPath(parent, path.parent_path());
  }

  if (path != kRoot)
    parent.listing->GetChild(path.filename(), *file_context.meta_data);
  file_context.grandparent_directory_id = parent.parent_id;
  file_context.parent_directory_id = parent.listing->directory_id();

  return file_context;
}

template <typename Storage>
Directory RootHandler<Storage>::GetFromPath(const boost::filesystem::path& path) const {
  {
    std::lock_guard<std::mutex> dir_lock(directories_mutex_);
    auto itr(recent_directories_.find(path));
    if (itr != std::end(recent_directories_))
      return itr->second;
  }
  auto directory_handler(GetHandler(path));
  Directory root_copy;
  {
    std::lock_guard<std::mutex> root_lock(root_mutex_);
    root_copy = root_;
  }
  return directory_handler ? directory_handler->GetFromPath(root_copy, path) : root_copy;
}

template <typename Storage>
boost::optional<boost::filesystem::path> RootHandler<Storage>::GetAlias(
    const boost::filesystem::path& path) const {
  auto alias(*(++std::begin(path)));
  return alias == path.filename() ? boost::optional<boost::filesystem::path>(alias)
                                  : boost::optional<boost::filesystem::path>();
}

template <>
void RootHandler<nfs_client::MaidNodeNfs>::CreateRoot(const Identity& unique_user_id);

template <>
void RootHandler<data_store::SureFileStore>::CreateRoot(const Identity& unique_user_id);

template <>
void RootHandler<nfs_client::MaidNodeNfs>::InitRoot(const Identity& unique_user_id,
                                                    const Identity& drive_root_id);

template <>
void RootHandler<data_store::SureFileStore>::InitRoot(const Identity& unique_user_id,
                                                      const Identity& drive_root_id);

template <typename Storage>
void RootHandler<Storage>::GetParentAndGrandparent(const boost::filesystem::path& path,
                                                   Directory& grandparent, Directory& parent,
                                                   MetaData& parent_meta_data) const {
  auto directory_handler(GetHandler(path));
  if (directory_handler) {
    if (path == kRoot) {
      grandparent = parent = Directory();
      parent_meta_data = MetaData();
    } else if (path.parent_path() == kRoot) {
      grandparent = Directory();
      std::lock_guard<std::mutex> root_lock(root_mutex_);
      parent = root_;
      parent_meta_data = root_meta_data_;
    } else {
      grandparent = GetFromPath(path.parent_path().parent_path());
      grandparent.listing->GetChild(path.parent_path().filename(), parent_meta_data);
      if (!(parent_meta_data.directory_id))
        ThrowError(CommonErrors::invalid_parameter);
      parent = GetFromPath(path.parent_path());
    }
  } else {
    ThrowError(CommonErrors::invalid_parameter);
  }
}

template <>
bool RootHandler<nfs_client::MaidNodeNfs>::CanAdd(const boost::filesystem::path& path) const;

template <>
bool RootHandler<data_store::SureFileStore>::CanAdd(const boost::filesystem::path& path) const;

template <typename Storage>
void RootHandler<Storage>::AddElement(const boost::filesystem::path& path,
                                      const MetaData& meta_data, DirectoryId& grandparent_id,
                                      DirectoryId& parent_id) {
  SCOPED_PROFILE
  if (!CanAdd(path))
    ThrowError(DriveErrors::permission_denied);

  if (GetAlias(path)) {
    on_service_added_();
    ThrowError(DriveErrors::permission_denied);
  }

  Directory grandparent, parent;
  MetaData parent_meta_data;
  GetParentAndGrandparent(path, grandparent, parent, parent_meta_data);

  assert(parent.listing);
  parent.listing->AddChild(meta_data);

  if (IsDirectory(meta_data)) {
    Directory directory(parent.listing->directory_id(),
                        std::make_shared<DirectoryListing>(*meta_data.directory_id),
                        std::make_shared<encrypt::DataMap>(), GetDirectoryType(path));
    try {
      Put(path, directory);
    }
    catch (const std::exception& exception) {
      parent.listing->RemoveChild(meta_data);
      boost::throw_exception(exception);
    }
  }

  parent_meta_data.UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent_meta_data.attributes.st_ctime = parent_meta_data.attributes.st_mtime;
  if (IsDirectory(meta_data))
    ++parent_meta_data.attributes.st_nlink;
#endif
  if (grandparent.listing)
    grandparent.listing->UpdateChild(parent_meta_data);

  try {
    Put(path.parent_path(), parent);
  }
  catch (const std::exception& exception) {
    parent.listing->RemoveChild(meta_data);
    boost::throw_exception(exception);
  }

  Put(path.parent_path().parent_path(), grandparent);

  if (grandparent.listing)
    grandparent_id = grandparent.listing->directory_id();
  parent_id = parent.listing->directory_id();
}

template <>
bool RootHandler<nfs_client::MaidNodeNfs>::CanDelete(const boost::filesystem::path& path) const;

template <>
bool RootHandler<data_store::SureFileStore>::CanDelete(const boost::filesystem::path& path) const;

template <typename Storage>
void RootHandler<Storage>::DeleteElement(const boost::filesystem::path& path, MetaData& meta_data) {
  SCOPED_PROFILE
  auto alias(GetAlias(path));
  if (alias) {
#ifndef NDEBUG
    {
      std::lock_guard<std::mutex> handlers_lock(handlers_mutex_);
      assert(directory_handlers_.find(*alias) != std::end(directory_handlers_));
    }
#endif
    on_service_removed_(*alias);
  }

  Directory grandparent, parent;
  MetaData parent_meta_data;
  GetParentAndGrandparent(path, grandparent, parent, parent_meta_data);

  assert(parent.listing);
  parent.listing->GetChild(path.filename(), meta_data);

  if (IsDirectory(meta_data)) {
    Directory directory(GetFromPath(path));
    Delete(path, directory);
  } else {
    auto directory_handler(GetHandler(path));
    if (directory_handler) {
      encrypt::SelfEncryptor<Storage>(meta_data.data_map, *directory_handler->storage())
          .DeleteAllChunks();
    } else {
      encrypt::SelfEncryptor<Storage>(meta_data.data_map, *default_storage_).DeleteAllChunks();
    }
  }

  parent.listing->RemoveChild(meta_data);
  parent_meta_data.UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent_meta_data.attributes.st_ctime = parent_meta_data.attributes.st_mtime;
  if (IsDirectory(meta_data))
    --parent_meta_data.attributes.st_nlink;
#endif

  try {
    if (grandparent.listing)
      grandparent.listing->UpdateChild(parent_meta_data);
  }
  catch (...) {/*Non-critical*/
  }

#ifndef MAIDSAFE_WIN32
  Put(path.parent_path().parent_path(), grandparent);
#endif
  Put(path.parent_path(), parent);
}

template <>
bool RootHandler<nfs_client::MaidNodeNfs>::CanRename(const boost::filesystem::path& from_path,
                                                     const boost::filesystem::path& to_path) const;

template <>
bool RootHandler<data_store::SureFileStore>::CanRename(
    const boost::filesystem::path& from_path, const boost::filesystem::path& to_path) const;

template <typename Storage>
void RootHandler<Storage>::RenameElement(const boost::filesystem::path& old_path,
                                         const boost::filesystem::path& new_path,
                                         MetaData& meta_data) {
  SCOPED_PROFILE
  if (old_path == new_path)
    return;
  if (!CanRename(old_path, new_path))
    ThrowError(CommonErrors::invalid_parameter);

  if (old_path.parent_path() == new_path.parent_path())
    RenameSameParent(old_path, new_path, meta_data);
  else
    RenameDifferentParent(old_path, new_path, meta_data);
}

template <typename Storage>
void RootHandler<Storage>::RenameSameParent(const boost::filesystem::path& old_path,
                                            const boost::filesystem::path& new_path,
                                            MetaData& meta_data) {
  Directory grandparent, parent;
  MetaData parent_meta_data;
  GetParentAndGrandparent(old_path, grandparent, parent, parent_meta_data);

#ifndef MAIDSAFE_WIN32
  struct stat old;
  old.st_ctime = meta_data.attributes.st_ctime;
  old.st_mtime = meta_data.attributes.st_mtime;
  time(&meta_data.attributes.st_mtime);
  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
#endif

  assert(parent.listing);
  if (!parent.listing->HasChild(new_path.filename())) {
    parent.listing->RemoveChild(meta_data);
    meta_data.name = new_path.filename();
    parent.listing->AddChild(meta_data);
  } else {
    MetaData old_meta_data;
    try {
      parent.listing->GetChild(new_path.filename(), old_meta_data);
    }
    catch (const std::exception& exception) {
#ifndef MAIDSAFE_WIN32
      meta_data.attributes.st_ctime = old.st_ctime;
      meta_data.attributes.st_mtime = old.st_mtime;
#endif
      boost::throw_exception(exception);
    }
    parent.listing->RemoveChild(old_meta_data);
    parent.listing->RemoveChild(meta_data);
    meta_data.name = new_path.filename();
    parent.listing->AddChild(meta_data);
  }

#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&parent_meta_data.last_write_time);
#else
  parent_meta_data.attributes.st_ctime = parent_meta_data.attributes.st_mtime =
      meta_data.attributes.st_mtime;
//   if (!same_parent && IsDirectory(meta_data)) {
//     --parent_meta_data.attributes.st_nlink;
//     ++new_parent_meta_data.attributes.st_nlink;
//     new_parent_meta_data.attributes.st_ctime =
//         new_parent_meta_data.attributes.st_mtime =
//         parent_meta_data.attributes.st_mtime;
//   }
#endif

  Put(new_path.parent_path(), parent);
#ifndef MAIDSAFE_WIN32
  try {
    if (grandparent.listing)
      grandparent.listing->UpdateChild(parent_meta_data);
  }
  catch (...) {/*Non-critical*/
  }
  Put(new_path.parent_path().parent_path(), grandparent);
#endif

  auto old_alias(GetAlias(old_path));
  auto new_alias(GetAlias(new_path));
  if (old_alias && new_alias && IsDirectory(meta_data)) {
    std::lock_guard<std::mutex> handlers_lock(handlers_mutex_);
    auto itr(directory_handlers_.find(*old_alias));
    assert(itr != std::end(directory_handlers_));
    directory_handlers_.insert(std::make_pair(*new_alias, itr->second));
    directory_handlers_.erase(*old_alias);
    on_service_renamed_(*old_alias, *new_alias);
  }
}

template <typename Storage>
void RootHandler<Storage>::RenameDifferentParent(const boost::filesystem::path& old_path,
                                                 const boost::filesystem::path& new_path,
                                                 MetaData& meta_data) {
  Directory old_grandparent, old_parent, new_grandparent, new_parent;
  MetaData old_parent_meta_data, new_parent_meta_data;
  GetParentAndGrandparent(old_path, old_grandparent, old_parent, old_parent_meta_data);
  GetParentAndGrandparent(new_path, new_grandparent, new_parent, new_parent_meta_data);
#ifndef MAIDSAFE_WIN32
  struct stat old;
  old.st_ctime = meta_data.attributes.st_ctime;
  old.st_mtime = meta_data.attributes.st_mtime;
  time(&meta_data.attributes.st_mtime);
  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
#endif
  assert(old_parent.listing);
  assert(new_parent.listing);

  auto old_handler(GetHandler(old_path)), new_handler(GetHandler(new_path));
  if (old_handler != new_handler) {
    if (IsDirectory(meta_data))
      ReStoreDirectory(old_handler, old_path, new_handler, new_path);
    else
      ReStoreFile(old_handler, new_handler, meta_data);
  }

  /*if (IsDirectory(meta_data)) {
    Directory directory(GetFromPath(old_path));
    if (directory.type != new_parent.type) {
      directory.listing->ResetChildrenIterator();
      MetaData child_meta_data;
      while (directory.listing->GetChildAndIncrementItr(child_meta_data)) {
        if (IsDirectory(child_meta_data))
          ReStoreDirectories(old_path / child_meta_data.name, new_path / child_meta_data.name);
      }
    }
    Delete(old_path, directory);
    directory.parent_id = new_parent.listing->directory_id();
    directory.type = new_parent.type;
    Put(new_path, directory);
  }*/

  old_parent.listing->RemoveChild(meta_data);

  if (!new_parent.listing->HasChild(new_path.filename())) {
    meta_data.name = new_path.filename();
    new_parent.listing->AddChild(meta_data);
  } else {
    MetaData old_meta_data;
    try {
      new_parent.listing->GetChild(new_path.filename(), old_meta_data);
    }
    catch (const std::exception& exception) {
#ifndef MAIDSAFE_WIN32
      meta_data.attributes.st_ctime = old.st_ctime;
      meta_data.attributes.st_mtime = old.st_mtime;
#endif
      boost::throw_exception(exception);
    }
    new_parent.listing->RemoveChild(old_meta_data);
    meta_data.name = new_path.filename();
    new_parent.listing->AddChild(meta_data);
  }

#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&old_parent_meta_data.last_write_time);
#else
  old_parent_meta_data.attributes.st_ctime = old_parent_meta_data.attributes.st_mtime =
      meta_data.attributes.st_mtime;
  if (IsDirectory(meta_data)) {
    --old_parent_meta_data.attributes.st_nlink;
    ++new_parent_meta_data.attributes.st_nlink;
    new_parent_meta_data.attributes.st_ctime = new_parent_meta_data.attributes.st_mtime =
        old_parent_meta_data.attributes.st_mtime;
  }
#endif
  Put(old_path.parent_path(), old_parent);
  Put(new_path.parent_path(), new_parent);

#ifndef MAIDSAFE_WIN32
  if (new_path.parent_path() != old_path.parent_path().parent_path()) {
    try {
      if (old_grandparent.listing)
        old_grandparent.listing->UpdateChild(old_parent_meta_data);
    }
    catch (...) {/*Non-critical*/
    }
    Put(old_path.parent_path().parent_path(), old_grandparent);
  }
#endif
}

template <typename Storage>
void RootHandler<Storage>::ReStoreDirectories(const boost::filesystem::path& old_path,
                                              const boost::filesystem::path& new_path) {
  Directory directory(GetFromPath(old_path));
  directory.listing->ResetChildrenIterator();
  MetaData meta_data;

  while (directory.listing->GetChildAndIncrementItr(meta_data)) {
    if (IsDirectory(meta_data))
      ReStoreDirectories(old_path / meta_data.name, new_path / meta_data.name);
  }

  Delete(old_path, directory);
  directory.type = GetDirectoryType(new_path);
  Put(new_path, directory);
}

template<typename Storage>
void RootHandler<Storage>::ReStoreDirectory(DirectoryHandlerPtr& old_handler,
                                            const boost::filesystem::path& old_path,
                                            DirectoryHandlerPtr& new_handler,
                                            const boost::filesystem::path& new_path) {
  Directory directory(GetFromPath(old_path));
  directory.listing->ResetChildrenIterator();
  MetaData meta_data;
  while (directory.listing->GetChildAndIncrementItr(meta_data)) {
    if (IsDirectory(meta_data))
      ReStoreDirectory(old_handler, old_path / meta_data.name,
                       new_handler, new_path / meta_data.name);
    else
      ReStoreFile(old_handler, new_handler, meta_data);
  }
  Delete(old_path, directory);
  Put(new_path, directory);
}

template<typename Storage>
void RootHandler<Storage>::ReStoreFile(DirectoryHandlerPtr& old_handler,
                                       DirectoryHandlerPtr& new_handler,
                                       const MetaData& meta_data) {
  for (auto& chunk : meta_data.data_map->chunks) {
    ImmutableData::Name name(ImmutableData::Name(Identity(chunk.hash)));
    auto chunk_future(old_handler->storage()->Get<ImmutableData>(name));
    new_handler->storage()->Put(chunk_future.get());
    old_handler->storage()->Delete<ImmutableData>(name);
  }
}

template<typename Storage>
void RootHandler<Storage>::UpdateParentDirectoryListing(const boost::filesystem::path& parent_path,
                                                        MetaData meta_data) {
  SCOPED_PROFILE
  Directory parent = GetFromPath(parent_path);
  parent.listing->UpdateChild(meta_data);
  Put(parent_path, parent);
}

template <>
void RootHandler<nfs_client::MaidNodeNfs>::Put(const boost::filesystem::path& path,
                                               Directory& directory);

template <>
void RootHandler<data_store::SureFileStore>::Put(const boost::filesystem::path& path,
                                                 Directory& directory);

template <>
void RootHandler<nfs_client::MaidNodeNfs>::Delete(const boost::filesystem::path& path,
                                                  const Directory& directory);

template <>
void RootHandler<data_store::SureFileStore>::Delete(const boost::filesystem::path& path,
                                                    const Directory& directory);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_ROOT_HANDLER_H_
