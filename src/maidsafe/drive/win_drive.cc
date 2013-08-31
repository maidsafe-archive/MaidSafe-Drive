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

#include "maidsafe/drive/win_drive.h"

#include <locale>
#include <cwchar>

#include "maidsafe/common/error.h"
#include "maidsafe/common/log.h"


namespace maidsafe {

namespace drive {

fs::path GetNextAvailableDrivePath() {
  uint32_t drive_letters(GetLogicalDrives()), mask(0x4);
  std::string path("C:");
  while (drive_letters & mask) {
    mask <<= 1;
    ++path[0];
  }
  if (path[0] > 'Z')
    ThrowError(DriveErrors::no_drive_letter_available);
  return fs::path(path);
}



namespace detail {

#ifndef CBFS_KEY
#  error CBFS_KEY must be defined.
#endif

fs::path RelativePath(const fs::path& mount_path, const fs::path& absolute_path) {
  if (absolute_path.root_name() != mount_path.root_name() &&
      absolute_path.root_name() != mount_path)
    return fs::path();
  return absolute_path.root_directory() / absolute_path.relative_path();
}

std::string WstringToString(const std::wstring &input) {
  std::locale locale("");
  std::string string_buffer(input.size() * 4 + 1, 0);
  std::use_facet<std::ctype<wchar_t>>(locale).narrow(
        &input[0], &input[0] + input.size(), '?', &string_buffer[0]);
  return std::string(&string_buffer[0]);
}

void ErrorMessage(const std::string &method_name, ECBFSError error) {
  LOG(kError) << "Cbfs::" << method_name << ": " << WstringToString(error.Message());
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
