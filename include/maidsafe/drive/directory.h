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

#ifndef MAIDSAFE_DRIVE_DIRECTORY_H_
#define MAIDSAFE_DRIVE_DIRECTORY_H_

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "boost/filesystem/path.hpp"

#include "maidsafe/common/tagged_value.h"
#include "maidsafe/common/types.h"
#include "maidsafe/data_types/immutable_data.h"
#include "maidsafe/data_types/structured_data_versions.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/file_context.h"

namespace maidsafe {

namespace drive {

namespace detail {

class Directory;

namespace test {

void DirectoriesMatch(const Directory& lhs, const Directory& rhs);
class DirectoryTest;

}  // namespace test

class Directory {
 public:
  Directory(ParentId parent_id, DirectoryId directory_id);
  Directory(ParentId parent_id, const std::string& serialised_directory,
            std::vector<StructuredDataVersions::VersionName> versions);
  // This serialises the appropriate member data (critically parent_id_ must never be serialised),
  // and sets 'contents_changed_' to false.
  std::string Serialise();

  bool HasChild(const boost::filesystem::path& name) const;
  const FileContext* GetChild(const boost::filesystem::path& name) const;
  FileContext* GetMutableChild(const boost::filesystem::path& name);
  const FileContext* GetChildAndIncrementItr();
  void AddChild(FileContext&& child);
  FileContext RemoveChild(const boost::filesystem::path& name);
  void RenameChild(const boost::filesystem::path& old_name,
                   const boost::filesystem::path& new_name);
  void ResetChildrenIterator() { children_itr_position_ = 0; }
  bool empty() const;
  DirectoryId directory_id() const { return directory_id_; }
  void MarkAsChanged();
  // If 'delay' is 'kApply', returns true if 'last_changed_' is non-null and greater than
  // 'kDirectoryInactivityDelay'.  If 'delay' is 'kIgnore', returns true if 'last_changed_' is
  // non-null.
  bool NeedsToBeSaved(StoreDelay delay) const;
  std::vector<StructuredDataVersions::VersionName> versions_;
  ParentId parent_id_;

  friend void test::DirectoriesMatch(const Directory& lhs, const Directory& rhs);
  friend class test::DirectoryTest;

 private:
  Directory(const Directory& other);
  Directory(Directory&& other);
  Directory& operator=(Directory other);

  typedef std::vector<std::unique_ptr<FileContext>> Children;

  Children::iterator Find(const boost::filesystem::path& name);
  Children::const_iterator Find(const boost::filesystem::path& name) const;
  void SortAndResetChildrenIterator();

  // This is non-null if any child has been added, removed or had its metadata changed.  It is set
  // to null by a call to Serialise().
  std::unique_ptr<std::chrono::steady_clock::time_point> last_changed_;
  DirectoryId directory_id_;
  MaxVersions max_versions_;
  Children children_;
  size_t children_itr_position_;
};

bool operator<(const Directory& lhs, const Directory& rhs);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_H_
