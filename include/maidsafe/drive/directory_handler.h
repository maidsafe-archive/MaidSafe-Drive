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

#include "maidsafe/common/rsa.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/profiler.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/data_types/data_type_values.h"
#include "maidsafe/data_types/mutable_data.h"

#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/meta_data.h"


namespace maidsafe {
namespace drive {
namespace detail {

namespace test {
class DirectoryHandlerTest;
}

template <typename Storage>
class DirectoryHandler {
 public:
  typedef std::shared_ptr<Storage> StoragePtr;
  typedef std::shared_ptr<DirectoryListing> DirectoryListingPtr;
  typedef encrypt::SelfEncryptor<Storage> SelfEncryptor;
  typedef encrypt::DataMap DataMap;
  typedef encrypt::DataMapPtr DataMapPtr;

  DirectoryHandler(StoragePtr storage, const Identity& unique_user_id,
                   const Identity& root_parent_id, bool create);
  virtual ~DirectoryHandler() {}

  void Add(const boost::filesystem::path& relative_path, const MetaData& meta_data,
           DirectoryId& grandparent_id, DirectoryId& parent_id);
  Directory Get(const boost::filesystem::path& relative_path);
  void Delete(const boost::filesystem::path& relative_path);
  void Rename(const boost::filesystem::path& old_relative_path,
              const boost::filesystem::path& new_relative_path, MetaData& meta_data);
  void UpdateParent(const boost::filesystem::path& parent_path, const MetaData& meta_data);

  Identity root_parent_id() const { return root_parent_id_; }

  friend class test::DirectoryHandlerTest;

 private:
  DirectoryHandler(const DirectoryHandler&);
  DirectoryHandler& operator=(const DirectoryHandler&);

  bool IsDirectory(const MetaData& meta_data) const;
  void GetParentAndGrandparent(const boost::filesystem::path& relative_path,
                               Directory& grandparent, Directory& parent,
                               MetaData& parent_meta_data);
  void RenameSameParent(const boost::filesystem::path& old_relative_path,
                        const boost::filesystem::path& new_relative_path, MetaData& meta_data);
  void RenameDifferentParent(const boost::filesystem::path& old_relative_path,
                             const boost::filesystem::path& new_relative_path,
                             MetaData& meta_data);
  void Put(const Directory& directory, const boost::filesystem::path& relative_path);
  Directory Get(const DirectoryId& parent_id, const DirectoryId& directory_id) const;
  void Delete(const Directory& directory);
  DataMapPtr GetDataMap(const DirectoryId& parent_id, const DirectoryId& directory_id) const;

 private:
  StoragePtr storage_;
  Identity unique_user_id_, root_parent_id_;
  mutable std::mutex cache_mutex_;
  std::map<boost::filesystem::path, Directory> cache_;
};

// ==================== Implementation details ====================================================

template <typename Storage>
DirectoryHandler<Storage>::DirectoryHandler(StoragePtr storage, const Identity& unique_user_id,
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
                                    const MetaData& meta_data, DirectoryId& grandparent_id,
                                    DirectoryId& parent_id) {
  SCOPED_PROFILE
  Directory grandparent, parent;
  MetaData parent_meta_data;
  GetParentAndGrandparent(relative_path, grandparent, parent, parent_meta_data);

  assert(parent.listing);
  parent.listing->AddChild(meta_data);

  if (IsDirectory(meta_data)) {
    Directory directory(parent.listing->directory_id(),
                        std::make_shared<DirectoryListing>(*meta_data.directory_id));
    try {
      Put(directory, relative_path);
    }
    catch(const std::exception& exception) {
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
    Put(parent, relative_path.parent_path());
  }
  catch (const std::exception& exception) {
    parent.listing->RemoveChild(meta_data);
    boost::throw_exception(exception);
  }

  Put(grandparent, relative_path.parent_path().parent_path());

  if (grandparent.listing)
    grandparent_id = grandparent.listing->directory_id();
  parent_id = parent.listing->directory_id();
}

template <typename Storage>
Directory DirectoryHandler<Storage>::Get(const boost::filesystem::path& relative_path) {
  SCOPED_PROFILE
  {  // NOLINT
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto itr(cache_.find(relative_path));
    if (itr != std::end(cache_))
      return itr->second;
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
  {  // NOLINT
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_[relative_path] = directory;
  }
  return directory;
}

template <typename Storage>
void DirectoryHandler<Storage>::Delete(const boost::filesystem::path& relative_path) {
  SCOPED_PROFILE
  Directory grandparent, parent;
  MetaData meta_data, parent_meta_data;
  GetParentAndGrandparent(relative_path, grandparent, parent, parent_meta_data);

  assert(parent.listing);
  parent.listing->GetChild(relative_path.filename(), meta_data);

  if (IsDirectory(meta_data)) {
    Directory directory(Get(relative_path));
    Delete(directory);
    {  // NOLINT
      std::lock_guard<std::mutex> lock(cache_mutex_);
      cache_.erase(relative_path);
    }
  } else {
    SelfEncryptor(meta_data.data_map, *storage_).DeleteAllChunks();
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

  if (old_relative_path.parent_path() == new_relative_path.parent_path())
    RenameSameParent(old_relative_path, new_relative_path, meta_data);
  else
    RenameDifferentParent(old_relative_path, new_relative_path, meta_data);
}

template <typename Storage>
void DirectoryHandler<Storage>::UpdateParent(const boost::filesystem::path& parent_path,
                                             const MetaData& meta_data) {
  SCOPED_PROFILE
  Directory parent = Get(parent_path);
  parent.listing->UpdateChild(meta_data);
  Put(parent, parent_path);
}

template <typename Storage>
bool DirectoryHandler<Storage>::IsDirectory(const MetaData& meta_data) const {
  return static_cast<bool>(meta_data.directory_id);
}

template <typename Storage>
void DirectoryHandler<Storage>::GetParentAndGrandparent(
    const boost::filesystem::path& relative_path, Directory& grandparent, Directory& parent,
    MetaData& parent_meta_data) {
  grandparent = Get(relative_path.parent_path().parent_path());
  grandparent.listing->GetChild(relative_path.parent_path().filename(), parent_meta_data);
  if (!(parent_meta_data.directory_id))
    ThrowError(CommonErrors::invalid_parameter);
  parent = Get(relative_path.parent_path());
}

template <typename Storage>
void DirectoryHandler<Storage>::RenameSameParent(const boost::filesystem::path& old_relative_path,
                                                 const boost::filesystem::path& new_relative_path,
                                                 MetaData& meta_data) {
  Directory grandparent, parent;
  MetaData parent_meta_data;
  GetParentAndGrandparent(old_relative_path, grandparent, parent, parent_meta_data);

#ifndef MAIDSAFE_WIN32
  struct stat old;
  old.st_ctime = meta_data.attributes.st_ctime;
  old.st_mtime = meta_data.attributes.st_mtime;
  time(&meta_data.attributes.st_mtime);
  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
#endif

  assert(parent.listing);
  if (!parent.listing->HasChild(new_relative_path.filename())) {
    parent.listing->RemoveChild(meta_data);
    meta_data.name = new_relative_path.filename();
    parent.listing->AddChild(meta_data);
  } else {
    MetaData old_meta_data;
    try {
      parent.listing->GetChild(new_relative_path.filename(), old_meta_data);
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
    meta_data.name = new_relative_path.filename();
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
  Directory old_grandparent, old_parent, new_grandparent, new_parent;
  MetaData old_parent_meta_data, new_parent_meta_data;

  GetParentAndGrandparent(old_relative_path, old_grandparent, old_parent, old_parent_meta_data);
  GetParentAndGrandparent(new_relative_path, new_grandparent, new_parent, new_parent_meta_data);
#ifndef MAIDSAFE_WIN32
  struct stat old;
  old.st_ctime = meta_data.attributes.st_ctime;
  old.st_mtime = meta_data.attributes.st_mtime;
  time(&meta_data.attributes.st_mtime);
  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
#endif
  assert(old_parent.listing);
  assert(new_parent.listing);

  if (IsDirectory(meta_data)) {
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
  if (IsDirectory(meta_data)) {
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
  std::string serialised_directory_listing(directory.listing->Serialise());

  try {
    Delete(directory);
  }
  catch(...) {}

  DataMapPtr data_map(new encrypt::DataMap);
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
Directory DirectoryHandler<Storage>::Get(
    const DirectoryId& parent_id, const DirectoryId& directory_id) const {
  DataMapPtr data_map(GetDataMap(parent_id, directory_id));
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
void DirectoryHandler<Storage>::Delete(const Directory& directory) {
  DataMapPtr data_map(GetDataMap(directory.parent_id, directory.listing->directory_id()));
  SelfEncryptor self_encryptor(data_map, *storage_);
  self_encryptor.DeleteAllChunks();
  MutableData::Name name(directory.listing->directory_id());
  storage_->Delete(name);
}

template <typename Storage>
typename DirectoryHandler<Storage>::DataMapPtr DirectoryHandler<Storage>::GetDataMap(
    const DirectoryId& parent_id, const DirectoryId& directory_id) const {
  MutableData::Name name = MutableData::Name(directory_id);
  MutableData directory(storage_->Get(name).get());
  auto data_map(std::make_shared<DataMap>());
  encrypt::DecryptDataMap(parent_id, directory_id, directory.data().string(), data_map);
  return data_map;
}

}  // namespace detail
}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_HANDLER_H_
