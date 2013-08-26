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
#include <map>
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

namespace drive {

namespace detail {

template<typename Storage>
class RootHandler{
 public:
  RootHandler(std::shared_ptr<nfs_client::MaidNodeNfs> maid_node_nfs,
              const Identity& unique_user_id,
              const Identity& drive_root_id,
              OnServiceAdded on_service_added);

  RootHandler(const Identity& drive_root_id,
              OnServiceAdded on_service_added,
              OnServiceRemoved on_service_removed);

  void AddService(const boost::filesystem::path& service_alias,
                  const boost::filesystem::path& store_path);
  void RemoveService(const boost::filesystem::path& service_alias);
  void ReInitialiseService(const boost::filesystem::path& service_alias,
                           const boost::filesystem::path& store_path,
                           const Identity& service_root_id);

  void GetMetaData(const boost::filesystem::path& relative_path,
                   MetaData& meta_data,
                   DirectoryId* grandparent_directory_id,
                   DirectoryId* parent_directory_id);

  DirectoryListingHandler<Storage>* GetHandler(const boost::filesystem::path& relative_path);

  Identity drive_root_id() const { return root_.listing->directory_id(); }

 private:
  // Called on the first run ever for this Drive (creating new user account)
  void CreateRoot(const Identity& unique_user_id);
  // Called when starting up a new session (user logging back in)
  void InitRoot(const Identity& unique_user_id, const Identity& drive_root_id);

  std::shared_ptr<Storage> default_storge_;  // MaidNodeNfs or nullptr
  DirectoryData root_;
  MetaData root_meta_data_;
  std::map<boost::filesystem::path, DirectoryListingHandler<Storage>> directory_listing_handlers_;
  OnServiceAdded on_service_added_;
  OnServiceRemoved on_service_removed_;
};



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

//template<>
//const std::vector<typename Default<nfs_client::MaidNodeNfs>::PathAndType>
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
//}();

// No default values for SureFile
template<>
const std::vector<typename Default<data_store::SureFileStore>::PathAndType>
    Default<data_store::SureFileStore>::kValues =
        std::vector<typename Default<data_store::SureFileStore>::PathAndType>();



template<typename Storage>
RootHandler<Storage>::RootHandler(std::shared_ptr<nfs_client::MaidNodeNfs> maid_node_nfs,
                                  const Identity& unique_user_id,
                                  const Identity& drive_root_id,
                                  OnServiceAdded on_service_added)
    : default_storge_(maid_node_nfs),
      root_(),
      root_meta_data_(kRoot, true),
      directory_listing_handlers_(),
      on_service_added_(on_service_added),
      on_service_removed_(nullptr) {
  if (!unique_user_id.IsInitialised())
    ThrowError(CommonErrors::uninitialised);
  drive_root_id.IsInitialised() ? InitRoot(unique_user_id, drive_root_id) :
                                  CreateRoot(unique_user_id);
}

template<typename Storage>
RootHandler<Storage>::RootHandler(const Identity& drive_root_id,
                                  OnServiceAdded on_service_added,
                                  OnServiceRemoved on_service_removed)
    : default_storge_(),
      root_(),
      root_meta_data_(kRoot, true),
      directory_listing_handlers_(),
      on_service_added_(on_service_added),
      on_service_removed_(on_service_removed) {
  drive_root_id.IsInitialised() ? InitRoot(Identity(), drive_root_id) : CreateRoot(Identity());
}

template<>
void RootHandler<data_store::SureFileStore>::AddService(
    const boost::filesystem::path& service_alias,
    const boost::filesystem::path& store_path);

template<>
void RootHandler<data_store::SureFileStore>::RemoveService(
    const boost::filesystem::path& service_alias);

template<>
void RootHandler<data_store::SureFileStore>::ReInitialiseService(
    const boost::filesystem::path& service_alias,
    const boost::filesystem::path& store_path,
    const Identity& service_root_id);

template<typename Storage>
DirectoryListingHandler<Storage>* RootHandler<Storage>::GetHandler(
    const boost::filesystem::path& relative_path) {
  auto alias(*(++std::begin(relative_path)));
  auto itr(directory_listing_handlers_.find(alias));
  return itr == std::end(directory_listing_handlers_) ? nullptr : &(itr->second);
}

template<typename Storage>
void RootHandler<Storage>::GetMetaData(const boost::filesystem::path& relative_path,
                                       MetaData& meta_data,
                                       DirectoryId* grandparent_directory_id,
                                       DirectoryId* parent_directory_id) {
  auto directory_listing_handler(GetHandler(relative_path));
  auto parent(directory_listing_handler ?
              directory_listing_handler->GetFromPath(relative_path.parent_path()).first : root_);
  if (relative_path == kRoot)
    meta_data = root_meta_data_;
  else
    parent.listing->GetChild(relative_path.filename(), meta_data);

  if (grandparent_directory_id)
    *grandparent_directory_id = parent.parent_id;
  if (parent_directory_id)
    *parent_directory_id = parent.listing->directory_id();
}

template<>
void RootHandler<nfs_client::MaidNodeNfs>::CreateRoot(const Identity& unique_user_id);

template<>
void RootHandler<data_store::SureFileStore>::CreateRoot(const Identity& unique_user_id);

template<>
void RootHandler<nfs_client::MaidNodeNfs>::InitRoot(const Identity& unique_user_id,
                                                    const Identity& drive_root_id);

template<>
void RootHandler<data_store::SureFileStore>::InitRoot(const Identity& unique_user_id,
                                                      const Identity& drive_root_id);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_ROOT_HANDLER_H_
