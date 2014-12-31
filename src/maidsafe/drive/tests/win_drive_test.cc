/*  Copyright 2014 MaidSafe.net limited

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
#ifdef WIN32  // Windows test

#include <aclapi.h>
#include <accctrl.h>

#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/make_unique.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/win_drive.h"
#include "maidsafe/drive/win_process.h"

#include "maidsafe/drive/tests/test_utils.h"

namespace maidsafe {
namespace drive {
namespace test {
namespace {

const auto WinLocalFreeLambda = [](EXPLICIT_ACCESS* buffer) {
  if (buffer != nullptr) {
    ::LocalFree(buffer);
  }
};
using WinAces = std::unique_ptr<EXPLICIT_ACCESS[], decltype(WinLocalFreeLambda)>;

bool IsOwnerEmpty(const std::unique_ptr<char[]>& security_desciptor) {
  PSID owner{};
  BOOL defaulted_owner = 0;
  if (GetSecurityDescriptorOwner(security_desciptor.get(), &owner, &defaulted_owner)) {
    return owner == nullptr;
  }
  return false;
}

bool IsExpectedOwner(PSID actual_owner) {
  const detail::WinProcess expected_owner{};

  if (actual_owner != nullptr && expected_owner.GetOwnerSid() != nullptr) {
    return EqualSid(expected_owner.GetOwnerSid(), actual_owner) != 0;
  }
  return false;
}

bool IsExpectedOwner(const std::unique_ptr<char[]>& security_descriptor) {
  PSID actual_owner{};
  BOOL defaulted_owner = 0;
  if (GetSecurityDescriptorOwner(security_descriptor.get(), &actual_owner, &defaulted_owner)) {
    return IsExpectedOwner(actual_owner);
  }
  return false;
}

bool IsExpectedGroup(PSID actual_owner) {
  const detail::WinProcess current_process{};

  DWORD group_token_size = 0;
  if (!GetTokenInformation(current_process.GetAccessToken().get(), TokenPrimaryGroup, NULL, 0,
                           &group_token_size) &&
      GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    const auto sid_memory = maidsafe::make_unique<char[]>(group_token_size);
    if (GetTokenInformation(current_process.GetAccessToken().get(), TokenPrimaryGroup,
                            sid_memory.get(), group_token_size, &group_token_size)) {
      TOKEN_PRIMARY_GROUP* const expected_group =
          reinterpret_cast<TOKEN_PRIMARY_GROUP*>(sid_memory.get());
      if (actual_owner != nullptr && expected_group != nullptr) {
        return EqualSid(expected_group->PrimaryGroup, actual_owner) != 0;
      }
    }
  }
  return false;
}

bool IsGroupEmpty(const std::unique_ptr<char[]>& security_desciptor) {
  PSID group{};
  BOOL defaulted_group = 0;
  if (GetSecurityDescriptorOwner(security_desciptor.get(), &group, &defaulted_group)) {
    return group == nullptr;
  }
  return false;
}

bool IsDaclEmpty(const std::unique_ptr<char[]>& security_desciptor) {
  BOOL dacl_present = 0;
  PACL dacl{};
  BOOL defaulted_dacl = 0;
  if (GetSecurityDescriptorDacl(security_desciptor.get(), &dacl_present, &dacl, &defaulted_dacl)) {
    return dacl_present == FALSE;
  }
  return false;
}

std::pair<WinAces, ULONG> GetWinAces(const std::unique_ptr<char[]>& security_descriptor) {
  BOOL dacl_present = 0;
  PACL dacl{};
  BOOL defaulted_dacl = 0;
  if (GetSecurityDescriptorDacl(security_descriptor.get(), &dacl_present, &dacl, &defaulted_dacl)) {
    ULONG count = 0;
    PEXPLICIT_ACCESS aces{};
    if (GetExplicitEntriesFromAcl(dacl, &count, &aces) == ERROR_SUCCESS) {
      return std::make_pair(WinAces{aces}, count);
    }
  }
  return std::make_pair(WinAces{}, 0);
}

void VerifySecurityFunctions(const detail::MetaData::FileType test_file_type,
                             const detail::MetaData::Permissions test_permissions,
                             const std::set<DWORD>& expected_owner_permissions) {
  MAIDSAFE_CONSTEXPR_OR_CONST std::initializer_list<DWORD> check_permissions = {
      GENERIC_READ, GENERIC_WRITE, GENERIC_EXECUTE, GENERIC_ALL, FILE_GENERIC_READ,
      FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_EXECUTE, FILE_ALL_ACCESS, FILE_TRAVERSE,
      DELETE, READ_CONTROL};

  const detail::WinProcess current_process{};
  ASSERT_NE(nullptr, current_process.GetAccessToken().get());

  // Check raw security descriptor first
  {
    const DWORD descriptor_length =
        detail::GetFileSecurityInternal(current_process, test_file_type, test_permissions, NULL, 0);
    ASSERT_LT(0u, descriptor_length);

    const std::unique_ptr<char[]> descriptor_storage(
        maidsafe::make_unique<char[]>(descriptor_length));

    // verify one less doesn't crash function
    EXPECT_EQ(descriptor_length,
              detail::GetFileSecurityInternal(current_process, test_file_type, test_permissions,
                                              descriptor_storage.get(), descriptor_length - 1));
    EXPECT_EQ(descriptor_length,
              detail::GetFileSecurityInternal(current_process, test_file_type, test_permissions,
                                              descriptor_storage.get(), descriptor_length));

    EXPECT_FALSE(IsOwnerEmpty(descriptor_storage));
    EXPECT_TRUE(IsExpectedOwner(descriptor_storage));
    EXPECT_FALSE(IsGroupEmpty(descriptor_storage));
    EXPECT_FALSE(IsDaclEmpty(descriptor_storage));
    const auto aces = GetWinAces(descriptor_storage);
    EXPECT_LT(0u, aces.second) << "Expected at least one ACE";

    const std::unique_ptr<SID> everyone_sid(maidsafe::make_unique<SID>());
    {
      DWORD sid_size = sizeof(SID);
      ASSERT_NE(0, CreateWellKnownSid(WinWorldSid, NULL, everyone_sid.get(), &sid_size))
          << GetLastError();
    }

    // Rough checks for ensuring that bits in access mask are set iff one of
    // permissions was given. Not perfect, but thats why a more strict test is
    // done below.
    for (ULONG index = 0; index < aces.second; ++index) {
      EXPECT_EQ(NO_MULTIPLE_TRUSTEE, aces.first[index].Trustee.MultipleTrusteeOperation);
      EXPECT_EQ(nullptr, aces.first[index].Trustee.pMultipleTrustee);
      ASSERT_EQ(TRUSTEE_IS_SID, aces.first[index].Trustee.TrusteeType);
      ASSERT_NE(nullptr, aces.first[index].Trustee.ptstrName);

      if (aces.first[index].grfAccessMode == GRANT_ACCESS &&
          aces.first[index].grfAccessPermissions != 0) {
        if (IsExpectedOwner(aces.first[index].Trustee.ptstrName)) {
          EXPECT_NE(0u, static_cast<unsigned>(test_permissions &
                                              detail::MetaData::Permissions::owner_all));
        } else if (IsExpectedGroup(aces.first[index].Trustee.ptstrName)) {
          EXPECT_NE(0u, static_cast<unsigned>(test_permissions &
                                              detail::MetaData::Permissions::group_all));
        } else if (EqualSid(everyone_sid.get(), aces.first[index].Trustee.ptstrName)) {
          EXPECT_NE(0u, static_cast<unsigned>(test_permissions &
                                              detail::MetaData::Permissions::others_all));
        } else {
          ADD_FAILURE() << "Permission was given to an unexpected SID";
        }
      }
    }
  }

  for (const auto check_permission : check_permissions) {
    const bool expect_access =
        (expected_owner_permissions.find(check_permission) != expected_owner_permissions.end());

    EXPECT_EQ(expect_access,
              detail::HaveAccessInternal(current_process.GetAccessToken(), check_permission,
                                         current_process, test_file_type, test_permissions))
        << "Failed permission: " << check_permission;
  }
}

}  // anonymous namespace

/* We can only test as current user, so access granting to owner, group,
and others will all return the same result. I gave up trying to figure out
how to do a check as an anonymous user - that user is only in the others
group iff a registry value is set. So it can only be used to verify that
owner and group permissions fail (but creating the token appears tricky!).
*/

TEST(WinDriveTests, BEH_NoPermissionsFile) {
  VerifySecurityFunctions(detail::MetaData::FileType::regular_file,
                          detail::MetaData::Permissions::no_perms, {READ_CONTROL});
}

TEST(WinDriveTests, BEH_NoPermissionsDirectory) {
  VerifySecurityFunctions(detail::MetaData::FileType::directory_file,
                          detail::MetaData::Permissions::no_perms, {READ_CONTROL});
}

TEST(WinDriveTests, BEH_ReadPermissionsFile) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::regular_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_read, Permissions::group_read, Permissions::others_read};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission,
                            {GENERIC_READ, FILE_GENERIC_READ, READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_ReadPermissionsDirectory) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::directory_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_read, Permissions::group_read, Permissions::others_read};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission, {READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_WritePermissionsFile) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::regular_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_write, Permissions::group_write, Permissions::others_write};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission,
                            {GENERIC_WRITE, FILE_GENERIC_WRITE, DELETE, READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_WritePermissionsDirectory) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::directory_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_write, Permissions::group_write, Permissions::others_write};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission,
                            {GENERIC_WRITE, FILE_GENERIC_WRITE, DELETE, READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_ExePermissionsFile) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::regular_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_exe, Permissions::group_exe, Permissions::others_exe};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission,
                            {GENERIC_EXECUTE, FILE_GENERIC_EXECUTE, FILE_EXECUTE, READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_ExePermissionsDirectory) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::directory_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_exe, Permissions::group_exe, Permissions::others_exe};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission, {READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_ReadWritePermissionsFile) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::regular_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_read | Permissions::owner_write,
      Permissions::group_read | Permissions::group_write,
      Permissions::others_read | Permissions::others_write};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(
        test_file_type, test_permission,
        {GENERIC_READ, GENERIC_WRITE, FILE_GENERIC_READ, FILE_GENERIC_WRITE, DELETE, READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_ReadWritePermissionsDirectory) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::directory_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_read | Permissions::owner_write,
      Permissions::group_read | Permissions::group_write,
      Permissions::others_read | Permissions::others_write};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission,
                            {GENERIC_WRITE, FILE_GENERIC_WRITE, DELETE, READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_ReadExePermissionsFile) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::regular_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_read | Permissions::owner_exe,
      Permissions::group_read | Permissions::group_exe,
      Permissions::others_read | Permissions::others_exe};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission,
                            {GENERIC_READ, GENERIC_EXECUTE, FILE_GENERIC_READ, FILE_GENERIC_EXECUTE,
                             FILE_EXECUTE, READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_ReadExePermissionsDirectory) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::directory_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_read | Permissions::owner_exe,
      Permissions::group_read | Permissions::group_exe,
      Permissions::others_read | Permissions::others_exe};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission,
                            {GENERIC_READ, GENERIC_EXECUTE, FILE_GENERIC_READ, FILE_GENERIC_EXECUTE,
                             FILE_TRAVERSE, READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_ReadWriteExePermissionsFile) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::regular_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_read | Permissions::owner_write | Permissions::owner_exe,
      Permissions::group_read | Permissions::group_write | Permissions::group_exe,
      Permissions::others_read | Permissions::others_write | Permissions::others_exe};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission,
                            {GENERIC_READ, GENERIC_WRITE, GENERIC_EXECUTE, FILE_GENERIC_READ,
                             FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_EXECUTE, DELETE,
                             READ_CONTROL});
  }
}

TEST(WinDriveTests, BEH_ReadWriteExePermissionsDirectory) {
  using Permissions = detail::MetaData::Permissions;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_file_type = detail::MetaData::FileType::directory_file;
  MAIDSAFE_CONSTEXPR_OR_CONST auto test_permissions = {
      Permissions::owner_read | Permissions::owner_write | Permissions::owner_exe,
      Permissions::group_read | Permissions::group_write | Permissions::group_exe,
      Permissions::others_read | Permissions::others_write | Permissions::others_exe};

  for (const auto test_permission : test_permissions) {
    VerifySecurityFunctions(test_file_type, test_permission,
                            {GENERIC_READ, GENERIC_WRITE, GENERIC_EXECUTE, FILE_GENERIC_READ,
                             FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_TRAVERSE, DELETE,
                             READ_CONTROL});
  }
}

}  // namespace test
}  // namespace drive
}  // namespace maidsafe

#endif  // WIN32
