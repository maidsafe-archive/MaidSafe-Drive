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
#    include "maidsafe/drive/mac_fuse.h"
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
  DataMapPtr data_map;
  DirectoryIdPtr directory_id;
  std::vector<std::string> notes;
};

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_META_DATA_H_
