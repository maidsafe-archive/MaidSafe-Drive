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

#ifndef MAIDSAFE_DRIVE_TOOLS_COMMANDS_UNIX_FILE_COMMANDS_H_
#define MAIDSAFE_DRIVE_TOOLS_COMMANDS_UNIX_FILE_COMMANDS_H_

#include <unistd.h>
#include <vector>
#include <string>

#include "boost/filesystem/path.hpp"


namespace maidsafe {
namespace drive {
namespace tools {
namespace commands {

void CreateDirectoryCommand(const boost::filesystem::path& path, mode_t mode);
int CreateFileCommand(const boost::filesystem::path& path, int flags);
int CreateFileCommand(const boost::filesystem::path& path, int flags, mode_t mode);
int CreateFileCommand(const boost::filesystem::path& path, mode_t mode);
int CreateTempFileCommand(boost::filesystem::path& path_template);
int GetFilePermissionsCommand(const boost::filesystem::path& path);
ssize_t WriteFileCommand(int file_descriptor, const std::string& buffer);
ssize_t WriteFileCommand(int file_descriptor, const std::string& buffer, off_t offset);
ssize_t ReadFileCommand(int file_descriptor, const std::string& buffer);
ssize_t ReadFileCommand(int file_descriptor, const std::string& buffer, off_t offset);
int GetFileSizeCommand(int file_descriptor);
int GetFileSizeCommand(const boost::filesystem::path& path);
mode_t GetModeCommand(int file_descriptor);
mode_t GetModeCommand(const boost::filesystem::path& path);
void SetModeCommand(int file_descriptor, mode_t mode);
void SetModeCommand(const boost::filesystem::path& path, mode_t mode);
void CloseFileCommand(int file_descriptor);
void UnlinkFileCommand(const boost::filesystem::path& path);
void RemoveDirectoryCommand(const boost::filesystem::path& path);
void SyncFileCommand(int file_descriptor);
off_t SetFileOffsetCommand(int file_descriptor, off_t offset, int whence);
std::vector<boost::filesystem::path> EnumerateDirectoryCommand(const boost::filesystem::path& path);

}  // namespace commands
}  // namespace tools
}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_TOOLS_COMMANDS_UNIX_FILE_COMMANDS_H_
