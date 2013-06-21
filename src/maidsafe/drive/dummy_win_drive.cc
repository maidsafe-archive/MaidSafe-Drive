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

#include "maidsafe/drive/dummy_win_drive.h"

namespace fs = boost::filesystem;
namespace bptime = boost::posix_time;
namespace args = std::placeholders;

namespace maidsafe {

namespace drive {

DummyWinDriveInUserSpace::DummyWinDriveInUserSpace(ClientNfs& client_nfs,
                                                   DataStore& data_store,
                                                   const Maid& maid,
                                                   const Identity& unique_user_id,
                                                   const std::string& root_parent_id,
                                                   const fs::path &mount_dir,
                                                   const fs::path &/*drive_name*/,
                                                   const int64_t &max_space,
                                                   const int64_t &used_space)
    : DriveInUserSpace(client_nfs,
                       data_store,
                       maid,
                       unique_user_id,
                       root_parent_id,
                       mount_dir,
                       max_space,
                       used_space) {}
int DummyWinDriveInUserSpace::Unmount(int64_t &/*max_space*/, int64_t &/*used_space*/) {
  return -1;
}
void DummyWinDriveInUserSpace::NotifyRename(const fs::path& /*from_relative_path*/,
                                            const fs::path& /*to_relative_path*/) const {}
void DummyWinDriveInUserSpace::SetNewAttributes(FileContext* /*file_context*/,
                                                bool /*is_directory*/,
                                                bool /*read_only*/) {}

}  // namespace drive

}  // namespace maidsafe
