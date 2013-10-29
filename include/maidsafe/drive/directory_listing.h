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

#ifndef MAIDSAFE_DRIVE_DIRECTORY_LISTING_H_
#define MAIDSAFE_DRIVE_DIRECTORY_LISTING_H_

#include <memory>
#include <string>
#include <set>
#include <utility>
#include <vector>
#include <algorithm>

#include "boost/filesystem/path.hpp"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"

namespace fs = boost::filesystem;
namespace bptime = boost::posix_time;

namespace maidsafe {

namespace drive {

namespace detail {

class DirectoryListing;

namespace test {

void DirectoriesMatch(const DirectoryListing& lhs, const DirectoryListing& rhs);
class DirectoryListingTest;

}  // namespace test

class DirectoryListing {
 public:
  explicit DirectoryListing(DirectoryId directory_id);
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
  friend void test::DirectoriesMatch(const DirectoryListing& lhs, const DirectoryListing& rhs);
  friend class test::DirectoryListingTest;

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
