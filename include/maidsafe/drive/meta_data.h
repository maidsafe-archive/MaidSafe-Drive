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
#include <sys/stat.h>
#endif

#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#include "boost/filesystem/path.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

#ifndef MAIDSAFE_WIN32
#include "fuse/fuse.h"
#endif
#include "maidsafe/encrypt/data_map.h"
#include "maidsafe/drive/config.h"

namespace maidsafe {

namespace drive {

// Represents directory and file information
struct MetaData {
  MetaData();
  MetaData(const boost::filesystem::path& name, bool is_directory);
  MetaData(const MetaData& meta_data);
  MetaData(MetaData&& meta_data);
  MetaData& operator=(MetaData other);

  explicit MetaData(const std::string& serialised_meta_data);
  std::string Serialise() const;

  boost::posix_time::ptime creation_posix_time() const;
  boost::posix_time::ptime last_write_posix_time() const;
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
  std::shared_ptr<DirectoryId> directory_id;
  std::vector<std::string> notes;
};

bool operator<(const MetaData& lhs, const MetaData& rhs);
void swap(MetaData& lhs, MetaData& rhs);

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_META_DATA_H_
