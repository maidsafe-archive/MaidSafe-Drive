/* Copyright 2013 MaidSafe.net limited

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

#include "maidsafe/drive/root_handler.h"

namespace maidsafe {

namespace drive {

namespace detail {

template<>
void RootHandler<data_store::SureFileStore>::AddService(
    const boost::filesystem::path& service_alias,
    const boost::filesystem::path& store_path) {
  MetaData meta_data(service_alias, true);
  auto directory(std::make_shared<DirectoryListing>(Identity(RandomString(64))));
  DirectoryData directory_data(root_.listing->directory_id(), directory);
  auto service_root(std::make_pair(directory_data, DataTagValue::kOwnerDirectoryValue));

  // TODO(Fraser#5#): 2013-08-26 - BEFORE_RELEASE - fix size
  auto storage(std::make_shared<data_store::SureFileStore>(store_path, DiskUsage(1 << 9)));

  DirectoryListingHandler<data_store::SureFileStore> handler(storage, service_root, true);
  directory_listing_handlers_.insert(std::make_pair(service_alias, handler));

  root_.listing->AddChild(meta_data);
}

template<>
void RootHandler<data_store::SureFileStore>::RemoveService(
    const boost::filesystem::path& /*service_alias*/) {
  // TODO(Fraser#5#): 2013-08-26 - Implement
  ThrowError(CommonErrors::unable_to_handle_request);
}

template<>
void RootHandler<data_store::SureFileStore>::ReInitialiseService(
    const boost::filesystem::path& service_alias,
    const boost::filesystem::path& store_path,
    const Identity& service_root_id) {
  // TODO(Fraser#5#): 2013-08-26 - BEFORE_RELEASE - fix size
  auto storage(std::make_shared<data_store::SureFileStore>(store_path, DiskUsage(1 << 9)));

  DirectoryListingHandler<data_store::SureFileStore> handler(storage, root_.listing->directory_id(),
                                                             service_root_id, true);
  directory_listing_handlers_.insert(std::make_pair(service_alias, handler));

  root_.listing->AddChild(MetaData(service_alias, true));
}

// TODO(dirvine) uncomment for lifestuff  #BEFORE_RELEASE
//template<>
//void RootHandler<nfs_client::MaidNodeNfs>::CreateRoot(const Identity& unique_user_id) {
//  drive_root_id_ = Identity(RandomString(64));
//  // First run, setup working directories.
//  // Root/Parent.
//  MetaData root_meta_data(kRoot, true);
//  std::shared_ptr<DirectoryListing> root_parent_directory(new DirectoryListing(drive_root_id_)),
//                      root_directory(new DirectoryListing(*root_meta_data.directory_id));
//  DirectoryData root_parent(unique_user_id_, root_parent_directory),
//                root(drive_root_id_, root_directory);

//  root_parent.listing->AddChild(root_meta_data);
//  PutToStorage(std::make_pair(root_parent, DataTagValue::kOwnerDirectoryValue));
//  // Owner.
//  MetaData owner_meta_data(kOwner, true);
//  std::shared_ptr<DirectoryListing> owner_directory(new DirectoryListing(*owner_meta_data.directory_id));
//  DirectoryData owner(root.listing->directory_id(), owner_directory);
//  PutToStorage(std::make_pair(owner, DataTagValue::kOwnerDirectoryValue));
//  // Group.
//  MetaData group_meta_data(kGroup, true), group_services_meta_data(kServices, true);
//  std::shared_ptr<DirectoryListing> group_directory(new DirectoryListing(*group_meta_data.directory_id)),
//          group_services_directory(new DirectoryListing(*group_services_meta_data.directory_id));
//  DirectoryData group(root.listing->directory_id(), group_directory),
//                group_services(group.listing->directory_id(), group_services_directory);
//  PutToStorage(std::make_pair(group_services, DataTagValue::kGroupDirectoryValue));
//  group.listing->AddChild(group_services_meta_data);
//  PutToStorage(std::make_pair(group, DataTagValue::kGroupDirectoryValue));
//  // World.
//  MetaData world_meta_data(kWorld, true), world_services_meta_data("Services", true);
//  std::shared_ptr<DirectoryListing> world_directory(new DirectoryListing(*world_meta_data.directory_id)),
//          world_services_directory(new DirectoryListing(*world_services_meta_data.directory_id));
//  DirectoryData world(root.listing->directory_id(), world_directory),
//                world_services(world.listing->directory_id(), world_services_directory);
//  PutToStorage(std::make_pair(world_services, DataTagValue::kWorldDirectoryValue));
//  world.listing->AddChild(world_services_meta_data);
//  PutToStorage(std::make_pair(world, DataTagValue::kWorldDirectoryValue));

//  root.listing->AddChild(owner_meta_data);
//  root.listing->AddChild(group_meta_data);
//  root.listing->AddChild(world_meta_data);
//  PutToStorage(std::make_pair(root, DataTagValue::kOwnerDirectoryValue));
//}

template<>
void RootHandler<data_store::SureFileStore>::CreateRoot(const Identity& unique_user_id) {
  assert(!unique_user_id.IsInitialised());
  root_.listing = std::make_shared<DirectoryListing>(Identity(RandomString(64)));
}

//template<>
//void RootHandler<nfs_client::MaidNodeNfs>::InitRoot(const Identity& unique_user_id,
//                                                    const Identity& drive_root_id) {
//  assert(drive_root_id.IsInitialised() && unique_user_id.IsInitialised());
//  DirectoryData directory(RetrieveFromStorage(unique_user_id_, drive_root_id_, DataTagValue::kOwnerDirectoryValue));
//}

template<>
void RootHandler<data_store::SureFileStore>::InitRoot(const Identity& unique_user_id,
                                                      const Identity& drive_root_id) {
  assert(!unique_user_id.IsInitialised() && drive_root_id.IsInitialised());
  root_.listing = std::make_shared<DirectoryListing>(drive_root_id);
}


}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
