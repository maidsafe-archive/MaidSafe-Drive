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

#ifndef MAIDSAFE_DRIVE_WIN_DRIVE_H_
#define MAIDSAFE_DRIVE_WIN_DRIVE_H_

#include <Windows.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "CbFs.h"  // NOLINT

#include "boost/filesystem/path.hpp"

#include "maidsafe/drive/drive_api.h"

namespace maidsafe {
namespace drive {

class CbfsDriveInUserSpace : public DriveInUserSpace {
 public:
  typedef nfs::ClientMaidNfs ClientNfs;
  typedef passport::Maid Maid;

  CbfsDriveInUserSpace(ClientNfs& client_nfs,
                       DataStore& data_store,
                       const Maid& maid,
                       const Identity& unique_user_id,
                       const std::string& root_parent_id,
                       const boost::filesystem::path &mount_dir,
                       const boost::filesystem::path &drive_name,
                       const int64_t &max_space,
                       const int64_t &used_space);
  virtual ~CbfsDriveInUserSpace();
  bool Init();
  bool Mount();
  virtual bool Unmount(int64_t &max_space, int64_t &used_space);
  int Install();
  void NotifyDirectoryChange(const boost::filesystem::path &relative_path, OpType op) const;
  uint32_t max_file_path_length();
  virtual void NotifyRename(const boost::filesystem::path& from_relative_path,
                            const boost::filesystem::path& to_relative_path) const;

 private:
  void UnmountDrive(const boost::posix_time::time_duration &timeout_before_force);
  std::wstring drive_name() const;

  void UpdateDriverStatus();
  void UpdateMountingPoints();
  void OnCallbackFsInit();
  int OnCallbackFsInstall();
  void OnCallbackFsUninstall();
  void OnCallbackFsDeleteStorage();
  void OnCallbackFsMount();
  void OnCallbackFsUnmount();
  void OnCallbackFsAddPoint(const boost::filesystem::path &);
  void OnCallbackFsDeletePoint();
  virtual void SetNewAttributes(FileContext* file_context,
                                bool is_directory,
                                bool read_only);

  static void CbFsMount(CallbackFileSystem* sender);
  static void CbFsUnmount(CallbackFileSystem* sender);
  static void CbFsGetVolumeSize(CallbackFileSystem* sender,
                         __int64* total_number_of_sectors,
                         __int64* number_of_free_sectors);
  static void CbFsGetVolumeLabel(CallbackFileSystem* sender,
                                 LPTSTR volume_label);
  static void CbFsSetVolumeLabel(CallbackFileSystem* sender,
                                 LPCTSTR volume_label);
  static void CbFsGetVolumeId(CallbackFileSystem* sender, PDWORD volume_id);
  static void CbFsCreateFile(CallbackFileSystem* sender,
                             LPCTSTR file_name,
                             ACCESS_MASK desired_access,
                             DWORD file_attributes,
                             DWORD share_mode,
                             CbFsFileInfo* file_info,
                             CbFsHandleInfo* handle_info);
  static void CbFsOpenFile(CallbackFileSystem* sender,
                           LPCWSTR file_name,
                           ACCESS_MASK desired_access,
                           DWORD file_attributes,
                           DWORD share_mode,
                           CbFsFileInfo* file_info,
                           CbFsHandleInfo* handle_info);
  static void CbFsCloseFile(CallbackFileSystem* sender,
                            CbFsFileInfo* file_info,
                            CbFsHandleInfo* handle_info);
  static void CbFsGetFileInfo(CallbackFileSystem* sender,
                              LPCTSTR file_name,
                              LPBOOL file_exists,
                              PFILETIME creation_time,
                              PFILETIME last_access_time,
                              PFILETIME last_write_time,
                              __int64* end_of_file,
                              __int64* allocation_size,
                              __int64* file_id,
                              PDWORD file_attributes,
                              LPWSTR short_file_name OPTIONAL,
                              PWORD short_file_name_length OPTIONAL,
                              LPWSTR real_file_name OPTIONAL,
                              LPWORD real_file_name_length OPTIONAL);
  static void CbFsEnumerateDirectory(CallbackFileSystem* sender,
                                     CbFsFileInfo* directory_info,
                                     CbFsHandleInfo* handle_info,
                                     CbFsDirectoryEnumerationInfo* directory_enumeration_info,
                                     LPCWSTR mask,
                                     INT index,
                                     BOOL restart,
                                     LPBOOL file_found,
                                     LPWSTR file_name,
                                     PDWORD file_name_length,
                                     LPWSTR short_file_name OPTIONAL,
                                     PUCHAR short_file_name_length OPTIONAL,
                                     PFILETIME creation_time,
                                     PFILETIME last_access_time,
                                     PFILETIME last_write_time,
                                     __int64* end_of_file,
                                     __int64* allocation_size,
                                     __int64* file_id,
                                     PDWORD file_attributes);
  static void CbFsCloseDirectoryEnumeration(CallbackFileSystem* sender,
                                            CbFsFileInfo* directory_info,
                                            CbFsDirectoryEnumerationInfo* enumeration_info);
  static void CbFsSetAllocationSize(CallbackFileSystem* sender,
                                    CbFsFileInfo* file_info,
                                    __int64 allocation_size);
  static void CbFsSetEndOfFile(CallbackFileSystem* sender,
                               CbFsFileInfo* file_info,
                               __int64 end_of_file);
  static void CbFsSetFileAttributes(CallbackFileSystem* sender,
                                    CbFsFileInfo* file_info,
                                    CbFsHandleInfo* handle_info,
                                    PFILETIME creation_time,
                                    PFILETIME last_access_time,
                                    PFILETIME last_write_time,
                                    DWORD file_attributes);
  static void CbFsCanFileBeDeleted(CallbackFileSystem* sender,
                                   CbFsFileInfo* file_info,
                                   CbFsHandleInfo* handle_info,
                                   LPBOOL can_be_deleted);
  static void CbFsDeleteFile(CallbackFileSystem* sender,
                             CbFsFileInfo* file_info);
  static void CbFsRenameOrMoveFile(CallbackFileSystem* sender,
                                   CbFsFileInfo* file_info,
                                   LPCTSTR new_file_name);
  static void CbFsReadFile(CallbackFileSystem* sender,
                           CbFsFileInfo* file_info,
                           __int64 position,
                           PVOID buffer,
                           DWORD bytes_to_read,
                           PDWORD bytes_read);
  static void CbFsWriteFile(CallbackFileSystem* sender,
                            CbFsFileInfo* file_info,
                            __int64 position,
                            PVOID buffer,
                            DWORD bytes_to_write,
                            PDWORD bytes_written);
  static void CbFsIsDirectoryEmpty(CallbackFileSystem* sender,
                                   CbFsFileInfo* directory_info,
                                   LPWSTR file_name,
                                   LPBOOL is_empty);
  static void CbFsSetFileSecurity(CallbackFileSystem* sender,
                                  CbFsFileInfo* file_info,
                                  PVOID file_handle_context,
                                  SECURITY_INFORMATION security_information,
                                  PSECURITY_DESCRIPTOR security_descriptor,
                                  DWORD length);
  static void CbFsGetFileSecurity(CallbackFileSystem* sender,
                                  CbFsFileInfo* file_info,
                                  PVOID file_handle_context,
                                  SECURITY_INFORMATION security_information,
                                  PSECURITY_DESCRIPTOR security_descriptor,
                                  DWORD length,
                                  PDWORD length_needed);
  static void CbFsFlushFile(CallbackFileSystem* sender,
                            CbFsFileInfo* file_info);
  static void CbFsOnEjectStorage(CallbackFileSystem* sender);

  mutable CallbackFileSystem callback_filesystem_;
  LPCSTR guid_;
  LPCWSTR icon_id_;
  std::wstring drive_name_;
};

}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_WIN_DRIVE_H_
