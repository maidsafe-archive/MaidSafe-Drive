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

#ifndef MAIDSAFE_DRIVE_TOOLS_COMMANDS_WINDOWS_FILE_COMMANDS_H_
#define MAIDSAFE_DRIVE_TOOLS_COMMANDS_WINDOWS_FILE_COMMANDS_H_

#include <Windows.h>
#include <vector>
#include <string>

#include "boost/filesystem/path.hpp"


namespace maidsafe {
namespace drive {
namespace tools {
namespace commands {

BOOL CreateDirectoryCommand(const boost::filesystem::path& path);
HANDLE CreateFileCommand(const boost::filesystem::path& path, DWORD desired_access,
                         DWORD share_mode, DWORD creation_disposition,
                         DWORD flags_and_attributes);
DWORD GetFileAttributesCommand(const boost::filesystem::path& path);
BOOL SetFileAttributesCommand(const boost::filesystem::path& path, DWORD attributes);
BOOL WriteFileCommand(HANDLE handle, const boost::filesystem::path& path,
                      const std::string& buffer, LPDWORD bytes_written, LPOVERLAPPED overlapped);
BOOL ReadFileCommand(HANDLE handle, const boost::filesystem::path& path,
                     const std::string& buffer, LPDWORD bytes_read, LPOVERLAPPED overlapped);
BOOL DeleteFileCommand(const boost::filesystem::path& path);
BOOL RemoveDirectoryCommand(const boost::filesystem::path& path);
BOOL CloseHandleCommand(HANDLE handle);
DWORD GetFileSizeCommand(HANDLE handle, LPDWORD file_size_high);
BOOL SetFilePointerCommand(HANDLE handle, const LARGE_INTEGER& distance_from_start);
BOOL SetEndOfFileCommand(HANDLE handle);
std::vector<WIN32_FIND_DATA> EnumerateDirectoryCommand(const boost::filesystem::path& path);

}  // namespace commands
}  // namespace tools
}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_TOOLS_COMMANDS_WINDOWS_FILE_COMMANDS_H_
