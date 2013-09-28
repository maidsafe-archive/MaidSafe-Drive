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

#include "maidsafe/drive/root_handler.h"

namespace maidsafe {

namespace drive {

namespace detail {

// TODO(Team): 2013-09-25 - Uncomment and fix or delete
// template<>
// const std::vector<typename Default<nfs_client::MaidNodeNfs>::PathAndType>
//    Default<nfs_client::MaidNodeNfs>::kValues =
//        []()->std::vector<typename Default<nfs_client::MaidNodeNfs>::PathAndType> {
//    std::vector<typename Default<nfs_client::MaidNodeNfs>::PathAndType> result;
//    result.push_back(std::make_pair(boost::filesystem::path("/Owner").make_preferred(),
//                                    DataTagValue::kOwnerDirectoryValue));
//    result.push_back(std::make_pair(boost::filesystem::path("/Group"),
//                                    DataTagValue::kGroupDirectoryValue));
//    result.push_back(std::make_pair(boost::filesystem::path("/Group/Services").make_preferred(),
//                                    DataTagValue::kGroupDirectoryValue));
//    result.push_back(std::make_pair(boost::filesystem::path("/World"),
//                                    DataTagValue::kWorldDirectoryValue));
//    result.push_back(std::make_pair(boost::filesystem::path("/World/Services"),
//                                    DataTagValue::kWorldDirectoryValue));
//    return result;
// }();

template <>
const std::vector<typename Default<data_store::SureFileStore>::PathAndType>
    Default<data_store::SureFileStore>::kValues =
        std::vector<typename Default<data_store::SureFileStore>::PathAndType>();

template <>
void RootHandler<data_store::SureFileStore>::AddService(
    const boost::filesystem::path& service_alias, const boost::filesystem::path& store_path,
    const Identity& service_root_id) {
  // TODO(Fraser#5#): 2013-08-26 - BEFORE_RELEASE - fix size
  auto storage(std::make_shared<data_store::SureFileStore>(store_path, DiskUsage(1 << 30)));
  Directory service_root;
  try {
    // Logging back in
    service_root = GetFromStorage(*storage, root_.listing->directory_id(), service_root_id,
                                  DataTagValue::kOwnerDirectoryValue);
  }
  catch (...) {
    // Creating new service
    auto listing(std::make_shared<DirectoryListing>(service_root_id));
    service_root = Directory(root_.listing->directory_id(), listing, nullptr,
                             DataTagValue::kOwnerDirectoryValue);
    PutToStorage(*storage, service_root);
    {
      std::lock_guard<std::mutex> dir_lock(directories_mutex_);
      recent_directories_[kRoot / service_alias] = service_root;
    }
  }

  DirectoryHandler<data_store::SureFileStore> handler(storage, DataTagValue::kOwnerDirectoryValue,
                                                      true);
  {
    std::lock_guard<std::mutex> handlers_lock(handlers_mutex_);
    directory_handlers_.insert(std::make_pair(service_alias, handler));
  }

  MetaData service_meta_data(service_alias, true);
  *service_meta_data.directory_id = service_root_id;
  std::lock_guard<std::mutex> root_lock(root_mutex_);
  root_.listing->AddChild(service_meta_data);
  root_meta_data_.UpdateLastModifiedTime();
#ifndef MAIDSAFE_WIN32
  //  root_meta_data_.attributes.st_ctime = parent_meta_data.attributes.st_mtime;
  ++root_meta_data_.attributes.st_nlink;
#endif
}

template <>
void RootHandler<data_store::SureFileStore>::RemoveService(
    const boost::filesystem::path& service_alias) {
  auto itr(directory_handlers_.find(service_alias));
  if (itr == std::end(directory_handlers_))
    ThrowError(CommonErrors::invalid_parameter);

  // TODO(Fraser#5#): 2013-09-04 - If required, delete 'itr->second.storage()' from disk.  If that
  //                               *is* done here, no need to get Directory or call
  //                               DeleteFromStorage (i.e. delete next 2 lines).
  Directory directory(itr->second.GetFromPath(root_, kRoot / service_alias));
  DeleteFromStorage(*itr->second.storage(), directory);

  {
    std::lock_guard<std::mutex> dir_lock(directories_mutex_);
    recent_directories_.erase(kRoot / service_alias);
  }

  root_.listing->RemoveChild(MetaData(service_alias, true));
  root_meta_data_.UpdateLastModifiedTime();
#ifndef MAIDSAFE_WIN32
  //  root_meta_data_.attributes.st_ctime = parent_meta_data.attributes.st_mtime;
  --root_meta_data_.attributes.st_nlink;
#endif
  directory_handlers_.erase(itr);
}

template <>
DataTagValue RootHandler<nfs_client::MaidNodeNfs>::GetDirectoryType(
    const boost::filesystem::path& path) const {
  auto handler(GetHandler(path));
  return handler ? handler->directory_type() : DataTagValue::kOwnerDirectoryValue;
}

template <>
DataTagValue RootHandler<data_store::SureFileStore>::GetDirectoryType(
    const boost::filesystem::path& /*path*/) const {
  return DataTagValue::kOwnerDirectoryValue;
}

// TODO(dirvine) uncomment for lifestuff  #BEFORE_RELEASE
template <>
void RootHandler<nfs_client::MaidNodeNfs>::CreateRoot(const Identity& /*unique_user_id*/) {
  //  drive_root_id_ = Identity(RandomString(64));
  //  // First run, setup working directories.
  //  // Root/Parent.
  //  MetaData root_meta_data(kRoot, true);
  //  std::shared_ptr<DirectoryListing> root_parent_directory(new DirectoryListing(drive_root_id_)),
  //                      root_directory(new DirectoryListing(*root_meta_data.directory_id));
  //  Directory root_parent(unique_user_id_, root_parent_directory),
  //                root(drive_root_id_, root_directory);

  //  root_parent.listing->AddChild(root_meta_data);
  //  PutToStorage(std::make_pair(root_parent, DataTagValue::kOwnerDirectoryValue));
  //  // Owner.
  //  MetaData owner_meta_data(kOwner, true);
  //  std::shared_ptr<DirectoryListing> owner_directory(
  //      new DirectoryListing(*owner_meta_data.directory_id));
  //  Directory owner(root.listing->directory_id(), owner_directory);
  //  PutToStorage(std::make_pair(owner, DataTagValue::kOwnerDirectoryValue));
  //  // Group.
  //  MetaData group_meta_data(kGroup, true), group_services_meta_data(kServices, true);
  //  std::shared_ptr<DirectoryListing> group_directory(
  //      new DirectoryListing(*group_meta_data.directory_id)),
  //          group_services_directory(new
  // DirectoryListing(*group_services_meta_data.directory_id));
  //  Directory group(root.listing->directory_id(), group_directory),
  //                group_services(group.listing->directory_id(), group_services_directory);
  //  PutToStorage(std::make_pair(group_services, DataTagValue::kGroupDirectoryValue));
  //  group.listing->AddChild(group_services_meta_data);
  //  PutToStorage(std::make_pair(group, DataTagValue::kGroupDirectoryValue));
  //  // World.
  //  MetaData world_meta_data(kWorld, true), world_services_meta_data("Services", true);
  //  std::shared_ptr<DirectoryListing> world_directory(
  //      new DirectoryListing(*world_meta_data.directory_id)),
  //          world_services_directory(new
  // DirectoryListing(*world_services_meta_data.directory_id));
  //  Directory world(root.listing->directory_id(), world_directory),
  //                world_services(world.listing->directory_id(), world_services_directory);
  //  PutToStorage(std::make_pair(world_services, DataTagValue::kWorldDirectoryValue));
  //  world.listing->AddChild(world_services_meta_data);
  //  PutToStorage(std::make_pair(world, DataTagValue::kWorldDirectoryValue));

  //  root.listing->AddChild(owner_meta_data);
  //  root.listing->AddChild(group_meta_data);
  //  root.listing->AddChild(world_meta_data);
  //  PutToStorage(std::make_pair(root, DataTagValue::kOwnerDirectoryValue));
}

template <>
void RootHandler<data_store::SureFileStore>::CreateRoot(const Identity& unique_user_id) {
  assert(!unique_user_id.IsInitialised());
  static_cast<void>(unique_user_id);
  root_.listing = std::make_shared<DirectoryListing>(Identity(RandomString(64)));
}

template <>
void RootHandler<nfs_client::MaidNodeNfs>::InitRoot(const Identity& unique_user_id,
                                                    const Identity& drive_root_id) {
  assert(unique_user_id.IsInitialised() && drive_root_id.IsInitialised());
  static_cast<void>(unique_user_id);
  static_cast<void>(drive_root_id);
  //  Directory directory(GetFromStorage(unique_user_id_, drive_root_id_,
  //                                     DataTagValue::kOwnerDirectoryValue));
}

template <>
void RootHandler<data_store::SureFileStore>::InitRoot(const Identity& unique_user_id,
                                                      const Identity& drive_root_id) {
  assert(!unique_user_id.IsInitialised() && drive_root_id.IsInitialised());
  static_cast<void>(unique_user_id);
  root_.listing = std::make_shared<DirectoryListing>(drive_root_id);
}

template <>
bool RootHandler<nfs_client::MaidNodeNfs>::CanAdd(const boost::filesystem::path& path) const {
  auto handler(GetHandler(path));
  if (!handler)
    return false;

  if (handler->directory_type() == DataTagValue::kGroupDirectoryValue ||
      (handler->directory_type() == DataTagValue::kWorldDirectoryValue &&
       !handler->world_is_writeable())) {
    return false;
  }
  fs::path relative_parent_filename(path.parent_path().filename());
  return !relative_parent_filename.empty() && relative_parent_filename != kRoot;
}

template <>
bool RootHandler<data_store::SureFileStore>::CanAdd(const boost::filesystem::path& /*path*/) const {
  return true;
}

template <>
bool RootHandler<nfs_client::MaidNodeNfs>::CanDelete(const boost::filesystem::path& path) const {
  SCOPED_PROFILE
  auto handler(GetHandler(path));
  if (!handler)
    return false;

  if (handler->directory_type() == DataTagValue::kGroupDirectoryValue ||
      (handler->directory_type() == DataTagValue::kWorldDirectoryValue &&
       !handler->world_is_writeable())) {
    return false;
  }
  boost::filesystem::path path_parent_filename(path.parent_path().filename());
  return !path_parent_filename.empty() && path_parent_filename != kRoot &&
         !(path_parent_filename == "World" && path.filename() == "Services");
}

template <>
bool RootHandler<data_store::SureFileStore>::CanDelete(
    const boost::filesystem::path& /*path*/) const {
  return true;
}

template <>
bool RootHandler<nfs_client::MaidNodeNfs>::CanRename(
    const boost::filesystem::path& /*from_path*/,
    const boost::filesystem::path& /*to_path*/) const {
  // TODO(Team): 2013-09-25 - Uncomment and fix or delete
  // boost::filesystem::path from_filename(from_path.filename()), to_filename(to_path.filename()),
  //         from_parent_filename(from_path.parent_path().filename()),
  //         to_parent_filename(to_path.parent_path().filename());
  // if ((from_filename == kRoot || to_filename == kRoot)
  //    || (from_parent_filename == kRoot || to_parent_filename == kRoot)
  //    || (from_filename == "Owner" && from_parent_filename == kRoot)
  //    || (to_filename == "Owner" && to_parent_filename == kRoot)
  //    || (from_filename == "Group" && from_parent_filename == kRoot)
  //    || (to_filename == "Group" && to_parent_filename == kRoot)
  //    || (from_filename == "World" && from_parent_filename == kRoot)
  //    || (to_filename == "World" && to_parent_filename == kRoot))
  //  return false;
  // auto from_type(GetDirectoryType(from_path)), to_type(GetDirectoryType(to_path));
  // if (from_type != to_type) {
  //  if (from_type == DataTagValue::kGroupDirectoryValue ||
  //      to_type == DataTagValue::kGroupDirectoryValue ||
  //          (from_type != DataTagValue::kWorldDirectoryValue &&
  //          to_type == DataTagValue::kWorldDirectoryValue && !world_is_writeable_)) {
  return false;
  //  }
  // }
  // return from_type != DataTagValue::kWorldDirectoryValue ||
  //       from_parent_filename != "World" ||
  //       from_filename != "Services";
}

template <>
bool RootHandler<data_store::SureFileStore>::CanRename(
    const boost::filesystem::path& from_path, const boost::filesystem::path& to_path) const {
  return from_path != kRoot && to_path != kRoot;
}

template <>
void RootHandler<nfs_client::MaidNodeNfs>::Put(const boost::filesystem::path& path,
                                               Directory& directory) {
  {
    std::lock_guard<std::mutex> dir_lock(directories_mutex_);
    recent_directories_[path] = directory;
  }
  PutToStorage(*default_storage_, directory);
}

template <>
void RootHandler<data_store::SureFileStore>::Put(const boost::filesystem::path& path,
                                                 Directory& directory) {
  SCOPED_PROFILE
  {  // NOLINT
    std::lock_guard<std::mutex> dir_lock(directories_mutex_);
    recent_directories_[path] = directory;
  }
  auto directory_handler(GetHandler(path));
  if (directory_handler)
    PutToStorage(*directory_handler->storage(), directory);
}

template <>
void RootHandler<nfs_client::MaidNodeNfs>::Delete(const boost::filesystem::path& path,
                                                  const Directory& directory) {
  {
    std::lock_guard<std::mutex> dir_lock(directories_mutex_);
    recent_directories_.erase(path);
  }
  DeleteFromStorage(*default_storage_, directory);
}

template <>
void RootHandler<data_store::SureFileStore>::Delete(const boost::filesystem::path& path,
                                                    const Directory& directory) {
  {
    std::lock_guard<std::mutex> dir_lock(directories_mutex_);
    recent_directories_.erase(path);
  }
  auto directory_handler(GetHandler(path));
  if (directory_handler)
    DeleteFromStorage(*directory_handler->storage(), directory);
}

template <>
nfs_client::MaidNodeNfs* RootHandler<nfs_client::MaidNodeNfs>::GetStorage(
    const boost::filesystem::path& /*path*/) const {
  return default_storage_.get();
}

template <>
data_store::SureFileStore* RootHandler<data_store::SureFileStore>::GetStorage(
    const boost::filesystem::path& path) const {
  auto directory_handler(GetHandler(path));
  assert(directory_handler);
  return directory_handler->storage();
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
