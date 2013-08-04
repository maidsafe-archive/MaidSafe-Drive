/* Copyright 2011 MaidSafe.net limited

This MaidSafe Software is licensed under the MaidSafe.net Commercial License, version 1.0 or later,
and The General Public License (GPL), version 3. By contributing code to this project You agree to
the terms laid out in the MaidSafe Contributor Agreement, version 1.0, found in the root directory
of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also available at:

http://www.novinet.com/license

Unless required by applicable law or agreed to in writing, software distributed under the License is
distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing permissions and limitations under the
License.
*/

#ifndef MAIDSAFE_DRIVE_DIRECTORY_LISTING_HANDLER_H_
#define MAIDSAFE_DRIVE_DIRECTORY_LISTING_HANDLER_H_

#include <memory>
#include <string>
#include <utility>
#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

#include "boost/algorithm/string/find.hpp"
#include "boost/assert.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/fstream.hpp"
#include "boost/filesystem/path.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

#include "maidsafe/common/rsa.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/drive_api.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/return_codes.h"

namespace bptime = boost::posix_time;

namespace maidsafe {
namespace drive {

namespace test { class DirectoryListingHandlerTest; }

const size_t kMaxAttempts = 3;

struct DirectoryData {
  DirectoryData(const DirectoryId& parent_id_in, DirectoryListingPtr dir_listing)
      : parent_id(parent_id_in),
        listing(dir_listing),
        last_save(boost::posix_time::microsec_clock::universal_time()),
        last_change(kMaidSafeEpoch),
        content_changed(false) {}
  DirectoryData()
      : parent_id(),
        listing(),
        last_save(boost::posix_time::microsec_clock::universal_time()),
        last_change(kMaidSafeEpoch),
        content_changed(false) {}
  DirectoryId parent_id;
  DirectoryListingPtr listing;
  bptime::ptime last_save, last_change;
  bool content_changed;
};

template<typename Storage>
class DirectoryListingHandler {
 public:
  typedef passport::Maid Maid;
  typedef std::pair<DirectoryData, uint32_t> DirectoryType;

  enum { kOwnerValue, kGroupValue, kWorldValue, kInvalidValue };

  DirectoryListingHandler(Storage& storage,
                          const Maid& maid,
                          const Identity& unique_user_id,
                          std::string root_parent_id);
  // Virtual destructor to allow inheritance in testing.
  virtual ~DirectoryListingHandler();

  Identity unique_user_id() const { return unique_user_id_; }
  Identity root_parent_id() const { return root_parent_id_; }
  DirectoryType GetFromPath(const boost::filesystem::path& relative_path);
  // Adds a directory or file represented by meta_data and relative_path to the appropriate parent
  // directory listing.  If the element is a directory, a new directory listing is created and
  // stored.  The parent directory's ID is returned in parent_id and its parent directory's ID is
  // returned in grandparent_id.
  void AddElement(const boost::filesystem::path& relative_path,
                  const MetaData& meta_data,
                  DirectoryId* grandparent_id,
                  DirectoryId* parent_id);
  // Deletes the directory or file represented by relative_path from the appropriate parent
  // directory listing.  If the element is a directory, its directory listing is deleted.
  // meta_data is filled with the element's details if found.  If the element is a file, this
  // allows the caller to delete any corresponding chunks.  If save_changes is true, the parent
  // directory listing is stored after the deletion.
  void DeleteElement(const boost::filesystem::path& relative_path, MetaData& meta_data);
  bool CanDelete(const boost::filesystem::path& relative_path);
  void RenameElement(const boost::filesystem::path& old_relative_path,
                     const boost::filesystem::path& new_relative_path,
                     MetaData& meta_data,
                     int64_t& reclaimed_space);
  void UpdateParentDirectoryListing(const boost::filesystem::path& parent_path,
                                    MetaData meta_data);

  void SetWorldReadWrite();
  void SetWorldReadOnly();

  Storage& storage() const { return storage_; }

  friend class test::DirectoryListingHandlerTest;

 protected:
  DirectoryListingHandler(const DirectoryListingHandler&);
  DirectoryListingHandler& operator=(const DirectoryListingHandler&);
  DirectoryData RetrieveFromStorage(const DirectoryId& parent_id,
                                    const DirectoryId& directory_id,
                                    int directory_type) const;
  void PutToStorage(const DirectoryType& directory);
  void DeleteStored(const DirectoryId& parent_id,
                    const DirectoryId& directory_id,
                    int directory_type);
  void RetrieveDataMap(const DirectoryId& parent_id,
                       const DirectoryId& directory_id,
                       int directory_type,
                       DataMapPtr data_map) const;
  bool IsDirectory(const MetaData& meta_data) const;
  void GetParentAndGrandparent(const boost::filesystem::path& relative_path,
                               DirectoryType* grandparent,
                               DirectoryType* parent,
                               MetaData* parent_meta_data);
  bool RenameTargetCanBeRemoved(const boost::filesystem::path& new_relative_path,
                                const MetaData& target_meta_data);
  int GetDirectoryType(const boost::filesystem::path& relative_path);
  bool CanAdd(const boost::filesystem::path& relative_path);
  bool CanRename(const boost::filesystem::path& from_path, const boost::filesystem::path& to_path);
  void RenameSameParent(const boost::filesystem::path& old_relative_path,
                        const boost::filesystem::path& new_relative_path,
                        MetaData& meta_data,
                        int64_t& reclaimed_space);
  void RenameDifferentParent(const boost::filesystem::path& old_relative_path,
                             const boost::filesystem::path& new_relative_path,
                             MetaData& meta_data,
                             int64_t& reclaimed_space);
  void ReStoreDirectories(const boost::filesystem::path& relative_path, int directory_type);

 private:
  Storage& storage_;
  const Maid kMaid_;
  Identity unique_user_id_, root_parent_id_;
  boost::filesystem::path relative_root_;
  bool world_is_writeable_;
};


template<typename Storage>
DirectoryListingHandler<Storage>::DirectoryListingHandler(Storage& storage,
                                                          const Maid& maid,
                                                          const Identity& unique_user_id,
                                                          std::string root_parent_id)
    : storage_(storage),
      kMaid_(maid),
      unique_user_id_(),
      root_parent_id_(),
      relative_root_(boost::filesystem::path("/").make_preferred()),
      world_is_writeable_(true) {
  if (unique_user_id.string().empty())
    ThrowError(CommonErrors::uninitialised);

  if (root_parent_id.empty()) {
    std::string const root_parent_id = RandomString(64);
    unique_user_id_ = unique_user_id;
    root_parent_id_ = Identity(root_parent_id);
    // First run, setup working directories.
    // Root/Parent.
    MetaData root_meta_data(relative_root_, true);
    DirectoryListingPtr root_parent_directory(new DirectoryListing(root_parent_id_)),
                        root_directory(new DirectoryListing(*root_meta_data.directory_id));
    DirectoryData root_parent(unique_user_id, root_parent_directory),
                  root(root_parent_id_, root_directory);

    root_parent.listing->AddChild(root_meta_data);
    PutToStorage(std::make_pair(root_parent, kOwnerValue));
    // Owner.
    MetaData owner_meta_data(kOwner, true);
    DirectoryListingPtr owner_directory(new DirectoryListing(*owner_meta_data.directory_id));
    DirectoryData owner(root.listing->directory_id(), owner_directory);
    PutToStorage(std::make_pair(owner, kOwnerValue));
    // Group.
    MetaData group_meta_data(kGroup, true), group_services_meta_data(kServices, true);
    DirectoryListingPtr group_directory(new DirectoryListing(*group_meta_data.directory_id)),
            group_services_directory(new DirectoryListing(*group_services_meta_data.directory_id));
    DirectoryData group(root.listing->directory_id(), group_directory),
                  group_services(group.listing->directory_id(), group_services_directory);
    PutToStorage(std::make_pair(group_services, kGroupValue));
    group.listing->AddChild(group_services_meta_data);
    PutToStorage(std::make_pair(group, kGroupValue));
    // World.
    MetaData world_meta_data(kWorld, true), world_services_meta_data("Services", true);
    DirectoryListingPtr world_directory(new DirectoryListing(*world_meta_data.directory_id)),
            world_services_directory(new DirectoryListing(*world_services_meta_data.directory_id));
    DirectoryData world(root.listing->directory_id(), world_directory),
                  world_services(world.listing->directory_id(), world_services_directory);
    PutToStorage(std::make_pair(world_services, kWorldValue));
    world.listing->AddChild(world_services_meta_data);
    PutToStorage(std::make_pair(world, kWorldValue));

    root.listing->AddChild(owner_meta_data);
    root.listing->AddChild(group_meta_data);
    root.listing->AddChild(world_meta_data);
    PutToStorage(std::make_pair(root, kOwnerValue));
  } else {
    unique_user_id_ = unique_user_id;
    root_parent_id_ = Identity(root_parent_id);
  }
}

template<typename Storage>
DirectoryListingHandler<Storage>::~DirectoryListingHandler() {}

template<typename Storage>
typename DirectoryListingHandler<Storage>::DirectoryType
    DirectoryListingHandler<Storage>::GetFromPath(const boost::filesystem::path& relative_path) {
  int directory_type(GetDirectoryType(relative_path));
  // Get root directory listing.
  DirectoryData directory(RetrieveFromStorage(unique_user_id_, root_parent_id_, kOwnerValue));
  // Get successive directory listings until found.
  MetaData meta_data;
  bool found_root = false;
  for (auto itr(relative_path.begin()); itr != relative_path.end(); ++itr) {
    // for itr == begin, path is "/" which is wrong for Windows.
    if (itr == relative_path.begin()) {
      directory.listing->GetChild(relative_root_, meta_data);
      found_root = true;
    } else {
      directory.listing->GetChild((*itr), meta_data);
    }

    if (!meta_data.directory_id)
      ThrowError(CommonErrors::invalid_parameter);
    directory = RetrieveFromStorage(directory.listing->directory_id(),
                                    *meta_data.directory_id,
                                    found_root ? kOwnerValue : directory_type);
    if (found_root)
      found_root = false;
  }
  return std::make_pair(directory, directory_type);
}

template<typename Storage>
void DirectoryListingHandler<Storage>::AddElement(const boost::filesystem::path& relative_path,
                                                  const MetaData& meta_data,
                                                  DirectoryId* grandparent_id,
                                                  DirectoryId* parent_id) {
  if (!CanAdd(relative_path))
    ThrowError(CommonErrors::invalid_parameter);

  int directory_type(GetDirectoryType(relative_path));
  DirectoryType grandparent, parent;
  MetaData parent_meta_data;

  GetParentAndGrandparent(relative_path, &grandparent, &parent, &parent_meta_data);
  parent.first.listing->AddChild(meta_data);

  if (IsDirectory(meta_data)) {
    DirectoryData directory(parent.first.listing->directory_id(),
                              DirectoryListingPtr(
                                  new DirectoryListing(*meta_data.directory_id)));
    try {
      PutToStorage(std::make_pair(directory, directory_type));
    }
    catch(const std::exception& exception) {
      parent.first.listing->RemoveChild(meta_data);
      boost::throw_exception(exception);
    }
  }

  parent_meta_data.UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent_meta_data.attributes.st_ctime = parent_meta_data.attributes.st_mtime;
  if (IsDirectory(meta_data))
    ++parent_meta_data.attributes.st_nlink;
#endif
  grandparent.first.listing->UpdateChild(parent_meta_data, true);

  try {
    PutToStorage(parent);
  }
  catch(const std::exception& exception) {
    parent.first.listing->RemoveChild(meta_data);
    boost::throw_exception(exception);
  }

  PutToStorage(grandparent);

  if (grandparent_id)
    *grandparent_id = grandparent.first.listing->directory_id();
  if (parent_id)
    *parent_id = parent.first.listing->directory_id();
}

template<typename Storage>
void DirectoryListingHandler<Storage>::DeleteElement(const boost::filesystem::path& relative_path,
                                                     MetaData& meta_data) {
  DirectoryType grandparent, parent;
  MetaData parent_meta_data;
  GetParentAndGrandparent(relative_path, &grandparent, &parent, &parent_meta_data);
  parent.first.listing->GetChild(relative_path.filename(), meta_data);

  if (IsDirectory(meta_data)) {
    DirectoryType directory(GetFromPath(relative_path));
    DeleteStored(parent.first.listing->directory_id(),
                 *meta_data.directory_id,
                 directory.second);
  }

  parent.first.listing->RemoveChild(meta_data);
  parent_meta_data.UpdateLastModifiedTime();

#ifndef MAIDSAFE_WIN32
  parent_meta_data.attributes.st_ctime = parent_meta_data.attributes.st_mtime;
  if (IsDirectory(meta_data))
    --parent_meta_data.attributes.st_nlink;
#endif

  try {
    grandparent.first.listing->UpdateChild(parent_meta_data, true);
  }
  catch(...) { /*Non-critical*/ }

#ifndef MAIDSAFE_WIN32
  PutToStorage(grandparent);
#endif
  PutToStorage(parent);

  return;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::RenameElement(
        const boost::filesystem::path& old_relative_path,
        const boost::filesystem::path& new_relative_path,
        MetaData& meta_data,
        int64_t& reclaimed_space) {
  if (old_relative_path == new_relative_path)
    return;
  if (!CanRename(old_relative_path, new_relative_path))
    ThrowError(CommonErrors::invalid_parameter);

  if (old_relative_path.parent_path() == new_relative_path.parent_path())
    RenameSameParent(old_relative_path, new_relative_path, meta_data, reclaimed_space);
  else
    RenameDifferentParent(old_relative_path, new_relative_path, meta_data, reclaimed_space);
  return;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::RenameSameParent(
        const boost::filesystem::path& old_relative_path,
        const boost::filesystem::path& new_relative_path,
        MetaData& meta_data,
        int64_t& reclaimed_space) {
  DirectoryType grandparent, parent;
  MetaData parent_meta_data;
  GetParentAndGrandparent(old_relative_path, &grandparent, &parent, &parent_meta_data);

#ifndef MAIDSAFE_WIN32
  struct stat old;
  old.st_ctime = meta_data.attributes.st_ctime;
  old.st_mtime = meta_data.attributes.st_mtime;
  time(&meta_data.attributes.st_mtime);
  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
#endif

  if (!parent.first.listing->HasChild(new_relative_path.filename())) {
    parent.first.listing->RemoveChild(meta_data);
    meta_data.name = new_relative_path.filename();
    parent.first.listing->AddChild(meta_data);
  } else {
    MetaData old_meta_data;
    try {
      parent.first.listing->GetChild(new_relative_path.filename(), old_meta_data);
    }
    catch(const std::exception& exception) {
#ifndef MAIDSAFE_WIN32
      meta_data.attributes.st_ctime = old.st_ctime;
      meta_data.attributes.st_mtime = old.st_mtime;
#endif
      boost::throw_exception(exception);
    }
    parent.first.listing->RemoveChild(old_meta_data);
    reclaimed_space = old_meta_data.GetAllocatedSize();
    parent.first.listing->RemoveChild(meta_data);
    meta_data.name = new_relative_path.filename();
    parent.first.listing->AddChild(meta_data);
  }

#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&parent_meta_data.last_write_time);
#else
  parent_meta_data.attributes.st_ctime =
      parent_meta_data.attributes.st_mtime =
      meta_data.attributes.st_mtime;
//   if (!same_parent && IsDirectory(meta_data)) {
//     --parent_meta_data.attributes.st_nlink;
//     ++new_parent_meta_data.attributes.st_nlink;
//     new_parent_meta_data.attributes.st_ctime =
//         new_parent_meta_data.attributes.st_mtime =
//         parent_meta_data.attributes.st_mtime;
//   }
#endif
  PutToStorage(parent);

#ifndef MAIDSAFE_WIN32
  try {
    grandparent.first.listing->UpdateChild(parent_meta_data, true);
  }
  catch(...) { /*Non-critical*/ }
  PutToStorage(grandparent);
#endif
  return;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::RenameDifferentParent(
        const boost::filesystem::path& old_relative_path,
        const boost::filesystem::path& new_relative_path,
        MetaData& meta_data,
        int64_t& reclaimed_space) {
  DirectoryType old_grandparent, old_parent, new_grandparent, new_parent;
  MetaData old_parent_meta_data, new_parent_meta_data;
  GetParentAndGrandparent(old_relative_path,
                          &old_grandparent,
                          &old_parent,
                          &old_parent_meta_data);
  GetParentAndGrandparent(new_relative_path,
                          &new_grandparent,
                          &new_parent,
                          &new_parent_meta_data);
#ifndef MAIDSAFE_WIN32
  struct stat old;
  old.st_ctime = meta_data.attributes.st_ctime;
  old.st_mtime = meta_data.attributes.st_mtime;
  time(&meta_data.attributes.st_mtime);
  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
#endif

  if (IsDirectory(meta_data)) {
    DirectoryType directory(GetFromPath(old_relative_path));
    if (directory.second != new_parent.second)  {
      directory.first.listing->ResetChildrenIterator();
      MetaData child_meta_data;
      while (directory.first.listing->GetChildAndIncrementItr(child_meta_data)) {
        if (IsDirectory(child_meta_data)) {
          ReStoreDirectories(old_relative_path / child_meta_data.name, new_parent.second);
        }
      }
    }
    DeleteStored(directory.first.parent_id,
                 directory.first.listing->directory_id(),
                 directory.second);
    directory.first.parent_id = new_parent.first.listing->directory_id();
    directory.second = new_parent.second;
    PutToStorage(directory);
  }

  old_parent.first.listing->RemoveChild(meta_data);

  if (!new_parent.first.listing->HasChild(new_relative_path.filename())) {
    meta_data.name = new_relative_path.filename();
    new_parent.first.listing->AddChild(meta_data);
  } else {
    MetaData old_meta_data;
    try {
      new_parent.first.listing->GetChild(new_relative_path.filename(), old_meta_data);
    }
    catch(const std::exception& exception) {
#ifndef MAIDSAFE_WIN32
      meta_data.attributes.st_ctime = old.st_ctime;
      meta_data.attributes.st_mtime = old.st_mtime;
#endif
      boost::throw_exception(exception);
    }
    new_parent.first.listing->RemoveChild(old_meta_data);
    reclaimed_space = old_meta_data.GetAllocatedSize();
    meta_data.name = new_relative_path.filename();
    new_parent.first.listing->AddChild(meta_data);
  }

#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&old_parent_meta_data.last_write_time);
#else
  old_parent_meta_data.attributes.st_ctime =
      old_parent_meta_data.attributes.st_mtime =
      meta_data.attributes.st_mtime;
  if (IsDirectory(meta_data)) {
    --old_parent_meta_data.attributes.st_nlink;
    ++new_parent_meta_data.attributes.st_nlink;
    new_parent_meta_data.attributes.st_ctime =
        new_parent_meta_data.attributes.st_mtime =
        old_parent_meta_data.attributes.st_mtime;
  }
#endif
  PutToStorage(old_parent);
  PutToStorage(new_parent);

#ifndef MAIDSAFE_WIN32
  try {
    old_grandparent.first.listing->UpdateChild(old_parent_meta_data, true);
  }
  catch(...) { /*Non-critical*/ }
  PutToStorage(old_grandparent);
#endif
  return;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::ReStoreDirectories(
        const boost::filesystem::path& relative_path,
        int directory_type) {
  DirectoryType directory(GetFromPath(relative_path));
  directory.first.listing->ResetChildrenIterator();
  MetaData meta_data;

  while (directory.first.listing->GetChildAndIncrementItr(meta_data)) {
    if (IsDirectory(meta_data)) {
      ReStoreDirectories(relative_path / meta_data.name, directory_type);
    }
  }

  DeleteStored(directory.first.parent_id,
               directory.first.listing->directory_id(),
               directory.second);
  directory.second = directory_type;
  PutToStorage(directory);
  return;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::UpdateParentDirectoryListing(
        const boost::filesystem::path& parent_path,
        MetaData meta_data) {
  DirectoryType parent = GetFromPath(parent_path);
  parent.first.listing->UpdateChild(meta_data, true);
  PutToStorage(parent);
  return;
}

template<typename Storage>
bool DirectoryListingHandler<Storage>::IsDirectory(const MetaData& meta_data) const {
  return static_cast<bool>(meta_data.directory_id);
}

template<typename Storage>
void DirectoryListingHandler<Storage>::GetParentAndGrandparent(
        const boost::filesystem::path& relative_path,
        DirectoryType* grandparent,
        DirectoryType* parent,
        MetaData* parent_meta_data) {
  *grandparent = GetFromPath(relative_path.parent_path().parent_path());
  grandparent->first.listing->GetChild(relative_path.parent_path().filename(),
                                       *parent_meta_data);
  if (!(parent_meta_data->directory_id)) {
    ThrowError(CommonErrors::invalid_parameter);
  }
  *parent = GetFromPath(relative_path.parent_path());
  return;
}

template<typename Storage>
DirectoryData DirectoryListingHandler<Storage>::RetrieveFromStorage(const DirectoryId& parent_id,
                                                                    const DirectoryId& directory_id,
                                                                    int directory_type) const {
  if (directory_type == kWorldValue) {
    WorldDirectory::name_type name(directory_id);
    WorldDirectory::serialised_type serialised_data;
    serialised_data.data = detail::Get<Storage, WorldDirectory>()(storage_, name);
    WorldDirectory world_directory(name, serialised_data);
    Identity id(std::string("", 64));
    DirectoryData directory(parent_id, std::make_shared<DirectoryListing>(id));
    directory.listing->Parse(world_directory.data().string());
    directory.parent_id = parent_id;
    return directory;
  }

  DataMapPtr data_map(new encrypt::DataMap);
  // Retrieve encrypted datamap.
  RetrieveDataMap(parent_id, directory_id, directory_type, data_map);
  // Decrypt serialised directory listing.
  encrypt::SelfEncryptor<Storage> self_encryptor(data_map, storage_);
  uint32_t data_map_chunks_size(static_cast<uint32_t>(data_map->chunks.size()));
  uint32_t data_map_size;
  if (data_map_chunks_size != 0) {
    data_map_size = (data_map_chunks_size - 1) * data_map->chunks[0].size +
                    data_map->chunks.rbegin()->size;
  } else {
    data_map_size = static_cast<uint32_t>(data_map->content.size());
  }
  std::string serialised_directory_listing(data_map_size, 0);
  if (!self_encryptor.Read(const_cast<char*>(serialised_directory_listing.c_str()),
                           data_map_size,
                           0)) {
    ThrowError(CommonErrors::invalid_parameter);
  }
  // Parse serialised directory listing.
  Identity id(std::string("", 64));
  DirectoryData directory(parent_id, std::make_shared<DirectoryListing>(id));
  directory.listing->Parse(serialised_directory_listing);
  assert(directory.listing->directory_id() == directory_id);
  return directory;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::PutToStorage(const DirectoryType& directory) {
  // Serialise directory listing.
  std::string serialised_directory_listing;
  directory.first.listing->Serialise(serialised_directory_listing);

  if (directory.second == kWorldValue) {
    try {
      DeleteStored(directory.first.parent_id, directory.first.listing->directory_id(), kWorldValue);
    }
    catch(...) {}
    // Store serialised listing.
    WorldDirectory world_directory(WorldDirectory::name_type(directory.first.listing->directory_id()),
                                   NonEmptyString(serialised_directory_listing));
    detail::Put<Storage, WorldDirectory>()(storage_, world_directory);
    return;
  }

  if (directory.second == kOwnerValue) {
    try {
      DeleteStored(directory.first.parent_id, directory.first.listing->directory_id(), kOwnerValue);
    }
    catch(...) {}
  } else {
    try {
      DeleteStored(directory.first.parent_id, directory.first.listing->directory_id(), kGroupValue);
    }
    catch(...) {}
  }

  // Self-encrypt serialised directory listing.
  DataMapPtr data_map(new encrypt::DataMap);
  {
    encrypt::SelfEncryptor<Storage> self_encryptor(data_map, storage_);
    assert(serialised_directory_listing.size() <= std::numeric_limits<uint32_t>::max());
    if (!self_encryptor.Write(serialised_directory_listing.c_str(),
                              static_cast<uint32_t>(serialised_directory_listing.size()),
                              0)) {
      ThrowError(CommonErrors::invalid_parameter);
    }
  }
  // Encrypt directory listing's datamap.
  asymm::CipherText encrypted_data_map =
                      encrypt::EncryptDataMap(directory.first.parent_id,
                                              directory.first.listing->directory_id(),
                                              data_map);
  if (directory.second == kOwnerValue) {
    // Store the encrypted datamap.
    OwnerDirectory owner_directory(OwnerDirectory::name_type(directory.first.listing->directory_id()),
                                   encrypted_data_map,
                                   kMaid_.private_key());
    detail::Put<Storage, OwnerDirectory>()(storage_, owner_directory);
  } else if (directory.second == kGroupValue) {
    // Store the encrypted datamap.
    GroupDirectory group_directory(GroupDirectory::name_type(directory.first.listing->directory_id()),
                                   encrypted_data_map,
                                   kMaid_.private_key());
    detail::Put<Storage, GroupDirectory>()(storage_, group_directory);
  } else {
    ThrowError(CommonErrors::not_a_directory);
  }
  return;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::DeleteStored(const DirectoryId& parent_id,
                                                    const DirectoryId& directory_id,
                                                    int directory_type) {
  if (directory_type != kWorldValue) {
    DataMapPtr data_map(new encrypt::DataMap);
    RetrieveDataMap(parent_id, directory_id, directory_type, data_map);
    encrypt::SelfEncryptor<Storage> self_encryptor(data_map, storage_);
    self_encryptor.DeleteAllChunks();
  }
  switch (directory_type) {
    case kOwnerValue: {
      OwnerDirectory::name_type name(directory_id);
      detail::Delete<Storage, OwnerDirectory>()(storage_, name);
      break;
    }
    case kGroupValue: {
      GroupDirectory::name_type name(directory_id);
      detail::Delete<Storage, GroupDirectory>()(storage_, name);
      break;
    }
    case kWorldValue: {
      WorldDirectory::name_type name(directory_id);
      detail::Delete<Storage, WorldDirectory>()(storage_, name);
      break;
    }
    default:
      LOG(kError) << "Invalid directory type.";
  }
  return;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::RetrieveDataMap(const DirectoryId& parent_id,
                                                       const DirectoryId& directory_id,
                                                       int directory_type,
                                                       DataMapPtr data_map) const {
  assert(data_map);
  if (directory_type == kOwnerValue) {
    OwnerDirectory::name_type name(directory_id);
    OwnerDirectory::serialised_type serialised_data;
    serialised_data.data = detail::Get<Storage, OwnerDirectory>()(storage_, name);
    // Parse.
    OwnerDirectory owner_directory(name, serialised_data);
    // Generate data map.
    encrypt::DecryptDataMap(parent_id,
                            directory_id,
                            owner_directory.data().string(),
                            data_map);
  } else if (directory_type == kGroupValue) {
    GroupDirectory::name_type name(directory_id);
    GroupDirectory::serialised_type serialised_data;
    serialised_data.data = detail::Get<Storage, GroupDirectory>()(storage_, name);
    // Parse.
    GroupDirectory group_directory(name, serialised_data);
    // Generate data map.
    encrypt::DecryptDataMap(parent_id,
                            directory_id,
                            group_directory.data().string(),
                            data_map);
  } else {
    ThrowError(CommonErrors::invalid_parameter);
  }
  return;
}

// If the target is a file it can be deleted.  On POSIX, if it's a non-empty
// directory, it can be deleted.
#ifndef MAIDSAFE_WIN32
template<typename Storage>
bool DirectoryListingHandler<Storage>::RenameTargetCanBeRemoved(const boost::filesystem::path& new_relative_path,
                                                                const MetaData& target_meta_data) {
  bool can_be_removed = !IsDirectory(target_meta_data);
  if (!can_be_removed) {
    DirectoryListingPtr target_directory_listing = GetFromPath(new_relative_path).first.listing;
    if (target_directory_listing)
      can_be_removed = target_directory_listing->empty();
  }
  return can_be_removed;
}
#endif

template<typename Storage>
int DirectoryListingHandler<Storage>::GetDirectoryType(const boost::filesystem::path& relative_path) {
  if (relative_path == kEmptyPath || relative_path == kRoot)
    return kOwnerValue;
  boost::filesystem::path::iterator it(relative_path.begin());
  ++it;
  if (it->filename() == kEmptyPath || it->filename() == kOwner)
    return kOwnerValue;
  else if (it->filename() == kGroup)
    return kGroupValue;
  else if (it->filename() == kWorld)
    return kWorldValue;
  else
    ThrowError(CommonErrors::invalid_parameter);
  return kInvalidValue;
}

template<typename Storage>
bool DirectoryListingHandler<Storage>::CanAdd(const boost::filesystem::path& relative_path) {
  int directory_type(GetDirectoryType(relative_path));
  if (directory_type == kGroupValue || (directory_type == kWorldValue && !world_is_writeable_))
    return false;
  fs::path relative_parent_filename(relative_path.parent_path().filename());
  if (relative_parent_filename == kEmptyPath || relative_parent_filename == kRoot)
    return false;
  return true;
}

template<typename Storage>
bool DirectoryListingHandler<Storage>::CanDelete(const fs::path& relative_path) {
  int directory_type(GetDirectoryType(relative_path));
  if (directory_type == kGroupValue
      || (directory_type == kWorldValue && !world_is_writeable_)
      || directory_type == kInvalidValue)
    return false;
  boost::filesystem::path relative_path_parent_filename(relative_path.parent_path().filename());
  if (relative_path_parent_filename == kEmptyPath
      || relative_path_parent_filename == kRoot
      || (relative_path_parent_filename == kWorld && relative_path.filename() == kServices))
    return false;
  return true;
}

template<typename Storage>
bool DirectoryListingHandler<Storage>::CanRename(const boost::filesystem::path& from_path,
                                                 const boost::filesystem::path& to_path) {
  boost::filesystem::path from_filename(from_path.filename()), to_filename(to_path.filename()),
           from_parent_filename(from_path.parent_path().filename()),
           to_parent_filename(to_path.parent_path().filename());
  if ((from_filename == kRoot || to_filename == kRoot)
      || (from_parent_filename == kRoot || to_parent_filename == kRoot)
      || (from_filename == kOwner && from_parent_filename == kRoot)
      || (to_filename == kOwner && to_parent_filename == kRoot)
      || (from_filename == kGroup && from_parent_filename == kRoot)
      || (to_filename == kGroup && to_parent_filename == kRoot)
      || (from_filename == kWorld && from_parent_filename == kRoot)
      || (to_filename == kWorld && to_parent_filename == kRoot))
    return false;
  int from_type(GetDirectoryType(from_path)),
      to_type(GetDirectoryType(to_path));
  if (from_type != to_type)
    if (from_type == kGroupValue || to_type == kGroupValue
        || (from_type != kWorldValue && to_type == kWorldValue && !world_is_writeable_))
      return false;
  if (from_type == kWorldValue && from_parent_filename == kWorld && from_filename == kServices)
    return false;
  return true;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::SetWorldReadWrite() {
  world_is_writeable_ = true;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::SetWorldReadOnly() {
  world_is_writeable_ = false;
}

}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_LISTING_HANDLER_H_
