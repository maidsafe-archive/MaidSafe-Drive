#ifndef WIN32 // non-Windows test

#include <boost/filesystem/operations.hpp>

#include "maidsafe/common/test.h"
#include "maidsafe/drive/unix_drive.h"

#include "meta_data_test.h"
#include "test_utils.h"

namespace maidsafe {
namespace drive {
namespace test {

namespace
{
  const std::initializer_list<mode_t> possible_types =
  {
      S_IFSOCK, S_IFLNK, S_IFREG, S_IFBLK, S_IFDIR, S_IFCHR, S_IFIFO
  };

  const std::initializer_list<mode_t> supported_types =
  {
      S_IFDIR, S_IFREG, S_IFLNK
  };

  const std::initializer_list<mode_t> possible_permissions =
  {
      S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP,
      S_IROTH, S_IWOTH, S_IXOTH, S_ISUID, S_ISGID, S_ISVTX
  };

  bool VerifyNonPermissionBits(const mode_t mode) {
    const mode_t NonPermissionBits = ~(detail::ModePermissionMask());
    return (mode & NonPermissionBits) == 0;
  }

  bool VerifyPermissions(
      const std::set<mode_t>& expected,
      const mode_t actual) {

    const auto has_expected = [actual](const mode_t expected) {
      return (actual & expected) == expected;
    };

    const auto not_has_expected = [has_expected](const mode_t not_expected) {
      return !has_expected(not_expected);
    };

    return detail::test::VerifyDistinctSets(
        expected, possible_permissions, has_expected, not_has_expected);
  }

} //anonymous namespace

TEST(UnixDriveTest, BEH_HaveEquivalentPermissions) {
  EXPECT_TRUE(detail::HaveEquivalentPermissions());
}

TEST(UnixDriveTest, BEH_ModePermissionMask) {
  EXPECT_TRUE(
      static_cast<mode_t>(boost::filesystem::perms_mask) == detail::ModePermissionMask());
}

TEST(UnixDriveTest, BEH_ToPermssionMode) {
  {
    EXPECT_TRUE(
        VerifyNonPermissionBits(
            detail::ToPermissionMode(
                static_cast<detail::MetaData::Permissions>(
                    std::numeric_limits<mode_t>::min()))));
  }
  {
    EXPECT_TRUE(
        VerifyNonPermissionBits(
            detail::ToPermissionMode(
                static_cast<detail::MetaData::Permissions>(
                    std::numeric_limits<mode_t>::max()))));
  }
  {
    const mode_t mode = detail::ToPermissionMode(detail::MetaData::Permissions::no_perms);
    EXPECT_TRUE(VerifyNonPermissionBits(mode));
    EXPECT_TRUE(VerifyPermissions({}, mode));
  }
  {
    const mode_t mode = detail::ToPermissionMode(detail::MetaData::Permissions::owner_read);
    EXPECT_TRUE(VerifyNonPermissionBits(mode));
    EXPECT_TRUE(VerifyPermissions({ S_IRUSR }, mode));
  }
  {
    const mode_t mode = detail::ToPermissionMode(
        detail::MetaData::Permissions::owner_read | detail::MetaData::Permissions::group_exe);
    EXPECT_TRUE(VerifyNonPermissionBits(mode));
    EXPECT_TRUE(VerifyPermissions({ S_IRUSR, S_IXGRP }, mode));
  }
  {
    const mode_t mode = detail::ToPermissionMode(
        detail::MetaData::Permissions::owner_all |
        detail::MetaData::Permissions::group_exe |
        detail::MetaData::Permissions::others_write |
        detail::MetaData::Permissions::set_uid_on_exe);
    EXPECT_TRUE(VerifyNonPermissionBits(mode));
    EXPECT_TRUE(VerifyPermissions(
        { S_IRUSR, S_IWUSR, S_IXUSR, S_IXGRP, S_IWOTH, S_ISUID }, mode));
  }
}

TEST(UnixDriveTest, BEH_ToFileType) {
  EXPECT_EQ(
      detail::MetaData::FileType::directory_file,
      detail::ToFileType(S_IFDIR | detail::ModePermissionMask()));
  EXPECT_EQ(
      detail::MetaData::FileType::regular_file,
      detail::ToFileType(S_IFREG | detail::ModePermissionMask()));
  EXPECT_EQ(
      detail::MetaData::FileType::symlink_file,
      detail::ToFileType(S_IFLNK | detail::ModePermissionMask()));

  EXPECT_EQ(
      detail::MetaData::FileType::status_error,
      detail::ToFileType(S_IFSOCK | detail::ModePermissionMask()));
  EXPECT_EQ(
      detail::MetaData::FileType::status_error,
      detail::ToFileType(S_IFBLK | detail::ModePermissionMask()));
  EXPECT_EQ(
      detail::MetaData::FileType::status_error,
      detail::ToFileType(S_IFCHR | detail::ModePermissionMask()));
  EXPECT_EQ(
      detail::MetaData::FileType::status_error,
      detail::ToFileType(S_IFIFO | detail::ModePermissionMask()));
}

TEST(UnixDriveTest, BEH_ToFileMode) {
  EXPECT_EQ(
      S_IFDIR, detail::ToFileMode(detail::MetaData::FileType::directory_file));
  EXPECT_EQ(
      S_IFREG, detail::ToFileMode(detail::MetaData::FileType::regular_file));
  EXPECT_EQ(
      S_IFLNK, detail::ToFileMode(detail::MetaData::FileType::symlink_file));
}

TEST(UnixDriveTest, BEH_IsSupported) {
  for (const mode_t type : possible_types) {
    const bool supported = std::find(
        supported_types.begin(), supported_types.end(), type) !=
        supported_types.end();

    EXPECT_EQ(supported, detail::IsSupported(type));
  }
}

} // test
} // drive
} // maidsafe

#endif
