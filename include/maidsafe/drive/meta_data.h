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
#include <chrono>

#include "boost/filesystem/path.hpp"

#include "maidsafe/common/config.h"
#include "maidsafe/encrypt/data_map.h"

#include "maidsafe/drive/config.h"

namespace maidsafe {

namespace drive {

namespace detail {

namespace protobuf { class MetaData; }

struct MaidSafeClock {
  // Chrono clock that uses MaidSafe epoch: 2000-01-01T00:00:00Z in 1 millisecond ticks
  typedef std::chrono::milliseconds duration;
  typedef duration::rep rep;
  typedef duration::period period;
  typedef std::chrono::time_point<MaidSafeClock> time_point;

  static const bool is_steady = std::chrono::system_clock::is_steady;

  static time_point now() MAIDSAFE_NOEXCEPT {
    // std::chrono::system_clock uses Unix epoch: 1970-01-01T00:00:00Z in 1 second ticks
    std::chrono::system_clock::time_point current = std::chrono::system_clock::now();
    const duration maidSafeEpochInSeconds = std::chrono::hours(262968); // 30 years
    return time_point
        (std::chrono::duration_cast<duration>
         (current.time_since_epoch() - maidSafeEpochInSeconds));
  }

  static std::time_t to_time_t(const time_point& t) {
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
  }
};

// Represents directory and file information
struct MetaData {
  using TimePoint = MaidSafeClock::time_point;

  MetaData();
  MetaData(const boost::filesystem::path& name, bool is_directory);
  explicit MetaData(const protobuf::MetaData& protobuf_meta_data);
  MetaData(MetaData&& other);
  MetaData& operator=(MetaData other);

  void ToProtobuf(protobuf::MetaData* protobuf_meta_data) const;

  bool operator<(const MetaData& other) const;
  void UpdateLastModifiedTime();
  uint64_t GetAllocatedSize() const;

  boost::filesystem::path name;
  TimePoint creation_time;
  TimePoint last_access_time;
  TimePoint last_write_time;

#ifdef MAIDSAFE_WIN32
  uint64_t end_of_file;
  uint64_t allocation_size;
  DWORD attributes;
#else
  struct stat attributes;
  boost::filesystem::path link_to;
#endif
  std::unique_ptr<encrypt::DataMap> data_map;
  std::unique_ptr<DirectoryId> directory_id;

 private:
  MetaData(const MetaData& other) = delete;
};

void swap(MetaData& lhs, MetaData& rhs) MAIDSAFE_NOEXCEPT;

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_META_DATA_H_
