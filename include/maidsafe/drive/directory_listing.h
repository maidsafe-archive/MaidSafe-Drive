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

#ifndef MAIDSAFE_DRIVE_DIRECTORY_LISTING_H_
#define MAIDSAFE_DRIVE_DIRECTORY_LISTING_H_

#include <memory>
#include <string>
#include <set>
#include <utility>
#include <vector>
#include "boost/filesystem/path.hpp"
#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/return_codes.h"


namespace fs = boost::filesystem;
namespace bptime= boost::posix_time;

namespace maidsafe {
namespace drive {

class DirectoryListing;

namespace meta_data_ops {

bool MetaDataHasName(const MetaData& meta_data, const fs::path& name);

}  // namespace meta_data_ops

class DirectoryListing {
 public:
  explicit DirectoryListing(const DirectoryId& directory_id);
  DirectoryListing(const DirectoryListing&);
  DirectoryListing& operator=(const DirectoryListing&);

  ~DirectoryListing() {}

  bool HasChild(const fs::path& name) const;
  void GetChild(const fs::path& name, MetaData& meta_data) const;
  bool GetChildAndIncrementItr(MetaData& meta_data);
  void AddChild(const MetaData& child);
  void RemoveChild(const MetaData& child);
  void UpdateChild(const MetaData& child, bool reset_itr);
  bool RenameChild(const MetaData& child, const fs::path& new_name, MetaData* target_if_exists);
  void ResetChildrenIterator() { children_itr_ = children_.begin(); }
  bool empty() const;
  DirectoryId directory_id() const { return directory_id_; }
  void set_directory_id(const DirectoryId& directory_id) { directory_id_ = directory_id; }

  // This function is internal to drive, do not use for native filesystem operations.
  void GetHiddenChildNames(std::vector<std::string> *names);

  bool operator<(const DirectoryListing& other) const;
  friend testing::AssertionResult test::DirectoriesMatch(
      DirectoryListingPtr directory1, DirectoryListingPtr directory2);

  void Serialise(std::string& serialised_directory_listing) const;
  void Parse(const std::string& serialised_directory_listing);

 private:
  DirectoryId directory_id_;
  std::set<MetaData> children_;
  std::set<MetaData>::const_iterator children_itr_;
};

}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_LISTING_H_
