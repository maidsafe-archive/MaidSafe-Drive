
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
  int RenameChild(const MetaData& child, const fs::path& new_name, MetaData* target_if_exists);
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
