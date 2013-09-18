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

#include <memory>
#include <cstdio>

#include "boost/filesystem/fstream.hpp"
#include "boost/lexical_cast.hpp"

#include "maidsafe/common/crypto.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/private/chunk_store/remote_chunk_store.h"
#include "maidsafe/private/utils/utilities.h"

#ifdef WIN32
#  include "maidsafe/drive/win_drive.h"
#else
#  include "maidsafe/drive/unix_drive.h"
#endif
#include "maidsafe/drive/directory_listing_handler.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/return_codes.h"
#include "maidsafe/drive/tests/test_utils.h"

namespace fs = boost::filesystem;
namespace args = std::placeholders;
namespace pcs = maidsafe::priv::chunk_store;

namespace maidsafe {

namespace drive {

namespace test {

#ifdef WIN32
typedef CbfsDriveInUserSpace TestDriveInUserSpace;
#else
typedef FuseDriveInUserSpace TestDriveInUserSpace;
#endif

typedef std::shared_ptr<DerivedDriveInUserSpace> DrivePtr;

class ShareTestsBase {
 public:
  ShareTestsBase() : main_test_dir_(maidsafe::test::CreateTestPath()),
                     chunk_store_(),
                     asio_service_(5) {}

 protected:
  DrivePtr CreateAndMountDrive(const std::string &root_parent_id,
                               std::string *unique_user_id,
                               const int64_t &max_space,
                               const int64_t &used_space,
                               fs::path *test_mount_dir = nullptr,
                               asymm::Keys *key_ring = nullptr) {
    if (!unique_user_id) {
      LOG(kError) << "null unique_user_id";
      return DrivePtr();
    }
    asio_service_.Start();
    if (unique_user_id->empty())
      *unique_user_id = crypto::Hash<crypto::SHA512>(RandomString(8));
    fs::path buffered_chunk_store_path(*main_test_dir_ / RandomAlphaNumericString(8));
    chunk_store_ = pcs::CreateLocalChunkStore(buffered_chunk_store_path,
                                              *main_test_dir_ / "local",
                                              *main_test_dir_ / "lock_path",
                                              asio_service_.service());

    asymm::Keys keyring;
    if (!key_ring || key_ring->identity.empty()) {
      asymm::GenerateKeyPair(&keyring);
      keyring.identity = RandomString(crypto::SHA512::DIGESTSIZE);
      if (key_ring)
        *key_ring = keyring;
    } else {
      keyring = *key_ring;
    }
    DrivePtr drive(new DerivedDriveInUserSpace(*chunk_store_, keyring));

#ifdef WIN32
    std::uint32_t drive_letters, mask = 0x4, count = 2;
    drive_letters = GetLogicalDrives();
    while ((drive_letters & mask) != 0) {
      mask <<= 1;
      ++count;
    }
    if (count > 25)
      LOG(kError) << "No available drive letters:";

    char drive_name[3] = {'A' + static_cast<char>(count), ':', '\0'};
    fs::path mount_dir(drive_name);
#else
    fs::path mount_dir(*main_test_dir_ / "MaidSafeDrive");
#endif

#ifndef WIN32
    boost::system::error_code error_code;
    fs::create_directories(mount_dir, error_code);
    if (error_code) {
      LOG(kError) << "Failed creating mount directory";
      asio_service_.Stop();
      return DrivePtr();
    }
#endif
    if (drive->Init(*unique_user_id, root_parent_id) != kSuccess) {
      LOG(kError) << "Failed to initialise drive";
      asio_service_.Stop();
      return DrivePtr();
    }

#ifdef WIN32
    if (drive->Mount(mount_dir, L"MaidSafe Drive", max_space, used_space)) {
      LOG(kError) << "Failed to mount drive";
      asio_service_.Stop();
      return DrivePtr();
    }

    mount_dir /= "\\";
#else
    // TODO(Team): Find out why, if the mount is put on the asio service,
    //             unmount hangs
    boost::thread th(std::bind(&DriveInUserSpace::Mount, drive, mount_dir, "TestDrive",
                               max_space, used_space, false, false));
    if (!drive->WaitUntilMounted()) {
      LOG(kError) << "Drive failed to mount";
      asio_service_.Stop();
      return DrivePtr();
    }
#endif

    if (test_mount_dir)
      *test_mount_dir = mount_dir;

    // Create root dir for share if not exists
    boost::system::error_code error_code_1;
    fs::path directory(mount_dir / kMsShareRoot);
    if (!(fs::exists(directory, error_code_1)))
      EXPECT_TRUE(fs::create_directories(directory, error_code_1)) << directory
                  << ": " << error_code_1.message();

    return drive;
  }

  void UnmountDrive(DrivePtr drive, int64_t &max_space, int64_t &used_space) {
#ifdef WIN32
    EXPECT_EQ(kSuccess, drive->Unmount(max_space, used_space));
#else
    drive->Unmount(max_space, used_space);
    drive->WaitUntilUnMounted();
#endif
    asio_service_.Stop();
  }

  virtual ~ShareTestsBase() {}

  maidsafe::test::TestPath main_test_dir_;
  RemoteChunkStorePtr chunk_store_;
  AsioService asio_service_;
};

class PrivateOpenShareTests : public ShareTestsBase,
                              public testing::TestWithParam<bool> {
 public:
  PrivateOpenShareTests() : ShareTestsBase(),
                            private_share_(GetParam()),
                            signaled_(false) {}

  void ShareRenamedSlot(const std::string&, const std::string&) {
    signaled_ = true;
  }

  bool private_share_;
  bool signaled_;

 protected:
  void SetUp() { /*asio_service_.Start(5);*/ }
  void TearDown() { /*asio_service_.Stop();*/ }
};

class SharesTest : public ShareTestsBase, public testing::Test {
 public:
  SharesTest() : ShareTestsBase() {}

 protected:
  void SetUp() { /*asio_service_.Start(5);*/ }
  void TearDown() { /*asio_service_.Stop();*/ }
};

INSTANTIATE_TEST_CASE_P(PivateAndOpenShareTests, PrivateOpenShareTests,
                        testing::Values(kMsOpenShare, kMsPrivateShare));


TEST_P(PrivateOpenShareTests, BEH_Share) {
  maidsafe::test::TestPath test_path(maidsafe::test::CreateTestPath());
  AsioService asio_service(5);
  std::shared_ptr<pcs::RemoteChunkStore> chunk_store;
  fs::path test_mount_dir;
  std::string unique_user_id(crypto::Hash<crypto::SHA512>(RandomString(8))), root_parent_id;
  asymm::Keys keys;
  int64_t max_space(1073741824), used_space(0), file_size(0);
  ASSERT_EQ(kSuccess, priv::utilities::CreateMaidsafeIdentity(keys));
  DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                   root_parent_id,
                                   keys,
                                   false,
                                   test_path,
                                   max_space,
                                   used_space,
                                   asio_service,
                                   chunk_store,
                                   test_mount_dir));
  ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

  // Create file on virtual drive
  fs::path dir0(CreateTestDirectory(test_mount_dir));
  fs::path dir1(CreateTestDirectory(dir0)),
           dir1_relative_path(RelativePath(test_mount_dir, dir1));
  fs::path new_dir(dir0);
  new_dir /= RandomAlphaNumericString(8);
  fs::path new_dir_relative_path(RelativePath(test_mount_dir, new_dir));
  fs::path dir2(CreateTestDirectory(dir1));
  fs::path file1(CreateTestFile(dir1, file_size)),
           file1_relative_path(RelativePath(test_mount_dir, file1));
  fs::path file2(CreateTestFile(dir2, file_size));
  boost::system::error_code error_code;
  EXPECT_TRUE(fs::exists(file1, error_code));
  EXPECT_EQ(error_code.value(), 0);
  EXPECT_TRUE(fs::exists(file2, error_code));
  EXPECT_EQ(error_code.value(), 0);

  // Try getting with invalid parameters
  std::string directory_id,
              share_id(RandomAlphaNumericString(64)),
              this_user_id(RandomAlphaNumericString(64));
  asymm::Keys share_keyring;
  maidsafe::asymm::GenerateKeyPair(&share_keyring);
  share_keyring.identity = RandomAlphaNumericString(64);
  EXPECT_EQ(kNullParameter, drive->SetShareDetails(dir1_relative_path,
                                                   share_id,
                                                   share_keyring,
                                                   this_user_id,
                                                   private_share_,
                                                   nullptr));
  EXPECT_EQ(kInvalidPath, drive->SetShareDetails("",
                                                 share_id,
                                                 share_keyring,
                                                 this_user_id,
                                                 private_share_,
                                                 &directory_id));
  EXPECT_EQ(kNoDirectoryId, drive->SetShareDetails(file1_relative_path,
                                                   share_id,
                                                   share_keyring,
                                                   this_user_id,
                                                   private_share_,
                                                   &directory_id));
  EXPECT_EQ(kFailedToGetMetaData,
            drive->SetShareDetails(dir1_relative_path / "Rubbish",
                                   share_id,
                                   share_keyring,
                                   this_user_id,
                                   private_share_,
                                   &directory_id));

  // Set to "not shared" (i.e. share status unchanged) by passing empty
  // share_id
  directory_id = "A";
  EXPECT_EQ(kSuccess, drive->SetShareDetails(dir1_relative_path,
                                             "",
                                             share_keyring,
                                             this_user_id,
                                             private_share_,
                                             &directory_id));
  EXPECT_TRUE(directory_id.empty());

  // Set to "shared"
  EXPECT_EQ(kSuccess, drive->SetShareDetails(dir1_relative_path,
                                             share_id,
                                             share_keyring,
                                             this_user_id,
                                             private_share_,
                                             &directory_id));
  EXPECT_FALSE(directory_id.empty());

  // Try inserting with invalid parameters
  EXPECT_EQ(kFailedToGetMetaData,
            drive->InsertShare(new_dir_relative_path / "Rubbish" / "Path",
                               this_user_id,
                               directory_id,
                               share_id,
                               share_keyring));
  EXPECT_EQ(kInvalidPath,
            drive->InsertShare("", this_user_id, directory_id,
                               share_id, share_keyring));
  EXPECT_EQ(kInvalidIds, drive->InsertShare(new_dir_relative_path,
                                            this_user_id,
                                            "Rubbish",
                                            share_id,
                                            share_keyring));
  EXPECT_EQ(kInvalidIds, drive->InsertShare(new_dir_relative_path,
                                            this_user_id,
                                            share_id,
                                            "Rubbish",
                                            share_keyring));
  EXPECT_EQ(kNoDirectoryId, drive->InsertShare(file1_relative_path / "Rubbish",
                                               this_user_id,
                                               directory_id,
                                               share_id,
                                               share_keyring));
  EXPECT_EQ(kInvalidPath, drive->InsertShare(dir1_relative_path,
                                             this_user_id,
                                             directory_id,
                                             share_id,
                                             share_keyring));

  // Store cached directory listings...
  EXPECT_EQ(kSuccess, drive->directory_listing_handler()->SaveCached(true));

  // ...
  asymm::Keys recovered_share_keyring;
  std::map<std::string, int> share_users_map;
  std::vector<ShareData> share_data_vector;
  fs::path root_share_path;
  // Remove shares...
  drive->directory_listing_handler()->share_keys().GetAll(share_data_vector);
  for (uint32_t i = 0; i != share_data_vector.size(); ++i) {
    // Get share details...
    DirectoryId directory_id;
    EXPECT_EQ(kSuccess, drive->GetShareDetails(share_data_vector[i].share_id,
                                               &root_share_path,
                                               &recovered_share_keyring,
                                               &directory_id,
                                               &share_users_map));
    // Remove the share...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(root_share_path,
                                               "",
                                               recovered_share_keyring,
                                               share_users_map.begin()->first,
                                               private_share_,
                                               &directory_id));
  }
  EXPECT_EQ(CalculateUsedSpace(test_mount_dir), drive->GetUsedSpace());
  UnmountDrive(drive, max_space, used_space);
}

TEST_P(PrivateOpenShareTests, BEH_SetShare) {
  maidsafe::test::TestPath test_path(maidsafe::test::CreateTestPath());
  AsioService asio_service(5);
  std::shared_ptr<pcs::RemoteChunkStore> chunk_store;
  fs::path test_mount_dir;
  std::string unique_user_id(crypto::Hash<crypto::SHA512>(RandomString(8))), root_parent_id;
  asymm::Keys keys;
  int64_t max_space(1073741824), used_space(0), file_size(0);
  ASSERT_EQ(kSuccess, priv::utilities::CreateMaidsafeIdentity(keys));
  DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                   root_parent_id,
                                   keys,
                                   false,
                                   test_path,
                                   max_space,
                                   used_space,
                                   asio_service,
                                   chunk_store,
                                   test_mount_dir));
  ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

  // Create a directory hierarchy...
  fs::path directory0(CreateTestDirectory(test_mount_dir)),
           directory1(CreateTestDirectory(directory0)),
           directory2(CreateTestDirectory(directory1)),
           directory3(CreateTestDirectory(directory2)),
           directory4(CreateTestDirectory(directory2)),
           directory5(CreateTestDirectory(directory4));

  fs::path directory3_relative_path(RelativePath(test_mount_dir, directory3)),
           directory1_relative_path(RelativePath(test_mount_dir, directory1)),
           directory4_relative_path(RelativePath(test_mount_dir, directory4));

  // Create a file in directory1...
  boost::system::error_code error_code;
  fs::path file1(CreateTestFile(directory1, file_size));
  EXPECT_TRUE(fs::exists(file1, error_code));
  EXPECT_EQ(error_code.value(), 0);

  // Create a file in directory3...
  fs::path file3(CreateTestFile(directory3, file_size));
  EXPECT_TRUE(fs::exists(file3, error_code));
  EXPECT_EQ(error_code.value(), 0);

  // Create a file in directory5...
  fs::path file5(CreateTestFile(directory5, file_size));
  EXPECT_TRUE(fs::exists(file5, error_code));
  EXPECT_EQ(error_code.value(), 0);

  std::string directory_id,
              update_directory_id(RandomAlphaNumericString(64)),
              first_share_id(RandomAlphaNumericString(64)),
              second_share_id(RandomAlphaNumericString(64)),
              first_user_id(drive->unique_user_id()),
              second_user_id(RandomAlphaNumericString(64)),
              third_user_id(RandomAlphaNumericString(64)),
              update_share_id(RandomAlphaNumericString(64));

  asymm::Keys first_share_keyring,
              second_share_keyring,
              recovered_share_keyring;
  maidsafe::asymm::GenerateKeyPair(&first_share_keyring);
  first_share_keyring.identity = RandomAlphaNumericString(64);
  maidsafe::asymm::GenerateKeyPair(&second_share_keyring);
  second_share_keyring.identity = RandomAlphaNumericString(64);

  std::map<std::string, int> share_users_map;
  std::vector<std::string> share_users_vector;
  std::vector<ShareData> share_data_vector;
  int has_admin_rights(kShareReadOnly);
  fs::path root_share_path;

  // Remove latent shares...
  drive->directory_listing_handler()->share_keys().GetAll(share_data_vector);
  for (size_t i(0); i != share_data_vector.size(); ++i) {
    DirectoryId directory_identity;
    // Get share details...
    EXPECT_EQ(kSuccess, drive->GetShareDetails(share_data_vector[i].share_id,
                                               &root_share_path,
                                               &recovered_share_keyring,
                                               &directory_identity,
                                               &share_users_map));
    // Remove the share...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(root_share_path,
                                               "",
                                               recovered_share_keyring,
                                               share_users_map.begin()->first,
                                               private_share_,
                                               &directory_identity));
  }

  // Set directory3 to shared...
  EXPECT_EQ(kSuccess, drive->SetShareDetails(directory3_relative_path,
                                             first_share_id,
                                             first_share_keyring,
                                             first_user_id,
                                             private_share_,
                                             &directory_id));
  share_users_map.insert(std::make_pair(second_user_id, kShareReadWrite));
  share_users_map.insert(std::make_pair(third_user_id, kShareReadOnly));
  EXPECT_EQ(kSuccess, drive->AddShareUsers(directory3_relative_path,
                                           share_users_map,
                                           private_share_));

  // Store cached directory listings...
  EXPECT_EQ(kSuccess, drive->directory_listing_handler()->SaveCached(true));

  // Get shares...
  share_data_vector.clear();
  drive->directory_listing_handler()->share_keys().GetAll(share_data_vector);
  EXPECT_EQ(1, share_data_vector.size());
  EXPECT_EQ(first_share_id, share_data_vector[0].share_id);
  EXPECT_EQ(first_share_keyring.identity,
            share_data_vector[0].keyring.identity);
#ifdef WIN32
  EXPECT_EQ(directory3, test_mount_dir.parent_path() /
                        share_data_vector[0].share_root_dir);
#else
  EXPECT_EQ(directory3, test_mount_dir / share_data_vector[0].share_root_dir);
#endif
  // Check a user's rights...
  EXPECT_EQ(kSuccess, drive->GetShareUsersRights(directory3_relative_path,
                                                 third_user_id,
                                                 &has_admin_rights));
  EXPECT_EQ(kShareReadOnlyUnConfirmed, has_admin_rights);
  // Change a user's rights...
  EXPECT_EQ(kSuccess, drive->SetShareUsersRights(directory3_relative_path,
                                                 third_user_id,
                                                 kShareReadWrite));
  EXPECT_EQ(kSuccess, drive->GetShareUsersRights(directory3_relative_path,
                                                 third_user_id,
                                                 &has_admin_rights));
  EXPECT_EQ(kShareReadWrite, has_admin_rights);

  // Try to set directory1 to shared...
  EXPECT_EQ(kShareAlreadyExistsInHierarchy,
            drive->SetShareDetails(directory1_relative_path,
                                   second_share_id,
                                   first_share_keyring,
                                   first_user_id,
                                   private_share_,
                                   &directory_id));
  boost::this_thread::sleep(boost::posix_time::milliseconds(10));
  // Try setting directory4 to shared using first_share_id as share id,
  // i.e. attempt to insert same id in shares_ set twice...
  EXPECT_EQ(kFailedToUpdateShareKeys,
            drive->SetShareDetails(directory4_relative_path,
                                   first_share_id,
                                   first_share_keyring,
                                   first_user_id,
                                   private_share_,
                                   &directory_id));
  // Set directory4 to shared...
  EXPECT_EQ(kSuccess, drive->SetShareDetails(directory4_relative_path,
                                             second_share_id,
                                             second_share_keyring,
                                             first_user_id,
                                             private_share_,
                                             &directory_id));
  share_users_map.clear();
  share_users_map.insert(std::make_pair(third_user_id, kShareReadOnly));
  EXPECT_EQ(kSuccess,
            drive->AddShareUsers(directory4_relative_path,
                                 share_users_map,
                                 private_share_));
  // Store cached directory listings...
  EXPECT_EQ(kSuccess, drive->directory_listing_handler()->SaveCached(true));
  // Get shares...
  drive->directory_listing_handler()->share_keys().GetAll(share_data_vector);
  EXPECT_EQ(2, share_data_vector.size());
  EXPECT_TRUE(first_share_id == share_data_vector[0].share_id ||
              second_share_id == share_data_vector[0].share_id);
  EXPECT_TRUE(first_share_id == share_data_vector[1].share_id ||
              second_share_id == share_data_vector[1].share_id);
  EXPECT_TRUE(first_share_keyring.identity ==
              share_data_vector[0].keyring.identity ||
              second_share_keyring.identity ==
              share_data_vector[0].keyring.identity);
  EXPECT_TRUE(first_share_keyring.identity ==
              share_data_vector[1].keyring.identity ||
              second_share_keyring.identity ==
              share_data_vector[1].keyring.identity);
#ifdef WIN32
  EXPECT_TRUE(directory3 == test_mount_dir.parent_path() /
                            share_data_vector[0].share_root_dir ||
              directory4 == test_mount_dir.parent_path() /
                            share_data_vector[0].share_root_dir);
  EXPECT_TRUE(directory3 == test_mount_dir.parent_path() /
                            share_data_vector[1].share_root_dir ||
              directory4 == test_mount_dir.parent_path() /
                            share_data_vector[1].share_root_dir);
#else
  EXPECT_TRUE(directory3 ==
                  test_mount_dir / share_data_vector[0].share_root_dir ||
              directory4 ==
                  test_mount_dir / share_data_vector[0].share_root_dir);
  EXPECT_TRUE(directory3 ==
                  test_mount_dir / share_data_vector[1].share_root_dir ||
              directory4 ==
                  test_mount_dir / share_data_vector[1].share_root_dir);
#endif
  // Remove users...
  share_users_vector.clear();
  share_users_vector.push_back(first_user_id);
  share_users_vector.push_back(third_user_id);
  // Remove share users from first share...
  EXPECT_EQ(kSuccess, drive->RemoveShareUsers(first_share_id,
                                              share_users_vector));
  // Remove all users from second share...
  EXPECT_EQ(kSuccess, drive->RemoveShareUsers(second_share_id,
                                              share_users_vector));
  // Get share details for first share...
  share_users_map.clear();
  EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                             &root_share_path,
                                             &recovered_share_keyring,
                                             &directory_id,
                                             &share_users_map));
#ifdef WIN32
  EXPECT_EQ(directory3, test_mount_dir.parent_path() / root_share_path);
#else
  EXPECT_EQ(directory3, test_mount_dir / root_share_path);
#endif
  EXPECT_EQ(first_share_keyring.identity, recovered_share_keyring.identity);
  EXPECT_EQ(1, share_users_map.size());
  EXPECT_EQ(second_user_id, share_users_map.begin()->first);
  EXPECT_EQ(CalculateUsedSpace(test_mount_dir), drive->GetUsedSpace());
  UnmountDrive(drive, max_space, used_space);
}

TEST_P(PrivateOpenShareTests, FUNC_InsertShare) {
  boost::system::error_code error_code;
  fs::path directory00, directory10, directory20, directory30, directory40,
           directory50, directory20_relative_path, directory61,
           directory61_relative_path;
  std::string directory_id, first_user_id, first_root_parent_id,
              update_directory_id(RandomAlphaNumericString(64)),
              first_share_id(RandomAlphaNumericString(64)),
              second_share_id(RandomAlphaNumericString(64)),
              second_user_id(crypto::Hash<crypto::SHA512>(RandomAlphaNumericString(64))),
              second_root_parent_id,
              third_user_id(RandomAlphaNumericString(64));

  asymm::Keys first_user_keyring,
              second_user_keyring,
              first_share_keyring,
              second_share_keyring,
              recovered_share_keyring;

  maidsafe::asymm::GenerateKeyPair(&first_share_keyring);
  first_share_keyring.identity = RandomAlphaNumericString(64);
  maidsafe::asymm::GenerateKeyPair(&second_share_keyring);
  second_share_keyring.identity = RandomAlphaNumericString(64);

  std::map<std::string, int> share_users_map;
  std::vector<ShareId> share_users_vector;
  fs::path root_share_path;
  int64_t max_space(1073741824), used_space1(0), used_space2(0);

  fs::path test_mount_dir;
  {  // user 1
    DrivePtr drive(CreateAndMountDrive("",
                                       &first_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    first_root_parent_id = drive->root_parent_id();
    // Create a directory hierarchy...
    directory00 = CreateTestDirectory(test_mount_dir);
    directory10 = CreateTestDirectory(directory00);
    directory20 = CreateTestDirectory(directory10);
    directory30 = CreateTestDirectory(directory20);
    directory40 = CreateTestDirectory(directory20);
    directory50 = CreateTestDirectory(directory40);
    directory20_relative_path = RelativePath(test_mount_dir, directory20);
    int64_t file_size(0);
    // Create a file in directory10...
    fs::path file10(CreateTestFile(directory10, file_size));
    EXPECT_TRUE(fs::exists(file10, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Create a file in directory30...
    fs::path file30(CreateTestFile(directory30, file_size));
    EXPECT_TRUE(fs::exists(file30, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Create a file in directory50...
    fs::path file50(CreateTestFile(directory50, file_size));
    EXPECT_TRUE(fs::exists(file50, error_code));
    EXPECT_EQ(error_code.value(), 0);

    // Set directory20 to shared...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory20_relative_path,
                                               first_share_id,
                                               first_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));
    share_users_map.insert(std::make_pair(second_user_id, kShareReadWrite));
    share_users_map.insert(std::make_pair(third_user_id, kShareReadOnly));
    EXPECT_EQ(kSuccess, drive->AddShareUsers(directory20_relative_path,
                                             share_users_map,
                                             private_share_));
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
  {  // user 2
    DrivePtr drive(CreateAndMountDrive("",
                                       &second_user_id,
                                       max_space,
                                       used_space2,
                                       &test_mount_dir,
                                       &second_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Save second user root parent id...
    second_root_parent_id = drive->root_parent_id();

    // Create a directory hierarchy...
    fs::path directory01(CreateTestDirectory(test_mount_dir)),
             directory11(CreateTestDirectory(directory01)),
             directory21(CreateTestDirectory(directory01)),
             directory31(CreateTestDirectory(directory01)),
             directory41(CreateTestDirectory(directory21)),
             directory51(CreateTestDirectory(directory41));
    directory61 = directory41 / RandomAlphaNumericString(5);
    directory61_relative_path = RelativePath(test_mount_dir, directory61);
    int64_t file_size(0);
    // Create a file in directory21...
    fs::path file21(CreateTestFile(directory21, file_size));
    EXPECT_TRUE(fs::exists(file21, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Create a file in directory31...
    fs::path file31(CreateTestFile(directory31, file_size));
    EXPECT_TRUE(fs::exists(file31, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Create a file in directory41...
    fs::path file41(CreateTestFile(directory41, file_size));
    EXPECT_TRUE(fs::exists(file41, error_code));
    EXPECT_EQ(error_code.value(), 0);

    // Insert share...
    EXPECT_EQ(kSuccess, drive->InsertShare(directory61_relative_path,
                                           first_user_id,
                                           directory_id,
                                           first_share_id,
                                           first_share_keyring));
    // Get details...
    share_users_map.clear();
    if (private_share_)
      EXPECT_EQ(kNoMsHidden, drive->GetShareDetails(first_share_id,
                                                    &root_share_path,
                                                    &recovered_share_keyring,
                                                    &directory_id,
                                                    &share_users_map));
    else
      EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                                &root_share_path,
                                                &recovered_share_keyring,
                                                &directory_id,
                                                &share_users_map));
#ifdef WIN32
    EXPECT_EQ(directory61, test_mount_dir.parent_path() / root_share_path);
#else
    EXPECT_EQ(directory61, test_mount_dir / root_share_path);
#endif
    EXPECT_EQ(first_share_keyring.identity, recovered_share_keyring.identity);
    if (private_share_) {
      EXPECT_EQ(0, share_users_map.size());
    } else {
      EXPECT_EQ(3, share_users_map.size());
      EXPECT_EQ(share_users_map.find(first_user_id)->first, first_user_id);
      EXPECT_EQ(share_users_map.find(first_user_id)->second, kShareOwner);
      EXPECT_EQ(share_users_map.find(second_user_id)->first, second_user_id);
      EXPECT_EQ(share_users_map.find(second_user_id)->second, kShareReadWriteUnConfirmed);
      EXPECT_EQ(share_users_map.find(third_user_id)->first, third_user_id);
      EXPECT_EQ(share_users_map.find(third_user_id)->second, kShareReadOnlyUnConfirmed);
    }
    // Unmount...
    UnmountDrive(drive, max_space, used_space2);
  }
  {  // user 1
    DrivePtr drive(CreateAndMountDrive(first_root_parent_id,
                                       &first_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

    // Reset directory20 details...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory20_relative_path,
                                               second_share_id,
                                               second_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));
    // Confirm a share user ...
    share_users_vector.clear();
    share_users_vector.push_back(second_user_id);
    EXPECT_EQ(kSuccess, drive->ConfirmShareUsers(second_share_id,
                                                 share_users_vector));
    // Remove a share user...
    share_users_vector.clear();
    share_users_vector.push_back(third_user_id);
    EXPECT_EQ(kSuccess, drive->RemoveShareUsers(second_share_id,
                                                share_users_vector));
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
  {  // user 2
    DrivePtr drive(CreateAndMountDrive(second_root_parent_id,
                                       &second_user_id,
                                       max_space,
                                       used_space2,
                                       &test_mount_dir,
                                       &second_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

    // Update share details for share...
    EXPECT_EQ(kSuccess, drive->UpdateShare(directory61_relative_path,
                                           first_share_id,
                                           &second_share_id,
                                           &directory_id,
                                           &second_share_keyring));
    // Get details...
    share_users_map.clear();
    if (private_share_)
      EXPECT_EQ(kNoMsHidden, drive->GetShareDetails(second_share_id,
                                                    &root_share_path,
                                                    &recovered_share_keyring,
                                                    &directory_id,
                                                    &share_users_map));
    else
      EXPECT_EQ(kSuccess, drive->GetShareDetails(second_share_id,
                                                &root_share_path,
                                                &recovered_share_keyring,
                                                &directory_id,
                                                &share_users_map));
#ifdef WIN32
    EXPECT_EQ(directory61, test_mount_dir.parent_path() / root_share_path);
#else
    EXPECT_EQ(directory61, test_mount_dir / root_share_path);
#endif
    EXPECT_EQ(second_share_keyring.identity, recovered_share_keyring.identity);
    if (private_share_) {
      EXPECT_EQ(0, share_users_map.size());
    } else {
      EXPECT_EQ(2, share_users_map.size());
      EXPECT_EQ(share_users_map.find(first_user_id)->first, first_user_id);
      EXPECT_EQ(share_users_map.find(first_user_id)->second, kShareOwner);
      EXPECT_EQ(share_users_map.find(second_user_id)->first, second_user_id);
      EXPECT_EQ(share_users_map.find(second_user_id)->second, kShareReadWrite);
    }
    // Unmount...
    UnmountDrive(drive, max_space, used_space2);
  }
}

TEST_P(PrivateOpenShareTests, BEH_RemoveShare) {
  std::string directory_id, first_user_id, first_root_parent_id,
              update_directory_id(RandomAlphaNumericString(64)),
              first_share_id(RandomAlphaNumericString(64)),
              second_share_id(RandomAlphaNumericString(64)),
              second_user_id(
                  crypto::Hash<crypto::SHA512>(RandomAlphaNumericString(64))),
              second_root_parent_id,
              third_user_id(RandomAlphaNumericString(64));

  asymm::Keys first_user_keyring, second_user_keyring,
              first_share_keyring, second_share_keyring,
              recovered_share_keyring;

  maidsafe::asymm::GenerateKeyPair(&first_share_keyring);
  first_share_keyring.identity = RandomAlphaNumericString(64);
  maidsafe::asymm::GenerateKeyPair(&second_share_keyring);
  second_share_keyring.identity = RandomAlphaNumericString(64);

  std::map<std::string, int> share_users_map;
  std::vector<ShareId> share_users_vector;
  fs::path root_share_path;
  fs::path directory00, directory10, directory20, directory10_relative_path,
           directory11, directory11_relative_path, file10, file20;
  fs::path test_mount_dir;
  int64_t max_space(1073741824), used_space1(0), used_space2(0);

  {  // user 1
    DrivePtr drive(CreateAndMountDrive("",
                                       &first_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    first_root_parent_id = drive->root_parent_id();

    directory00 = CreateTestDirectory(test_mount_dir);
    directory10 = CreateTestDirectory(directory00);
    directory20 = CreateTestDirectory(directory10);
    directory10_relative_path = RelativePath(test_mount_dir, directory10);
    int64_t file_size;
    // Create a file in directory10...
    boost::system::error_code error_code;
    file10 = CreateTestFile(directory10, file_size);
    EXPECT_TRUE(fs::exists(file10, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Create a file in directory20...
    file20 = CreateTestFile(directory20, file_size);
    EXPECT_TRUE(fs::exists(file20, error_code));
    EXPECT_EQ(error_code.value(), 0);

    // Set directory10 to shared...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory10_relative_path,
                                               first_share_id,
                                               first_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));

    share_users_map.insert(std::make_pair(second_user_id, kShareReadWrite));
    share_users_map.insert(std::make_pair(third_user_id, kShareReadOnly));
    EXPECT_EQ(kSuccess, drive->AddShareUsers(directory10_relative_path,
                                             share_users_map,
                                             private_share_));
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
  {  // user 2
    DrivePtr drive(CreateAndMountDrive("",
                                       &second_user_id,
                                       max_space,
                                       used_space2,
                                       &test_mount_dir,
                                       &second_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Save second user root parent id...
    second_root_parent_id = drive->root_parent_id();

    // Create a directory hierarchy...
    fs::path directory01(CreateTestDirectory(test_mount_dir));
    directory11 = directory01 / RandomAlphaNumericString(5);
    directory11_relative_path = RelativePath(test_mount_dir, directory11);
    EXPECT_FALSE(fs::exists(directory11)) << directory11;

    // Insert share...
    EXPECT_EQ(kSuccess, drive->InsertShare(directory11_relative_path,
                                           first_user_id,
                                           directory_id,
                                           first_share_id,
                                           first_share_keyring));
    // Get details...
    share_users_map.clear();
    if (private_share_)
      EXPECT_EQ(kNoMsHidden, drive->GetShareDetails(first_share_id,
                                                    &root_share_path,
                                                    &recovered_share_keyring,
                                                    &directory_id,
                                                    &share_users_map));
    else
      EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                                &root_share_path,
                                                &recovered_share_keyring,
                                                &directory_id,
                                                &share_users_map));
    EXPECT_TRUE(fs::exists(directory11)) << directory11;
#ifdef WIN32
    EXPECT_EQ(directory11, test_mount_dir.parent_path() / root_share_path);
#else
    EXPECT_EQ(directory11, test_mount_dir / root_share_path);
#endif
    EXPECT_EQ(first_share_keyring.identity, recovered_share_keyring.identity);
    if (private_share_) {
      EXPECT_EQ(0, share_users_map.size());
    } else {
      EXPECT_EQ(3, share_users_map.size());
      EXPECT_EQ(share_users_map.find(first_user_id)->first, first_user_id);
      EXPECT_EQ(share_users_map.find(first_user_id)->second, kShareOwner);
      EXPECT_EQ(share_users_map.find(second_user_id)->first, second_user_id);
      EXPECT_EQ(share_users_map.find(second_user_id)->second, kShareReadWriteUnConfirmed);
      EXPECT_EQ(share_users_map.find(third_user_id)->first, third_user_id);
      EXPECT_EQ(share_users_map.find(third_user_id)->second, kShareReadOnlyUnConfirmed);
    }
    // Unmount...
    UnmountDrive(drive, max_space, used_space2);
  }
  {  // user 1
    DrivePtr drive(CreateAndMountDrive(first_root_parent_id,
                                       &first_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

    // Reset directory10 details...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory10_relative_path,
                                               "",
                                               first_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
  {  // user 2
    DrivePtr drive(CreateAndMountDrive(second_root_parent_id,
                                       &second_user_id,
                                       max_space,
                                       used_space2,
                                       &test_mount_dir,
                                       &second_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

    // Remove share...
    boost::system::error_code ec;
    EXPECT_TRUE(fs::exists(directory11, ec)) << directory11;
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(kSuccess, drive->RemoveShare(directory11_relative_path));

    while (fs::exists(directory11, ec))
      Sleep(bptime::milliseconds(1000));

    EXPECT_FALSE(fs::exists(directory11, ec)) << directory11;
    EXPECT_NE(0, ec.value());

    // Unmount...
    UnmountDrive(drive, max_space, used_space2);
  }
  {  // user 1
    DrivePtr drive(CreateAndMountDrive(first_root_parent_id,
                                       &first_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

    EXPECT_TRUE(fs::exists(directory10)) << directory10;
    EXPECT_TRUE(fs::exists(directory20)) << directory20;
    EXPECT_TRUE(fs::exists(file10)) << file10;
    EXPECT_TRUE(fs::exists(file20)) << file20;

    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
}

TEST_P(PrivateOpenShareTests, FUNC_RemoveUser) {
  std::string first_unique_user_id, second_unique_user_id,
              first_root_parent_id, second_root_parent_id,
              first_user_id(RandomAlphaNumericString(8)),
              second_user_id(RandomAlphaNumericString(8)),
              first_share_id(RandomAlphaNumericString(64)),
              updated_share_id(RandomAlphaNumericString(64)),
              directory_id;

  asymm::Keys first_user_keyring, second_user_keyring,
              first_share_keyring, updated_share_keyring,
              second_share_keyring, recovered_share_keyring;
  maidsafe::asymm::GenerateKeyPair(&first_share_keyring);
  first_share_keyring.identity = RandomAlphaNumericString(64);
  maidsafe::asymm::GenerateKeyPair(&updated_share_keyring);
  updated_share_keyring.identity = RandomAlphaNumericString(64);
  second_share_keyring.identity = first_share_keyring.identity;

  std::map<std::string, int> share_users_map;
  std::vector<std::string> user_ids;

  fs::path directory00, directory00_relative_path,
           subdirectory00, subdirectory00_relative_path,
           directory10, directory10_relative_path,
           subdirectory10, root_share_path;
  fs::path test_mount_dir;
  int64_t max_space(1073741824), used_space1(0), used_space2(0);
  boost::system::error_code error_code;

  {  // user 1
    DrivePtr drive(CreateAndMountDrive("",
                                       &first_unique_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Save first user root parent id...
    first_root_parent_id = drive->root_parent_id();
    // Create a directory...
    directory00 = CreateTestDirectory(test_mount_dir);
    directory00_relative_path = RelativePath(test_mount_dir, directory00);
    // Set directory00 to shared...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory00_relative_path,
                                               first_share_id,
                                               first_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));
    share_users_map.insert(std::make_pair(second_user_id, kShareReadOnly));
    EXPECT_EQ(kSuccess, drive->AddShareUsers(directory00_relative_path,
                                             share_users_map,
                                             private_share_));
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }

  {  // user 2
    DrivePtr drive(CreateAndMountDrive("",
                                       &second_unique_user_id,
                                       max_space,
                                       used_space2,
                                       &test_mount_dir,
                                       &second_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Save second user root parent id...
    second_root_parent_id = drive->root_parent_id();
    // Set the share path...
    directory10 = test_mount_dir / directory00.filename();
    directory10_relative_path = RelativePath(test_mount_dir, directory10);
    EXPECT_FALSE(fs::exists(directory10)) << directory10;
    // Insert share...
    EXPECT_EQ(kSuccess, drive->InsertShare(directory10_relative_path,
                                           first_user_id,
                                           directory_id,
                                           first_share_id,
                                           second_share_keyring));
    // Creating a directory should be disallowed...
    subdirectory10 = directory10 / RandomAlphaNumericString(5);
    EXPECT_FALSE(fs::exists(subdirectory10, error_code)) << subdirectory10;
    fs::create_directory(subdirectory10, error_code);
    EXPECT_NE(0, error_code.value());
    EXPECT_FALSE(fs::exists(subdirectory10, error_code)) << subdirectory10;
    // Get details...
    share_users_map.clear();
    if (private_share_)
      EXPECT_EQ(kNoMsHidden, drive->GetShareDetails(first_share_id,
                                                    &root_share_path,
                                                    &recovered_share_keyring,
                                                    &directory_id,
                                                    &share_users_map));
    else
      EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                                &root_share_path,
                                                &recovered_share_keyring,
                                                &directory_id,
                                                &share_users_map));
    EXPECT_TRUE(fs::exists(directory10)) << directory10;
#ifdef WIN32
    EXPECT_EQ(directory10, test_mount_dir.parent_path() / root_share_path);
#else
    EXPECT_EQ(directory10, test_mount_dir / root_share_path);
#endif
    EXPECT_EQ(first_share_keyring.identity, recovered_share_keyring.identity);
    if (private_share_) {
      EXPECT_EQ(0, share_users_map.size());
    } else {
      EXPECT_EQ(2, share_users_map.size());
      EXPECT_EQ(share_users_map.find(first_user_id)->first, first_user_id);
      EXPECT_EQ(share_users_map.find(first_user_id)->second, kShareOwner);
      EXPECT_EQ(share_users_map.find(second_user_id)->first, second_user_id);
      EXPECT_EQ(share_users_map.find(second_user_id)->second, kShareReadOnlyUnConfirmed);
    }
    // Unmount...
    UnmountDrive(drive, max_space, used_space2);
  }

  {  // user 1
    DrivePtr drive(CreateAndMountDrive(first_root_parent_id,
                                       &first_unique_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    EXPECT_TRUE(fs::exists(directory00, error_code)) << directory00;
    // Creating a directory should be allowed...
    subdirectory00 = directory00 / RandomAlphaNumericString(5);
    EXPECT_FALSE(fs::exists(subdirectory00, error_code)) << subdirectory00;
    fs::create_directory(subdirectory00, error_code);
    EXPECT_EQ(0, error_code.value());
    EXPECT_TRUE(fs::exists(subdirectory00, error_code)) << subdirectory00;
    // Remove second user...
    user_ids.push_back(second_user_id);
    share_users_map.clear();
    EXPECT_EQ(kSuccess, drive->RemoveShareUsers(first_share_id,
                                                user_ids));
    EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                               &root_share_path,
                                               &recovered_share_keyring,
                                               &directory_id,
                                               &share_users_map));
    EXPECT_EQ(1, share_users_map.size());
    // Reset share details for directory00...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory00_relative_path,
                                               updated_share_id,
                                               updated_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));
    // Get details...
    share_users_map.clear();
    EXPECT_EQ(kSuccess, drive->GetShareDetails(updated_share_id,
                                               &root_share_path,
                                               &recovered_share_keyring,
                                               &directory_id,
                                               &share_users_map));
    EXPECT_TRUE(fs::exists(directory00)) << directory00;
#ifdef WIN32
    EXPECT_EQ(directory00, test_mount_dir.parent_path() / root_share_path);
#else
    EXPECT_EQ(directory00, test_mount_dir / root_share_path);
#endif
    EXPECT_EQ(updated_share_keyring.identity, recovered_share_keyring.identity);
    EXPECT_EQ(1, share_users_map.size());
    EXPECT_EQ(share_users_map.find(first_user_id)->first, first_user_id);
    EXPECT_EQ(share_users_map.find(first_user_id)->second, kShareOwner);
    EXPECT_EQ(CalculateUsedSpace(test_mount_dir), drive->GetUsedSpace());
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }

  {  // user 2
    DrivePtr drive(CreateAndMountDrive(second_root_parent_id,
                                       &second_unique_user_id,
                                       max_space,
                                       used_space2,
                                       &test_mount_dir,
                                       &second_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    EXPECT_TRUE(fs::exists(directory10, error_code)) << directory10;
    EXPECT_EQ(kSuccess, drive->RemoveShare(directory10_relative_path));

    while (fs::exists(directory10, error_code))
      Sleep(bptime::milliseconds(1000));

    EXPECT_FALSE(fs::exists(directory10, error_code)) << directory10;
    ASSERT_NE(0, error_code.value());
    // Unmount...
    UnmountDrive(drive, max_space, used_space2);
  }

  {  // user 1
    DrivePtr drive(CreateAndMountDrive(first_root_parent_id,
                                       &first_unique_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    EXPECT_TRUE(fs::exists(directory00, error_code)) << directory00;
    EXPECT_TRUE(fs::exists(subdirectory00, error_code)) << subdirectory00;
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
}

TEST_P(PrivateOpenShareTests, BEH_ShareUserRights) {
  std::string first_unique_user_id, second_unique_user_id,
              third_unique_user_id, first_root_parent_id,
              first_user_id(RandomAlphaNumericString(8)),
              second_user_id(RandomAlphaNumericString(8)),
              third_user_id(RandomAlphaNumericString(8)),
              first_share_id(RandomAlphaNumericString(64)),
              directory_id, first_user_copy, second_user_copy, third_user_copy,
              unmodified_third_user_copy;

  asymm::Keys first_user_keyring, second_user_keyring, third_user_keyring,
              first_share_keyring, third_share_keyring,
              recovered_share_keyring;
  maidsafe::asymm::GenerateKeyPair(&first_share_keyring);
  first_share_keyring.identity = RandomAlphaNumericString(64);
  third_share_keyring.identity = first_share_keyring.identity;

  std::map<std::string, int> share_users_map;
  std::vector<ShareId> share_users_vector;

  fs::path directory00, directory10, directory20, directory10_relative_path,
           file10, root_share_path;
  fs::path test_mount_dir;
  int64_t max_space(1073741824), used_space1(0), used_space2(0), used_space3(0), file_size(0);
  boost::system::error_code error_code;

  {  // user 1
    DrivePtr drive(CreateAndMountDrive("",
                                       &first_unique_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Save first user root parent id...
    first_root_parent_id = drive->root_parent_id();

    directory00 = CreateTestDirectory(test_mount_dir);
    directory10 = CreateTestDirectory(directory00);
    directory20 = CreateTestDirectory(directory10);
    directory10_relative_path = RelativePath(test_mount_dir, directory10);
    // Create a file in directory10...
    file10 = CreateTestFile(directory10, file_size);
    EXPECT_TRUE(fs::exists(file10, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Create a file in directory20...
    fs::path file20(CreateTestFile(directory20, file_size));
    EXPECT_TRUE(fs::exists(file20, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Copy file10 to mirror...
    first_user_copy = file10.filename().string() + ".first_user_copy";
    fs::copy_file(file10, *main_test_dir_ / first_user_copy);
    EXPECT_TRUE(fs::exists(*main_test_dir_ / first_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Set directory10 to shared...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory10_relative_path,
                                               first_share_id,
                                               first_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));
    share_users_map.insert(std::make_pair(second_user_id, kShareReadWrite));
    share_users_map.insert(std::make_pair(third_user_id, kShareReadOnly));
    EXPECT_EQ(kSuccess, drive->AddShareUsers(directory10_relative_path,
                                             share_users_map,
                                             private_share_));
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }

  {  // user 2
    DrivePtr drive(CreateAndMountDrive("",
                                       &second_unique_user_id,
                                       max_space,
                                       used_space2,
                                       &test_mount_dir,
                                       &second_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Create a directory hierarchy...
    fs::path directory01(CreateTestDirectory(test_mount_dir)),
             directory11(directory01 / RandomAlphaNumericString(5)),
             directory11_relative_path(RelativePath(test_mount_dir, directory11));
    EXPECT_FALSE(fs::exists(directory11)) << directory11;
    // Insert share...
    EXPECT_EQ(kSuccess, drive->InsertShare(directory11_relative_path,
                                           first_user_id,
                                           directory_id,
                                           first_share_id,
                                           first_share_keyring));
    // Get details...
    share_users_map.clear();
    if (private_share_)
      EXPECT_EQ(kNoMsHidden, drive->GetShareDetails(first_share_id,
                                                    &root_share_path,
                                                    &recovered_share_keyring,
                                                    &directory_id,
                                                    &share_users_map));
    else
      EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                                &root_share_path,
                                                &recovered_share_keyring,
                                                &directory_id,
                                                &share_users_map));
    EXPECT_TRUE(fs::exists(directory11)) << directory11;
#ifdef WIN32
    EXPECT_EQ(directory11, test_mount_dir.parent_path() / root_share_path);
#else
    EXPECT_EQ(directory11, test_mount_dir / root_share_path);
#endif
    EXPECT_EQ(first_share_keyring.identity, recovered_share_keyring.identity);
    if (private_share_) {
      EXPECT_EQ(0, share_users_map.size());
    } else {
      EXPECT_EQ(3, share_users_map.size());
      EXPECT_EQ(share_users_map.find(first_user_id)->first, first_user_id);
      EXPECT_EQ(share_users_map.find(first_user_id)->second, kShareOwner);
      EXPECT_EQ(share_users_map.find(second_user_id)->first, second_user_id);
      EXPECT_EQ(share_users_map.find(second_user_id)->second, kShareReadWriteUnConfirmed);
      EXPECT_EQ(share_users_map.find(third_user_id)->first, third_user_id);
      EXPECT_EQ(share_users_map.find(third_user_id)->second, kShareReadOnlyUnConfirmed);
    }
    // Test second user's rights...
    // Read file10...
    second_user_copy = file10.filename().string() + ".second_user_copy";
    fs::path second_user_file10(directory11 / file10.filename());
    EXPECT_TRUE(fs::exists(second_user_file10)) << second_user_file10;
    fs::copy_file(second_user_file10, *main_test_dir_ / second_user_copy);
    EXPECT_TRUE(fs::exists(*main_test_dir_ / second_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Compare contents...
    EXPECT_TRUE(SameFileContents(*main_test_dir_ / first_user_copy,
                                 *main_test_dir_ / second_user_copy));
    // Write to file10...
    EXPECT_TRUE(ModifyFile(second_user_file10, file_size));
    fs::copy_file(second_user_file10, *main_test_dir_ / second_user_copy,
                  fs::copy_option::overwrite_if_exists, error_code);
    EXPECT_EQ(error_code.value(), 0);
    // Unmount...
    UnmountDrive(drive, max_space, used_space2);
  }

  {  // user 3
    DrivePtr drive(CreateAndMountDrive("",
                                       &third_unique_user_id,
                                       max_space,
                                       used_space3,
                                       &test_mount_dir,
                                       &third_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Create a directory hierarchy...
    fs::path directory02(CreateTestDirectory(test_mount_dir)),
             directory12(directory02 / RandomAlphaNumericString(5)),
             directory12_relative_path(RelativePath(test_mount_dir, directory12));
    EXPECT_FALSE(fs::exists(directory12)) << directory12;
    // Insert share...
    EXPECT_EQ(kSuccess, drive->InsertShare(directory12_relative_path,
                                           first_user_id,
                                           directory_id,
                                           first_share_id,
                                           third_share_keyring));
    // Get details...
    share_users_map.clear();
    if (private_share_)
      EXPECT_EQ(kNoMsHidden, drive->GetShareDetails(first_share_id,
                                                    &root_share_path,
                                                    &recovered_share_keyring,
                                                    &directory_id,
                                                    &share_users_map));
    else
      EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                                &root_share_path,
                                                &recovered_share_keyring,
                                                &directory_id,
                                                &share_users_map));
    EXPECT_TRUE(fs::exists(directory12)) << directory12;
#ifdef WIN32
    EXPECT_EQ(directory12, test_mount_dir.parent_path() / root_share_path);
#else
    EXPECT_EQ(directory12, test_mount_dir / root_share_path);
#endif
    EXPECT_EQ(third_share_keyring.identity, recovered_share_keyring.identity);
    if (private_share_) {
      EXPECT_EQ(0, share_users_map.size());
    } else {
      EXPECT_EQ(3, share_users_map.size());
      EXPECT_EQ(share_users_map.find(first_user_id)->first, first_user_id);
      EXPECT_EQ(share_users_map.find(first_user_id)->second, kShareOwner);
      EXPECT_EQ(share_users_map.find(second_user_id)->first, second_user_id);
      EXPECT_EQ(share_users_map.find(second_user_id)->second, kShareReadWriteUnConfirmed);
      EXPECT_EQ(share_users_map.find(third_user_id)->first, third_user_id);
      EXPECT_EQ(share_users_map.find(third_user_id)->second, kShareReadOnlyUnConfirmed);
    }
    // Test third user's rights...
    // Read file10...
    third_user_copy = file10.filename().string() + ".third_user_copy";
    unmodified_third_user_copy = file10.filename().string() + ".unmodified_third_user_copy";
    fs::path third_user_file10(directory12 / file10.filename());
    EXPECT_TRUE(fs::exists(third_user_file10, error_code)) << third_user_file10;
    fs::copy_file(third_user_file10, *main_test_dir_ / third_user_copy,
                  error_code);
    EXPECT_TRUE(fs::exists(*main_test_dir_ / third_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);

    // Compare contents...
    EXPECT_TRUE(SameFileContents(*main_test_dir_ / third_user_copy,
                                 *main_test_dir_ / second_user_copy));
    // Try to write to file10...
    EXPECT_FALSE(ModifyFile(third_user_file10, file_size)) << third_user_file10;

    // Copy the file again...
    fs::copy_file(third_user_file10, *main_test_dir_ / unmodified_third_user_copy, error_code);
    EXPECT_EQ(error_code.value(), 0);
    // Compare contents...
    EXPECT_TRUE(SameFileContents(*main_test_dir_ / unmodified_third_user_copy,
                                 *main_test_dir_ / second_user_copy));
    // Unmount...
    UnmountDrive(drive, max_space, used_space3);
  }

  {  // user 1
    DrivePtr drive(CreateAndMountDrive(first_root_parent_id,
                                       &first_unique_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // First user...
    // Read file10...
    EXPECT_TRUE(fs::exists(*main_test_dir_ / first_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Compare contents...
    EXPECT_FALSE(SameFileContents(*main_test_dir_ / first_user_copy,
                                  *main_test_dir_ / second_user_copy));
    // Copy the file again...
    fs::copy_file(file10, *main_test_dir_ / first_user_copy,
                  fs::copy_option::overwrite_if_exists, error_code);
    EXPECT_EQ(error_code.value(), 0);
    // Compare contents...
    EXPECT_TRUE(SameFileContents(*main_test_dir_ / first_user_copy,
                                 *main_test_dir_ / second_user_copy));
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
}

TEST_P(PrivateOpenShareTests, BEH_InsertShareExisted) {
  std::string first_unique_user_id, second_unique_user_id,
              first_root_parent_id,
              first_user_id(RandomAlphaNumericString(8)),
              second_user_id(RandomAlphaNumericString(8)),
              first_share_id(RandomAlphaNumericString(64)),
              directory_id, first_user_copy, second_user_copy;

  asymm::Keys first_user_keyring, second_user_keyring, first_share_keyring,
              recovered_share_keyring;
  maidsafe::asymm::GenerateKeyPair(&first_share_keyring);
  first_share_keyring.identity = RandomAlphaNumericString(64);

  std::map<std::string, int> share_users_map;
  std::vector<ShareId> share_users_vector;

  fs::path directory00, directory10, directory20, directory10_relative_path,
           file10, root_share_path;
  fs::path test_mount_dir;
  int64_t max_space(1073741824), used_space1(0), used_space2(0);
  boost::system::error_code error_code;

  {  // user 1
    DrivePtr drive(CreateAndMountDrive("",
                                       &first_unique_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Save first user root parent id...
    first_root_parent_id = drive->root_parent_id();

    directory00 = CreateTestDirectory(test_mount_dir);
    directory10 = CreateTestDirectory(directory00);
    directory20 = CreateTestDirectory(directory10);
    directory10_relative_path = RelativePath(test_mount_dir, directory10);
    int64_t file_size;
    // Create a file in directory10...
    file10 = CreateTestFile(directory10, file_size);
    EXPECT_TRUE(fs::exists(file10, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Create a file in directory20...
    fs::path file20(CreateTestFile(directory20, file_size));
    EXPECT_TRUE(fs::exists(file20, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Copy file10 to mirror...
    first_user_copy = file10.filename().string() + ".first_user_copy";
    fs::copy_file(file10, *main_test_dir_ / first_user_copy);
    EXPECT_TRUE(fs::exists(*main_test_dir_ / first_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Set directory10 to shared...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory10_relative_path,
                                               first_share_id,
                                               first_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));
    share_users_map.insert(std::make_pair(second_user_id, kShareReadWrite));
    EXPECT_EQ(kSuccess, drive->AddShareUsers(directory10_relative_path,
                                             share_users_map,
                                             private_share_));
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
  {  // user 2
    DrivePtr drive(CreateAndMountDrive("",
                                       &second_unique_user_id,
                                       max_space,
                                       used_space2,
                                       &test_mount_dir,
                                       &second_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Create a directory hierarchy...
    fs::path directory01(CreateTestDirectory(test_mount_dir));
    fs::path directory(directory01 / directory10_relative_path.filename());
    boost::system::error_code error_code;
    EXPECT_TRUE(fs::create_directories(directory, error_code)) << directory
                << ": " << error_code.message();
    fs::path directory_relative(RelativePath(test_mount_dir, directory));

    // Insert share...
    EXPECT_EQ(kInvalidPath,
              drive->InsertShare(directory_relative, first_user_id,
                                 directory_id, first_share_id,
                                 first_share_keyring));
    std::string new_path_file(directory10_relative_path.filename().string() +
                              RandomAlphaNumericString(5));
    fs::path new_path(directory01 / new_path_file);
    fs::path new_path_relative(RelativePath(test_mount_dir, new_path));
    EXPECT_EQ(kSuccess,
              drive->InsertShare(new_path_relative, first_user_id,
                                 directory_id, first_share_id,
                                 first_share_keyring));
    EXPECT_TRUE(fs::exists(new_path)) << new_path;
    // Get details...
    share_users_map.clear();
    if (private_share_)
      EXPECT_EQ(kNoMsHidden, drive->GetShareDetails(first_share_id,
                                                    &root_share_path,
                                                    &recovered_share_keyring,
                                                    &directory_id,
                                                    &share_users_map));
    else
      EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                                &root_share_path,
                                                &recovered_share_keyring,
                                                &directory_id,
                                                &share_users_map));
    EXPECT_EQ(new_path_relative, root_share_path);
    EXPECT_EQ(first_share_keyring.identity, recovered_share_keyring.identity);
    if (private_share_) {
      EXPECT_EQ(0, share_users_map.size());
    } else {
      EXPECT_EQ(2, share_users_map.size());
      EXPECT_EQ(share_users_map.find(first_user_id)->first, first_user_id);
      EXPECT_EQ(share_users_map.find(first_user_id)->second, kShareOwner);
      EXPECT_EQ(share_users_map.find(second_user_id)->first, second_user_id);
      EXPECT_EQ(share_users_map.find(second_user_id)->second, kShareReadWriteUnConfirmed);
    }
    // Test second user's rights...
    // Read file10...
    second_user_copy = file10.filename().string() + ".second_user_copy";
    fs::path second_user_file10(new_path / file10.filename());
    EXPECT_TRUE(fs::exists(second_user_file10)) << second_user_file10;
    fs::copy_file(second_user_file10, *main_test_dir_ / second_user_copy);
    EXPECT_TRUE(fs::exists(*main_test_dir_ / second_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Compare contents...
    EXPECT_TRUE(SameFileContents(*main_test_dir_ / first_user_copy,
                                 *main_test_dir_ / second_user_copy));
    int64_t file_size(0);
    // Write to file10...
    EXPECT_TRUE(ModifyFile(second_user_file10, file_size));
    fs::copy_file(second_user_file10, *main_test_dir_ / second_user_copy,
                  fs::copy_option::overwrite_if_exists, error_code);
    EXPECT_EQ(error_code.value(), 0);
    EXPECT_EQ(fs::file_size(second_user_file10, error_code), file_size);
    EXPECT_EQ(error_code.value(), 0);
    // Unmount...
    UnmountDrive(drive, max_space, used_space2);
  }
  {  // user 1
    DrivePtr drive(CreateAndMountDrive(first_root_parent_id,
                                       &first_unique_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // First user...
    // Read file10...
    EXPECT_TRUE(fs::exists(*main_test_dir_ / first_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Compare contents...
    EXPECT_FALSE(SameFileContents(*main_test_dir_ / first_user_copy,
                                  *main_test_dir_ / second_user_copy));
    // Copy the file again...
    fs::copy_file(file10, *main_test_dir_ / first_user_copy,
                  fs::copy_option::overwrite_if_exists, error_code);
    EXPECT_EQ(error_code.value(), 0);
    // Compare contents...
    EXPECT_TRUE(SameFileContents(*main_test_dir_ / first_user_copy,
                                 *main_test_dir_ / second_user_copy));
    // Get details...
    share_users_map.clear();
    EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                               &root_share_path,
                                               &recovered_share_keyring,
                                               &directory_id,
                                               &share_users_map));
    EXPECT_EQ(directory10_relative_path, root_share_path);
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
}

TEST_P(PrivateOpenShareTests, BEH_UserRenameShare) {
  std::string first_unique_user_id, second_unique_user_id,
              first_root_parent_id,
              first_user_id(RandomAlphaNumericString(8)),
              second_user_id(RandomAlphaNumericString(8)),
              first_share_id(RandomAlphaNumericString(64)),
              directory_id, first_user_copy, second_user_copy;

  asymm::Keys first_user_keyring, second_user_keyring, first_share_keyring,
              recovered_share_keyring;
  maidsafe::asymm::GenerateKeyPair(&first_share_keyring);
  first_share_keyring.identity = RandomAlphaNumericString(64);

  std::map<std::string, int> share_users_map;
  std::vector<ShareId> share_users_vector;

  fs::path directory00, directory10, directory20, directory10_relative_path,
           file10, root_share_path;
  fs::path test_mount_dir;
  int64_t max_space(1073741824), used_space1(0), used_space2(0);
  boost::system::error_code error_code;

  {  // user 1
    DrivePtr drive(CreateAndMountDrive("",
                                       &first_unique_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Save first user root parent id...
    first_root_parent_id = drive->root_parent_id();
    // Create some directories...
    directory00 = CreateTestDirectory(test_mount_dir);
    directory10 = CreateTestDirectory(directory00);
    directory20 = CreateTestDirectory(directory10);
    directory10_relative_path = RelativePath(test_mount_dir, directory10);
    int64_t file_size;
    // Create a file in directory10...
    file10 = CreateTestFile(directory10, file_size);
    EXPECT_TRUE(fs::exists(file10, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Create a file in directory20...
    fs::path file20(CreateTestFile(directory20, file_size));
    EXPECT_TRUE(fs::exists(file20, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Copy file10 off drive...
    first_user_copy = file10.filename().string() + ".first_user_copy";
    fs::copy_file(file10, *main_test_dir_ / first_user_copy);
    EXPECT_TRUE(fs::exists(*main_test_dir_ / first_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Set directory10 to shared...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory10_relative_path,
                                               first_share_id,
                                               first_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));
    share_users_map.insert(std::make_pair(second_user_id, kShareReadWrite));
    EXPECT_EQ(kSuccess, drive->AddShareUsers(directory10_relative_path,
                                             share_users_map,
                                             private_share_));
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
  {  // user 2
    DrivePtr drive(CreateAndMountDrive("",
                                       &second_unique_user_id,
                                       max_space,
                                       used_space2,
                                       &test_mount_dir,
                                       &second_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    signaled_ = false;
    bs2::connection connection(drive->ConnectToShareRenamed(
        std::bind(&PrivateOpenShareTests::ShareRenamedSlot,
                  this, args::_1, args::_2)));
    // Create a directory hierarchy...
    fs::path directory01(CreateTestDirectory(test_mount_dir));
    fs::path directory(directory01 / directory10_relative_path.filename());
    fs::path directory_relative(RelativePath(test_mount_dir, directory));
    // Insert share...
    EXPECT_EQ(kSuccess,
              drive->InsertShare(directory_relative,
                                 first_user_id,
                                 directory_id,
                                 first_share_id,
                                 first_share_keyring));
    // Read file10...
    second_user_copy = file10.filename().string() + ".second_user_copy";
    fs::path second_user_file10(directory / file10.filename());
    EXPECT_TRUE(fs::exists(second_user_file10)) << second_user_file10;
    fs::copy_file(second_user_file10, *main_test_dir_ / second_user_copy);
    // Compare contents...
    EXPECT_TRUE(SameFileContents(*main_test_dir_ / first_user_copy,
                                 *main_test_dir_ / second_user_copy));
    // Rename the share directory
    std::string new_share_name(directory10_relative_path.filename().string() +
                               RandomAlphaNumericString(5));
    fs::path new_path(directory01 / new_share_name);
    fs::path new_path_relative(RelativePath(test_mount_dir, new_path));
    fs::rename(directory, new_path, error_code);
    EXPECT_EQ(0, error_code.value());
    while (!signaled_)
      Sleep(bptime::milliseconds(100));
    second_user_file10 = new_path / file10.filename();
    EXPECT_TRUE(fs::exists(second_user_file10)) << second_user_file10;
    // Get details...
    share_users_map.clear();
    if (private_share_)
      EXPECT_EQ(kNoMsHidden, drive->GetShareDetails(first_share_id,
                                                    &root_share_path,
                                                    &recovered_share_keyring,
                                                    &directory_id,
                                                    &share_users_map));
    else
      EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                                 &root_share_path,
                                                 &recovered_share_keyring,
                                                 &directory_id,
                                                 &share_users_map));
    EXPECT_EQ(new_path_relative, root_share_path);
    EXPECT_EQ(first_share_keyring.identity, recovered_share_keyring.identity);
    if (private_share_) {
      EXPECT_EQ(0, share_users_map.size());
    } else {
      EXPECT_EQ(2, share_users_map.size());
      EXPECT_EQ(share_users_map.find(first_user_id)->first, first_user_id);
      EXPECT_EQ(share_users_map.find(first_user_id)->second, kShareOwner);
      EXPECT_EQ(share_users_map.find(second_user_id)->first, second_user_id);
      EXPECT_EQ(share_users_map.find(second_user_id)->second, kShareReadWriteUnConfirmed);
    }
    int64_t file_size(0);
    // Write to file10...
    EXPECT_TRUE(ModifyFile(second_user_file10, file_size));
    fs::copy_file(second_user_file10, *main_test_dir_ / second_user_copy,
                  fs::copy_option::overwrite_if_exists, error_code);
    EXPECT_EQ(error_code.value(), 0);
    // Unmount...
    UnmountDrive(drive, max_space, used_space2);
  }
  {  // user 1
    DrivePtr drive(CreateAndMountDrive(first_root_parent_id,
                                       &first_unique_user_id,
                                       max_space,
                                       used_space1,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // First user...
    // Read file10...
    EXPECT_TRUE(fs::exists(*main_test_dir_ / first_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Compare contents...
    EXPECT_FALSE(SameFileContents(*main_test_dir_ / first_user_copy,
                                  *main_test_dir_ / second_user_copy));
    // Copy the file again...
    fs::copy_file(file10, *main_test_dir_ / first_user_copy,
                  fs::copy_option::overwrite_if_exists, error_code);
    EXPECT_EQ(error_code.value(), 0);
    // Compare contents...
    EXPECT_TRUE(SameFileContents(*main_test_dir_ / first_user_copy,
                                 *main_test_dir_ / second_user_copy));
    // Get details...
    share_users_map.clear();
    EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                               &root_share_path,
                                               &recovered_share_keyring,
                                               &directory_id,
                                               &share_users_map));
    EXPECT_EQ(directory10_relative_path, root_share_path);
    // Unmount...
    UnmountDrive(drive, max_space, used_space1);
  }
}

TEST_P(PrivateOpenShareTests, BEH_OwnerRenameShare) {
  std::string first_unique_user_id, second_unique_user_id,
              first_root_parent_id,
              first_user_id(RandomAlphaNumericString(8)),
              second_user_id(RandomAlphaNumericString(8)),
              third_user_id(RandomAlphaNumericString(8)),
              first_share_id(RandomAlphaNumericString(64)),
              directory_id, first_user_copy, second_user_copy;

  asymm::Keys first_user_keyring, second_user_keyring, first_share_keyring,
              recovered_share_keyring;
  maidsafe::asymm::GenerateKeyPair(&first_share_keyring);
  first_share_keyring.identity = RandomAlphaNumericString(64);

  std::map<std::string, int> share_users_map;
  std::vector<ShareId> share_users_vector;

  fs::path directory00, directory10, directory20, directory10_relative_path,
           file10, root_share_path;
  fs::path test_mount_dir;
  int64_t max_space(1073741824), used_space(0);
  boost::system::error_code error_code;

  {  // user 1
    DrivePtr drive(CreateAndMountDrive("",
                                       &first_unique_user_id,
                                       max_space,
                                       used_space,
                                       &test_mount_dir,
                                       &first_user_keyring));
    ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";
    // Save first user root parent id...
    first_root_parent_id = drive->root_parent_id();
    // Create some directories...
    directory00 = CreateTestDirectory(test_mount_dir);
    directory10 = CreateTestDirectory(directory00);
    directory20 = CreateTestDirectory(directory10);
    directory10_relative_path = RelativePath(test_mount_dir, directory10);
    int64_t file_size(0);
    // Create a file in directory10...
    file10 = CreateTestFile(directory10, file_size);
    EXPECT_TRUE(fs::exists(file10, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Create a file in directory20...
    fs::path file20(CreateTestFile(directory20, file_size));
    EXPECT_TRUE(fs::exists(file20, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Copy file10 off drive...
    first_user_copy = file10.filename().string() + ".first_user_copy";
    fs::copy_file(file10, *main_test_dir_ / first_user_copy);
    EXPECT_TRUE(fs::exists(*main_test_dir_ / first_user_copy, error_code));
    EXPECT_EQ(error_code.value(), 0);
    // Set directory10 to shared...
    EXPECT_EQ(kSuccess, drive->SetShareDetails(directory10_relative_path,
                                               first_share_id,
                                               first_share_keyring,
                                               first_user_id,
                                               private_share_,
                                               &directory_id));
    share_users_map.insert(std::make_pair(second_user_id, kShareReadWrite));
    EXPECT_EQ(kSuccess, drive->AddShareUsers(directory10_relative_path,
                                             share_users_map,
                                             private_share_));

    signaled_ = false;
    bs2::connection connection(drive->ConnectToShareRenamed(
        std::bind(&PrivateOpenShareTests::ShareRenamedSlot,
                  this, args::_1, args::_2)));
    // Rename the share directory
    std::string new_share_name(directory10_relative_path.filename().string() +
                               RandomAlphaNumericString(5));
    fs::path new_path(directory00 / new_share_name);
    fs::path new_path_relative(RelativePath(test_mount_dir, new_path));

    fs::rename(directory10, new_path, error_code);

    EXPECT_EQ(0, error_code.value());
    while (!signaled_)
      Sleep(bptime::milliseconds(100));

    share_users_map.clear();
    share_users_map.insert(std::make_pair(third_user_id, kShareReadWrite));
//     EXPECT_EQ(kFailedToGetChild,
//               drive->AddShareUsers(directory10_relative_path,
//                                    share_users_map,
//                                    private_share_));
    EXPECT_EQ(kSuccess, drive->AddShareUsers(new_path_relative,
                                             share_users_map,
                                             private_share_));
    share_users_map.clear();
    EXPECT_EQ(kSuccess, drive->GetShareDetails(first_share_id,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               &share_users_map));
    EXPECT_EQ(3, share_users_map.size());
    EXPECT_EQ(CalculateUsedSpace(test_mount_dir), drive->GetUsedSpace());
    // Unmount...
    UnmountDrive(drive, max_space, used_space);
  }
}

TEST_F(SharesTest, BEH_ShareKeys) {
  maidsafe::test::TestPath test_path(maidsafe::test::CreateTestPath());
  AsioService asio_service(5);
  std::shared_ptr<pcs::RemoteChunkStore> chunk_store;
  fs::path test_mount_dir;
  std::string unique_user_id(crypto::Hash<crypto::SHA512>(RandomString(8))), root_parent_id;
  asymm::Keys keys;
  int64_t max_space(1073741824), used_space(0);
  ASSERT_EQ(kSuccess, priv::utilities::CreateMaidsafeIdentity(keys));
  DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                   root_parent_id,
                                   keys,
                                   false,
                                   test_path,
                                   max_space,
                                   used_space,
                                   asio_service,
                                   chunk_store,
                                   test_mount_dir));
  ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

  fs::path directory0(CreateTestDirectory(test_mount_dir));
  fs::path directory1(CreateTestDirectory(directory0));
  fs::path directory2(CreateTestDirectory(directory1));
  fs::path directory3(CreateTestDirectory(directory2));
  fs::path directory4(CreateTestDirectory(directory2));

  ShareId first_share_id(RandomAlphaNumericString(64)),
          second_share_id(RandomAlphaNumericString(64));
  std::string share_owner_id(RandomAlphaNumericString(64));
  asymm::Keys first_share_keyring, second_share_keyring;
  priv::utilities::CreateMaidsafeIdentity(first_share_keyring);
  priv::utilities::CreateMaidsafeIdentity(second_share_keyring);

  std::vector<ShareData> share_data_vector;
  ShareKeys share_keys1, share_keys2, share_keys3;
  ShareData share_data1(first_share_id,
                        share_owner_id,
                        directory3,
                        first_share_keyring,
                        kShareReadWrite),
            share_data2(second_share_id,
                        share_owner_id,
                        directory4,
                        second_share_keyring,
                        kShareReadWrite),
            share_data;

  // Set share_keys1 data...
  std::string serialised_shares;
  EXPECT_TRUE(share_keys1.Add(share_data1, serialised_shares));
  EXPECT_TRUE(share_keys1.Add(share_data2, serialised_shares));

  // Get share_keys1 data...
  EXPECT_TRUE(share_keys1.Get(first_share_id, share_data));
  EXPECT_EQ(first_share_id, share_data.share_id);
  EXPECT_EQ(directory3, share_data.share_root_dir);
  EXPECT_EQ(first_share_keyring.identity, share_data.keyring.identity);
  EXPECT_TRUE(share_keys1.Get(second_share_id, share_data));
  EXPECT_EQ(second_share_id, share_data.share_id);
  EXPECT_EQ(directory4, share_data.share_root_dir);
  EXPECT_EQ(second_share_keyring.identity, share_data.keyring.identity);

  // Set share_keys2 data...
  EXPECT_TRUE(share_keys2.Init(serialised_shares));

  // Get share_keys2 data...
  EXPECT_TRUE(share_keys2.Get(first_share_id, share_data));
  EXPECT_EQ(first_share_id, share_data.share_id);
  EXPECT_EQ(directory3, share_data.share_root_dir);
  EXPECT_EQ(first_share_keyring.identity, share_data.keyring.identity);

  EXPECT_TRUE(share_keys2.Get(second_share_id, share_data));
  EXPECT_EQ(second_share_id, share_data.share_id);
  EXPECT_EQ(directory4, share_data.share_root_dir);
  EXPECT_EQ(second_share_keyring.identity, share_data.keyring.identity);

  // Get share_keys2 data again...
  share_keys2.GetAll(share_data_vector);
  EXPECT_EQ(2, share_data_vector.size());
  EXPECT_TRUE(first_share_id == share_data_vector[0].share_id ||
              second_share_id == share_data_vector[0].share_id);
  EXPECT_TRUE(first_share_id == share_data_vector[1].share_id ||
              second_share_id == share_data_vector[1].share_id);
  EXPECT_TRUE(directory3 == share_data_vector[0].share_root_dir ||
              directory4 == share_data_vector[0].share_root_dir);
  EXPECT_TRUE(directory3 == share_data_vector[1].share_root_dir ||
              directory4 == share_data_vector[1].share_root_dir);
  EXPECT_TRUE(first_share_keyring.identity == share_data_vector[0].keyring.identity ||
              second_share_keyring.identity == share_data_vector[0].keyring.identity);
  EXPECT_TRUE(first_share_keyring.identity == share_data_vector[1].keyring.identity ||
              second_share_keyring.identity == share_data_vector[1].keyring.identity);

  // Delete share_keys2 first_share_id data...
  EXPECT_TRUE(share_keys2.Delete(first_share_id, serialised_shares));

  // Get share_keys2 data...
  share_data_vector.clear();
  share_keys2.GetAll(share_data_vector);
  EXPECT_EQ(1, share_data_vector.size());
  EXPECT_TRUE(second_share_id == share_data_vector[0].share_id);
  EXPECT_TRUE(directory4 == share_data_vector[0].share_root_dir);
  EXPECT_TRUE(second_share_keyring.identity == share_data_vector[0].keyring.identity);

  // Delete share_keys2 second_share_id data...
  EXPECT_TRUE(share_keys2.Delete(second_share_id, serialised_shares));

  // Get share_keys2 data...
  share_data_vector.clear();
  share_keys2.GetAll(share_data_vector);
  EXPECT_EQ(0, share_data_vector.size());

  // Set share_keys3 data...
  EXPECT_TRUE(share_keys3.Init(serialised_shares));
  share_data_vector.clear();
  share_keys3.GetAll(share_data_vector);
  EXPECT_EQ(0, share_data_vector.size());

  UnmountDrive(drive, max_space, used_space);
}

TEST_F(SharesTest, FUNC_WriteHiddenFile) {
  maidsafe::test::TestPath test_path(maidsafe::test::CreateTestPath());
  AsioService asio_service(5);
  std::shared_ptr<pcs::RemoteChunkStore> chunk_store;
  fs::path test_mount_dir;
  std::string unique_user_id(crypto::Hash<crypto::SHA512>(RandomString(8))), root_parent_id;
  asymm::Keys keys;
  int64_t max_space(1073741824), used_space(0);
  ASSERT_EQ(kSuccess, priv::utilities::CreateMaidsafeIdentity(keys));

  std::string content(RandomAlphaNumericString(128));
  for (int i = 0; i < 10; ++i)
    content += content + RandomAlphaNumericString(10);

  DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                   root_parent_id,
                                   keys,
                                   false,
                                   test_path,
                                   max_space,
                                   used_space,
                                   asio_service,
                                   chunk_store,
                                   test_mount_dir));
  ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

  fs::path directory00(CreateTestDirectory(test_mount_dir));
  fs::path directory10(CreateTestDirectory(directory00));
  fs::path directory10_relative_path(RelativePath(test_mount_dir, directory10));
  std::string hidden_file_name(RandomAlphaNumericString(5) + ".ms_hidden");
  fs::path hidden_file(directory10 / hidden_file_name);
  fs::path hidden_file_relative(RelativePath(test_mount_dir, hidden_file));

  EXPECT_EQ(kSuccess,
            drive->WriteHiddenFile(hidden_file_relative, content, true));
  boost::system::error_code error_code;
  EXPECT_FALSE(fs::exists(hidden_file, error_code));
  EXPECT_EQ(error_code.value(), 2);

  std::string read_content;
  EXPECT_EQ(kSuccess,
            drive->ReadHiddenFile(hidden_file_relative, &read_content));
  EXPECT_EQ(content, read_content);

  // trying to write with invalid parameters
  EXPECT_EQ(kInvalidPath,
            drive->WriteHiddenFile("", content, true));
  EXPECT_EQ(kInvalidPath,
            drive->WriteHiddenFile("test.txt", content, true));
  EXPECT_EQ(kMsHiddenAlreadyExists,
            drive->WriteHiddenFile(hidden_file_relative, content, false));

  // replace the hidden file with new content
  content += RandomAlphaNumericString(10);
  EXPECT_EQ(kSuccess,
            drive->WriteHiddenFile(hidden_file_relative, content, true));
  EXPECT_EQ(kSuccess,
            drive->ReadHiddenFile(hidden_file_relative, &read_content));
  EXPECT_EQ(content, read_content);

  // Unmount...
  UnmountDrive(drive, max_space, used_space);
}

TEST_F(SharesTest, FUNC_ReadHiddenFile) {
  maidsafe::test::TestPath test_path(maidsafe::test::CreateTestPath());
  AsioService asio_service(5);
  std::shared_ptr<pcs::RemoteChunkStore> chunk_store;
  fs::path test_mount_dir;
  std::string unique_user_id(crypto::Hash<crypto::SHA512>(RandomString(8))), root_parent_id;
  asymm::Keys keys;
  int64_t max_space(1073741824), used_space(0);
  ASSERT_EQ(kSuccess, priv::utilities::CreateMaidsafeIdentity(keys));

  std::string content(RandomAlphaNumericString(128));
  for (int i = 0; i < 10; ++i)
    content += content + RandomAlphaNumericString(10);

  DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                   root_parent_id,
                                   keys,
                                   false,
                                   test_path,
                                   max_space,
                                   used_space,
                                   asio_service,
                                   chunk_store,
                                   test_mount_dir));
  ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

  fs::path directory00(CreateTestDirectory(test_mount_dir));
  fs::path directory10(CreateTestDirectory(directory00));
  fs::path directory10_relative_path(RelativePath(test_mount_dir, directory10));
  std::string hidden_file_name(RandomAlphaNumericString(5) + ".ms_hidden");
  fs::path hidden_file(directory10 / hidden_file_name);
  fs::path hidden_file_relative(RelativePath(test_mount_dir, hidden_file));

  EXPECT_EQ(kSuccess,
            drive->WriteHiddenFile(hidden_file_relative, content, true));
  std::string read_content;
  EXPECT_EQ(kSuccess,
            drive->ReadHiddenFile(hidden_file_relative, &read_content));
  EXPECT_EQ(content, read_content);

  // trying to read with invalid parameters
  EXPECT_EQ(kInvalidPath, drive->ReadHiddenFile("", &read_content));
  EXPECT_EQ(kInvalidPath, drive->ReadHiddenFile("test.txt", &read_content));
  EXPECT_EQ(kNullParameter,
            drive->ReadHiddenFile(hidden_file_relative, nullptr));
  EXPECT_EQ(kNoMsHidden,
            drive->ReadHiddenFile("test.ms_hidden", &read_content));

  // Unmount...
  UnmountDrive(drive, max_space, used_space);
}

TEST_F(SharesTest, FUNC_DeleteHiddenFile) {
  maidsafe::test::TestPath test_path(maidsafe::test::CreateTestPath());
  AsioService asio_service(5);
  std::shared_ptr<pcs::RemoteChunkStore> chunk_store;
  fs::path test_mount_dir;
  std::string unique_user_id(crypto::Hash<crypto::SHA512>(RandomString(8))), root_parent_id;
  asymm::Keys keys;
  int64_t max_space(1073741824), used_space(0);
  ASSERT_EQ(kSuccess, priv::utilities::CreateMaidsafeIdentity(keys));

  std::string content(RandomAlphaNumericString(128));
  for (int i = 0; i < 10; ++i)
    content += content + RandomAlphaNumericString(10);

  DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                   root_parent_id,
                                   keys,
                                   false,
                                   test_path,
                                   max_space,
                                   used_space,
                                   asio_service,
                                   chunk_store,
                                   test_mount_dir));
  ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

  fs::path directory00(CreateTestDirectory(test_mount_dir));
  fs::path directory10(CreateTestDirectory(directory00));
  fs::path directory10_relative_path(RelativePath(test_mount_dir, directory10));
  std::string hidden_file_name(RandomAlphaNumericString(5) + ".ms_hidden");
  fs::path hidden_file(directory10 / hidden_file_name);
  fs::path hidden_file_relative(RelativePath(test_mount_dir, hidden_file));

  EXPECT_EQ(kSuccess,
            drive->WriteHiddenFile(hidden_file_relative, content, true));
  std::string read_content;
  EXPECT_EQ(kSuccess,
            drive->ReadHiddenFile(hidden_file_relative, &read_content));
  EXPECT_EQ(content, read_content);

  // trying to delete with invalid parameters
  EXPECT_EQ(kInvalidPath, drive->DeleteHiddenFile(""));
  EXPECT_EQ(kInvalidPath, drive->DeleteHiddenFile("test.txt"));

  // deleting the hidden file
  EXPECT_EQ(kSuccess,
            drive->DeleteHiddenFile(hidden_file_relative));
  EXPECT_EQ(kNoMsHidden,
            drive->ReadHiddenFile(hidden_file_relative, &read_content));

  EXPECT_EQ(kFailedToGetChild,
            drive->DeleteHiddenFile("test.ms_hidden"));
  EXPECT_EQ(kFailedToGetChild,
            drive->DeleteHiddenFile(hidden_file_relative));

  // re-create the hidden file
  EXPECT_EQ(kSuccess,
            drive->WriteHiddenFile(hidden_file_relative, content, false));
  EXPECT_EQ(kSuccess,
            drive->ReadHiddenFile(hidden_file_relative, &read_content));
  EXPECT_EQ(content, read_content);

  // Unmount...
  UnmountDrive(drive, max_space, used_space);
}

TEST_F(SharesTest, BEH_SearchFiles) {
  maidsafe::test::TestPath test_path(maidsafe::test::CreateTestPath());
  AsioService asio_service(5);
  std::shared_ptr<pcs::RemoteChunkStore> chunk_store;
  fs::path test_mount_dir;
  std::string unique_user_id(crypto::Hash<crypto::SHA512>(RandomString(8))), root_parent_id;
  asymm::Keys keys;
  int64_t max_space(1073741824), used_space(0), file_size(0), total_size(0);
  ASSERT_EQ(kSuccess, priv::utilities::CreateMaidsafeIdentity(keys));

  DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                   root_parent_id,
                                   keys,
                                   false,
                                   test_path,
                                   max_space,
                                   used_space,
                                   asio_service,
                                   chunk_store,
                                   test_mount_dir));
  ASSERT_NE(nullptr, drive.get()) << "Failed to mount drive.";

  fs::path directory(CreateTestDirectory(test_mount_dir)),
           relative_path(RelativePath(test_mount_dir, directory));
  std::vector<std::string> files;
  std::string filename("file0");
  std::string content("Hidden");
  for (uint32_t i = 0; i != 10; ++i) {
    if (i%2 == 0) {
      fs::path file(CreateNamedFile(directory,
                                    filename + boost::lexical_cast<std::string>(i),
                                    file_size));
      total_size += file_size;
    } else {
      drive->WriteHiddenFile(relative_path / (filename +
                                              boost::lexical_cast<std::string>(i) +
                                              kMsHidden.string()),
                             content,
                             false);
      total_size += content.size();
    }
  }

  filename = "file1";
  for (uint32_t i = 0; i != 5; ++i) {
    if (i%2 == 0) {
      drive->WriteHiddenFile(relative_path / (filename +
                                              boost::lexical_cast<std::string>(i) +
                                              kMsHidden.string()),
                             content,
                             false);
      total_size += content.size();
    } else {
      fs::path file(CreateNamedFile(directory,
                                    filename + boost::lexical_cast<std::string>(i),
                                    file_size));
      total_size += file_size;
    }
  }

  filename = "file2";
  for (uint32_t i = 0; i != 7; ++i) {
    if (i%2 == 0) {
      fs::path file(CreateNamedFile(directory,
                                    filename + boost::lexical_cast<std::string>(i),
                                    file_size));
      total_size += file_size;
    } else {
      drive->WriteHiddenFile(relative_path / (filename +
                                              boost::lexical_cast<std::string>(i) +
                                              kMsHidden.string()),
                             content,
                             false);
      total_size += content.size();
    }
  }

  EXPECT_EQ(kSuccess, drive->SearchHiddenFiles(relative_path, &files));
  EXPECT_EQ(11, files.size());
  for (uint32_t i = 0; i != files.size(); ++i)
    EXPECT_EQ(kMsHidden, fs::path(files[i]).extension());
  EXPECT_EQ(CalculateUsedSpace(test_mount_dir), drive->GetUsedSpace());
  UnmountDrive(drive, max_space, used_space);
}

TEST(StandAloneDriveTest, FUNC_ReadOnlyDrive) {
  maidsafe::test::TestPath test_path(maidsafe::test::CreateTestPath());
  AsioService asio_service(5);
  std::shared_ptr<pcs::RemoteChunkStore> chunk_store;
  fs::path mount_directory, test_directory;
  std::string unique_user_id(crypto::Hash<crypto::SHA512>(RandomString(8))), root_parent_id;
  asymm::Keys keys;
  int64_t used_space(0), max_space(1024 * 1024 * 1024);
  ASSERT_EQ(kSuccess, priv::utilities::CreateMaidsafeIdentity(keys));
  {
    DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                     root_parent_id,
                                     keys,
                                     false,
                                     test_path,
                                     max_space,
                                     used_space,
                                     asio_service,
                                     chunk_store,
                                     mount_directory));
    ASSERT_NE(nullptr, drive.get());
    test_directory = CreateTestDirectoriesAndFiles(mount_directory);
    root_parent_id = drive->root_parent_id();
    UnmountDrive(drive, asio_service);
  }
  {
    DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                     root_parent_id,
                                     keys,
                                     true,
                                     test_path,
                                     max_space,
                                     used_space,
                                     asio_service,
                                     chunk_store,
                                     mount_directory));
    int64_t zeroth_used_space(drive->GetUsedSpace());

    // Perform operations that should fail because of read only status
    boost::system::error_code error_code;
    EXPECT_FALSE(fs::create_directory(mount_directory / RandomAlphaNumericString(5), error_code));
    EXPECT_EQ(boost::system::errc::read_only_file_system, error_code.value());

    EXPECT_FALSE(WriteFile(mount_directory / RandomAlphaNumericString(5), RandomString(1)));
    EXPECT_FALSE(WriteFile(mount_directory / RandomAlphaNumericString(5), RandomString(64)));
    EXPECT_FALSE(WriteFile(mount_directory / RandomAlphaNumericString(5), RandomString(1024)));
    EXPECT_FALSE(WriteFile(mount_directory / RandomAlphaNumericString(5),
                           RandomString(1024 * 1024)));

    fs::path leaf(test_directory.filename());
    EXPECT_FALSE(WriteFile(mount_directory / leaf / RandomAlphaNumericString(5), RandomString(1)));
    EXPECT_FALSE(WriteFile(mount_directory / leaf / RandomAlphaNumericString(5), RandomString(64)));
    EXPECT_FALSE(WriteFile(mount_directory / leaf / RandomAlphaNumericString(5),
                           RandomString(1024)));
    EXPECT_FALSE(WriteFile(mount_directory / leaf / RandomAlphaNumericString(5),
                           RandomString(1024 * 1024)));

    // Iterate created directory
    fs::recursive_directory_iterator begin(mount_directory / leaf), end;
    std::string pre_file_content, post_file_content, new_file_content(RandomString(64)),
                new_filename(RandomAlphaNumericString(8));
    fs::path current_path, rename_path;
    try {
      for (; begin != end; ++begin) {
        current_path = fs::path(*begin);
        rename_path = current_path.parent_path() / new_filename;
        fs::rename(current_path, rename_path, error_code);
        EXPECT_EQ(boost::system::errc::read_only_file_system, error_code.value());
        EXPECT_FALSE(fs::exists(rename_path, error_code));
        EXPECT_EQ(boost::system::errc::no_such_file_or_directory, error_code.value());
        EXPECT_TRUE(fs::exists(current_path, error_code));
        EXPECT_EQ(0, error_code.value());
        if (fs::is_directory(current_path)) {
          EXPECT_FALSE(fs::remove(current_path, error_code));
          EXPECT_EQ(boost::system::errc::read_only_file_system, error_code.value());
        } else if (fs::is_regular_file(current_path)) {
          EXPECT_FALSE(fs::remove(current_path, error_code));
          EXPECT_EQ(boost::system::errc::read_only_file_system, error_code.value());

          EXPECT_TRUE(ReadFile(current_path, &pre_file_content));
          EXPECT_FALSE(WriteFile(current_path, new_file_content));
          EXPECT_TRUE(ReadFile(current_path, &post_file_content));
        }
      }
    }
    catch(...) {
      EXPECT_TRUE(false) << "Failure during traversal.";
    }

    EXPECT_EQ(zeroth_used_space, drive->GetUsedSpace());
    UnmountDrive(drive, asio_service);
  }
  {
    DrivePtr drive(MakeAndMountDrive(unique_user_id,
                                     root_parent_id,
                                     keys,
                                     false,
                                     test_path,
                                     max_space,
                                     used_space,
                                     asio_service,
                                     chunk_store,
                                     mount_directory));
    ASSERT_NE(nullptr, drive.get());
    test_directory = CreateTestDirectoriesAndFiles(mount_directory);
    root_parent_id = drive->root_parent_id();
    UnmountDrive(drive, asio_service);
  }
}

}  // namespace test

}  // namespace drive

}  // namespace maidsafe
