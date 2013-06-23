/*******************************************************************************
 *  Copyright 2011 maidsafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 *******************************************************************************
 */

#include "maidsafe/drive/directory_listing_handler.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

#include "boost/algorithm/string/find.hpp"
#include "boost/assert.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/fstream.hpp"

#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/return_codes.h"
#include "maidsafe/drive/utils.h"


namespace args = std::placeholders;

namespace maidsafe {

namespace drive {

DirectoryListingHandler::DirectoryListingHandler(ClientNfs& client_nfs,
                                                 DataStore& data_store,
                                                 const Maid& maid,
                                                 const Identity& unique_user_id,
                                                 std::string root_parent_id)
    : client_nfs_(client_nfs),
      data_store_(data_store),
      kMaid_(maid),
      unique_user_id_(),
      root_parent_id_(),
      relative_root_(fs::path("/").make_preferred()),
      world_is_writeable_(true) {
  if (unique_user_id.string().empty())
    ThrowError(CommonErrors::uninitialised);

  if (root_parent_id.empty()) {
    std::string const root_parent_id = RandomString(64);
    unique_user_id_ = unique_user_id;
    root_parent_id_ = Identity(root_parent_id);
    // First run, setup working directories...
    // Root/Parent...
    MetaData root_meta_data(relative_root_, true);
    DirectoryListingPtr root_parent_directory(new DirectoryListing(root_parent_id_)),
                        root_directory(new DirectoryListing(*root_meta_data.directory_id));
    DirectoryData root_parent(unique_user_id, root_parent_directory),
                  root(root_parent_id_, root_directory);

    root_parent.listing->AddChild(root_meta_data);
    PutToStorage(std::make_pair(root_parent, kOwnerValue));
    // Owner...
    MetaData owner_meta_data(kOwner, true);
    DirectoryListingPtr owner_directory(new DirectoryListing(*owner_meta_data.directory_id));
    DirectoryData owner(root.listing->directory_id(), owner_directory);
    PutToStorage(std::make_pair(owner, kOwnerValue));
    // Group...
    MetaData group_meta_data(kGroup, true), group_services_meta_data(kServices, true);
    DirectoryListingPtr group_directory(new DirectoryListing(*group_meta_data.directory_id)),
            group_services_directory(new DirectoryListing(*group_services_meta_data.directory_id));
    DirectoryData group(root.listing->directory_id(), group_directory),
                  group_services(group.listing->directory_id(), group_services_directory);
    PutToStorage(std::make_pair(group_services, kGroupValue));
    group.listing->AddChild(group_services_meta_data);
    PutToStorage(std::make_pair(group, kGroupValue));
    // World...
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

DirectoryListingHandler::~DirectoryListingHandler() {}

DirectoryListingHandler::DirectoryType
    DirectoryListingHandler::GetFromPath(const fs::path& relative_path) {
  int directory_type(GetDirectoryType(relative_path));
  // Get root directory listing
  DirectoryData directory(RetrieveFromStorage(unique_user_id_, root_parent_id_, kOwnerValue));
  // Get successive directory listings until found
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

void DirectoryListingHandler::AddElement(const fs::path& relative_path,
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

void DirectoryListingHandler::DeleteElement(const fs::path& relative_path, MetaData& meta_data) {
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

void DirectoryListingHandler::RenameElement(const fs::path& old_relative_path,
                                            const fs::path& new_relative_path,
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

void DirectoryListingHandler::RenameSameParent(const fs::path& old_relative_path,
                                               const fs::path& new_relative_path,
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

void DirectoryListingHandler::RenameDifferentParent(const fs::path& old_relative_path,
                                                    const fs::path& new_relative_path,
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
  // TODO(Fraser#5#) 2011-06-02 - Check Windows needs this.
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

void DirectoryListingHandler::ReStoreDirectories(const fs::path& relative_path,
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

void DirectoryListingHandler::UpdateParentDirectoryListing(const fs::path& parent_path,
                                                           MetaData meta_data) {
  DirectoryType parent = GetFromPath(parent_path);
  parent.first.listing->UpdateChild(meta_data, true);
  PutToStorage(parent);
  return;
}

bool DirectoryListingHandler::IsDirectory(const MetaData& meta_data) const {
  return static_cast<bool>(meta_data.directory_id);
}

void DirectoryListingHandler::GetParentAndGrandparent(const fs::path& relative_path,
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

DirectoryData DirectoryListingHandler::RetrieveFromStorage(const DirectoryId& parent_id,
                                                           const DirectoryId& directory_id,
                                                           int directory_type) const {
  if (directory_type == kWorldValue) {
    WorldDirectoryNameType name(directory_id);
    WorldDirectorySerialisedType serialised_data;
#ifdef TESTING
      serialised_data.data = data_store_.Get(name);
#else
      client_nfs_.Get<WorldDirectory>(name, nullptr);
#endif
    WorldDirectory world_directory(name, serialised_data);
    Identity id(std::string("", 64));
    DirectoryData directory(parent_id, std::make_shared<DirectoryListing>(id));
    directory.listing->Parse(world_directory.data().string());
    directory.parent_id = parent_id;
    return directory;
  }

  DataMapPtr data_map(new encrypt::DataMap);
  // Retrieve encrypted datamap...
  RetrieveDataMap(parent_id, directory_id, directory_type, data_map);
  // Decrypt serialised directory listing...
  encrypt::SelfEncryptor self_encryptor(data_map, client_nfs_, data_store_);
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
  // Parse serialised directory listing...
  Identity id(std::string("", 64));
  DirectoryData directory(parent_id, std::make_shared<DirectoryListing>(id));
  directory.listing->Parse(serialised_directory_listing);
  assert(directory.listing->directory_id() == directory_id);
  return directory;
}

void DirectoryListingHandler::PutToStorage(const DirectoryType& directory) {
  // Serialise directory listing
  std::string serialised_directory_listing;
  directory.first.listing->Serialise(serialised_directory_listing);

  if (directory.second == kWorldValue) {
    // Store serialised listing...
    WorldDirectory world_directory(WorldDirectoryNameType(directory.first.listing->directory_id()),
                                   NonEmptyString(serialised_directory_listing));
#ifdef TESTING
      data_store_.Put(world_directory.name(), world_directory.Serialise());
#else
      client_nfs_.Put<WorldDirectory>(world_directory,
                                      passport::PublicPmid::name_type(world_directory.name()),
                                      nullptr);
#endif
    return;
  }

  // Self-encrypt serialised directory listing
  DataMapPtr data_map(new encrypt::DataMap);
  {
    encrypt::SelfEncryptor self_encryptor(data_map, client_nfs_, data_store_);
    assert(serialised_directory_listing.size() <= std::numeric_limits<uint32_t>::max());
    if (!self_encryptor.Write(serialised_directory_listing.c_str(),
                              static_cast<uint32_t>(serialised_directory_listing.size()),
                              0)) {
      ThrowError(CommonErrors::invalid_parameter);
    }
  }
  // Encrypt directory listing's datamap
  asymm::CipherText encrypted_data_map =
                      encrypt::EncryptDataMap(directory.first.parent_id,
                                              directory.first.listing->directory_id(),
                                              data_map);
  if (directory.second == kOwnerValue) {
    // Store the encrypted datamap
    OwnerDirectory owner_directory(OwnerDirectoryNameType(directory.first.listing->directory_id()),
                                   encrypted_data_map,
                                   kMaid_.private_key());
#ifdef TESTING
      data_store_.Put(owner_directory.name(), owner_directory.Serialise());
#else
      client_nfs_.Put<OwnerDirectory>(owner_directory,
                                      passport::PublicPmid::name_type(owner_directory.name()),
                                      nullptr);
#endif
  } else if (directory.second == kGroupValue) {
    // Store the encrypted datamap
    GroupDirectory group_directory(GroupDirectoryNameType(directory.first.listing->directory_id()),
                                   encrypted_data_map,
                                   kMaid_.private_key());
#ifdef TESTING
      data_store_.Put(group_directory.name(), group_directory.Serialise());
#else
      client_nfs_.Put<GroupDirectory>(group_directory,
                                      passport::PublicPmid::name_type(group_directory.name()),
                                      nullptr);
#endif
  } else {
    ThrowError(CommonErrors::not_a_directory);
  }
  return;
}

void DirectoryListingHandler::DeleteStored(const DirectoryId& parent_id,
                                           const DirectoryId& directory_id,
                                           int directory_type) {
  if (directory_type != kWorldValue) {
    DataMapPtr data_map(new encrypt::DataMap);
    RetrieveDataMap(parent_id, directory_id, directory_type, data_map);
    encrypt::SelfEncryptor self_encryptor(data_map, client_nfs_, data_store_);
    self_encryptor.DeleteAllChunks();
  }
#ifdef TESTING
  switch (directory_type) {
    case kOwnerValue: {
      data_store_.Delete(OwnerDirectoryNameType(directory_id));
      break;
    }
    case kGroupValue: {
      data_store_.Delete(GroupDirectoryNameType(directory_id));
      break;
    }
    case kWorldValue: {
      data_store_.Delete(WorldDirectoryNameType(directory_id));
      break;
    }
    default:
      LOG(kError) << "Invalid directory type.";
  }
#else
  switch (directory_type) {
    case kOwnerValue: {
      client_nfs_.Delete<OwnerDirectory>(OwnerDirectoryNameType(directory_id), nullptr);
      break;
    }
    case kGroupValue: {
      client_nfs_.Delete<GroupDirectory>(GroupDirectoryNameType(directory_id), nullptr);
      break;
    }
    case kWorldValue: {
      client_nfs_.Delete<WorldDirectory>(WorldDirectoryNameType(directory_id), nullptr);
      break;
    }
    default:
      LOG(kError) << "Invalid directory type.";
  }
#endif
  return;
}

void DirectoryListingHandler::RetrieveDataMap(const DirectoryId& parent_id,
                                              const DirectoryId& directory_id,
                                              int directory_type,
                                              DataMapPtr data_map) const {
  assert(data_map);
  if (directory_type == kOwnerValue) {
    OwnerDirectoryNameType name(directory_id);
    OwnerDirectorySerialisedType serialised_data;
#ifdef TESTING
      serialised_data.data = data_store_.Get(name);
#else
      client_nfs_.Get<OwnerDirectory>(name, nullptr);
#endif
    // Parse...
    OwnerDirectory owner_directory(name, serialised_data);
    // Generate data map...
    encrypt::DecryptDataMap(parent_id,
                            directory_id,
                            owner_directory.data().string(),
                            data_map);
  } else if (directory_type == kGroupValue) {
    GroupDirectoryNameType name(directory_id);
    GroupDirectorySerialisedType serialised_data;
#ifdef TESTING
      serialised_data.data = data_store_.Get(name);
#else
      client_nfs_.Get<GroupDirectory>(name, nullptr);
#endif
    // Parse...
    GroupDirectory group_directory(name, serialised_data);
    // Generate data map...
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
bool DirectoryListingHandler::RenameTargetCanBeRemoved(const fs::path& new_relative_path,
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

int DirectoryListingHandler::GetDirectoryType(const fs::path& relative_path) {
  if (relative_path == kEmptyPath || relative_path == kRoot)
    return kOwnerValue;
  fs::path::iterator it(relative_path.begin());
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

bool DirectoryListingHandler::CanAdd(const fs::path& relative_path) {
  int directory_type(GetDirectoryType(relative_path));
  if (directory_type == kGroupValue || (directory_type == kWorldValue && !world_is_writeable_))
    return false;
  fs::path relative_parent_filename(relative_path.parent_path().filename());
  if (relative_parent_filename == kEmptyPath || relative_parent_filename == kRoot)
    return false;
  return true;
}

bool DirectoryListingHandler::CanDelete(const fs::path& relative_path) {
  int directory_type(GetDirectoryType(relative_path));
  if (directory_type == kGroupValue
      || (directory_type == kWorldValue && !world_is_writeable_)
      || directory_type == kInvalidValue)
    return false;
  fs::path relative_path_parent_filename(relative_path.parent_path().filename());
  if (relative_path_parent_filename == kEmptyPath
      || relative_path_parent_filename == kRoot
      || (relative_path_parent_filename == kWorld && relative_path.filename() == kServices))
    return false;
  return true;
}

bool DirectoryListingHandler::CanRename(const fs::path& from_path, const fs::path& to_path) {
  fs::path from_filename(from_path.filename()), to_filename(to_path.filename()),
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

// int DirectoryListingHandler::MoveDirectory(const fs::path& from,
//                                           MetaData& from_meta_data,
//                                           const fs::path& to) {
//  assert(!fs::exists(to));
//  DirectoryType from_grandparent, from_parent, to_grandparent, to_parent;
//  MetaData from_parent_meta_data, to_parent_meta_data;
//  int result(GetParentAndGrandparent(from,
//                                     &from_grandparent,
//                                     &from_parent,
//                                     &from_parent_meta_data));
//  if (result != kSuccess) {
//    LOG(kError) << "Failed to get parent for " << from;
//    return result;
//  }
//  result = from_parent.first.listing->RemoveChild(from_meta_data);
//  if (result != kSuccess) {
//    LOG(kError) << "Failed to remove child " << from_meta_data.name << " from " << from;
//    return result;
//  }
//  result = GetParentAndGrandparent(to, &to_grandparent, &to_parent, &to_parent_meta_data);
//  if (result != kSuccess) {
//    LOG(kError) << "Failed to get parent for " << to;
//    int temp = from_parent.first.listing->AddChild(from_meta_data);
//    if (temp != kSuccess) {
//      LOG(kError) << "Failed to add child " << from_meta_data.name << " back to " << from;
//      return temp;
//    }
//    return result;
//  }
//  fs::path temp_path(from_meta_data.name);
//  from_meta_data.name = to.filename();
//  result = to_parent.first.listing->AddChild(from_meta_data);
//  if (result != kSuccess) {
//    LOG(kError) << "Failed to add child " << from_meta_data.name << " to " << to;
//    from_meta_data.name = temp_path;
//    int temp = from_parent.first.listing->AddChild(from_meta_data);
//    if (temp != kSuccess) {
//      LOG(kError) << "Failed to add child " << from_meta_data.name << " back to " << from;
//      return temp;
//    }
//    return result;
//  }
//
//  DirectoryData from_directory(RetrieveFromStorage(from_parent.first.listing->directory_id(),
//                                                   *from_meta_data.directory_id,
//                                                   kOwnerValue));  // fix me kOwnerValue.....
//  if (!IsValid(from_directory)) {
//    LOG(kError) << "Failed to get directory " << from;
//    int temp = to_parent.first.listing->RemoveChild(from_meta_data);
//    if (temp != kSuccess) {
//      LOG(kError) << "Failed to remove child " << from_meta_data.name << " from " << to;
//    }
//    from_meta_data.name = temp_path;
//    temp = from_parent.first.listing->AddChild(from_meta_data);
//    if (temp != kSuccess) {
//      LOG(kError) << "Failed to add child " << from_meta_data.name << " back to " << from;
//    }
//    return kFailedToGetMetaData;
//  }
//
//  from_directory.parent_id = to_parent.first.listing->directory_id();
//  result = PutToStorage(std::make_pair(from_directory, kOwnerValue));  // liar   ..........
//  if (result != kSuccess) {
//    LOG(kError) << "Failed to store " << from;
//    int temp = to_parent.first.listing->RemoveChild(from_meta_data);
//    if (temp != kSuccess) {
//      LOG(kError) << "Failed to remove child " << from_meta_data.name << " from " << to;
//    }
//    from_meta_data.name = temp_path;
//    temp = from_parent.first.listing->AddChild(from_meta_data);
//    if (temp != kSuccess) {
//      LOG(kError) << "Failed to add child " << from_meta_data.name << " back to " << from;
//    }
//    from_directory.parent_id = from_parent.first.listing->directory_id();
//    return result;
//  }
//
//  return kSuccess;
// }

void DirectoryListingHandler::SetWorldReadWrite() {
  world_is_writeable_ = true;
}

void DirectoryListingHandler::SetWorldReadOnly() {
  world_is_writeable_ = false;
}

}  // namespace drive

}  // namespace maidsafe
