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

#ifndef MAIDSAFE_DRIVE_META_DATA_H_
#define MAIDSAFE_DRIVE_META_DATA_H_

#ifdef MAIDSAFE_WIN32
#  include <windows.h>
#else
#  include <sys/stat.h>
#endif

#include <memory>
#include <string>
#include <vector>

#include "boost/filesystem/path.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

#ifndef MAIDSAFE_WIN32
#  ifdef MAIDSAFE_APPLE
#    undef FUSE_USE_VERSION
#    define FUSE_USE_VERSION 26
#    include "osxfuse/fuse/fuse.h"
#  else
#    include "fuse/fuse.h"
#  endif
#endif
#include "maidsafe/encrypt/data_map.h"
#include "maidsafe/drive/config.h"


namespace maidsafe {

namespace drive {

// Represents directory and file information
struct MetaData {
  MetaData();
  MetaData(const boost::filesystem::path &name, bool is_directory);
  MetaData(const MetaData& meta_data);

  void Serialise(std::string& serialised_meta_data) const;
  void Parse(const std::string& serialised_meta_data);
  boost::posix_time::ptime creation_posix_time() const;
  boost::posix_time::ptime last_write_posix_time() const;
  bool operator<(const MetaData &other) const;
  void UpdateLastModifiedTime();
  uint64_t GetAllocatedSize() const;

  boost::filesystem::path name;
#ifdef MAIDSAFE_WIN32
  uint64_t end_of_file;
  uint64_t allocation_size;
  DWORD attributes;
  FILETIME creation_time;
  FILETIME last_access_time;
  FILETIME last_write_time;
#else
  struct stat attributes;
  boost::filesystem::path link_to;
#endif
  detail::DataMapPtr data_map;
  detail::DirectoryIdPtr directory_id;
  std::vector<std::string> notes;
};

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_META_DATA_H_
