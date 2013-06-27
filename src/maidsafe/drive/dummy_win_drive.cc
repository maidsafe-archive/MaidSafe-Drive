/* Copyright 2013 MaidSafe.net limited

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
