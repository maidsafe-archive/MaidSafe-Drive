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


namespace maidsafe {
namespace drive {

DummyWinDrive::DummyWinDrive(
  StoragePtr storage, const Identity& unique_user_id, const Identity& root_parent_id,
  const boost::filesystem::path& mount_dir, const std::string& product_id,
  const boost::filesystem::path& drive_name)
    : Drive(storage, unique_user_id, root_parent_id, mount_dir) {}

int DummyWinDrive::Unmount() {
  return -1;
}

void DummyWinDrive::NotifyRename(const boost::filesystem::path& /*from_relative_path*/,
                                 const boost::filesystem::path& /*to_relative_path*/) const {}
void DummyWinDrive::SetNewAttributes(FileContext* /*file_context*/, bool /*is_directory*/,
                                     bool /*read_only*/) {}

}  // namespace drive
}  // namespace maidsafe
