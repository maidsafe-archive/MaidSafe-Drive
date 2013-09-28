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

#include "maidsafe/drive/dummy_win_drive.h"

namespace fs = boost::filesystem;
namespace bptime = boost::posix_time;
namespace args = std::placeholders;

namespace maidsafe {

namespace drive {

DummyWinDriveInUserSpace::DummyWinDriveInUserSpace(
    ClientNfs& client_nfs, DataStore& data_store, const Maid& maid, const Identity& unique_user_id,
    const std::string& root_parent_id, const fs::path& mount_dir, const fs::path& /*drive_name*/,
    const int64_t& max_space, const int64_t& used_space)
    : DriveInUserSpace(client_nfs, data_store, maid, unique_user_id, root_parent_id, mount_dir,
                       max_space, used_space) {}
int DummyWinDriveInUserSpace::Unmount(int64_t& /*max_space*/, int64_t& /*used_space*/) {
  return -1;
}
void DummyWinDriveInUserSpace::NotifyRename(const fs::path& /*from_relative_path*/,
                                            const fs::path& /*to_relative_path*/) const {}
void DummyWinDriveInUserSpace::SetNewAttributes(FileContext* /*file_context*/,
                                                bool /*is_directory*/, bool /*read_only*/) {}

}  // namespace drive

}  // namespace maidsafe
