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
#include <algorithm>
#include <cstdio>
#include <limits>

#include "boost/scoped_array.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/preprocessor/stringize.hpp"

#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/directory_listing_handler.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/return_codes.h"
#include "maidsafe/drive/utils.h"


namespace fs = boost::filesystem;
namespace bptime = boost::posix_time;
namespace args = std::placeholders;

namespace maidsafe {

namespace drive {

fs::path RelativePath(const fs::path &mount_dir, const fs::path &absolute_path) {
  if (absolute_path.root_name() != mount_dir.root_name() && absolute_path.root_name() != mount_dir)
    return fs::path();
  return absolute_path.root_directory() / absolute_path.relative_path();
}

namespace {

CbfsDriveInUserSpace *g_cbfs_drive;

#ifndef CBFS_KEY
#  error CBFS_KEY must be defined.
#endif
LPCSTR registration_key(BOOST_PP_STRINGIZE(CBFS_KEY));

struct DirectoryEnumerationContext {
  explicit DirectoryEnumerationContext(const std::pair<DirectoryData, uint32_t>& directory_in)
      : exact_match(false),
        directory(directory_in) {}
  DirectoryEnumerationContext() : exact_match(false), directory() {}
  bool exact_match;
  std::pair<DirectoryData, uint32_t> directory;
};


fs::path GetRelativePath(CbFsFileInfo *file_info) {
  BOOST_ASSERT(file_info);
  boost::scoped_array<WCHAR> file_name(new WCHAR[g_cbfs_drive->max_file_path_length()]);
  file_info->get_FileName(file_name.get());
  return fs::path(file_name.get());
}

std::string WstringToString(const std::wstring &input) {
  std::locale locale("");
  std::string string_buffer(input.size() * 4 + 1, 0);
  std::use_facet< std::ctype<wchar_t> >(locale).narrow(&input[0],
                                                       &input[0] + input.size(),
                                                       '?',
                                                       &string_buffer[0]);
  return std::string(&string_buffer[0]);
}

void ErrorMessage(const std::string &method_name, ECBFSError error) {
  LOG(kError) << "Cbfs::" << method_name << ": " << WstringToString(error.Message());
}

}  // unnamed namespace

CbfsDriveInUserSpace::CbfsDriveInUserSpace(ClientNfs& client_nfs,
                                           DataStore& data_store,
                                           const Maid& maid,
                                           const Identity& unique_user_id,
                                           const std::string& root_parent_id,
                                           const fs::path &mount_dir,
                                           const fs::path &drive_name,
                                           const int64_t &max_space,
                                           const int64_t &used_space)
    : DriveInUserSpace(client_nfs,
                       data_store,
                       maid,
                       unique_user_id,
                       root_parent_id,
                       mount_dir,
                       max_space,
                       used_space),
      callback_filesystem_(),
      guid_("713CC6CE-B3E2-4fd9-838D-E28F558F6866"),
      icon_id_(L"SigmoidCoreDriveIcon"),
      drive_name_(drive_name.wstring()) {
  g_cbfs_drive = this;
  int result = Init();
  if (result != kSuccess) {
    LOG(kError) << "Failed to initialise drive.  Result: " << result;
    ThrowError(LifeStuffErrors::kCreateStorageError);
  }
  result = Mount();
  if (result != kSuccess) {
    LOG(kError) << "Failed to mount drive.  Result: " << result;
    ThrowError(LifeStuffErrors::kMountError);
  }
}

CbfsDriveInUserSpace::~CbfsDriveInUserSpace() {
  Unmount(max_space_, used_space_);
}

int CbfsDriveInUserSpace::Init() {
  if (drive_stage_ != kCleaned) {
    OnCallbackFsInit();
    UpdateDriverStatus();
  }

  try {
    callback_filesystem_.Initialize(guid_);
    callback_filesystem_.CreateStorage();
    LOG(kInfo) << "Created Storage.";
  } catch(ECBFSError error) {
    ErrorMessage("Init CreateStorage ", error);
    return kCreateStorageError;
  } catch(const std::exception &e) {
    LOG(kError) << "Cbfs::Init: " << e.what();
    return kCreateStorageError;
  }
  // SetIcon can only be called after CreateStorage has successfully completed.
  try {
    callback_filesystem_.SetIcon(icon_id_);
  }
  catch(ECBFSError error) {
    ErrorMessage("Init", error);
  }
  drive_stage_ = kInitialised;
  return kSuccess;
}

int CbfsDriveInUserSpace::Mount() {
  try {
#ifndef NDEBUG
    int timeout_milliseconds(0);
#else
    int timeout_milliseconds(30000);
#endif
    callback_filesystem_.MountMedia(timeout_milliseconds);
    // The following can only be called when the media is mounted.
//    callback_filesystem_.SetFileSystemName
//    callback_filesystem_.DisableMetaDataCache
    LOG(kInfo) << "Started mount point.";
    callback_filesystem_.AddMountingPoint(mount_dir_.c_str());
//    callback_filesystem_.AddMountingPoint(mount_dir.wstring().c_str(),
//                                          CBFS_SYMLINK_MOUNT_MANAGER, nullptr);
    UpdateMountingPoints();
    LOG(kInfo) << "Added mount point.";
  }
  catch(ECBFSError error) {
    std::wstring errorMsg(error.Message());
    ErrorMessage("Mount", error);
    return kMountError;
  }
  drive_stage_ = kMounted;
  SetMountState(true);
  return kSuccess;
}

void CbfsDriveInUserSpace::UnmountDrive(const bptime::time_duration &timeout_before_force) {
  // directory_listing_handler_->CleanUp();
  bptime::time_duration running_time(bptime::milliseconds(0));
  bptime::milliseconds sleep_interval(200);
  while (drive_stage_ == kMounted) {
    bool force(running_time > timeout_before_force);
    try {
      for (int index = callback_filesystem_.GetMountingPointCount() - 1;
           index >= 0; --index) {
        callback_filesystem_.DeleteMountingPoint(index);
//        callback_filesystem_.DeleteMountingPoint(index, CBFS_SYMLINK_MOUNT_MANAGER, nullptr);
      }
//      UpdateMountingPoints();
      callback_filesystem_.UnmountMedia(force);
    }
    catch(ECBFSError error) {
      // ErrorMessage("UnmountDrive", error);
    }
    boost::this_thread::sleep(sleep_interval);
    running_time += sleep_interval;
  }
}

int CbfsDriveInUserSpace::Unmount(int64_t &max_space, int64_t &used_space) {
  if (drive_stage_ != kCleaned) {
    if (callback_filesystem_.Active())
      UnmountDrive(bptime::seconds(3));
    if (callback_filesystem_.StoragePresent()) {
      try {
        callback_filesystem_.DeleteStorage(TRUE);
      }
      catch(ECBFSError error) {
        ErrorMessage("Unmount", error);
        return kUnmountError;
      }
    }
    drive_stage_ = kCleaned;
  }
  max_space = max_space_;
  used_space = used_space_;
  return kSuccess;
}

void CbfsDriveInUserSpace::NotifyRename(const fs::path& from_relative_path,
                                        const fs::path& to_relative_path) const {
  NotifyDirectoryChange(from_relative_path, kRemoved);
  NotifyDirectoryChange(to_relative_path, kRemoved);
}

void CbfsDriveInUserSpace::NotifyDirectoryChange(const fs::path &relative_path, OpType op) const {
  BOOL success(FALSE);
  switch (op) {
    case kRemoved: {
      success = callback_filesystem_.NotifyDirectoryChange(relative_path.wstring().c_str(),
                                                           callback_filesystem_.fanRemoved,
                                                           TRUE);
      break;
    }
    case kAdded: {
      success = callback_filesystem_.NotifyDirectoryChange(relative_path.wstring().c_str(),
                                                           callback_filesystem_.fanAdded,
                                                           TRUE);
      break;
    }
    case kModified: {
      success = callback_filesystem_.NotifyDirectoryChange(relative_path.wstring().c_str(),
                                                           callback_filesystem_.fanModified,
                                                           TRUE);
      break;
    }
  }
  if (success != TRUE)
    LOG(kError) << "Failed to notify directory change";
}

uint32_t CbfsDriveInUserSpace::max_file_path_length() {
  return static_cast<uint32_t>(callback_filesystem_.GetMaxFilePathLength());
}

std::wstring CbfsDriveInUserSpace::drive_name() const {
  return drive_name_;
}

void CbfsDriveInUserSpace::UpdateDriverStatus() {
  BOOL installed = false;
  INT version_high = 0, version_low = 0;
  SERVICE_STATUS status;
  CallbackFileSystem::GetModuleStatus(guid_,
                                      CBFS_MODULE_DRIVER,
                                      &installed,
                                      &version_high,
                                      &version_low,
                                      &status);
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
        string_status =L"in undefined state";
    }
    LOG(kInfo) << "Driver (version " << (version_high >> 16) << "."
               << (version_high & 0xFFFF) << "." << (version_low >> 16) << "."
               << (version_low & 0xFFFF) << ") installed, service "
               << string_status;
  }
}

void CbfsDriveInUserSpace::UpdateMountingPoints() {
//  LVITEM item;
  DWORD flags;
  LUID authentication_id;
  LPTSTR mounting_point = nullptr;
//  ListView_DeleteAllItems(g_hList);
  for (int index = callback_filesystem_.GetMountingPointCount() - 1;
       index >= 0; --index) {
    if (callback_filesystem_.GetMountingPoint(index,
                                              &mounting_point,
                                              &flags,
                                              &authentication_id)) {
//      item.mask = LVIF_TEXT;
//      item.iItem = ListView_GetItemCount(g_hList);
//      item.iSubItem = 0;
//      item.stateMask = (UINT)-1;
//      item.pszText = mounting_point;
//      item.cchTextMax = _MAX_PATH;
//      ListView_InsertItem(g_hList,  &item);
    }
    if (mounting_point) {
      free(mounting_point);
      mounting_point = nullptr;
    }
  }
//  if (ListView_GetItemCount(g_hList) > 0) {
//    ListView_SetItemState(g_hList, 0, LVIS_SELECTED | LVIS_FOCUSED, 0x000F);
//  }
}

void CbfsDriveInUserSpace::OnCallbackFsInit() {
  try {
    callback_filesystem_.SetRegistrationKey(registration_key);
    callback_filesystem_.SetOnStorageEjected(CbFsOnEjectStorage);
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
    callback_filesystem_.SetSerializeCallbacks(true);
    callback_filesystem_.SetFileCacheEnabled(false);
    // Options: scFloppyDiskette, scReadOnlyDevice, scWriteOnceMedia,
    //          scRemovableMedia, scShowInEjectionTray, scAllowEjection
    // callback_filesystem_.SetStorageCharacteristics(
    //    CallbackFileSystem::CbFsStorageCharacteristics(
    //        CallbackFileSystem::scRemovableMedia |
    //        CallbackFileSystem::scShowInEjectionTray |
    //        CallbackFileSystem::scAllowEjection));
    //// Options: stDisk, stCDROM, stVirtualDisk, and stDiskPnP
    // callback_filesystem_.SetStorageType(CallbackFileSystem::stDiskPnP);
    callback_filesystem_.SetStorageType(CallbackFileSystem::stDisk);
//    callback_filesystem_.SetOnEnumerateNamedStreams(
//        CbFsEnumerateNamedStreams);
//    callback_filesystem_.SetThreadPoolSize(100);
//    callback_filesystem_.SetCallAllOpenCloseCallbacks
//    callback_filesystem_.SetMaxFileNameLength
//    callback_filesystem_.SetMaxFilePathLength
//    callback_filesystem_.SetSectorSize
//    callback_filesystem_.SetShortFileNameSupport
//    callback_filesystem_.SetTag
//    callback_filesystem_.SetOnSetFileSecurity(CbFsSetFileSecurity);
//    callback_filesystem_.SetOnGetFileSecurity(CbFsGetFileSecurity);
      // TODO(Fraser#5#): 2011-03-31 - Implement CbFsGetFileNameByFileId
//    callback_filesystem_.SetOnGetFileNameByFileId(CbFsGetFileNameByFileId);
//    callback_filesystem_.SetVCB
  }
  catch(ECBFSError error) {
    ErrorMessage("OnCallbackFsInit", error);
  }
  return;
}

int CbfsDriveInUserSpace::Install() {
  return OnCallbackFsInstall();
}

int CbfsDriveInUserSpace::OnCallbackFsInstall() {
  TCHAR file_name[MAX_PATH];
  DWORD reboot = 0;

  if (!GetModuleFileName(nullptr, file_name, MAX_PATH)) {
    DWORD error = GetLastError();
    ErrorMessage("OnCallbackFsInstall::GetModuleFileName", error);
    return error;
  }
  try {
    fs::path drive_path(fs::path(file_name).parent_path().parent_path());
    fs::path cab_path(drive_path / "drivers\\cbfs\\cbfs.cab");
    LOG(kInfo) << "CbfsDriveInUserSpace::OnCallbackFsInstall cabinet file: " << cab_path.string();

    callback_filesystem_.Install(cab_path.wstring().c_str(),
                                 guid_,
                                 fs::path().wstring().c_str(),
                                 false,
                                 CBFS_MODULE_DRIVER |
                                    CBFS_MODULE_NET_REDIRECTOR_DLL |
                                    CBFS_MODULE_MOUNT_NOTIFIER_DLL,
                                 &reboot);
    return reboot;
  }
  catch(ECBFSError error) {
    ErrorMessage("OnCallbackFsInstall", error);
    return -1111;
  }
}

// =============================== CALLBACKS =================================


void CbfsDriveInUserSpace::CbFsMount(CallbackFileSystem *sender) {
  LOG(kInfo) << "CbFsMount";
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsUnmount(CallbackFileSystem *sender) {
  LOG(kInfo) << "CbFsUnmount";
  g_cbfs_drive->SetMountState(false);
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsGetVolumeSize(CallbackFileSystem *sender,
                                             int64_t *total_number_of_sectors,
                                             int64_t *number_of_free_sectors) {
  LOG(kInfo) << "CbFsGetVolumeSize";

  WORD sector_size(sender->GetSectorSize());
  *total_number_of_sectors = g_cbfs_drive->max_space_ / sector_size;
  *number_of_free_sectors = (g_cbfs_drive->max_space_ - g_cbfs_drive->used_space_) / sector_size;
}


void CbfsDriveInUserSpace::CbFsGetVolumeLabel(CallbackFileSystem *sender,
                                              LPTSTR volume_label) {
  LOG(kInfo) << "CbFsGetVolumeLabel";
  wcsncpy_s(volume_label, g_cbfs_drive->drive_name().size() + 1,
            &g_cbfs_drive->drive_name().at(0),
            g_cbfs_drive->drive_name().size() + 1);
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsSetVolumeLabel(CallbackFileSystem *sender,
                                              LPCTSTR volume_label) {
  LOG(kInfo) << "CbFsSetVolumeLabel";
  UNREFERENCED_PARAMETER(sender);
  UNREFERENCED_PARAMETER(volume_label);
}

void CbfsDriveInUserSpace::CbFsGetVolumeId(CallbackFileSystem *sender,
                                           PDWORD volume_id) {
  LOG(kInfo) << "CbFsGetVolumeId";
  *volume_id = 0x68451321;
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsCreateFile(CallbackFileSystem *sender,
                                          LPCTSTR file_name,
                                          ACCESS_MASK desired_access,
                                          DWORD file_attributes,
                                          DWORD share_mode,
                                          CbFsFileInfo* file_info,
                                          CbFsHandleInfo* handle_info) {
  fs::path relative_path(file_name);
  LOG(kInfo) << "CbFsCreateFile - " << relative_path << " 0x" << std::hex << file_attributes;
  file_info->set_UserContext(nullptr);
  bool is_directory((file_attributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
  FileContext* file_context(new FileContext(relative_path.filename(), is_directory));
  file_context->meta_data->attributes = file_attributes;

  try {
    g_cbfs_drive->AddFile(relative_path,
                          *file_context->meta_data.get(),
                          &file_context->grandparent_directory_id,
                          &file_context->parent_directory_id);
  }
  catch(...) {
    throw ECBFSError(ERROR_ACCESS_DENIED);
  }

  if (is_directory) {
    g_cbfs_drive->used_space_ += kDirectorySize;
  } else {
    encrypt::DataMapPtr data_map(new encrypt::DataMap());
    *data_map = *file_context->meta_data->data_map;
    file_context->meta_data->data_map = data_map;
    file_context->self_encryptor.reset(
        new encrypt::SelfEncryptor(file_context->meta_data->data_map,
                                   g_cbfs_drive->client_nfs_,
                                   g_cbfs_drive->data_store_));
  }

  g_cbfs_drive->drive_changed_signal_(g_cbfs_drive->mount_dir_ / relative_path,
                                      fs::path(),
                                      kCreated);
  file_info->set_UserContext(file_context);
  BOOST_ASSERT(file_info->get_UserContext());

  UNREFERENCED_PARAMETER(sender);
  UNREFERENCED_PARAMETER(desired_access);
  UNREFERENCED_PARAMETER(share_mode);
  UNREFERENCED_PARAMETER(handle_info);
}

void CbfsDriveInUserSpace::CbFsOpenFile(CallbackFileSystem* sender,
                                        LPCWSTR file_name,
                                        ACCESS_MASK desired_access,
                                        DWORD file_attributes,
                                        DWORD share_mode,
                                        CbFsFileInfo* file_info,
                                        CbFsHandleInfo* handle_info) {
  LOG(kInfo) << "CbFsOpenFile - " << fs::path(file_name);
  if (file_info->get_UserContext() != nullptr)
    return;

  fs::path relative_path(file_name);
  FileContext *file_context(new FileContext);
  file_context->meta_data->name = relative_path.filename();
  try {
    g_cbfs_drive->GetMetaData(relative_path,
                              *file_context->meta_data.get(),
                              &file_context->grandparent_directory_id,
                              &file_context->parent_directory_id);
  }
  catch(...) {
    throw ECBFSError(ERROR_FILE_NOT_FOUND);
  }

  if (!file_context->meta_data->directory_id) {
    encrypt::DataMapPtr data_map(new encrypt::DataMap());
    *data_map = *file_context->meta_data->data_map;
    file_context->meta_data->data_map = data_map;
    if (!file_context->self_encryptor) {
      encrypt::DataMapPtr data_map(new encrypt::DataMap());
      *data_map = *file_context->meta_data->data_map;
      file_context->meta_data->data_map = data_map;
      file_context->self_encryptor.reset(
          new encrypt::SelfEncryptor(file_context->meta_data->data_map,
                                     g_cbfs_drive->client_nfs_,
                                     g_cbfs_drive->data_store_));
    }
  }
  file_info->set_UserContext(file_context);

  UNREFERENCED_PARAMETER(sender);
  UNREFERENCED_PARAMETER(desired_access);
  UNREFERENCED_PARAMETER(share_mode);
  UNREFERENCED_PARAMETER(file_attributes);
  UNREFERENCED_PARAMETER(handle_info);
}

void CbfsDriveInUserSpace::CbFsCloseFile(CallbackFileSystem* sender,
                                         CbFsFileInfo* file_info,
                                         CbFsHandleInfo* handle_info) {
  fs::path relative_path(GetRelativePath(file_info));
  LOG(kInfo) << "CbFsCloseFile - " << relative_path;
  if (file_info->get_UserContext() != nullptr) {
    FileContext *file_context(static_cast<FileContext*>(file_info->get_UserContext()));
    if (!(file_context->meta_data->attributes & FILE_ATTRIBUTE_DIRECTORY)) {
      if (file_context->meta_data->end_of_file < file_context->meta_data->allocation_size) {
          file_context->meta_data->end_of_file = file_context->meta_data->allocation_size;
      } else if (file_context->meta_data->allocation_size <
                      file_context->meta_data->end_of_file) {
          file_context->meta_data->allocation_size = file_context->meta_data->end_of_file;
      }
      if (file_context->self_encryptor) {
        if (file_context->self_encryptor->Flush()) {
          if (file_context->content_changed) {
            try {
              g_cbfs_drive->UpdateParent(file_context, relative_path.parent_path());
            }
            catch(...) {
              throw ECBFSError(ERROR_ERRORS_ENCOUNTERED);
            }
          }
        } else {
          LOG(kError) << "CbFsCloseFile: failed to flush " << relative_path;
        }
      }
    }
    delete file_context;
    file_context = nullptr;
    file_info->set_UserContext(nullptr);
  }
  UNREFERENCED_PARAMETER(sender);
  UNREFERENCED_PARAMETER(handle_info);
}

void CbfsDriveInUserSpace::CbFsGetFileInfo(
    CallbackFileSystem *sender,
    LPCTSTR file_name,
    LPBOOL file_exists,
    PFILETIME creation_time,
    PFILETIME last_access_time,
    PFILETIME last_write_time,
    int64_t *end_of_file,
    int64_t *allocation_size,
    int64_t *file_id,
    PDWORD file_attributes,
    LPWSTR short_file_name OPTIONAL,
    PWORD short_file_name_length OPTIONAL,
    LPWSTR real_file_name OPTIONAL,
    LPWORD real_file_name_length OPTIONAL) {
  fs::path relative_path(file_name);
  LOG(kInfo) << "CbFsGetFileInfo - " << relative_path;
  *file_exists = false;
  *file_attributes = 0xFFFFFFFF;

  if (relative_path.extension() == kMsHidden)
    throw ECBFSError(ERROR_INVALID_NAME);
  std::unique_ptr<FileContext> file_context(new FileContext);
  try {
    g_cbfs_drive->GetMetaData(relative_path,
                              *file_context->meta_data.get(),
                              &file_context->grandparent_directory_id,
                              &file_context->parent_directory_id);
  }
  catch(...) {
    throw ECBFSError(ERROR_FILE_NOT_FOUND);
  }
  *file_exists = true;
  *creation_time = file_context->meta_data->creation_time;
  *last_access_time = file_context->meta_data->last_access_time;
  *last_write_time = file_context->meta_data->last_write_time;
  if (file_context->meta_data->end_of_file < file_context->meta_data->allocation_size) {
    file_context->meta_data->end_of_file = file_context->meta_data->allocation_size;
  } else if (file_context->meta_data->allocation_size < file_context->meta_data->end_of_file) {
    file_context->meta_data->allocation_size = file_context->meta_data->end_of_file;
  }
  *end_of_file = file_context->meta_data->end_of_file;
  *allocation_size = file_context->meta_data->allocation_size;
  *file_id = 0;
  *file_attributes = file_context->meta_data->attributes;

  UNREFERENCED_PARAMETER(sender);
  UNREFERENCED_PARAMETER(short_file_name);
  UNREFERENCED_PARAMETER(short_file_name_length);
  UNREFERENCED_PARAMETER(real_file_name);
  UNREFERENCED_PARAMETER(real_file_name_length);
}

void CbfsDriveInUserSpace::CbFsEnumerateDirectory(
    CallbackFileSystem* sender,
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
    PDWORD file_attributes) {
  fs::path relative_path(GetRelativePath(directory_info));
  DirectoryEnumerationContext* enum_context = nullptr;
  std::wstring mask_str(mask);
  LOG(kInfo) << "CbFsEnumerateDirectory - " << relative_path
             << " index: " << index << std::boolalpha
             << " nullptr context: "
             << (directory_enumeration_info == nullptr)
             << " mask: " << WstringToString(mask_str)
             << " restart: " << restart;
  bool exact_match(mask_str != L"*");
  *file_found = false;

  if (restart && (directory_enumeration_info->get_UserContext() != nullptr)) {
    enum_context =
      static_cast<DirectoryEnumerationContext*>(directory_enumeration_info->get_UserContext());
    delete enum_context;
    enum_context = nullptr;
    directory_enumeration_info->set_UserContext(nullptr);
  }

  std::pair<DirectoryData, uint32_t> directory;
  if (directory_enumeration_info->get_UserContext() == nullptr) {
    try {
      directory = g_cbfs_drive->directory_listing_handler_->GetFromPath(relative_path);
    }
    catch(...) {
      throw ECBFSError(ERROR_PATH_NOT_FOUND);
    }
    enum_context = new DirectoryEnumerationContext(directory);
    BOOST_ASSERT(enum_context);
    enum_context->directory.first.listing->ResetChildrenIterator();
    directory_enumeration_info->set_UserContext(enum_context);
  } else {
    enum_context =
      static_cast<DirectoryEnumerationContext*>(directory_enumeration_info->get_UserContext());
    if (restart)
      enum_context->directory.first.listing->ResetChildrenIterator();
  }

  MetaData meta_data;
  if (exact_match) {
    while (!(*file_found)) {
      if (!enum_context->directory.first.listing->GetChildAndIncrementItr(meta_data))
        break;
      *file_found = MatchesMask(mask_str, meta_data.name);
    }
  } else {
    *file_found = enum_context->directory.first.listing->GetChildAndIncrementItr(meta_data);
  }

  if (*file_found) {
#pragma warning(push)
#pragma warning(disable: 4996)
    // Need to use wcscpy rather than the secure wcsncpy_s as file_name has a
    // size of 0 in some cases.  CBFS docs specify that callers must assign
    // MAX_PATH chars to file_name, so we assume this is done.
    wcscpy(file_name, meta_data.name.wstring().c_str());
#pragma warning(pop)
    *file_name_length = static_cast<DWORD>(meta_data.name.wstring().size());
    *creation_time = meta_data.creation_time;
    *last_access_time = meta_data.last_access_time;
    *last_write_time = meta_data.last_write_time;
    *end_of_file = meta_data.end_of_file;
    *allocation_size = meta_data.allocation_size;
    *file_id = 0;
    *file_attributes = meta_data.attributes;
  }
  enum_context->exact_match = exact_match;

  UNREFERENCED_PARAMETER(sender);
  UNREFERENCED_PARAMETER(index);
  UNREFERENCED_PARAMETER(short_file_name);
  UNREFERENCED_PARAMETER(short_file_name_length);
  UNREFERENCED_PARAMETER(handle_info);
}

void CbfsDriveInUserSpace::CbFsCloseDirectoryEnumeration(
    CallbackFileSystem* sender,
    CbFsFileInfo* directory_info,
    CbFsDirectoryEnumerationInfo* directory_enumeration_info) {
  fs::path relative_path(GetRelativePath(directory_info));
  LOG(kInfo) << "CbFsCloseEnumeration - " << relative_path;
  if (directory_enumeration_info != nullptr) {
    DirectoryEnumerationContext* enum_context =
      static_cast<DirectoryEnumerationContext*>(directory_enumeration_info->get_UserContext());
    delete enum_context;
    enum_context = nullptr;
    directory_enumeration_info->set_UserContext(nullptr);
  }
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsSetAllocationSize(CallbackFileSystem *sender,
                                                 CbFsFileInfo *file_info,
                                                 int64_t allocation_size) {
  fs::path relative_path(GetRelativePath(file_info));
  LOG(kInfo) << "CbFsSetAllocationSize - " << relative_path << " to " << allocation_size
             << " bytes.";
  if (file_info->get_UserContext() != nullptr) {
    FileContext *file_context(static_cast<FileContext*>(file_info->get_UserContext()));
    if (file_context->meta_data->allocation_size != file_context->meta_data->end_of_file) {
      if (file_context->meta_data->allocation_size < static_cast<uint64_t>(allocation_size)) {
        int64_t additional_size(allocation_size - file_context->meta_data->allocation_size);
        if (additional_size + g_cbfs_drive->used_space_ > g_cbfs_drive->max_space_) {
          LOG(kError) << "CbFsSetAllocationSize: " << relative_path << ", not enough memory.";
          throw ECBFSError(ERROR_DISK_FULL);
        } else {
          g_cbfs_drive->used_space_ += additional_size;
        }
      } else if (file_context->meta_data->allocation_size >
                 static_cast<uint64_t>(allocation_size)) {
        int64_t reduced_size(file_context->meta_data->allocation_size - allocation_size);
        if (g_cbfs_drive->used_space_ < reduced_size) {
          g_cbfs_drive->used_space_ = 0;
        } else {
          g_cbfs_drive->used_space_ -= reduced_size;
        }
      }
      if (g_cbfs_drive->TruncateFile(file_context, allocation_size)) {
        file_context->meta_data->allocation_size = allocation_size;
        if (!file_context->self_encryptor->Flush()) {
          LOG(kError) << "CbFsSetAllocationSize: " << relative_path << ", failed to flush";
        }
      } else {
        LOG(kError) << "Truncate failed for " << file_context->meta_data->name.c_str();
        if (file_context->meta_data->allocation_size < static_cast<uint64_t>(allocation_size)) {
          int64_t additional_size(allocation_size - file_context->meta_data->allocation_size);
            g_cbfs_drive->used_space_ -= additional_size;
        } else if (file_context->meta_data->allocation_size >
                   static_cast<uint64_t>(allocation_size)) {
          int64_t reduced_size(file_context->meta_data->allocation_size - allocation_size);
          g_cbfs_drive->used_space_ += reduced_size;
        }
        return;
      }
      file_context->content_changed = true;
    }
  }
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsSetEndOfFile(CallbackFileSystem *sender,
                                            CbFsFileInfo *file_info,
                                            int64_t end_of_file) {
  fs::path relative_path(GetRelativePath(file_info));
  LOG(kInfo) << "CbFsSetEndOfFile - " << relative_path << " to " << end_of_file << " bytes.";
  if (file_info->get_UserContext() != nullptr) {
    FileContext *file_context(static_cast<FileContext*>(file_info->get_UserContext()));
    if (g_cbfs_drive->TruncateFile(file_context, end_of_file)) {
      file_context->meta_data->end_of_file = end_of_file;
      if (!file_context->self_encryptor->Flush()) {
        LOG(kError) << "CbFsSetEndOfFile: " << relative_path << ", failed to flush";
      }
    } else {
      LOG(kError) << "Truncate failed for " << file_context->meta_data->name;
    }
    if (file_context->meta_data->allocation_size != static_cast<uint64_t>(end_of_file)) {
      if (file_context->meta_data->allocation_size < static_cast<uint64_t>(end_of_file)) {
        int64_t additional_size(end_of_file - file_context->meta_data->allocation_size);
        if (additional_size + g_cbfs_drive->used_space_ > g_cbfs_drive->max_space_) {
          LOG(kError) << "CbFsSetEndOfFile: " << relative_path << ", not enough memory.";
          throw ECBFSError(ERROR_DISK_FULL);
        } else {
          g_cbfs_drive->used_space_ += additional_size;
        }
      } else if (file_context->meta_data->allocation_size > static_cast<uint64_t>(end_of_file)) {
        int64_t reduced_size(file_context->meta_data->allocation_size - end_of_file);
        if (g_cbfs_drive->used_space_ < reduced_size) {
          g_cbfs_drive->used_space_ = 0;
        } else {
          g_cbfs_drive->used_space_ -= reduced_size;
        }
      }
      file_context->meta_data->allocation_size = end_of_file;
      file_context->content_changed = true;
    }
  }
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsSetFileAttributes(CallbackFileSystem *sender,
                                                 CbFsFileInfo* file_info,
                                                 CbFsHandleInfo* handle_info,
                                                 PFILETIME creation_time,
                                                 PFILETIME last_access_time,
                                                 PFILETIME last_write_time,
                                                 DWORD file_attributes) {
  fs::path relative_path(GetRelativePath(file_info));
  LOG(kInfo) << "CbFsSetFileAttributes- " << relative_path << " 0x" << std::hex << file_attributes;
  if (file_info->get_UserContext() != nullptr) {
    FileContext *file_context(static_cast<FileContext*>(file_info->get_UserContext()));
    if (file_attributes != 0)
      file_context->meta_data->attributes = file_attributes;
    if (creation_time != nullptr)
      file_context->meta_data->creation_time = *creation_time;
    if (last_access_time != nullptr)
      file_context->meta_data->last_access_time = *last_access_time;
    if (last_write_time != nullptr)
      file_context->meta_data->last_write_time = *last_write_time;

    file_context->content_changed = true;
  }
  UNREFERENCED_PARAMETER(sender);
  UNREFERENCED_PARAMETER(handle_info);
}

void CbfsDriveInUserSpace::CbFsCanFileBeDeleted(CallbackFileSystem *sender,
                                                CbFsFileInfo* file_info,
                                                CbFsHandleInfo* handle_info,
                                                LPBOOL can_be_deleted) {
  fs::path relative_path(GetRelativePath(file_info));
  LOG(kInfo) << "CbFsCanFileBeDeleted - " << relative_path;

  if (g_cbfs_drive->CanRemove(relative_path)) {
    *can_be_deleted = true;
  } else {
    *can_be_deleted = false;
  }
  UNREFERENCED_PARAMETER(sender);
  UNREFERENCED_PARAMETER(handle_info);
}

void CbfsDriveInUserSpace::CbFsDeleteFile(CallbackFileSystem *sender, CbFsFileInfo *file_info) {
  fs::path relative_path(GetRelativePath(file_info));
  LOG(kInfo) << "CbFsDeleteFile - " << relative_path;
  std::shared_ptr<FileContext> file_context(new FileContext);
  try {
    g_cbfs_drive->GetMetaData(relative_path, *file_context->meta_data.get(), nullptr, nullptr);
    g_cbfs_drive->RemoveFile(relative_path);
  }
  catch(...) {
    throw ECBFSError(ERROR_FILE_NOT_FOUND);
  }

  if (!file_context->meta_data->directory_id) {
    g_cbfs_drive->used_space_ -= file_context->meta_data->allocation_size;
  } else {
    g_cbfs_drive->used_space_ -= kDirectorySize;
  }

  g_cbfs_drive->drive_changed_signal_(g_cbfs_drive->mount_dir_ / relative_path,
                                      fs::path(),
                                      kRemoved);
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsRenameOrMoveFile(CallbackFileSystem *sender,
                                                CbFsFileInfo *file_info,
                                                LPCTSTR new_file_name) {
  fs::path relative_path(GetRelativePath(file_info));
  LOG(kInfo) << "CbFsRenameOrMoveFile - " << relative_path << " to " << fs::path(new_file_name);
  fs::path new_relative_path(new_file_name);
  std::shared_ptr<FileContext> file_context(new FileContext);
  try {
    g_cbfs_drive->GetMetaData(relative_path, *file_context->meta_data.get(), nullptr, nullptr);
  }
  catch(...) {
    throw ECBFSError(ERROR_FILE_NOT_FOUND);
  }
  int64_t reclaimed_space(0);
  try {
    g_cbfs_drive->RenameFile(relative_path,
                             new_file_name,
                             *file_context->meta_data.get(),
                             reclaimed_space);
  }
  catch(...) {
    throw ECBFSError(ERROR_ACCESS_DENIED);
  }
  g_cbfs_drive->used_space_ -= reclaimed_space;

  UNREFERENCED_PARAMETER(file_info);
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsReadFile(CallbackFileSystem *sender,
                                        CbFsFileInfo *file_info,
                                        int64_t position,
                                        PVOID buffer,
                                        DWORD bytes_to_read,
                                        PDWORD bytes_read) {
  fs::path relative_path(GetRelativePath(file_info));
  if (file_info->get_UserContext() != nullptr) {
    FileContext *file_context(static_cast<FileContext*>(file_info->get_UserContext()));
    LOG(kInfo) << "CbFsReadFile- " << relative_path << " reading " << bytes_to_read << " of "
               << file_context->meta_data->end_of_file << " at position " << position;
    BOOST_ASSERT(file_context->self_encryptor);
    *bytes_read = 0;

    if (!file_context->self_encryptor) {
      throw ECBFSError(ERROR_INVALID_PARAMETER);
    }
    if (!file_context->self_encryptor->Read(static_cast<char*>(buffer), bytes_to_read, position)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }
    if (static_cast<uint64_t>(position + bytes_to_read) > file_context->self_encryptor->size())
      *bytes_read = static_cast<DWORD>(file_context->self_encryptor->size() - position);
    else
      *bytes_read = bytes_to_read;
    GetSystemTimeAsFileTime(&file_context->meta_data->last_access_time);
    file_context->content_changed = true;
  }
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsWriteFile(CallbackFileSystem *sender,
                                         CbFsFileInfo *file_info,
                                         int64_t position,
                                         PVOID buffer,
                                         DWORD bytes_to_write,
                                         PDWORD bytes_written) {
  fs::path relative_path(GetRelativePath(file_info));
  if (file_info->get_UserContext() != nullptr) {
    FileContext* file_context(static_cast<FileContext*>(file_info->get_UserContext()));
    LOG(kInfo) << "CbFsWriteFile- " << relative_path << " writing " << bytes_to_write
               << " bytes at position " << position;
    BOOST_ASSERT(file_context->self_encryptor);
    *bytes_written = 0;
    if (!file_context->self_encryptor) {
      throw ECBFSError(ERROR_INVALID_PARAMETER);
    }
    if (!file_context->self_encryptor->Write(static_cast<char*>(buffer),
                                             bytes_to_write,
                                             position)) {
      throw ECBFSError(ERROR_FILE_NOT_FOUND);
    }

    *bytes_written = bytes_to_write;
    GetSystemTimeAsFileTime(&file_context->meta_data->last_write_time);
    file_context->content_changed = true;
  }
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsIsDirectoryEmpty(CallbackFileSystem *sender,
                                                CbFsFileInfo* directory_info,
                                                LPWSTR file_name,
                                                LPBOOL is_empty) {
  LOG(kInfo) << "CbFsIsDirectoryEmpty - " << fs::path(file_name);
  try {
    DirectoryListingHandler::DirectoryType
        directory(g_cbfs_drive->directory_listing_handler_->GetFromPath(file_name));
    *is_empty = directory.first.listing->empty();
  }
  catch(...) {
    throw ECBFSError(ERROR_PATH_NOT_FOUND);
  }
  UNREFERENCED_PARAMETER(sender);
  UNREFERENCED_PARAMETER(directory_info);
}

void CbfsDriveInUserSpace::CbFsFlushFile(CallbackFileSystem* sender,
                                         CbFsFileInfo* file_info) {
  if (file_info) {
    fs::path relative_path(GetRelativePath(file_info));
    FileContext *file_context(static_cast<FileContext*>(file_info->get_UserContext()));
    if (file_context != nullptr) {
      LOG(kInfo) << "CbFsFlushFile - " << relative_path;
      if (file_context->self_encryptor->Flush()) {
        if (file_context->content_changed) {
          try {
            g_cbfs_drive->UpdateParent(file_context, relative_path);
          }
          catch(...) {
            throw ECBFSError(ERROR_ERRORS_ENCOUNTERED);
          }
        }
      } else {
        LOG(kError) << "CbFsFlushFile: " << relative_path << ", failed to flush";
      }
    } else {
      LOG(kInfo) << "CbFsFlushFile: file_context for " << relative_path << " is null.";
    }
  }
  // else {
  //  flush everything related to the disk
  // }
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::CbFsOnEjectStorage(CallbackFileSystem *sender) {
  LOG(kInfo) << "CbFsOnEjectStorage";
  g_cbfs_drive->SetMountState(false);
  UNREFERENCED_PARAMETER(sender);
}

void CbfsDriveInUserSpace::SetNewAttributes(FileContext *file_context,
                                            bool is_directory,
                                            bool read_only) {
  FILETIME file_time;
  GetSystemTimeAsFileTime(&file_time);
  file_context->meta_data->creation_time =
    file_context->meta_data->last_access_time =
    file_context->meta_data->last_write_time = file_time;

  if (is_directory) {
    file_context->meta_data->attributes = FILE_ATTRIBUTE_DIRECTORY;
  } else {
    if (read_only)
      file_context->meta_data->attributes = FILE_ATTRIBUTE_READONLY;
    else
      file_context->meta_data->attributes = FILE_ATTRIBUTE_NORMAL;
    file_context->self_encryptor.reset(new encrypt::SelfEncryptor(
        file_context->meta_data->data_map, client_nfs_, data_store_));
    file_context->meta_data->end_of_file =
      file_context->meta_data->allocation_size =
      file_context->self_encryptor->size();
  }
}

}  // namespace drive

}  // namespace maidsafe
