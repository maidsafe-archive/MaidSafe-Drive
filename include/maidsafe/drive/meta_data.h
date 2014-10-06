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

#ifndef MAIDSAFE_DRIVE_META_DATA_H_
#define MAIDSAFE_DRIVE_META_DATA_H_

#ifdef MAIDSAFE_WIN32
#include <windows.h>
#else
#include <fuse/fuse.h>  // NOLINT
#include <sys/stat.h>  // NOLINT
#endif

#include <cstdint>
#include <memory>

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"

#include "maidsafe/common/config.h"
#include "maidsafe/common/clock.h"
#include "maidsafe/encrypt/data_map.h"

#include "maidsafe/drive/config.h"

namespace maidsafe {

namespace drive {

namespace detail {

namespace protobuf { class Path; class Attributes; }

// Represents directory and file information
class MetaData {
 public:

  using TimePoint = common::Clock::time_point;
  using FileType = boost::filesystem::file_type;
  using Permissions = boost::filesystem::perms;

  // TODO Drop this extra constructor - required by path currently
  explicit MetaData(FileType file_type);
  MetaData(const boost::filesystem::path& name, FileType);
  explicit MetaData(const protobuf::Path& protobuf_path);
  MetaData(MetaData&& other);

  MetaData& operator=(MetaData other);

  void ToProtobuf(protobuf::Attributes& protobuf_attributes) const;

  bool operator<(const MetaData& other) const;

  Permissions GetPermissions(Permissions base_permissions) const;

  const encrypt::DataMap* data_map() const { return data_map_.get(); }
  encrypt::DataMap* data_map() { return data_map_.get(); }
  const DirectoryId* directory_id() const { return directory_id_.get(); }
  DirectoryId* directory_id() { return directory_id_.get(); }
  const boost::filesystem::path& name() const { return name_; }

  FileType file_type() const { return file_type_; }
  TimePoint creation_time() const { return creation_time_; }
  TimePoint last_status_time() const { return last_status_time_; }
  TimePoint last_write_time() const { return last_write_time_; }
  TimePoint last_access_time() const { return last_access_time_; }
  std::uint64_t size() const { return size_; }
  std::uint64_t allocation_size() const { return allocation_size_; }

#ifdef MAIDSAFE_WIN32
  DWORD attributes() const { return attributes_; }
  void set_attributes(const DWORD new_attributes) { attributes_ = new_attributes; }
#endif // MAIDSAFE_WIN32

  void set_name(const boost::filesystem::path& new_name) { name_ = new_name; }

  // Methods that automatically grab current time are preferred
  void set_creation_time(const TimePoint new_time) { creation_time_ = new_time; }
  void set_status_time(const TimePoint new_time) { last_status_time_ = new_time; }
  void set_last_access_time(const TimePoint new_time) { last_access_time_ = new_time; }
  void set_last_write_time(const TimePoint new_time) { last_write_time_ = new_time; }

  // Updates the last attributes modification time and access time
  void UpdateLastStatusTime();

  // Updates the last file modification time and access time
  void UpdateLastModifiedTime();

  // Updates the last access time
  void UpdateLastAccessTime();

  /* Updates the size of the file, status time, write time, and access time.
     Allocation size is modified to match thet size. */
  void UpdateSize(const std::uint64_t new_size);

  // Updates the allocated size of the file, status time, write time, and access time
  void UpdateAllocationSize(const std::uint64_t new_size);

  void swap(MetaData& rhs) MAIDSAFE_NOEXCEPT;

 private:

  std::unique_ptr<encrypt::DataMap> data_map_;
  std::unique_ptr<DirectoryId> directory_id_;
  boost::filesystem::path name_;

  FileType file_type_;
  // Time file was created
  TimePoint creation_time_;
  // Last time file attributes were modified
  TimePoint last_status_time_;
  // Last time file content was modified
  TimePoint last_write_time_;
  // Last known time file was accessed
  TimePoint last_access_time_;
  std::uint64_t size_;
  std::uint64_t allocation_size_;

#ifdef MAIDSAFE_WIN32
  DWORD attributes_;
#endif

 private:
  MetaData(const MetaData&) = delete;
};

inline void swap(MetaData& lhs, MetaData& rhs) MAIDSAFE_NOEXCEPT {
  lhs.swap(rhs);
}

inline bool HasPermission(const MetaData::Permissions permissions,
                          const MetaData::Permissions expected_permission) {
  return (permissions & expected_permission) == expected_permission;
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_META_DATA_H_
