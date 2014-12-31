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

#include "maidsafe/drive/tools/commands/unix_file_commands.h"

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "maidsafe/common/utils.h"


namespace maidsafe {
namespace drive {
namespace tools {
namespace commands {

void CreateDirectoryCommand(const boost::filesystem::path& path, mode_t mode) {
  int result(mkdir(path.string().c_str(), mode));
  if (result != 0) {
    LOG(kError) << "Failed to create directory " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
}

int CreateFileCommand(const boost::filesystem::path& path, int flags) {
  int file_descriptor(open(path.string().c_str(), flags));
  if (file_descriptor == -1) {
    LOG(kError) << "Failed to open " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return file_descriptor;
}

int CreateFileCommand(const boost::filesystem::path& path, int flags, mode_t mode) {
  int file_descriptor(open(path.string().c_str(), flags, mode));
  if (file_descriptor == -1) {
    LOG(kError) << "Failed to open " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return file_descriptor;
}

int CreateFileCommand(const boost::filesystem::path& path, mode_t mode) {
  int file_descriptor(creat(path.string().c_str(), mode));
  if (file_descriptor == -1) {
    LOG(kError) << "Failed to open " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return file_descriptor;
}

int CreateTempFileCommand(boost::filesystem::path& path_template) {
  int file_descriptor(mkstemp(const_cast<char*>(path_template.string().c_str())));
  if (file_descriptor == -1) {
    LOG(kError) << "Failed to create temp file " << path_template;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return file_descriptor;
}

ssize_t WriteFileCommand(int file_descriptor, const std::string& buffer) {
  ssize_t bytes_written(write(file_descriptor, buffer.c_str(), buffer.size()));
  if (bytes_written == -1) {
    // TODO(Brian) look into the potential error codes returned here they're dependent on factors
    // such as the parameters passed when opening the file.
    LOG(kError) << "Failed to write to file with descriptor " << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return bytes_written;
}

ssize_t WriteFileCommand(int file_descriptor, const std::string& buffer, off_t offset) {
  ssize_t bytes_written(pwrite(file_descriptor, buffer.c_str(), buffer.size(), offset));
  if (bytes_written == -1) {
    LOG(kError) << "Failed to write to file with descriptor " << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return bytes_written;
}

ssize_t ReadFileCommand(int file_descriptor, const std::string& buffer) {
  ssize_t bytes_read(read(file_descriptor, const_cast<char*>(buffer.c_str()), buffer.size()));
  if (bytes_read == -1) {
    LOG(kError) << "Failed to readfrom file with descriptor " << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return bytes_read;
}

ssize_t ReadFileCommand(int file_descriptor, const std::string& buffer, off_t offset) {
  ssize_t bytes_read(
      pread(file_descriptor, const_cast<char*>(buffer.c_str()), buffer.size(), offset));
  if (bytes_read == -1) {
    LOG(kError) << "Failed to read from file with descriptor " << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return bytes_read;
}

int GetFileSizeCommand(int file_descriptor) {
  struct stat stbuf;
  int result(fstat(file_descriptor, &stbuf));
  if (result != 0) {
    LOG(kError) << "Failed to get size for file with descriptor " << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return stbuf.st_size;
}

int GetFileSizeCommand(const boost::filesystem::path& path) {
  struct stat stbuf;
  int result(stat(path.string().c_str(), &stbuf));
  if (result != 0) {
    LOG(kError) << "Failed to get size for file " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return stbuf.st_size;
}

mode_t GetModeCommand(int file_descriptor) {
  struct stat stbuf;
  int result(fstat(file_descriptor, &stbuf));
  if (result != 0) {
    LOG(kError) << "Failed to get mode for file with descriptor" << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return stbuf.st_mode;
}

mode_t GetModeCommand(const boost::filesystem::path& path) {
  struct stat stbuf;
  int result(stat(path.string().c_str(), &stbuf));
  if (result != 0) {
    LOG(kError) << "Failed to get mode for file " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return stbuf.st_mode;
}

void SetModeCommand(int file_descriptor, mode_t mode) {
  int result(fchmod(file_descriptor, mode));
  if (result != 0) {
    LOG(kError) << "Failed to set mode for file with descriptor " << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
}

void SetModeCommand(const boost::filesystem::path& path, mode_t mode) {
  int result(chmod(path.string().c_str(), mode));
  if (result != 0) {
    LOG(kError) << "Failed to set mode for file " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
}

void CloseFileCommand(int file_descriptor) {
  int result(close(file_descriptor));
  if (result != 0) {
    LOG(kError) << "Failed to close file with descriptor " << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
}

void UnlinkFileCommand(const boost::filesystem::path& path) {
  int result(unlink(path.string().c_str()));
  if (result != 0) {
    LOG(kError) << "Failed to unlink file " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
}

void RemoveDirectoryCommand(const boost::filesystem::path& path) {
  int result(rmdir(path.string().c_str()));
  if (result != 0) {
    LOG(kError) << "Failed to delete " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
}

void SyncFileCommand(int file_descriptor) {
  int result(fsync(file_descriptor));
  if (result != 0) {
    LOG(kError) << "Failed to sync file with descriptor " << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
}

off_t SetFileOffsetCommand(int file_descriptor, off_t offset, int whence) {
  off_t result(lseek(file_descriptor, offset, whence));
  if (result == -1) {
    LOG(kError) << "Failed to set offset for file with descriptor " << file_descriptor;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return result;
}

std::vector<boost::filesystem::path> EnumerateDirectoryCommand(
    const boost::filesystem::path& path) {
  std::vector<boost::filesystem::path> files;
  DIR* directory;
  struct dirent* dir;
  directory = opendir(path.string().c_str());
  if (directory) {
    // TODO(Brian) change to use readdir_r
    while ((dir = readdir(directory)) != nullptr) {  // NOLINT
      if (dir->d_type == DT_REG) {
        files.push_back(dir->d_name);
      }
    }
    closedir(directory);
  }
  return files;
}

}  // namespace commands
}  // namespace tools
}  // namespace drive
}  // namespace maidsafe
