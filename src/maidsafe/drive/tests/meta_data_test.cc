#include <algorithm>
#include <set>
#include <vector>

#include "maidsafe/common/test.h"
#include "maidsafe/drive/meta_data.h"

#include "test_utils.h"

namespace maidsafe {
namespace drive {
namespace detail {
namespace test {

namespace
{
  const std::initializer_list<MetaData::Permissions> possible_permissions =
  {
      MetaData::Permissions::owner_read, MetaData::Permissions::owner_write,
      MetaData::Permissions::owner_exe,
      MetaData::Permissions::group_read, MetaData::Permissions::group_write,
      MetaData::Permissions::group_exe,
      MetaData::Permissions::others_read, MetaData::Permissions::others_write,
      MetaData::Permissions::others_exe
  };
}

bool VerifyPermissions(
    const std::set<MetaData::Permissions>& expected_permissions,
    const MetaData::Permissions actual)
{
  const auto has_permission = [actual](const MetaData::Permissions permission) {
    return HasPermission(actual, permission);
  };
  const auto not_has_permission = [has_permission](const MetaData::Permissions permission) {
    return !has_permission(permission);
  };

  return VerifyDistinctSets(
      expected_permissions, possible_permissions, has_permission, not_has_permission);
}

TEST(MetaDataTest, BEH_HasPermission) {
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::owner_read,
          MetaData::Permissions::owner_read));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::owner_read |
              MetaData::Permissions::group_read),
          MetaData::Permissions::owner_read));
  EXPECT_FALSE(
      HasPermission(
          (
              MetaData::Permissions::group_read |
              MetaData::Permissions::others_read),
          MetaData::Permissions::owner_read));
  EXPECT_FALSE(
      HasPermission(
          (
              MetaData::Permissions::owner_read |
              MetaData::Permissions::owner_write |
              MetaData::Permissions::owner_exe |
              MetaData::Permissions::group_read |
              MetaData::Permissions::group_write |
              MetaData::Permissions::group_exe |
              MetaData::Permissions::others_read |
              MetaData::Permissions::others_exe),
          MetaData::Permissions::others_write));
}

TEST(MetaDataTest, BEH_GetPermissions) {
  {
    const MetaData not_directory(MetaData::FileType::regular_file);

    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::owner_read },
            not_directory.GetPermissions(MetaData::Permissions::owner_read)));
    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::group_read },
            not_directory.GetPermissions(MetaData::Permissions::group_read)));
    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::others_read },
            not_directory.GetPermissions(MetaData::Permissions::others_read)));

    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::owner_read, MetaData::Permissions::owner_write },
            not_directory.GetPermissions(
                MetaData::Permissions::owner_read | MetaData::Permissions::owner_write)));
    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::group_read, MetaData::Permissions::group_write },
            not_directory.GetPermissions(
                MetaData::Permissions::group_read | MetaData::Permissions::group_write)));
    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::others_read, MetaData::Permissions::others_write },
            not_directory.GetPermissions(
                MetaData::Permissions::others_read | MetaData::Permissions::others_write)));

    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::owner_read, MetaData::Permissions::group_read },
            not_directory.GetPermissions(
                MetaData::Permissions::owner_read | MetaData::Permissions::group_read)));
    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::owner_read, MetaData::Permissions::others_read },
            not_directory.GetPermissions(
                MetaData::Permissions::owner_read | MetaData::Permissions::others_read)));
    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::group_read, MetaData::Permissions::others_read },
            not_directory.GetPermissions(
                MetaData::Permissions::group_read | MetaData::Permissions::others_read)));

    EXPECT_TRUE(
        VerifyPermissions(
            {
                MetaData::Permissions::owner_read, MetaData::Permissions::owner_write,
                MetaData::Permissions::group_read, MetaData::Permissions::group_write,
                MetaData::Permissions::others_read, MetaData::Permissions::others_write
            },
            not_directory.GetPermissions(
                MetaData::Permissions::owner_read | MetaData::Permissions::owner_write |
                MetaData::Permissions::group_read | MetaData::Permissions::group_write |
                MetaData::Permissions::others_read | MetaData::Permissions::others_write)));
  }
  {
    const MetaData directory(MetaData::FileType::directory_file);

    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::owner_read, MetaData::Permissions::owner_exe },
            directory.GetPermissions(MetaData::Permissions::owner_read)));
    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::group_read, MetaData::Permissions::group_exe },
            directory.GetPermissions(MetaData::Permissions::group_read)));
    EXPECT_TRUE(
        VerifyPermissions(
            { MetaData::Permissions::others_read, MetaData::Permissions::others_exe },
            directory.GetPermissions(MetaData::Permissions::others_read)));

    EXPECT_TRUE(
        VerifyPermissions(
            {
                MetaData::Permissions::owner_read, MetaData::Permissions::owner_write,
                MetaData::Permissions::owner_exe
            },
            directory.GetPermissions(
                MetaData::Permissions::owner_read | MetaData::Permissions::owner_write)));
    EXPECT_TRUE(
        VerifyPermissions(
            {
                MetaData::Permissions::group_read, MetaData::Permissions::group_write,
                MetaData::Permissions::group_exe
            },
            directory.GetPermissions(
                MetaData::Permissions::group_read | MetaData::Permissions::group_write)));
    EXPECT_TRUE(
        VerifyPermissions(
            {
                MetaData::Permissions::others_read, MetaData::Permissions::others_write,
                MetaData::Permissions::others_exe
            },
            directory.GetPermissions(
                MetaData::Permissions::others_read | MetaData::Permissions::others_write)));

    EXPECT_TRUE(
        VerifyPermissions(
            {
                MetaData::Permissions::owner_read, MetaData::Permissions::owner_exe,
                MetaData::Permissions::group_read, MetaData::Permissions::group_exe
            },
            directory.GetPermissions(
                MetaData::Permissions::owner_read | MetaData::Permissions::group_read)));
    EXPECT_TRUE(
        VerifyPermissions(
            {
                MetaData::Permissions::owner_read, MetaData::Permissions::owner_exe,
                MetaData::Permissions::others_read, MetaData::Permissions::others_exe
            },
            directory.GetPermissions(
                MetaData::Permissions::owner_read | MetaData::Permissions::others_read)));
    EXPECT_TRUE(
        VerifyPermissions(
            {
                MetaData::Permissions::group_read, MetaData::Permissions::group_exe,
                MetaData::Permissions::others_read, MetaData::Permissions::others_exe
            },
            directory.GetPermissions(
                MetaData::Permissions::group_read | MetaData::Permissions::others_read)));

    EXPECT_TRUE(
        VerifyPermissions(
            {
                MetaData::Permissions::owner_read, MetaData::Permissions::owner_write,
                MetaData::Permissions::owner_exe,
                MetaData::Permissions::group_read, MetaData::Permissions::group_write,
                MetaData::Permissions::group_exe,
                MetaData::Permissions::others_read, MetaData::Permissions::others_write,
                MetaData::Permissions::others_exe
            },
            directory.GetPermissions(
                MetaData::Permissions::owner_read | MetaData::Permissions::owner_write |
                MetaData::Permissions::group_read | MetaData::Permissions::group_write |
                MetaData::Permissions::others_read | MetaData::Permissions::others_write)));
  }
}

} // test
} // detail
} // drive
} // maidsafe
