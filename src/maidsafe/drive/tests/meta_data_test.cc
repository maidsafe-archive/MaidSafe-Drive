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
    const MetaData::Permissions actual) {
  const auto has_permission = [actual](const MetaData::Permissions permission) {
    return HasPermission(actual, permission);
  };
  const auto not_has_permission = [has_permission](const MetaData::Permissions permission) {
    return !has_permission(permission);
  };

  return VerifyDistinctSets(
      expected_permissions, possible_permissions, has_permission, not_has_permission);
}

TEST(MetaDataTest, BEH_DirectoryConstructedState) {
  const MetaData metadata(MetaData::FileType::directory_file);

  EXPECT_TRUE(metadata.data_map() == nullptr);
  EXPECT_TRUE(metadata.directory_id() == nullptr);
  EXPECT_EQ(boost::filesystem::path(""), metadata.name());

  EXPECT_EQ(MetaData::FileType::directory_file, metadata.file_type());
  EXPECT_NE(MetaData::TimePoint(std::chrono::seconds(0)), metadata.creation_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_access_time());
  EXPECT_EQ(0, metadata.size());
    EXPECT_EQ(0, metadata.allocation_size());
}

TEST(MetaDataTest, BEH_FileConstructedState) {
  const MetaData metadata(MetaData::FileType::regular_file);

  EXPECT_TRUE(metadata.data_map() == nullptr);
  EXPECT_TRUE(metadata.directory_id() == nullptr);
  EXPECT_EQ(boost::filesystem::path(""), metadata.name());

  EXPECT_EQ(MetaData::FileType::regular_file, metadata.file_type());
  EXPECT_NE(MetaData::TimePoint(std::chrono::seconds(0)), metadata.creation_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_access_time());
  EXPECT_EQ(0, metadata.size());
  EXPECT_EQ(0, metadata.allocation_size());
}

TEST(MetaDataTest, BEH_DirectoryAndPathConstructedState) {
  const MetaData metadata("/stuff", MetaData::FileType::directory_file);

  EXPECT_TRUE(metadata.data_map() == nullptr);
  EXPECT_TRUE(metadata.directory_id() != nullptr);
  EXPECT_EQ(boost::filesystem::path("/stuff"), metadata.name());

  EXPECT_EQ(MetaData::FileType::directory_file, metadata.file_type());
  EXPECT_NE(MetaData::TimePoint(std::chrono::seconds(0)), metadata.creation_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_access_time());
#ifdef MAIDSAFE_WIN32
  EXPECT_EQ(0, metadata.size());
#else
  EXPECT_EQ(4096, metadata.size());
#endif
  EXPECT_EQ(0, metadata.allocation_size());
}

TEST(MetaDataTest, BEH_FileAndPathConstructedState) {
  const MetaData metadata("/stuff", MetaData::FileType::regular_file);

  EXPECT_TRUE(metadata.data_map() != nullptr);
  EXPECT_TRUE(metadata.directory_id() == nullptr);
  EXPECT_EQ(boost::filesystem::path("/stuff"), metadata.name());

  EXPECT_EQ(MetaData::FileType::regular_file, metadata.file_type());
  EXPECT_NE(MetaData::TimePoint(std::chrono::seconds(0)), metadata.creation_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_access_time());
  EXPECT_EQ(0, metadata.size());
  EXPECT_EQ(0, metadata.allocation_size());
}

TEST(MetaDataTest, BEH_SetLastAccessTime) {
  MetaData metadata("/", MetaData::FileType::regular_file);

  EXPECT_NE(MetaData::TimePoint(std::chrono::seconds(-1)), metadata.last_access_time());
  metadata.set_last_access_time(MetaData::TimePoint(std::chrono::seconds(-1)));
  EXPECT_EQ(MetaData::TimePoint(std::chrono::seconds(-1)), metadata.last_access_time());
}

TEST(MetaDataTest, BEH_SetStatusTime) {
  MetaData metadata("/", MetaData::FileType::regular_file);

  EXPECT_NE(MetaData::TimePoint(std::chrono::seconds(-1)), metadata.last_status_time());
  metadata.set_status_time(MetaData::TimePoint(std::chrono::seconds(-1)));
  EXPECT_EQ(MetaData::TimePoint(std::chrono::seconds(-1)), metadata.last_status_time());
}

TEST(MetaDataTest, BEH_SetLastWriteTime) {
  MetaData metadata("/", MetaData::FileType::regular_file);

  EXPECT_NE(MetaData::TimePoint(std::chrono::seconds(-1)), metadata.last_write_time());
  metadata.set_last_write_time(MetaData::TimePoint(std::chrono::seconds(-1)));
  EXPECT_EQ(MetaData::TimePoint(std::chrono::seconds(-1)), metadata.last_write_time());
}

TEST(MetaDataTest, BEH_SetCreationTime) {
  MetaData metadata("/", MetaData::FileType::regular_file);

  EXPECT_NE(MetaData::TimePoint(std::chrono::seconds(-1)), metadata.creation_time());
  metadata.set_creation_time(MetaData::TimePoint(std::chrono::seconds(-1)));
  EXPECT_EQ(MetaData::TimePoint(std::chrono::seconds(-1)), metadata.creation_time());
}

TEST(MetaDataTest, BEH_UpdateLastStatusTime) {
  MetaData metadata("/", MetaData::FileType::regular_file);

  EXPECT_EQ(metadata.last_access_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.last_access_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.last_access_time(), metadata.creation_time());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  metadata.UpdateLastStatusTime();

  EXPECT_LE(metadata.creation_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.last_status_time(), metadata.last_access_time());
}

TEST(MetaDataTest, BEH_UpdateLastModifiedTime) {
  MetaData metadata("/", MetaData::FileType::regular_file);

  EXPECT_EQ(metadata.last_access_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.last_access_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.last_access_time(), metadata.creation_time());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  metadata.UpdateLastModifiedTime();

  EXPECT_LE(metadata.creation_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.last_write_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.last_write_time(), metadata.last_access_time());
}

TEST(MetaDataTest, BEH_UpdateLastAccessTime) {
  MetaData metadata("/", MetaData::FileType::regular_file);

  EXPECT_EQ(metadata.last_access_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.last_access_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.last_access_time(), metadata.creation_time());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  metadata.UpdateLastAccessTime();

  EXPECT_LE(metadata.creation_time(), metadata.last_access_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.creation_time(), metadata.last_status_time());
}

TEST(MetaDataTest, BEH_UpdateSize) {
  MetaData metadata("/", MetaData::FileType::regular_file);

  EXPECT_EQ(metadata.last_access_time(), metadata.last_status_time());
  EXPECT_EQ(metadata.last_access_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.last_access_time(), metadata.creation_time());
  EXPECT_EQ(0, metadata.size());
  EXPECT_EQ(0, metadata.allocation_size());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  metadata.UpdateSize(1000);

  EXPECT_LE(metadata.creation_time(), metadata.last_write_time());
  EXPECT_EQ(metadata.last_write_time(), metadata.last_access_time());
  EXPECT_EQ(metadata.last_write_time(), metadata.last_status_time());
  EXPECT_EQ(1000, metadata.size());
  EXPECT_EQ(1000, metadata.allocation_size());

  const MetaData::TimePoint last_modification = metadata.last_write_time();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  metadata.UpdateSize(100);

  EXPECT_LE(metadata.creation_time(), metadata.last_write_time());
  EXPECT_LE(last_modification, metadata.last_write_time());
  EXPECT_EQ(metadata.last_write_time(), metadata.last_access_time());
  EXPECT_EQ(metadata.last_write_time(), metadata.last_status_time());
  EXPECT_EQ(100, metadata.size());
  EXPECT_EQ(100, metadata.allocation_size());
}

TEST(MetaDataTest, BEH_Swap) {
  MetaData one("/one", MetaData::FileType::regular_file);
  one.UpdateSize(100);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  MetaData two("/two", MetaData::FileType::directory_file);
  two.UpdateSize(1000);

  const auto one_data_map = one.data_map();
  const auto one_directory_id = one.directory_id();
  const auto one_name = one.name();
  const auto one_file_type = one.file_type();
  const auto one_creation_time = one.creation_time();
  const auto one_last_status_time = one.last_status_time();
  const auto one_last_access_time = one.last_access_time();
  const auto one_size = one.size();
  const auto one_allocation_size = one.allocation_size();

  const auto two_data_map = two.data_map();
  const auto two_directory_id = two.directory_id();
  const auto two_name = two.name();
  const auto two_file_type = two.file_type();
  const auto two_creation_time = two.creation_time();
  const auto two_last_status_time = two.last_status_time();
  const auto two_last_access_time = two.last_access_time();
  const auto two_size = two.size();
  const auto two_allocation_size = two.allocation_size();

  swap(one, two);

  EXPECT_EQ(one_data_map, two.data_map());
  EXPECT_EQ(one_directory_id, two.directory_id());
  EXPECT_EQ(one_name, two.name());
  EXPECT_EQ(one_file_type, two.file_type());
  EXPECT_EQ(one_creation_time, two.creation_time());
  EXPECT_EQ(one_last_status_time, two.last_status_time());
  EXPECT_EQ(one_last_access_time, two.last_access_time());
  EXPECT_EQ(one_size, two.size());
  EXPECT_EQ(one_allocation_size, two.allocation_size());

  EXPECT_EQ(two_data_map, one.data_map());
  EXPECT_EQ(two_directory_id, one.directory_id());
  EXPECT_EQ(two_name, one.name());
  EXPECT_EQ(two_file_type, one.file_type());
  EXPECT_EQ(two_creation_time, one.creation_time());
  EXPECT_EQ(two_last_status_time, one.last_status_time());
  EXPECT_EQ(two_last_access_time, one.last_access_time());
  EXPECT_EQ(two_size, one.size());
  EXPECT_EQ(two_allocation_size, one.allocation_size());
}

TEST(MetaDataTest, BEH_HasPermission) {
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::owner_read, MetaData::Permissions::owner_read));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::owner_write, MetaData::Permissions::owner_write));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::owner_exe, MetaData::Permissions::owner_exe));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::group_read, MetaData::Permissions::group_read));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::group_write, MetaData::Permissions::group_write));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::group_exe, MetaData::Permissions::group_exe));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::others_read, MetaData::Permissions::others_read));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::others_write, MetaData::Permissions::others_write));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::others_exe, MetaData::Permissions::others_exe));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::set_uid_on_exe, MetaData::Permissions::set_uid_on_exe));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::set_gid_on_exe, MetaData::Permissions::set_gid_on_exe));
  EXPECT_TRUE(
      HasPermission(
          MetaData::Permissions::sticky_bit, MetaData::Permissions::sticky_bit));

  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::owner_read |
              MetaData::Permissions::group_read),
          MetaData::Permissions::owner_read));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::owner_write |
              MetaData::Permissions::group_read),
          MetaData::Permissions::owner_write));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::owner_exe |
              MetaData::Permissions::group_read),
          MetaData::Permissions::owner_exe));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::group_read |
              MetaData::Permissions::owner_read),
          MetaData::Permissions::group_read));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::group_write |
              MetaData::Permissions::owner_read),
          MetaData::Permissions::group_write));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::group_exe |
              MetaData::Permissions::owner_read),
          MetaData::Permissions::group_exe));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::others_read |
              MetaData::Permissions::set_gid_on_exe),
          MetaData::Permissions::others_read));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::others_write |
              MetaData::Permissions::set_gid_on_exe),
          MetaData::Permissions::others_write));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::others_exe |
              MetaData::Permissions::set_gid_on_exe),
          MetaData::Permissions::others_exe));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::set_uid_on_exe |
              MetaData::Permissions::others_read),
          MetaData::Permissions::set_uid_on_exe));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::set_gid_on_exe |
              MetaData::Permissions::others_read),
          MetaData::Permissions::set_gid_on_exe));
  EXPECT_TRUE(
      HasPermission(
          (
              MetaData::Permissions::sticky_bit |
              MetaData::Permissions::others_read),
          MetaData::Permissions::sticky_bit));

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

TEST(MetaDataTest, BEH_GetPermissionsNotDirectory) {
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
TEST(MetaDataTest, BEH_GetPermissionsDirectory) {
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

} // test
} // detail
} // drive
} // maidsafe
