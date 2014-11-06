/*  Copyright 2011 MaidSafe.net limited

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

#ifndef MAIDSAFE_DRIVE_WIN_DRIVE_H_
#define MAIDSAFE_DRIVE_WIN_DRIVE_H_

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#pragma pack(push, r1, 8)
#include "CbFs.h"  // NOLINT
#pragma pack(pop, r1)

#include "boost/filesystem/path.hpp"
#include "boost/preprocessor/stringize.hpp"

#include "maidsafe/common/profiler.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/drive.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/win_handle.h"
#include "maidsafe/drive/win_process.h"

namespace maidsafe {

namespace drive {

template <typename Storage>
class CbfsDrive;

namespace detail {

const char* const GetCbfsKey();

template <typename Storage>
CbfsDrive<Storage>* GetDrive(CallbackFileSystem* sender) {
  return static_cast<CbfsDrive<Storage>*>(sender->GetTag());
}

template <typename Storage>
boost::filesystem::path GetRelativePath(CbfsDrive<Storage>* cbfs_drive, CbFsFileInfo* file_info) {
  assert(file_info);
  static std::unique_ptr<WCHAR[]> file_name(new WCHAR[cbfs_drive->max_file_path_length()]);
  file_info->get_FileName(file_name.get());
  return boost::filesystem::path(file_name.get());
}

// By default on Win7 onwards, the registry has NtfsDisableLastAccessUpdate == 1.  This means that
// the LastAccessTime is never updated.  If the registry value is 0 or non-existent we should handle
// updating LastAccessTime, otherwise updates can be ignored.
bool LastAccessUpdateIsDisabled();

// Returns new timestamp if changed, empty optional if not
boost::optional<common::Clock::time_point> GetNewFiletime(
    const common::Clock::time_point filetime, const PFILETIME new_value);

void ErrorMessage(const std::string& method_name, ECBFSError error);

// ToFileTime is inherently lossy because FILETIME cannot represent nanosecond accuracy
FILETIME ToFileTime(const common::Clock::time_point&);
common::Clock::time_point ToTimePoint(const FILETIME&);

// Return true if access is granted to originator. common_error thrown if
// a windows function fails unexpectedly.
bool HaveAccessInternal(
    const WinHandle& originator,
    DWORD desired_permissions,
    const detail::WinProcess& owner,
    const detail::MetaData::FileType path_type,
    const detail::MetaData::Permissions path_permissions);

// Return the number of bytes needed for the security descriptor.
// common_error thrown if a windows function fails unexpectedly.
DWORD GetFileSecurityInternal(
    const detail::WinProcess& owner,
    const detail::MetaData::FileType path_type,
    const detail::MetaData::Permissions path_permissions,
    PSECURITY_DESCRIPTOR out_descriptor,
    const DWORD out_descriptor_length);

}  // namespace detail

template <typename Storage>
class CbfsDrive : public Drive<Storage> {
 public:
  CbfsDrive(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
            const Identity& root_parent_id, const boost::filesystem::path& mount_dir,
            const boost::filesystem::path& user_app_dir, const boost::filesystem::path& drive_name,
            const std::string& mount_status_shared_object_name, bool create);

  virtual ~CbfsDrive();

  // This must be called before 'Mount' to allow 'Mount' to succeed.
  void SetGuid(const std::string& guid);
  virtual void Mount();
  virtual void Unmount();
  uint32_t max_file_path_length() const;

 private:
  CbfsDrive(const CbfsDrive&);
  CbfsDrive(CbfsDrive&&);
  CbfsDrive& operator=(CbfsDrive);

  void UnmountDrive(const std::chrono::steady_clock::duration& timeout_before_force);
  std::wstring drive_name() const;

  void FlushAll();
  void GetDriverStatus();
  void UpdateMountingPoints();
  void InitialiseCbfs();

  // Return true if path has desired permissions. Throw common_error if
  // windows system call fails. Only invoke from certain callbacks - see
  // CBFS documentation for info about GetOriginatorToken().
  bool HaveAccess(const detail::Path& metadata, const DWORD permissions);

  static void CbFsMount(CallbackFileSystem* sender);
  static void CbFsUnmount(CallbackFileSystem* sender);
  static void CbFsGetVolumeSize(CallbackFileSystem* sender, int64_t* total_number_of_sectors,
                                int64_t* number_of_free_sectors);
  static void CbFsGetVolumeLabel(CallbackFileSystem* sender, LPTSTR volume_label);
  static void CbFsSetVolumeLabel(CallbackFileSystem* sender, LPCTSTR volume_label);
  static void CbFsGetVolumeId(CallbackFileSystem* sender, PDWORD volume_id);
  static void CbFsCreateFile(CallbackFileSystem* sender, LPCTSTR file_name,
                             ACCESS_MASK desired_access, DWORD file_attributes, DWORD share_mode,
                             CbFsFileInfo* file_info, CbFsHandleInfo* handle_info);
  static void CbFsOpenFile(CallbackFileSystem* sender, LPCWSTR file_name,
                           ACCESS_MASK desired_access, DWORD file_attributes, DWORD share_mode,
                           CbFsFileInfo* file_info, CbFsHandleInfo* handle_info);
  static void CbFsCloseFile(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                            CbFsHandleInfo* handle_info);
  static void CbFsGetFileInfo(CallbackFileSystem* sender, LPCTSTR file_name, LPBOOL file_exists,
                              PFILETIME creation_time, PFILETIME last_access_time,
                              PFILETIME last_write_time, int64_t* end_of_file,
                              int64_t* allocation_size, int64_t* file_id OPTIONAL,
                              PDWORD file_attributes, LPWSTR short_file_name OPTIONAL,
                              PWORD short_file_name_length OPTIONAL,
                              LPWSTR real_file_name OPTIONAL,
                              LPWORD real_file_name_length OPTIONAL);
  static void CbFsEnumerateDirectory(
      CallbackFileSystem* sender, CbFsFileInfo* directory_info, CbFsHandleInfo* handle_info,
      CbFsDirectoryEnumerationInfo* directory_enumeration_info, LPCWSTR mask, int index,
      BOOL restart, LPBOOL file_found, LPWSTR file_name, PDWORD file_name_length,
      LPWSTR short_file_name OPTIONAL, PUCHAR short_file_name_length OPTIONAL,
      PFILETIME creation_time, PFILETIME last_access_time, PFILETIME last_write_time,
      int64_t* end_of_file, int64_t* allocation_size, int64_t* file_id OPTIONAL,
      PDWORD file_attributes);
  static void CbFsCloseDirectoryEnumeration(CallbackFileSystem* sender,
                                            CbFsFileInfo* directory_info,
                                            CbFsDirectoryEnumerationInfo* enumeration_info);
  static void CbFsSetAllocationSize(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                    int64_t allocation_size);
  static void CbFsSetEndOfFile(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                               int64_t end_of_file);
  static void CbFsSetFileAttributes(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                    CbFsHandleInfo* handle_info, PFILETIME creation_time,
                                    PFILETIME last_access_time, PFILETIME last_write_time,
                                    DWORD file_attributes);
  static void CbFsCanFileBeDeleted(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                   CbFsHandleInfo* handle_info, LPBOOL can_be_deleted);
  static void CbFsDeleteFile(CallbackFileSystem* sender, CbFsFileInfo* file_info);
  static void CbFsRenameOrMoveFile(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                   LPCTSTR new_file_name);
  static void CbFsReadFile(CallbackFileSystem* sender, CbFsFileInfo* file_info, int64_t position,
                           PVOID buffer, DWORD bytes_to_read, PDWORD bytes_read);
  static void CbFsWriteFile(CallbackFileSystem* sender, CbFsFileInfo* file_info, int64_t position,
                            PVOID buffer, DWORD bytes_to_write, PDWORD bytes_written);
  static void CbFsIsDirectoryEmpty(CallbackFileSystem* sender, CbFsFileInfo* directory_info,
                                   LPCWSTR file_name, LPBOOL is_empty);
  static void CbFsSetFileSecurity(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                  CbFsHandleInfo* file_handle_context,
                                  SECURITY_INFORMATION security_information,
                                  PSECURITY_DESCRIPTOR security_descriptor, DWORD length);
  static void CbFsGetFileSecurity(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                  CbFsHandleInfo* file_handle_context,
                                  SECURITY_INFORMATION security_information,
                                  PSECURITY_DESCRIPTOR security_descriptor, DWORD length,
                                  PDWORD length_needed);
  static void CbFsFlushFile(CallbackFileSystem* sender, CbFsFileInfo* file_info);
  static void CbFsStorageEjected(CallbackFileSystem* sender);

  const detail::WinProcess process_owner_;
  mutable CallbackFileSystem callback_filesystem_;
  LPCWSTR icon_id_;
  std::wstring drive_name_;
  std::string guid_;
  boost::promise<void> unmounted_;
};

template <typename Storage>
CbfsDrive<Storage>::CbfsDrive(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
                              const Identity& root_parent_id,
                              const boost::filesystem::path& mount_dir,
                              const boost::filesystem::path& user_app_dir,
                              const boost::filesystem::path& drive_name,
                              const std::string& mount_status_shared_object_name, bool create)
    : Drive(storage, unique_user_id, root_parent_id, mount_dir, user_app_dir,
            mount_status_shared_object_name, create),
      process_owner_(),
      callback_filesystem_(),
      icon_id_(L"MaidSafeDriveIcon"),
      drive_name_(drive_name.wstring()),
      unmounted_() {}

template <typename Storage>
CbfsDrive<Storage>::~CbfsDrive() {
  Unmount();
}

template <typename Storage>
void CbfsDrive<Storage>::SetGuid(const std::string& guid) {
  if (!guid_.empty()) {
    LOG(kError) << "GUID has already been set to " << guid_;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::unable_to_handle_request));
  }
  guid_ = guid;
}

template <typename Storage>
void CbfsDrive<Storage>::Mount() {
#ifndef NDEBUG
    int timeout_milliseconds(0);
#else
    int timeout_milliseconds(30000);
#endif
  if (guid_.empty()) {
    LOG(kError) << "GUID is empty - 'SetGuid' must be called before 'Mount'";
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::uninitialised));
  }
  try {
    InitialiseCbfs();
    GetDriverStatus();
    callback_filesystem_.Initialize(guid_.c_str());
    callback_filesystem_.CreateStorage();
    // SetIcon can only be called after CreateStorage has successfully completed.
    callback_filesystem_.SetIcon(icon_id_);
    callback_filesystem_.MountMedia(timeout_milliseconds);
    // The following can only be called when the media is mounted.
    callback_filesystem_.AddMountingPoint(kMountDir_.c_str());
    UpdateMountingPoints();
  }
  catch (const ECBFSError& error) {
    detail::ErrorMessage("Mount: ", error);
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::uninitialised));
  }
  catch (const std::exception& e) {
    LOG(kError) << "Mount: " << e.what();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::uninitialised));
  }

  LOG(kInfo) << "Mounted.";
  mount_promise_.set_value();
  if (!kMountStatusSharedObjectName_.empty()) {
    NotifyMountedAndWaitForUnmountRequest(kMountStatusSharedObjectName_);
    Unmount();
  }
  auto wait_until_unmounted(unmounted_.get_future());
  wait_until_unmounted.get();
}

template <typename Storage>
void CbfsDrive<Storage>::UnmountDrive(
    const std::chrono::steady_clock::duration& timeout_before_force) {
  std::chrono::steady_clock::time_point timeout(std::chrono::steady_clock::now() +
                                                timeout_before_force);
  while (callback_filesystem_.Active()) {
    try {
      for (int index = callback_filesystem_.GetMountingPointCount() - 1; index >= 0; --index)
        callback_filesystem_.DeleteMountingPoint(index);
      callback_filesystem_.UnmountMedia(std::chrono::steady_clock::now() < timeout);
    }
    catch (const ECBFSError&) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

template <typename Storage>
void CbfsDrive<Storage>::Unmount() {
  try {
    std::call_once(this->unmounted_once_flag_, [&] {
        // Only one instance of this lambda function can be run simultaneously.  If any CBFS
        // function throws, the unmounted_once_flag_ remains unset and another attempt can be made.
        UnmountDrive(std::chrono::seconds(3));
        if (callback_filesystem_.StoragePresent())
          callback_filesystem_.DeleteStorage();
        callback_filesystem_.SetRegistrationKey(nullptr);
        unmounted_.set_value();
        if (!kMountStatusSharedObjectName_.empty())
          NotifyUnmounted(kMountStatusSharedObjectName_);

        this->directory_handler_->StoreAll();
    });
  }
  catch (const ECBFSError& error) {
    detail::ErrorMessage("Unmount", error);
  }
}

template <typename Storage>
uint32_t CbfsDrive<Storage>::max_file_path_length() const {
  return static_cast<uint32_t>(callback_filesystem_.GetMaxFilePathLength());
}

template <typename Storage>
std::wstring CbfsDrive<Storage>::drive_name() const {
  return drive_name_;
}

template <typename Storage>
void CbfsDrive<Storage>::FlushAll() {
  directory_handler_->FlushAll();
}

template <typename Storage>
void CbfsDrive<Storage>::GetDriverStatus() {
  BOOL installed = false;
  int version_high = 0, version_low = 0;
  SERVICE_STATUS status;
  CallbackFileSystem::GetModuleStatus(guid_.c_str(), CBFS_MODULE_DRIVER, &installed, &version_high,
                                      &version_low, &status);
  if (installed) {
    LPTSTR string_status = L"in undefined state";
    switch (status.dwCurrentState) {
      case SERVICE_CONTINUE_PENDING:
        string_status = L"continue is pending";
        break;
      case SERVICE_PAUSE_PENDING:
        string_status = L"pause is pending";
        break;
      case SERVICE_PAUSED:
        string_status = L"is paused";
        break;
      case SERVICE_RUNNING:
        string_status = L"is running";
        break;
      case SERVICE_START_PENDING:
        string_status = L"is starting";
        break;
      case SERVICE_STOP_PENDING:
        string_status = L"is stopping";
        break;
      case SERVICE_STOPPED:
        string_status = L"is stopped";
        break;
      default:
        string_status = L"in undefined state";
    }
    LOG(kInfo) << "Driver (version " << (version_high >> 16) << "." << (version_high & 0xFFFF)
               << "." << (version_low >> 16) << "." << (version_low & 0xFFFF)
               << ") installed, service " << string_status;
  } else {
    LOG(kError) << "CbFs driver is not installed.  Run 'cbfs_driver_installer -i' to rectify.";
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::driver_not_installed));
  }
}

template <typename Storage>
void CbfsDrive<Storage>::UpdateMountingPoints() {
  DWORD flags;
  LUID authentication_id;
  LPTSTR mounting_point = nullptr;
  for (int index = callback_filesystem_.GetMountingPointCount() - 1; index >= 0; --index) {
    callback_filesystem_.GetMountingPoint(index, &mounting_point, &flags, &authentication_id);
    if (mounting_point) {
      free(mounting_point);
      mounting_point = nullptr;
    }
  }
}

template <typename Storage>
void CbfsDrive<Storage>::InitialiseCbfs() {
  try {
    // Properties
    callback_filesystem_.SetCallAllOpenCloseCallbacks(true); // True needed for proper permissions checking
    callback_filesystem_.SetCaseSensitiveFileNames(true);
    callback_filesystem_.SetClusterSize(32 * detail::kFileBlockSize);  // must be a multiple of sector size
    callback_filesystem_.SetFileCacheEnabled(true);
    callback_filesystem_.SetMaxFileNameLength(MAX_PATH);
    callback_filesystem_.SetMaxFilePathLength(32767);
    callback_filesystem_.SetMaxReadWriteBlockSize(0xFFFFFFFF);
    callback_filesystem_.SetMetaDataCacheEnabled(true);
    callback_filesystem_.SetNonexistentFilesCacheEnabled(true);
    callback_filesystem_.SetParalleledProcessingAllowed(true);
    callback_filesystem_.SetProcessRestrictionsEnabled(false);
    callback_filesystem_.SetSectorSize(detail::kFileBlockSize);
    callback_filesystem_.SetSerializeCallbacks(true);
    callback_filesystem_.SetShortFileNameSupport(false);
//    callback_filesystem_.SetStorageCharacteristics(
//        CallbackFileSystem::CbFsStorageCharacteristics(
//            CallbackFileSystem::scRemovableMedia |
//            CallbackFileSystem::scShowInEjectionTray |
//            CallbackFileSystem::scAllowEjection));
//    callback_filesystem_.SetStorageType(CallbackFileSystem::stDiskPnP);
    callback_filesystem_.SetStorageType(CallbackFileSystem::stDisk);
    callback_filesystem_.SetTag(static_cast<void*>(this));
//    callback_filesystem_.SetMaxWorkerThreadCount(Concurrency());
    callback_filesystem_.SetUseFileCreationFlags(true);

    // Methods
    callback_filesystem_.SetRegistrationKey(detail::GetCbfsKey());

    // Events
    callback_filesystem_.SetOnStorageEjected(CbFsStorageEjected);
    callback_filesystem_.SetOnMount(CbFsMount);
    callback_filesystem_.SetOnUnmount(CbFsUnmount);
    callback_filesystem_.SetOnGetVolumeSize(CbFsGetVolumeSize);
    callback_filesystem_.SetOnGetVolumeLabel(CbFsGetVolumeLabel);
    callback_filesystem_.SetOnSetVolumeLabel(CbFsSetVolumeLabel);
    callback_filesystem_.SetOnGetVolumeId(CbFsGetVolumeId);
    callback_filesystem_.SetOnCreateFile(CbFsCreateFile);
    callback_filesystem_.SetOnOpenFile(CbFsOpenFile);
    callback_filesystem_.SetOnCloseFile(CbFsCloseFile);
    callback_filesystem_.SetOnGetFileInfo(CbFsGetFileInfo);
    callback_filesystem_.SetOnEnumerateDirectory(CbFsEnumerateDirectory);
    callback_filesystem_.SetOnCloseDirectoryEnumeration(CbFsCloseDirectoryEnumeration);
    callback_filesystem_.SetOnSetAllocationSize(CbFsSetAllocationSize);
    callback_filesystem_.SetOnSetEndOfFile(CbFsSetEndOfFile);
    callback_filesystem_.SetOnSetFileAttributes(CbFsSetFileAttributes);
    callback_filesystem_.SetOnCanFileBeDeleted(CbFsCanFileBeDeleted);
    callback_filesystem_.SetOnDeleteFile(CbFsDeleteFile);
    callback_filesystem_.SetOnRenameOrMoveFile(CbFsRenameOrMoveFile);
    callback_filesystem_.SetOnReadFile(CbFsReadFile);
    callback_filesystem_.SetOnWriteFile(CbFsWriteFile);
    callback_filesystem_.SetOnIsDirectoryEmpty(CbFsIsDirectoryEmpty);
    callback_filesystem_.SetOnFlushFile(CbFsFlushFile);
    callback_filesystem_.SetOnSetFileSecurity(CbFsSetFileSecurity);
    callback_filesystem_.SetOnGetFileSecurity(CbFsGetFileSecurity);
  }
  catch (const ECBFSError& error) {
    detail::ErrorMessage("InitialiseCbfs", error);
  }
  return;
}

// Return true if access is granted to originator. common_error thrown if
// a windows function fails unexpectedly.
template<typename Storage>
bool CbfsDrive<Storage>::HaveAccess(
    const detail::Path& path, const DWORD desired_permissions) {
  return HaveAccessInternal(
      detail::WinHandle(callback_filesystem_.GetOriginatorToken()),
      desired_permissions,
      process_owner_,
      path.meta_data.file_type(),
      path.meta_data.GetPermissions(this->get_base_file_permissions()));
}

// =============================== Callbacks =======================================================

// Quote from CBFS documentation:
//
// This event is fired after Callback File System mounts the storage and makes it available. The
// event is optional - you don't have to handle it.
template <typename Storage>
void CbfsDrive<Storage>::CbFsMount(CallbackFileSystem* /*sender*/) {
  LOG(kInfo) << "CbFsMount";
}

// Quote from CBFS documentation:
//
// This event is fired after Callback File System unmounts the storage and it becomes unavailable
// for the system. The event is optional - you don't have to handle it.
template <typename Storage>
void CbfsDrive<Storage>::CbFsUnmount(CallbackFileSystem* /*sender*/) {
  LOG(kInfo) << "CbFsUnmount";
}

// Quote from CBFS documentation:
//
// This event is fired when the OS wants to obtain information about the size and available space on
// the disk. Minimal size of the volume accepted by Windows is 6144 bytes (based on 3072-byte sector
// and 2 sectors per cluster), however CBFS adjusts the size to be at least 16 sectors to ensure
// compatibility with possible changes in future versions of Windows.
template <typename Storage>
void CbfsDrive<Storage>::CbFsGetVolumeSize(CallbackFileSystem* sender,
                                           int64_t* total_number_of_sectors,
                                           int64_t* number_of_free_sectors) {
  LOG(kInfo) << "CbFsGetVolumeSize";
  WORD sector_size(sender->GetSectorSize());
  *total_number_of_sectors = (std::numeric_limits<int64_t>::max() - 10000) / sector_size;
  *number_of_free_sectors = (std::numeric_limits<int64_t>::max() - 10000) / sector_size;
}

// Quote from CBFS documentation:
//
// This event is fired when the OS wants to obtain the volume label.
template <typename Storage>
void CbfsDrive<Storage>::CbFsGetVolumeLabel(CallbackFileSystem* sender, LPTSTR volume_label) {
  LOG(kInfo) << "CbFsGetVolumeLabel";
  auto cbfs_drive(detail::GetDrive<Storage>(sender));
  wcsncpy_s(volume_label, cbfs_drive->drive_name().size() + 1, &cbfs_drive->drive_name()[0],
            cbfs_drive->drive_name().size() + 1);
}

// Quote from CBFS documentation:
//
// This event is fired when the OS wants to change the volume label.
template <typename Storage>
void CbfsDrive<Storage>::CbFsSetVolumeLabel(CallbackFileSystem* /*sender*/,
                                            LPCTSTR /*volume_label*/) {
  LOG(kInfo) << "CbFsSetVolumeLabel";
}

// Quote from CBFS documentation:
//
// This event is fired when Callback File System wants to obtain the volume Id. The volume Id is
// unique user defined value (within Callback File System volumes).
template <typename Storage>
void CbfsDrive<Storage>::CbFsGetVolumeId(CallbackFileSystem* /*sender*/, PDWORD volume_id) {
  LOG(kInfo) << "CbFsGetVolumeId";
  *volume_id = 0x68451321;
}

// Quote from CBFS documentation:
//
// This event is fired when the OS wants to create a file or directory with given name and
// attributes. The directories are created with this call.
//
// To check, what should be created (file or directory), check FileAttributes as follows
// (C++ / C# notation):
// Directory = FileAttributes & FILE_ATTRIBUTE_DIRECTORY == FILE_ATTRIBUTE_DIRECTORY;
//
// If the file name contains semicolon (":"), this means that the request is made to create a named
// stream in a file. The part before the semicolon is the name of the file itself and the name after
// the semicolon is the name of the named stream. If you don't want to deal with named streams,
// don't implement the handler for OnEnumerateNamedStreams event. In this case CBFS API will tell
// the OS that the named streams are not supported by the file system.
//
// DesiredAccess, ShareMode and Attributes are passed as they were specified in the call to
// CreateFile() Windows API function.
//
// The application can use FileInfo's and HandleInfo's UserContext property to store the reference
// to some information, identifying the file or directory (such as file/directory handle or database
// record ID or reference to the stream class etc). The value, set in the event handler, is later
// passed to all operations, related to this file, together with file/directory name and attributes.
//
// Note, that if CallAllOpenCloseCallbacks property is set to false (default value), then this event
// is fired only when the first handle to the file is opened.
//
// Sometimes it can happen that OnCreateFile is fired for a file which already exists. Normally such
// situation will not happen, as the OS knows which files exist before creating or opening files
// (this information is requested via OnGetFileInfo and OnEnumerateDirectory). However, if your
// files come from outside, a race condition can happen and the file will exist externally but will
// not be known to the OS and to CBFS yet. In this case you need to decide based on your application
// logic - you can either truncate an existing file or report the error. ERROR_ALREADY_EXISTS is a
// proper error code in this situation.
template <typename Storage>
void CbfsDrive<Storage>::CbFsCreateFile(CallbackFileSystem* sender, LPCTSTR file_name,
                                        ACCESS_MASK /*desired_access*/,
                                        DWORD file_attributes, DWORD /*share_mode*/,
                                        CbFsFileInfo* /*file_info*/,
                                        CbFsHandleInfo* /*handle_info*/) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_name != nullptr);
  if (sender == nullptr || file_name == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));

  const bool is_directory((file_attributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
  const boost::filesystem::path relative_path(file_name);

  LOG(kInfo) << "CbFsCreateFile - " << relative_path << " 0x" << std::hex << file_attributes;
 
  try {
    //
    // Check for write access to directory
    //
    {
      const auto parent_directory(cbfs_drive->GetContext(relative_path.parent_path()));
      assert(parent_directory != nullptr);

      if (!cbfs_drive->HaveAccess(*parent_directory, FILE_GENERIC_WRITE)) {
        LOG(kWarning) << "CbFsCreateFile " << relative_path << ": Access denied";
        throw ECBFSError(ERROR_ACCESS_DENIED);
      }
    }

    // The desired_access field is currently ignored, and we enforce our own.
    // This could be confusing - but denying the file creation seems odd since
    // the user does have write permissions on the directory.
    const auto file(
        detail::File::Create(
            cbfs_drive->asio_service_.service(), relative_path.filename(), is_directory));
    assert(file.get() != nullptr);
    file->meta_data.set_attributes(file_attributes);
    cbfs_drive->Create(relative_path, file);
  }
  catch (const maidsafe_error& error) {
    LOG(kWarning) << "CbfsCreateFile: " << relative_path << ": " << error.what();
    if (error.code() == make_error_code(DriveErrors::file_exists)) {
      throw ECBFSError(ERROR_ALREADY_EXISTS);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS wants to open an existing file or directory with given name and
// attributes. The directory can be opened, for example, in order to change it's attributes or to
// enumerate it's contents.
//
// If the file name contains semicolon (":"), this means that the request is made to open a named
// stream in a file. The part before the semicolon is the name of the file itself and the name after
// the semicolon is the name of the named stream. If you don't want to deal with named streams,
// don't implement the handler for OnEnumerateNamedStreams event. In this case CBFS API will tell
// the OS that the named streams are not supported by the file system.
//
// The application can use FileInfo's and HandleInfo's UserContext property to store the reference
// to some information, identifying the file or directory (such as file/directory handle or database
// record ID or reference to the stream class etc). The value, set in the event handler, is later
// passed to all operations, related to this file, together with file/directory name and attributes.
//
// Note, that if CallAllOpenCloseCallbacks property is set to false (default value), then this event
// is fired only when the first handle to the file is opened.
//
// Sometimes it can happen that OnOpenFile is fired for a file which doesn't already exist. Normally
// such situation will not happen, as the OS knows which files exist before creating or opening
// files (this information is requested via OnGetFileInfo and OnEnumerateDirectory). However, if
// your files are created and deleted from outside, a race condition can happen and the file will
// disappear but will still be known to the OS and to CBFS. In this case you need to report
// ERROR_FILE_NOT_FOUND error.
template <typename Storage>
void CbfsDrive<Storage>::CbFsOpenFile(CallbackFileSystem* sender, LPCWSTR file_name,
                                      ACCESS_MASK desired_access,
                                      DWORD /*file_attributes*/, DWORD /*share_mode*/,
                                      CbFsFileInfo* /*file_info*/,
                                      CbFsHandleInfo* /*handle_info*/) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_name != nullptr);
  if (sender == nullptr || file_name == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  const boost::filesystem::path relative_path(file_name);
  LOG(kInfo) << "CbFsOpenFile - " << relative_path;

  CbfsDrive<Storage>* const cbfs_drive = detail::GetDrive<Storage>(sender);
  assert(cbfs_drive != nullptr);

  try {
    const auto open_file(cbfs_drive->template GetMutableContext<detail::File>(relative_path));
    if (open_file == nullptr) {
      throw ECBFSError(ERROR_INVALID_HANDLE);
    }

    if (!cbfs_drive->HaveAccess(*open_file, desired_access)) {
      LOG(kWarning) << "CbfsOpenFile: " << relative_path <<
                        ": Access denied (Requested access " << desired_access << ")";
      throw ECBFSError(ERROR_ACCESS_DENIED);
    }
    cbfs_drive->Open(*open_file);
  }
  catch (const maidsafe_error& error) {
    LOG(kWarning) << "CbFsOpenFile: " << relative_path << ": " << error.what();
    if (error.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS needs to close the previously created or opened file. Use
// FileInfo and HandleInfo to identify the file that needs to be closed.
//
// Note, that if CallAllOpenCloseCallbacks property is set to false (default value), then this event
// is fired only after the last handle to the file is closed.
template <typename Storage>
void CbfsDrive<Storage>::CbFsCloseFile(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                       CbFsHandleInfo* /*handle_info*/) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_info != nullptr);
  if (sender == nullptr || file_info == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  assert(cbfs_drive != nullptr);

  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));
  LOG(kInfo) << "CbFsCloseFile - " << relative_path;

  try {
    const auto close_file = cbfs_drive->template GetMutableContext<detail::File>(relative_path);
    if (close_file == nullptr) {
      throw ECBFSError(ERROR_INVALID_HANDLE);
    }
    close_file->Close();
  }
  catch (const maidsafe_error& error) {
    LOG(kWarning) << "CbFsCloseFile: " << relative_path << ": " << error.what();
    if (error.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// The event is fired when the OS needs to get information about the file or directory. If the file
// exists, FileExists parameter must be set to true and all information (besides optional
// parameters) must be set. If the file doesn't exist, then FileExists must be set to false. In this
// case no parameters are read back.
//
// If you have enabled short file name support, your callback should return short name (which must
// not exceed 12 characters in 8.3 format) via ShortFileName parameter. Note, that it is possible
// that the OS sends you short filename in FileName parameter, in which case you still need to
// provide the same short name in ShortFileName parameter.
//
// If you have enabled case-sensitive file name support, the driver might need to ask your code for
// "normalized" filename. This means that if the driver gets a request for "QWERTY.txt", but only
// "qwErTy.TxT" file exists on the filesystem, your code can return the existing file name using
// RealFileName parameter.
//
// To speed-up operations (save one string length measurement per name) the driver doesn't measure
// the length of the passed short and real file names, so your code must put the length of the
// passed file names into ShortFileNameLength and RealFileNameLength parameters.
template <typename Storage>
void CbfsDrive<Storage>::CbFsGetFileInfo(
    CallbackFileSystem* sender, LPCTSTR file_name, LPBOOL file_exists, PFILETIME creation_time,
    PFILETIME last_access_time, PFILETIME last_write_time, int64_t* end_of_file,
    int64_t* allocation_size, int64_t* /*file_id*/ OPTIONAL, PDWORD file_attributes,
    LPWSTR /*short_file_name*/ OPTIONAL, PWORD /*short_file_name_length*/ OPTIONAL,
    LPWSTR real_file_name OPTIONAL, LPWORD real_file_name_length OPTIONAL) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_name != nullptr);
  assert(file_exists != nullptr);
  if (sender == nullptr || file_name == nullptr || file_exists == nullptr ||
      creation_time == nullptr || last_access_time == nullptr || last_write_time == nullptr ||
      end_of_file == nullptr || allocation_size == nullptr || file_attributes == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  *file_exists = false;
  *file_attributes = 0xFFFFFFFF;

  const boost::filesystem::path relative_path(file_name);
  LOG(kInfo) << "CbFsGetFileInfo - " << relative_path;
  std::shared_ptr<const detail::Path> file;

  try {
    CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
    assert(cbfs_drive != nullptr);

    file = cbfs_drive->GetContext(relative_path);

    if (!cbfs_drive->HaveAccess(*file, GENERIC_READ)) {
      LOG(kWarning) << "CbFsGetfileInfo " << relative_path << ": Access denied";
      throw ECBFSError(ERROR_ACCESS_DENIED);
    }
  }
  catch (const maidsafe_error& error) {
    LOG(kWarning) << "CbFsGetFileInfo: " << relative_path << ": " << error.what();
    if (error.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }

  *file_exists = true;
  *creation_time = detail::ToFileTime(file->meta_data.creation_time());
  *last_access_time = detail::ToFileTime(file->meta_data.last_access_time());
  *last_write_time = detail::ToFileTime(file->meta_data.last_write_time());
  // if (file->meta_data.size < file->meta_data.allocation_size)
  //   file->meta_data.size = file->meta_data.allocation_size;
  // else if (file->meta_data.allocation_size < file->meta_data.size)
  //   file->meta_data.allocation_size = file->meta_data.size;
  *end_of_file = file->meta_data.size();
  *allocation_size = file->meta_data.allocation_size();
  // *file_id = 0;
  *file_attributes = file->meta_data.attributes();
  if (file->meta_data.file_type() == boost::filesystem::directory_file) {
    *file_attributes |= FILE_ATTRIBUTE_DIRECTORY;
  }
  if (real_file_name && real_file_name_length) {
    wcscpy(real_file_name, file->meta_data.name().wstring().c_str());
    *real_file_name_length = static_cast<WORD>(file->meta_data.name().wstring().size());
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS wants to enumerate the directory entries by mask.
//
// The mask can (but not necessarily does) include wildcard characters ("*" and "?") and any
// characters, allowed in file names, in any combination. Eg. you can receive masks like
// "smth?*.abc?e?*" and other complex combinations.
//
// The application must report information about the entry (file, directory, link) in the directory
// specified by DirectoryInfo. If the entry is present, FileFound must be set to true and the
// information about the entry must be included. If the entry is not present, FileFound must be set
// to false.
//
// Time-related parameters (CreationTime, LastAccessTime, LastWriteTime) are in UTC timezone.
//
// This event can be fired in some other cases, such as when the application uses FindFirstFile with
// file name (i.e. no wildcards in Mask) to get information provided about the file during
// enumeration or even before opening the file. So you must be ready to handle any mask (and just a
// file name without wildcard characters), and not just "*" or "*.*".
//
// Context in EnumerationInfo can be used to store information, which speeds up subsequent
// enumeration calls. The application can use this context to store the reference to some
// information, identifying the search (such as directory handle or database record ID etc). The
// value, set in the event handler, is later passed to all operations, related to this enumeration,
// i.e. subsequent calls to OnEnumerateDirectory and OnCloseEnumeration event handlers.
//
// The entry to be reported is identified by the data that the application stores in Enumeration
// Context. It is the application's job to track what entry it needs to report next.
//
// If you have enabled short file name support, your callback can receive a short directory name in
// DirectoryInfo. Also if you support short file names, you should provide the short file name via
// ShortFileName parameter. To speed-up operations (save one string length measurement) CBFS doesn't
// measure the length of the passed short file name (you will know it when putting it to
// ShortFileName) so your code must put the length of the passed short file name into
// ShortFileNameLength.
template <typename Storage>
void CbfsDrive<Storage>::CbFsEnumerateDirectory(
    CallbackFileSystem* sender, CbFsFileInfo* directory_info, CbFsHandleInfo* /*handle_info*/,
    CbFsDirectoryEnumerationInfo* /*directory_enumeration_info*/, LPCWSTR mask, int /*index*/,
    BOOL restart, LPBOOL file_found, LPWSTR file_name, PDWORD file_name_length,
    LPWSTR /*short_file_name*/ OPTIONAL, PUCHAR /*short_file_name_length*/ OPTIONAL,
    PFILETIME creation_time, PFILETIME last_access_time, PFILETIME last_write_time,
    int64_t* end_of_file, int64_t* allocation_size, int64_t* /*file_id*/ OPTIONAL,
    PDWORD file_attributes) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(directory_info != nullptr);
  if (sender == nullptr || directory_info == nullptr || mask == nullptr || file_found == nullptr ||
      file_name == nullptr || file_name_length == nullptr || creation_time == nullptr ||
      last_access_time == nullptr || last_write_time == nullptr || end_of_file == nullptr ||
      allocation_size == nullptr || file_attributes == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }
  *file_found = false;

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, directory_info));
  const std::wstring mask_str(mask);
  LOG(kInfo) << "CbFsEnumerateDirectory - " << relative_path << " mask: "
             << WstringToString(mask_str) << " restart: " << std::boolalpha << (restart != 0);
  const bool exact_match(mask_str != L"*");

  std::shared_ptr<detail::Directory> directory(nullptr);
  try {
    directory = cbfs_drive->directory_handler_->template Get<detail::Directory>(relative_path);
    assert(directory.get() != nullptr);

    if (!cbfs_drive->HaveAccess(*directory, GENERIC_READ)) {
      LOG(kWarning) << "CbfsEnumerateDirectory " << relative_path << ": Access denied";
      throw ECBFSError(ERROR_ACCESS_DENIED);
    }

    if (restart) {
      directory->ResetChildrenCounter();
    }
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "Failed enumerating " << relative_path << ": " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }

  std::shared_ptr<const detail::Path> file(nullptr);
  if (exact_match) {
    while (!(*file_found)) {
      file = directory->GetChildAndIncrementCounter();
      if (!file)
        break;
      *file_found = detail::MatchesMask(mask_str, file->meta_data.name());
    }
  } else {
    file = directory->GetChildAndIncrementCounter();
    *file_found = (file != nullptr);
  }

  if (*file_found) {
    // Need to use wcscpy rather than the secure wcsncpy_s as file_name has a size of 0 in some
    // cases.  CBFS docs specify that callers must assign MAX_PATH chars to file_name, so we assume
    // this is done.
    wcscpy(file_name, file->meta_data.name().wstring().c_str());
    *file_name_length = static_cast<DWORD>(file->meta_data.name().wstring().size());
    *creation_time = detail::ToFileTime(file->meta_data.creation_time());
    *last_access_time = detail::ToFileTime(file->meta_data.last_access_time());
    *last_write_time = detail::ToFileTime(file->meta_data.last_write_time());
    *end_of_file = file->meta_data.size();
    *allocation_size = file->meta_data.allocation_size();
    *file_attributes = file->meta_data.attributes();
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS has finished enumerating the directory contents and requests the
// resources, allocated for enumeration, to be released.
template <typename Storage>
void CbfsDrive<Storage>::CbFsCloseDirectoryEnumeration(
    CallbackFileSystem* sender, CbFsFileInfo* directory_info,
    CbFsDirectoryEnumerationInfo* /*directory_enumeration_info*/) {
  assert(sender != nullptr);
  assert(directory_info != nullptr);
  if (sender == nullptr || directory_info == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, directory_info));
  LOG(kInfo) << "CbFsCloseEnumeration - " << relative_path;
  try {
    cbfs_drive->ReleaseDir(relative_path);
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "CbFsCloseDirectoryEnumeration " << relative_path << ": " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS or the application needs to set the allocation size of the file.
//
// AllocationSize is usually larger (and much larger) than the size of the file data. This happens
// because some file operations first reserve space for the file, then start writing actual data to
// this file. The application should track such situations and avoid re-allocating file space where
// possible to improve speed.
template <typename Storage>
void CbfsDrive<Storage>::CbFsSetAllocationSize(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                               int64_t allocation_size) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_info != nullptr);
  if (sender == nullptr || file_info == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  assert(cbfs_drive != nullptr);

  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));
  LOG(kInfo) << "CbFsSetAllocationSize - " << relative_path << " to " << allocation_size
             << " bytes.";
  try {
    const auto file(cbfs_drive->GetMutableContext(relative_path));
    assert(file.get() != nullptr);
    file->meta_data.UpdateAllocationSize(allocation_size);
    file->ScheduleForStoring();
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "CbFsSetAllocationSize " << relative_path << ": " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS or the application needs to change the size of the open file.
template <typename Storage>
void CbfsDrive<Storage>::CbFsSetEndOfFile(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                          int64_t end_of_file) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_info != nullptr);
  if (sender == nullptr || file_info == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  assert(cbfs_drive != nullptr);
  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));
  LOG(kInfo) << "CbFsSetEndOfFile - " << relative_path << " to " << end_of_file << " bytes.";
  try {
    const auto file(cbfs_drive->GetMutableContext<detail::File>(relative_path));
    if (file == nullptr) {
      throw ECBFSError(ERROR_INVALID_HANDLE);
    }
    file->Truncate(end_of_file);
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "CbFsSetEndOfFile " << relative_path << ": " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS or the application needs to change the times and/or the
// attributes of the open file.  If times are nullptrs or if attributes is 0, they aren't set.
template <typename Storage>
void CbfsDrive<Storage>::CbFsSetFileAttributes(
    CallbackFileSystem* sender, CbFsFileInfo* file_info, CbFsHandleInfo* /*handle_info*/,
    PFILETIME creation_time, PFILETIME last_access_time, PFILETIME last_write_time,
    DWORD file_attributes) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_info != nullptr);
  if (sender == nullptr || file_info == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  assert(cbfs_drive != nullptr);

  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));
  LOG(kInfo) << "CbFsSetFileAttributes- " << relative_path << " 0x" << std::hex << file_attributes;

  try {

    // File type cannot be changed
    bool changed = false;
    const auto path(cbfs_drive->GetMutableContext(relative_path));
    assert(path != nullptr);
    
    if (file_attributes && path->meta_data.attributes() != file_attributes) {
      changed = true;
      path->meta_data.set_attributes(file_attributes);
    }

    {
      const boost::optional<common::Clock::time_point> new_creation_time =
          detail::GetNewFiletime(path->meta_data.creation_time(), creation_time);
      const boost::optional<common::Clock::time_point> new_last_write_time =
          detail::GetNewFiletime(path->meta_data.last_write_time(), last_write_time);

      if (new_creation_time) {
        changed = true;
        path->meta_data.set_creation_time(*new_creation_time);
      }
      if (new_last_write_time) {
        changed = true;
        path->meta_data.set_last_write_time(*new_last_write_time);
      }
    }

    if (!detail::LastAccessUpdateIsDisabled()) {
      // TODO(Fraser#5#): 2013-12-05 - Decide whether to treat this as worthy of marking the
      //                  metadata as changed (hence causing a new directory version to be stored).
      const boost::optional<common::Clock::time_point> new_last_access_time =
          detail::GetNewFiletime(path->meta_data.last_access_time(), last_access_time);
      if (new_last_access_time) {
        path->meta_data.set_last_access_time(*new_last_access_time);
      }
    }

    if (changed) {
      path->meta_data.set_status_time(common::Clock::now());
      path->ScheduleForStoring();
    }
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "CbFsSetFileAttributes " << relative_path << ": " << e.what();
    if (e.code() == maidsafe::DriveErrors::no_such_file) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS needs to check if the file or directory can be deleted. Firing of
// this event doesn't necessarily means, that the entry will be deleted even if CanBeDeleted was set
// to true.
template <typename Storage>
void CbfsDrive<Storage>::CbFsCanFileBeDeleted(CallbackFileSystem* /*sender*/,
                                              CbFsFileInfo* /*file_info*/,
                                              CbFsHandleInfo* /*handle_info*/,
                                              LPBOOL can_be_deleted) {
  SCOPED_PROFILE
  if (can_be_deleted == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }
  LOG(kInfo) << "CbFsCanFileBeDeleted - ";  //  << relative_path;
  *can_be_deleted = true;
}

// Quote from CBFS documentation:
//
// This event is fired when the OS needs to delete the file or directory. There's no way to cancel
// deletion of the file or directory from this event. If your application needs to prevent deletion,
// you need to do this in OnCanFileBeDeleted callback/event handler.
template <typename Storage>
void CbfsDrive<Storage>::CbFsDeleteFile(CallbackFileSystem* sender, CbFsFileInfo* file_info) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_info != nullptr);
  if (sender == nullptr || file_info == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  assert(cbfs_drive != nullptr);

  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));
  LOG(kInfo) << "CbFsDeleteFile - " << relative_path;
  try {
    cbfs_drive->Delete(relative_path);
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "CbFsDeleteFile " << relative_path << ": " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS needs to rename or move the file within a file system.
template <typename Storage>
void CbfsDrive<Storage>::CbFsRenameOrMoveFile(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                              LPCTSTR new_file_name) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_info != nullptr);
  assert(new_file_name != nullptr);
  if (sender == nullptr || file_info == nullptr || new_file_name == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  assert(cbfs_drive != nullptr);

  const auto old_relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));
  const boost::filesystem::path new_relative_path(new_file_name);
  LOG(kInfo) << "CbFsRenameOrMoveFile - " << old_relative_path << " to " << new_relative_path;
  try {
    cbfs_drive->Rename(old_relative_path, new_relative_path);
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "CbFsRenameOrMoveFile " << old_relative_path << " to " << new_relative_path << ": " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS needs to read the data from the open file or volume. Write the
// data (no more than BytesToRead bytes) to the povided Buffer. Put the actual number of read bytes
// to BytesRead. Note, that unless you create the virtual disk for some specific application, your
// callback handler should be able to provide exactly BytesToRead bytes of data. Reading less data
// than expected is an unexpected situation for many applications, and they will fail if you provide
// less bytes than requested.
template <typename Storage>
void CbfsDrive<Storage>::CbFsReadFile(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                      int64_t position, PVOID buffer, DWORD bytes_to_read,
                                      PDWORD bytes_read) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_info != nullptr);
  assert(buffer != nullptr);
  assert(bytes_read != nullptr);
  if (sender == nullptr || file_info == nullptr || buffer == nullptr || bytes_read == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }
  *bytes_read = 0;

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  assert(cbfs_drive != nullptr);
  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));
  try {
    const auto read_file = cbfs_drive->template GetMutableContext<detail::File>(relative_path);
    if (read_file == nullptr) {
      throw ECBFSError(ERROR_INVALID_HANDLE);
    }
    *bytes_read = static_cast<DWORD>(
        read_file->Read(static_cast<char*>(buffer), bytes_to_read, position));
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "Failed to read " << relative_path << ": " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS needs to write the data to the open file or volume. Note, that
// unless you create the virtual disk for some specific application, your callback handler should be
// able to write exactly BytesToWrite bytes of data. Writing less data than expected is an
// unexpected situation for many applications, and they will fail if you write less bytes than
// requested.
template <typename Storage>
void CbfsDrive<Storage>::CbFsWriteFile(CallbackFileSystem* sender, CbFsFileInfo* file_info,
                                       int64_t position, PVOID buffer, DWORD bytes_to_write,
                                       PDWORD bytes_written) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_info != nullptr);
  assert(buffer != nullptr);
  assert(bytes_written != nullptr);
  if (sender == nullptr || file_info == nullptr ||
      buffer == nullptr || bytes_written == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }
  *bytes_written = 0;

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  assert(cbfs_drive != nullptr);
  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));
  LOG(kInfo) << "CbFsWriteFile- " << relative_path << " writing " << bytes_to_write
             << " bytes at position " << position;
  try {
    const auto write_file = cbfs_drive->template GetMutableContext<detail::File>(relative_path);
    if (write_file == nullptr) {
      throw ECBFSError(ERROR_INVALID_HANDLE);
    }
    *bytes_written = static_cast<DWORD>(
        write_file->Write(static_cast<char*>(buffer), bytes_to_write, position));
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "Failed to write " << relative_path << ": " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS wants to check whether the directory is empty or contains some
// files.
template <typename Storage>
void CbfsDrive<Storage>::CbFsIsDirectoryEmpty(
    CallbackFileSystem* sender,
    CbFsFileInfo* /*directory_info*/, LPCWSTR file_name,
    LPBOOL is_empty) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  assert(file_name != nullptr);
  assert(is_empty != nullptr);
  if (sender == nullptr || file_name == nullptr || is_empty == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }
  *is_empty = true;

  LOG(kInfo) << "CbFsIsDirectoryEmpty - " << boost::filesystem::path(file_name);
  try {
    CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
    *is_empty = cbfs_drive->directory_handler_->template Get<detail::Directory>(file_name)->empty();
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "CbFsIsDirectoryEmpty " << file_name << ": " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

template<typename Storage>
void CbfsDrive<Storage>::CbFsSetFileSecurity(
    CallbackFileSystem*, CbFsFileInfo*, CbFsHandleInfo*, SECURITY_INFORMATION, PSECURITY_DESCRIPTOR, DWORD) {
  throw ECBFSError(ERROR_NOT_SUPPORTED);
}

template<typename Storage>
void CbfsDrive<Storage>::CbFsGetFileSecurity(
    CallbackFileSystem* sender,
    CbFsFileInfo* file_info,
    CbFsHandleInfo*,
    SECURITY_INFORMATION /*requested_information*/,
    PSECURITY_DESCRIPTOR security_descriptor,
    DWORD length,
    PDWORD length_needed) {
  assert(sender != nullptr);
  assert(file_info != nullptr);
  assert(length_needed != nullptr);
  if (sender == nullptr || file_info == nullptr || length_needed == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  *length_needed = 0;
  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  assert(cbfs_drive != nullptr);

  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));

  try {
    const auto path(cbfs_drive->GetContext(relative_path));
    assert(path.get() != nullptr);

    /* The requested_information parameter is ignored because if a DACL is
    not provided, the access defaults to grant. Therefore, to prevent issues,
    the DACL is always provided (and thus no needed for the parameter). */
    *length_needed =
        detail::GetFileSecurityInternal(
            cbfs_drive->process_owner_,
            path->meta_data.file_type(),
            path->meta_data.GetPermissions(cbfs_drive->get_base_file_permissions()),
            security_descriptor,
            length);

    if (*length_needed > length) {
      throw ECBFSError(ERROR_INSUFFICIENT_BUFFER);
    }
  }
  catch (const maidsafe_error& e) {
    LOG(kWarning) << "CbfsGetFile " << relative_path << " : " << e.what();
    if (e.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the OS tells the file system, that file buffers (incuding all possible
// metadata) must be flushed and written to the backend storage. FileInfo contains information about
// the file to be flushed. If FileInfo is empty, your code should attempt to flush everything
// related to the disk.
template <typename Storage>
void CbfsDrive<Storage>::CbFsFlushFile(CallbackFileSystem* sender, CbFsFileInfo* file_info) {
  SCOPED_PROFILE
  assert(sender != nullptr);
  if (sender == nullptr) {
    throw ECBFSError(ERROR_INVALID_PARAMETER);
  }

  CbfsDrive<Storage>* const cbfs_drive(detail::GetDrive<Storage>(sender));
  if (file_info == nullptr) {
    LOG(kInfo) << "CbFsFlushFile - All files";
    try {
      cbfs_drive->FlushAll();
      return;
    }
    catch (const maidsafe_error& e) {
      LOG(kWarning) << "CbFsFlushFile for all files: " << e.what();
      if (e.code() == make_error_code(DriveErrors::no_such_file)) {
        throw ECBFSError(ERROR_ERRORS_ENCOUNTERED);
      }
      throw ECBFSError(ERROR_FUNCTION_FAILED);
    }
  }

  assert(file_info != nullptr);
  const auto relative_path(detail::GetRelativePath<Storage>(cbfs_drive, file_info));
  LOG(kInfo) << "CbFsFlushFile - " << relative_path;
  try {
    cbfs_drive->GetMutableContext(relative_path)->ScheduleForStoring();
  }
  catch (const maidsafe_error& error) {
    LOG(kWarning) << "CbFsFlushFile " << relative_path << ": " << error.what();
    if (error.code() == make_error_code(DriveErrors::no_such_file)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    throw ECBFSError(ERROR_FUNCTION_FAILED);
  }
}

// Quote from CBFS documentation:
//
// This event is fired when the storage is removed by the user using Eject command in Explorer. When
// the event is fired, the storage has been completely destroyed. You don't need to call
// UnmountMedia() or DeleteStorage() methods.
template <typename Storage>
void CbfsDrive<Storage>::CbFsStorageEjected(CallbackFileSystem* sender) {
  LOG(kInfo) << "CbFsStorageEjected";
  auto cbfs_drive(detail::GetDrive<Storage>(sender));
  boost::async(boost::launch::async, [cbfs_drive] { cbfs_drive->Unmount(); });
}

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_WIN_DRIVE_H_
