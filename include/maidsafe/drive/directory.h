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
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "boost/asio/io_service.hpp"
#include "boost/asio/steady_timer.hpp"
#include "boost/filesystem/path.hpp"
#include "boost/system/error_code.hpp"

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
  Directory(ParentId parent_id, DirectoryId directory_id, boost::asio::io_service& io_service,
            std::function<void(const boost::system::error_code&)> store_functor);
  Directory(ParentId parent_id, const std::string& serialised_directory,
            const std::vector<StructuredDataVersions::VersionName>& versions,
            boost::asio::io_service& io_service,
            std::function<void(const boost::system::error_code&)> store_functor);
  // This serialises the appropriate member data (critically parent_id_ must never be serialised),
  // and sets 'store_pending_' false.  It also stores all new chunks from all children, increments
  // all the other chunks, and resets all self_encryptors & buffers.
  std::string Serialise(
      std::function<void(const ImmutableData&)> put_chunk_functor,
      std::function<void(const std::vector<ImmutableData::Name>&)> increment_chunks_functor);

  bool HasChild(const boost::filesystem::path& name) const;
  const FileContext* GetChild(const boost::filesystem::path& name) const;
  FileContext* GetMutableChild(const boost::filesystem::path& name);
  const FileContext* GetChildAndIncrementItr();
  void AddChild(FileContext&& child);
  FileContext RemoveChild(const boost::filesystem::path& name);
  void RenameChild(const boost::filesystem::path& old_name,
                   const boost::filesystem::path& new_name);
  void ResetChildrenIterator();
  bool empty() const;
  ParentId parent_id() const;
  void SetParentId(const ParentId parent_id);
  DirectoryId directory_id() const;
  void ScheduleForStoring();
  void StoreImmediatelyIfPending();
  // Returns directory_id and most recent 2 version names (including the one passed in).
  std::tuple<DirectoryId, StructuredDataVersions::VersionName, StructuredDataVersions::VersionName>
      AddNewVersion(ImmutableData::Name version_id);

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
  void DoScheduleForStoring(bool use_delay = true);

  mutable std::mutex mutex_;
  ParentId parent_id_;
  DirectoryId directory_id_;
  boost::asio::steady_timer timer_;
  std::function<void(const boost::system::error_code&)> store_functor_;
  std::deque<StructuredDataVersions::VersionName> versions_;
  MaxVersions max_versions_;
  Children children_;
  size_t children_itr_position_;
  bool store_pending_;
};

bool operator<(const Directory& lhs, const Directory& rhs);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_H_
