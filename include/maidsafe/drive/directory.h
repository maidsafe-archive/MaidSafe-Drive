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
#include "boost/filesystem/path.hpp"
#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"


namespace maidsafe {
namespace drive {
namespace detail {

class DirectoryListing;

namespace test {

testing::AssertionResult DirectoriesMatch(const DirectoryListing& lhs,
                                          const DirectoryListing& rhs);
class DirectoryListingTest_BEH_IteratorResetAndFailures_Test;

}  // namespace test

class DirectoryListing {
 public:
  explicit DirectoryListing(const DirectoryId& directory_id);
  explicit DirectoryListing(const std::string& serialised_directory_listing);
  DirectoryListing(const DirectoryListing&);
  DirectoryListing& operator=(const DirectoryListing&);

  ~DirectoryListing() {}

  bool HasChild(const boost::filesystem::path& name) const;
  void GetChild(const boost::filesystem::path& name, MetaData& meta_data) const;
  bool GetChildAndIncrementItr(MetaData& meta_data);
  void AddChild(const MetaData& child);
  void RemoveChild(const MetaData& child);
  void UpdateChild(const MetaData& child);
  void ResetChildrenIterator();
  bool empty() const;
  DirectoryId directory_id() const;

  // This function is internal to drive, do not use for native filesystem operations.
  std::vector<std::string> GetHiddenChildNames() const;

  std::string Serialise() const;

  bool operator<(const DirectoryListing& other) const;
  friend testing::AssertionResult test::DirectoriesMatch(const DirectoryListing& lhs,
                                                         const DirectoryListing& rhs);

 private:
  DirectoryId directory_id_;
  std::set<MetaData> children_;
  std::set<MetaData>::const_iterator children_itr_;
};

struct Directory {
  typedef std::shared_ptr<DirectoryListing> DirectoryListingPtr;

  Directory(const DirectoryId& parent_id, DirectoryListingPtr directory_listing)
      : parent_id(parent_id),
        listing(directory_listing),
        content_changed(false) {}
  Directory()
      : parent_id(),
        listing(),
        content_changed(false) {}

  DirectoryId parent_id;
  DirectoryListingPtr listing;
  bool content_changed;
};

}  // namespace detail
}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_LISTING_H_
