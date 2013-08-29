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


namespace fs = boost::filesystem;
namespace bptime= boost::posix_time;

namespace maidsafe {

namespace drive {

namespace detail {

namespace test {

testing::AssertionResult DirectoriesMatch(const DirectoryListing& lhs, const DirectoryListing& rhs);
class DirectoryListingTest_BEH_IteratorResetAndFailures_Test;

}  // namespace test



class DirectoryListing {
 public:
  explicit DirectoryListing(const DirectoryId& directory_id);
  explicit DirectoryListing(const std::string& serialised_directory_listing);
  DirectoryListing(const DirectoryListing& other);
  DirectoryListing(DirectoryListing&& other);
  DirectoryListing& operator=(DirectoryListing other);

  ~DirectoryListing() {}

  bool HasChild(const fs::path& name) const;
  void GetChild(const fs::path& name, MetaData& meta_data) const;
  bool GetChildAndIncrementItr(MetaData& meta_data);
  void AddChild(const MetaData& child);
  void RemoveChild(const MetaData& child);
  void UpdateChild(const MetaData& child);
  void ResetChildrenIterator();
  bool empty() const;
  DirectoryId directory_id() const { return directory_id_; }

  // This function is internal to drive, do not use for native filesystem operations.
  std::vector<std::string> GetHiddenChildNames() const;

  std::string Serialise() const;

  friend void swap(DirectoryListing& lhs, DirectoryListing& rhs);
  friend testing::AssertionResult test::DirectoriesMatch(const DirectoryListing& lhs,
                                                         const DirectoryListing& rhs);
  friend class test::DirectoryListingTest_BEH_IteratorResetAndFailures_Test;

 private:
  void SortAndResetChildrenIterator();

  DirectoryId directory_id_;
  std::vector<MetaData> children_;
  size_t children_itr_position_;
};

bool operator<(const DirectoryListing& lhs, const DirectoryListing& rhs);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_LISTING_H_
