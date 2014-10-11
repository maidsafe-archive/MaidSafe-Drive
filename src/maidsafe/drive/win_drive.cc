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

#include "maidsafe/drive/win_drive.h"

#include <accctrl.h>
#include <aclapi.h>
#include <windows.h>

#include <array>
#include <tuple>

#include "maidsafe/common/log.h"
#include "maidsafe/common/make_unique.h"
#include "maidsafe/drive/cbfs_key.h"

namespace maidsafe {

namespace drive {

namespace {

MAIDSAFE_CONSTEXPR_OR_CONST GENERIC_MAPPING default_access_mappings = {
  FILE_GENERIC_READ,
  FILE_GENERIC_WRITE,
  FILE_GENERIC_EXECUTE,
  FILE_ALL_ACCESS
};

const auto WinLocalFreeLambda = [](void* memory) {
  if (memory) {
    LocalFree(memory);
  }
};
using WinAcl = std::unique_ptr<ACL, decltype(WinLocalFreeLambda)>;

const auto WinDestroyPrivateObjectSecurityLamda = [](PSECURITY_DESCRIPTOR descriptor) {
  if (descriptor) {
    DestroyPrivateObjectSecurity(&descriptor);
  }
};
using WinPrivateObjectSecurity =
    std::unique_ptr<void, decltype(WinDestroyPrivateObjectSecurityLamda)>;

DWORD GetDisableLastAccessUpdateRegKey() {
  HKEY handle_to_key(nullptr);
  std::wstring name(L"SYSTEM\\CurrentControlSet\\Control\\FileSystem");
  auto open_result(RegOpenKeyExW(HKEY_LOCAL_MACHINE, name.c_str(), NULL, KEY_READ, &handle_to_key));

  // If the key doesn't exist, the meaning is equivalent to the key's value being 0.
  if (open_result == ERROR_FILE_NOT_FOUND)
    return 0;
  // If we can't access the key, assume the value is 1 (the default for Windows 7 onwards).
  if (open_result != ERROR_SUCCESS)
    return 1;

  DWORD value_data(0);
  DWORD buffer_size(sizeof(value_data));
  std::wstring value_name(L"NtfsDisableLastAccessUpdate");
  auto query_result(RegQueryValueExW(handle_to_key, value_name.c_str(), NULL, NULL,
                                     reinterpret_cast<LPBYTE>(&value_data), &buffer_size));
  // If the key doesn't exist, the meaning is equivalent to the key's value being 0.
  if (query_result == ERROR_FILE_NOT_FOUND)
    return 0;
  // If we can't access the key, assume the value is 1 (the default for Windows 7 onwards).
  return query_result == ERROR_SUCCESS ? value_data : 1;
}

// do not use std::string for parameter, it could do system call and invalidate GetLastError
void ThrowWinFunctionError(const char* const function_name) {
  const DWORD win_error_code = GetLastError();
  const std::string error_msg =
      std::string(function_name) + " failed with code " + std::to_string(win_error_code);

  BOOST_THROW_EXCEPTION(
      common_error(
          make_error_code(CommonErrors::unable_to_handle_request), error_msg));
}

std::pair<EXPLICIT_ACCESS, std::unique_ptr<SID>> MakeAce(
    const detail::MetaData::FileType path_type,
    const detail::MetaData::Permissions path_permissions,
    const std::tuple<
        const WELL_KNOWN_SID_TYPE,
        const TRUSTEE_TYPE,
        const detail::MetaData::Permissions,
        const detail::MetaData::Permissions,
        const detail::MetaData::Permissions>& mappings) {

  MAIDSAFE_CONSTEXPR_OR_CONST std::size_t SidType = 0;
  MAIDSAFE_CONSTEXPR_OR_CONST std::size_t TrusteeType = 1;
  MAIDSAFE_CONSTEXPR_OR_CONST std::size_t ReadPermission = 2;
  MAIDSAFE_CONSTEXPR_OR_CONST std::size_t WritePermission = 3;
  MAIDSAFE_CONSTEXPR_OR_CONST std::size_t ExecutePermission = 4;

  std::unique_ptr<SID> sid(maidsafe::make_unique<SID>());
  {
    DWORD sid_size = sizeof(SID);
    if (!CreateWellKnownSid(std::get<SidType>(mappings), NULL, sid.get(), &sid_size)) {
      ThrowWinFunctionError("CreateWellKnownSid");
    }
  }

  ACCESS_MASK access_mask{};
  if (detail::HasPermission(path_permissions, std::get<WritePermission>(mappings))) {
    access_mask |= FILE_GENERIC_WRITE;
    access_mask |= DELETE;
  }

  if (path_type == detail::MetaData::FileType::directory_file) {
    if (detail::HasPermission(path_permissions, std::get<ReadPermission>(mappings)) &&
        detail::HasPermission(path_permissions, std::get<ExecutePermission>(mappings))) {
      access_mask |= FILE_GENERIC_READ;
      access_mask |= FILE_TRAVERSE;
      access_mask |= READ_CONTROL; // allow user to see permissions
    }
  }
  else {
    if (detail::HasPermission(path_permissions, std::get<ReadPermission>(mappings))) {
      access_mask |= FILE_GENERIC_READ;
      access_mask |= READ_CONTROL; // allow user to see permissions
    }

    if (detail::HasPermission(path_permissions, std::get<ExecutePermission>(mappings))) {
      access_mask |= FILE_GENERIC_EXECUTE;
    }
  }

  EXPLICIT_ACCESS ace{};
  ace.grfAccessPermissions = access_mask;
  ace.grfAccessMode = GRANT_ACCESS;
  ace.grfInheritance = NO_INHERITANCE;
  ace.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ace.Trustee.TrusteeType = std::get<TrusteeType>(mappings);
  ace.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid.get());

  return std::make_pair(ace, std::move(sid));
}

DWORD ConvertToRelative(
    const detail::WinProcess& object_creator,
    const bool is_directory,
    SECURITY_DESCRIPTOR& absolute,
    PSECURITY_DESCRIPTOR relative,
    const DWORD relative_size) {

  WinPrivateObjectSecurity private_descriptor{};
  {
    GENERIC_MAPPING mapping = default_access_mappings;

    PSECURITY_DESCRIPTOR temp_private_descriptor{};
    const bool fail =
        (CreatePrivateObjectSecurityEx(
            NULL,
            &absolute,
            &temp_private_descriptor,
            NULL,
            is_directory,
            SEF_DACL_AUTO_INHERIT,
            object_creator.GetAccessToken(),
            &mapping) == 0);
    private_descriptor.reset(temp_private_descriptor);

    if (fail) {
      ThrowWinFunctionError("CreatePrivateObjectSecurity");
    }
  }

  assert(private_descriptor.get() != nullptr);
  assert(IsValidSecurityDescriptor(private_descriptor.get()) != 0);

  const DWORD actual_size = GetSecurityDescriptorLength(private_descriptor.get());
  if (actual_size <= relative_size) {
    std::memcpy(relative, private_descriptor.get(), actual_size);
  }

  return actual_size;
}

}  // unnamed namespace

namespace detail {

bool LastAccessUpdateIsDisabled() {
  static const bool kIsDisabled(GetDisableLastAccessUpdateRegKey() == 1U);
  return kIsDisabled;
}

boost::optional<common::Clock::time_point> GetNewFiletime(
    const common::Clock::time_point filetime, const PFILETIME new_value) {
  if (new_value) {
    const common::Clock::time_point t = ToTimePoint(*new_value);
    if (filetime != t) {
      return t;
    }
  }
  return boost::none;
}

void ErrorMessage(const std::string &method_name, ECBFSError error) {
  LOG(kError) << "Cbfs::" << method_name << ": " << WstringToString(error.Message());
}

FILETIME ToFileTime(const common::Clock::time_point& input) {
  // FILETIME epoch = 1601-01-01T00:00:00Z in 100 nanosecond ticks
  // MaidSafe epoch = 1970-01-01T00:00:00Z in 1 nanosecond ticks
  using namespace std::chrono;
  const ULONGLONG filetimeTicks = 100ULL;
  const ULONGLONG chronoTicks = 1ULL;
  // 369 years
  const ULONGLONG epochDifference = 11644473600ULL * (chronoTicks / filetimeTicks);
  ULONGLONG stamp
      = epochDifference
      + (duration_cast<nanoseconds>(input.time_since_epoch()).count() / filetimeTicks);
  FILETIME result;
  result.dwHighDateTime = stamp >> 32;
  result.dwLowDateTime = stamp & 0xFFFFFFFF;
  return result;
}

common::Clock::time_point ToTimePoint(const FILETIME& input) {
  // See ToFileTime
  using namespace std::chrono;
  const ULONGLONG filetimeTicks = 100ULL;
  const ULONGLONG chronoTicks = 1ULL;
  const ULONGLONG epochDifference = 11644473600ULL * (chronoTicks / filetimeTicks);
  ULONGLONG filetime = ((ULONGLONG)(input.dwHighDateTime) << 32) + input.dwLowDateTime;
  ULONGLONG stamp = (filetime - epochDifference) * (filetimeTicks / chronoTicks);
  return common::Clock::time_point(nanoseconds(stamp));
}

bool HaveAccessInternal(
    const WinHandle& originator,
    DWORD desired_permissions,
    const detail::WinProcess& owner,
    const detail::MetaData::FileType path_type,
    const detail::MetaData::Permissions path_permissions) {

  MAIDSAFE_CONSTEXPR_OR_CONST SECURITY_INFORMATION request_information =
      (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION);

  const DWORD desired_length =
      GetFileSecurityInternal(
          owner, path_type, path_permissions, request_information, NULL, 0);

  const std::unique_ptr<char[]> security(
      maidsafe::make_unique<char[]>(desired_length));

  const DWORD actual_length =
      GetFileSecurityInternal(
          owner, path_type, path_permissions, request_information, security.get(), desired_length);

  assert(actual_length >= desired_length);
  assert(IsValidSecurityDescriptor(security.get()) != 0);

  WinHandle impersonation_token{};
  {
    HANDLE temp_impersonation_token{};
    const bool fail =
        (DuplicateToken(originator.get(), SecurityImpersonation, &temp_impersonation_token) == 0);
    impersonation_token.reset(temp_impersonation_token);

    if (fail) {
      ThrowWinFunctionError("DuplicateToken");
    }
  }

  GENERIC_MAPPING mapping = default_access_mappings;
  MapGenericMask(&desired_permissions, &mapping);

  // MSDN says this is optional, but it doesn't appear to be
  const std::unique_ptr<PRIVILEGE_SET> privilege_set(
      maidsafe::make_unique<PRIVILEGE_SET>());
  DWORD privilege_length = sizeof(PRIVILEGE_SET);

  DWORD granted_access = 0;
  BOOL access_status = false;
  if (!AccessCheck(
        security.get(),
        impersonation_token.get(),
        desired_permissions,
        &mapping,
        privilege_set.get(),
        &privilege_length,
        &granted_access,
        &access_status)) {
    ThrowWinFunctionError("AccessCheck");
  }

  return access_status != 0;
}

DWORD GetFileSecurityInternal(
    const detail::WinProcess& owner,
    const detail::MetaData::FileType path_type,
    const detail::MetaData::Permissions path_permissions,
    SECURITY_INFORMATION requested_information,
    PSECURITY_DESCRIPTOR out_descriptor,
    DWORD out_descriptor_length) {

  const std::unique_ptr<SECURITY_DESCRIPTOR> temp_descriptor(
      maidsafe::make_unique<SECURITY_DESCRIPTOR>());

  if (!InitializeSecurityDescriptor(temp_descriptor.get(), SECURITY_DESCRIPTOR_REVISION)) {
    ThrowWinFunctionError("InitializeSecurityDescriptor");
  }

  if (RequestedSecurityInfo(requested_information, OWNER_SECURITY_INFORMATION)) {
    if (!owner.GetOwnerSid() ||
        !SetSecurityDescriptorOwner(temp_descriptor.get(), owner.GetOwnerSid(), false)) {

      // If a owner could not be determined/set, designate no owner
      if (!SetSecurityDescriptorOwner(temp_descriptor.get(), NULL, false)) {
        ThrowWinFunctionError("SetSecurityDescriptorOwner");
      }
    }
  }

  if (RequestedSecurityInfo(requested_information, GROUP_SECURITY_INFORMATION)) {
    if (!SetSecurityDescriptorGroup(temp_descriptor.get(), NULL, false)) {
      ThrowWinFunctionError("SetSecurityDescriptorGroup");
    }
  }

  // DACL is the list of ACEs that indicate access permissions
  // SACL is the list of ACEs that indicate logging permissions for the DACL
  WinAcl dacl{};

  MAIDSAFE_CONSTEXPR_OR_CONST std::size_t aces_count = 3;
  std::array<std::unique_ptr<SID>, aces_count> sids{};
  std::array<EXPLICIT_ACCESS, aces_count> aces{};

  if (RequestedSecurityInfo(requested_information, DACL_SECURITY_INFORMATION)) {
    using Permissions = detail::MetaData::Permissions;
    MAIDSAFE_CONSTEXPR_OR_CONST auto ownership_mappings =
    {
        std::make_tuple(
            WinCreatorOwnerSid, TRUSTEE_IS_USER,
            Permissions::owner_read, Permissions::owner_write, Permissions::owner_exe),
        std::make_tuple(
            WinCreatorGroupSid, TRUSTEE_IS_WELL_KNOWN_GROUP,
            Permissions::group_read, Permissions::group_write, Permissions::group_exe),
        std::make_tuple(
            WinWorldSid, TRUSTEE_IS_WELL_KNOWN_GROUP,
            Permissions::others_read, Permissions::others_write, Permissions::others_exe)
    };

    // TODO upgrade to static_assert with newer Visual Studios
    assert(aces_count == ownership_mappings.size());
    assert(ownership_mappings.size() == aces.size());
    assert(ownership_mappings.size() == sids.size());

    std::size_t index = 0;
    for (const auto& mapping : ownership_mappings) {
      std::pair<EXPLICIT_ACCESS, std::unique_ptr<SID>> new_ace =
          MakeAce(path_type, path_permissions, mapping);
      aces[index] = new_ace.first;
      sids[index] = std::move(new_ace.second);
      ++index;
    }

    {
      PACL newDacl{};
      const bool acl_success =
          (SetEntriesInAcl(static_cast<ULONG>(aces.size()), aces.data(), NULL, &newDacl) == ERROR_SUCCESS);
      dacl.reset(newDacl);

      if (!acl_success) {
        ThrowWinFunctionError("SetEntriesInAcl");
      }
    }

    assert(IsValidAcl(dacl.get()) != 0);
    if (!SetSecurityDescriptorDacl(temp_descriptor.get(), true, dacl.get(), false)) {
      ThrowWinFunctionError("SetSecurityDescriptorDacl");
    }
  }

  return ConvertToRelative(
      owner,
      path_type == detail::MetaData::FileType::directory_file,
      *temp_descriptor,
      out_descriptor,
      out_descriptor_length);
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
