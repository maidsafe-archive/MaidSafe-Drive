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

#include "maidsafe/drive/tools/commands/windows_file_commands.h"

#include "maidsafe/common/utils.h"


namespace maidsafe {
namespace drive {
namespace tools {
namespace commands {

BOOL CreateDirectoryCommand(const boost::filesystem::path& path) {
  BOOL result(CreateDirectory(path.wstring().c_str(), nullptr));
  if (!result) {
    LOG(kError) << "Failed to create directory " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return result;
}

HANDLE CreateFileCommand(const boost::filesystem::path& path, DWORD desired_access,
                         DWORD creation_disposition, DWORD flags_and_attributes) {
  HANDLE handle(CreateFile(path.wstring().c_str(), desired_access, 0, NULL, creation_disposition,
                           flags_and_attributes, NULL));
  if (handle == INVALID_HANDLE_VALUE) {
    LOG(kError) << "Failed to create file " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return handle;
}

DWORD GetFileAttributesCommand(const boost::filesystem::path& path) {
  DWORD attributes(GetFileAttributes(path.wstring().c_str()));
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    LOG(kError) << "Failed to get attributes for " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return attributes;
}

BOOL SetFileAttributesCommand(const boost::filesystem::path& path, DWORD attributes) {
  BOOL result(SetFileAttributes(path.wstring().c_str(), attributes));
  if (!result) {
    LOG(kError) << "Failed to set attributes for " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return result;
}

BOOL WriteFileCommand(HANDLE handle, const boost::filesystem::path& path,
                      const std::string& buffer, LPDWORD bytes_written, LPOVERLAPPED overlapped) {
  BOOL result(::WriteFile(handle, buffer.c_str(), static_cast<DWORD>(buffer.size()), bytes_written,
                          overlapped));
  if (!result) {
    LOG(kError) << "Failed to write to " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return result;
}

BOOL DeleteFileCommand(const boost::filesystem::path& path) {
  BOOL result(DeleteFile(path.wstring().c_str()));
  if (!result) {
    LOG(kError) << "Failed to delete " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return result;
}

BOOL RemoveDirectoryCommand(const boost::filesystem::path& path) {
  BOOL result(RemoveDirectory(path.wstring().c_str()));
  if (!result) {
    LOG(kError) << "Failed to delete " << path.string();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return result;
}

BOOL CloseHandleCommand(HANDLE handle) {
  BOOL result(CloseHandle(handle));
  if (!result) {
    LOG(kError) << "Failed to close handle";
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  return result;
}

DWORD GetFileSizeCommand(HANDLE handle, LPDWORD file_size_high) {
  return GetFileSize(handle, file_size_high);
}

std::vector<WIN32_FIND_DATA> EnumerateDirectoryCommand(const boost::filesystem::path& path) {
  WIN32_FIND_DATA file_data;
  HANDLE search_handle(nullptr);
  BOOL done = FALSE;
  std::vector<WIN32_FIND_DATA> files;
  std::wstring dot(L"."), double_dot(L".."), filename;

  search_handle = FindFirstFile((path / "\\*").wstring().c_str(), &file_data);
  if (search_handle == INVALID_HANDLE_VALUE) {
    LOG(kError) << "No files found at " << path.string();
    return files;
  }

  filename = file_data.cFileName;
  if (filename != dot && filename != double_dot)
    files.push_back(file_data);

  while (!done) {
    if (!FindNextFile(search_handle, &file_data)) {
      if (GetLastError() == ERROR_NO_MORE_FILES) {
        done = TRUE;
      } else {
        LOG(kWarning) << "Could not find next file.";
        return files;
      }
    } else {
      filename = file_data.cFileName;
      if (filename != dot && filename != double_dot)
        files.push_back(file_data);
    }
  }

  FindClose(search_handle);
  return files;
}

}  // namespace commands
}  // namespace tools
}  // namespace drive
}  // namespace maidsafe
