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

#ifndef MAIDSAFE_DRIVE_ROOT_HANDLER_H_
#define MAIDSAFE_DRIVE_ROOT_HANDLER_H_

#include <algorithm>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "maidsafe/data_store/surefile_store.h"
#include "maidsafe/data_types/data_type_values.h"
#include "maidsafe/nfs/client/maid_node_nfs.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory_listing_handler.h"
#include "maidsafe/drive/directory_listing.h"

namespace maidsafe {

namespace data_store { class SureFileStore; }

namespace drive {

namespace detail {

template<typename Storage>
class RootHandler{
public:
 RootHandler(nfs_client::MaidNodeNfs& maid_node_nfs,
             const Identity& unique_user_id,
             const Identity& root_parent_id);

 RootHandler(const Identity& root_parent_id,
             OnServiceAdded on_service_added,
             OnServiceRemoved on_service_removed);

 void AddService(const boost::filesystem::path& service_alias,
                 const boost::filesystem::path& store_path);
 void RemoveService(const boost::filesystem::path& service_alias);
 void ReInitialiseService(const boost::filesystem::path& service_alias,
                          const boost::filesystem::path& store_path,
                          const Identity& service_root_id);

 Identity root_parent_id() const { return root_parent_id_; }

private:
 // Called on the first run ever for this Drive (creating new user account)
 void CreateRoot();
 // Called when starting up a new session (user logging back in)
 void InitRoot();

 nfs_client::MaidNodeNfs& maid_node_nfs_;
 Identity unique_user_id_, drive_root_id_;
 OnServiceAdded on_service_added_;
 OnServiceRemoved on_service_removed_;
};

//template<>
//class RootHandler<nfs_client::MaidNodeNfs> {
// public:
//  RootHandler(nfs_client::MaidNodeNfs& maid_node_nfs,
//              const Identity& unique_user_id,
//              const Identity& root_parent_id);

//  Identity root_parent_id() const { return root_parent_id_; }

// private:
//  // Called on the first run ever for this Drive (creating new user account)
//  void CreateRoot();
//  // Called when starting up a new session (user logging back in)
//  void InitRoot();

//  nfs_client::MaidNodeNfs& maid_node_nfs_;
//  Identity unique_user_id_, root_parent_id_;
//};

//template<>
//class RootHandler<data_store::SureFileStore> {
// public:
//  RootHandler(const Identity& unique_user_id,
//              const Identity& root_parent_id,
//              std::function<void(const boost::filesystem::path&)> root_subdir_added,
//              std::function<void(const boost::filesystem::path&)> root_subdir_removed);

//  Identity root_parent_id() const { return root_parent_id_; }

// private:
//  // Called on the first run ever for this Drive (creating new user account)
//  void CreateRoot();
//  // Called when starting up a new session (user logging back in)
//  void InitRoot();

//  Identity unique_user_id_, root_parent_id_;
//  std::function<void(const boost::filesystem::path&)> root_subdir_added_;
//  std::function<void(const boost::filesystem::path&)> root_subdir_removed_;
//};



// ==================== Implementation =============================================================
template<typename Storage>
struct Default {
  typedef std::pair<boost::filesystem::path, DataTagValue> PathAndType;
  static const std::vector<PathAndType> kValues;
  static bool IsDefault(const boost::filesystem::path& path) {
    return std::any_of(std::begin(kValues),
                       std::end(kValues),
                       [&path](const PathAndType& value) { return path == value.first; });
  }
};

template<typename Storage>
const std::vector<typename Default<Storage>::PathAndType> Default<Storage>::kValues = {
    PathAndType(boost::filesystem::path("/Owner").make_preferred(), DataTagValue::kOwnerDirectoryValue),
    PathAndType(boost::filesystem::path("/Group"), DataTagValue::kGroupDirectoryValue),
    PathAndType(boost::filesystem::path("/Group/Services").make_preferred(),
                DataTagValue::kGroupDirectoryValue),
    PathAndType(boost::filesystem::path("/World"), DataTagValue::kWorldDirectoryValue),
    PathAndType(boost::filesystem::path("/World/Services"), DataTagValue::kWorldDirectoryValue)
};

// No default values for SureFile
template<>
const std::vector<typename Default<data_store::SureFileStore>::PathAndType>
    Default<data_store::SureFileStore>::kValues;


template<typename Storage>
RootHandler<Storage>::RootHandler(nfs_client::MaidNodeNfs& maid_node_nfs,
            const Identity& unique_user_id,
            const Identity& root_parent_id)
    : unique_user_id_(unique_user_id),
      root_parent_id_(root_parent_id) {
  if (!unique_user_id.IsInitialised())
    ThrowError(CommonErrors::uninitialised);
  return root_parent_id_.IsInitialised() ? InitRoot() : CreateRoot();
}


// TODO(dirvine) uncomment for lifestuff  #BEFORE_RELEASE

//template<>
//void RootHandler<nfs_client::MaidNodeNfs>::CreateRoot() {
//  assert(!root_parent_id_.IsInitialised());
//  root_parent_id_ = Identity(RandomString(64));
//  // First run, setup working directories.
//  // Root/Parent.
//  MetaData root_meta_data(kRoot, true);
//  std::shared_ptr<DirectoryListing> root_parent_directory(new DirectoryListing(root_parent_id_)),
//                      root_directory(new DirectoryListing(*root_meta_data.directory_id));
//  DirectoryData root_parent(unique_user_id_, root_parent_directory),
//                root(root_parent_id_, root_directory);

//  root_parent.listing->AddChild(root_meta_data);
//  PutToStorage(std::make_pair(root_parent, kOwnerValue));
//  // Owner.
//  MetaData owner_meta_data(kOwner, true);
//  std::shared_ptr<DirectoryListing> owner_directory(new DirectoryListing(*owner_meta_data.directory_id));
//  DirectoryData owner(root.listing->directory_id(), owner_directory);
//  PutToStorage(std::make_pair(owner, kOwnerValue));
//  // Group.
//  MetaData group_meta_data(kGroup, true), group_services_meta_data(kServices, true);
//  std::shared_ptr<DirectoryListing> group_directory(new DirectoryListing(*group_meta_data.directory_id)),
//          group_services_directory(new DirectoryListing(*group_services_meta_data.directory_id));
//  DirectoryData group(root.listing->directory_id(), group_directory),
//                group_services(group.listing->directory_id(), group_services_directory);
//  PutToStorage(std::make_pair(group_services, kGroupValue));
//  group.listing->AddChild(group_services_meta_data);
//  PutToStorage(std::make_pair(group, kGroupValue));
//  // World.
//  MetaData world_meta_data(kWorld, true), world_services_meta_data("Services", true);
//  std::shared_ptr<DirectoryListing> world_directory(new DirectoryListing(*world_meta_data.directory_id)),
//          world_services_directory(new DirectoryListing(*world_services_meta_data.directory_id));
//  DirectoryData world(root.listing->directory_id(), world_directory),
//                world_services(world.listing->directory_id(), world_services_directory);
//  PutToStorage(std::make_pair(world_services, kWorldValue));
//  world.listing->AddChild(world_services_meta_data);
//  PutToStorage(std::make_pair(world, kWorldValue));

//  root.listing->AddChild(owner_meta_data);
//  root.listing->AddChild(group_meta_data);
//  root.listing->AddChild(world_meta_data);
//  PutToStorage(std::make_pair(root, kOwnerValue));
//}

template<typename Storage>
void RootHandler<Storage>::InitRoot() {
  assert(root_parent_id_.IsInitialised());
  DirectoryData directory(RetrieveFromStorage(unique_user_id_, root_parent_id_, kOwnerValue));
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_ROOT_HANDLER_H_
