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

#ifdef MAIDSAFE_WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include <fstream>  // NOLINT
#include <string>
#include <chrono>

#include "boost/filesystem.hpp"
#include "boost/scoped_array.hpp"

#include "maidsafe/common/crypto.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/data_store/permanent_store.h"
#include "maidsafe/data_store/surefile_store.h"

#include "maidsafe/encrypt/data_map.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/tests/test_utils.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace test {

#ifdef MAIDSAFE_WIN32
testing::AssertionResult TimesMatch(const FILETIME& time1, const FILETIME& time2) {
  if (time1.dwHighDateTime != time2.dwHighDateTime) {
    return testing::AssertionFailure() << "time1.dwHighDateTime (" << time1.dwHighDateTime
                                       << ") != time2.dwHighDateTime (" << time2.dwHighDateTime
                                       << ")";
  }
  if (time1.dwLowDateTime != time2.dwLowDateTime) {
    return testing::AssertionFailure() << "time1.dwLowDateTime (" << time1.dwLowDateTime
                                       << ") != time2.dwLowDateTime (" << time2.dwLowDateTime
                                       << ")";
  }
  return testing::AssertionSuccess();
}
#endif

void SetLastAccessTime(MetaData* meta_data) {
#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&meta_data->last_access_time);
#else
  time(&meta_data->attributes.st_atime);
#endif
}

testing::AssertionResult LastAccessTimesMatch(const MetaData& meta_data1,
                                              const MetaData& meta_data2) {
#ifdef MAIDSAFE_WIN32
  return TimesMatch(meta_data1.last_access_time, meta_data2.last_access_time);
#else
  if (meta_data1.attributes.st_atime == meta_data2.attributes.st_atime)
    return testing::AssertionSuccess();
  else
    return testing::AssertionFailure() << "meta_data1.attributes.st_atime ("
                                       << meta_data1.attributes.st_atime
                                       << ") != meta_data2.attributes."
                                       << "st_atime (" << meta_data2.attributes.st_atime << ")";
#endif
}

TEST(Drive, BEH_SureStore) {
  OnServiceAdded on_added([] { LOG(kInfo) << "Trying to add a service."; });
  OnServiceRemoved on_removed([](const fs::path &
                                 alias) { LOG(kInfo) << "Trying to remove " << alias; });
  OnServiceRenamed on_renamed([](const fs::path & old_alias, const fs::path & new_alias) {
    LOG(kInfo) << "Renamed " << old_alias << " to " << new_alias;
  });
  maidsafe::test::TestPath main_test_dir(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive"));
  Identity root_id;
  Identity service_root_id(RandomString(64));
  fs::path service_name("AnotherService"), service_root, file_name("test.txt");
  std::string content("Content\n");
  {
#ifdef MAIDSAFE_WIN32
    auto mount_dir(GetNextAvailableDrivePath());
    VirtualDrive<data_store::SureFileStore>::value_type drive(
        Identity(), mount_dir, std::string(), "SureFileDrive", on_added, on_removed, on_renamed);
    mount_dir /= "\\";
#else
    fs::path mount_dir(*main_test_dir / "mount");
    VirtualDrive<data_store::SureFileStore>::value_type drive(
        Identity(), mount_dir, "SureFileDrive", on_added, on_removed, on_renamed);
#endif
    root_id = drive.drive_root_id();
    MetaData meta_data("TestService", true);
    DirectoryId grandparent_id, parent_id;
    drive.AddService(meta_data.name, *main_test_dir / "TestService", Identity(RandomString(64)));
    drive.AddService(service_name, *main_test_dir / service_name, service_root_id);

    service_root = mount_dir / service_name;
    EXPECT_TRUE(WriteFile(service_root / file_name, content));
    EXPECT_EQ(NonEmptyString(content), ReadFile(service_root / file_name));
  }

  {
#ifdef MAIDSAFE_WIN32
    auto mount_dir(GetNextAvailableDrivePath());
    VirtualDrive<data_store::SureFileStore>::value_type drive(
        root_id, mount_dir, std::string(), "SureFileDrive", on_added, on_removed, on_renamed);
    mount_dir /= "\\";
#else
    fs::path mount_dir(*main_test_dir / "mount");
    VirtualDrive<data_store::SureFileStore>::value_type drive(root_id, mount_dir, "SureFileDrive",
                                                              on_added, on_removed, on_renamed);
#endif
    drive.AddService(service_name, *main_test_dir / service_name, service_root_id);
    service_root = mount_dir / service_name;
    EXPECT_EQ(NonEmptyString(content), ReadFile(service_root / file_name));
  }
}

// TODO(Team): 2013-09-25 - Uncomment and fix or delete
// class DriveApiTest : public testing::Test {
// public:
//  typedef data_store::PermanentStore DataStore;
//
//  DriveApiTest()
//      : main_test_dir_(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive")),
//        default_maid_(maidsafe::passport::Maid::signer_type()),
//        routing_(default_maid_),
//        client_nfs_(routing_, default_maid_),
//        data_store_(*main_test_dir_ / "local", DiskUsage(1073741824)),
//        unique_user_id_(Identity(crypto::Hash<crypto::SHA512>(main_test_dir_->string()))),
//        relative_root_(fs::path("/").make_preferred()),
//        relative_root_meta_data_(relative_root_, true),
//        owner_(relative_root_ / "Owner"),
//        owner_meta_data_(owner_, true),
//        max_space_(1073741824),
//        used_space_(0),
//        drive_(std::make_shared<Drive<Storage>::TestDriveInUserSpace(client_nfs_,
//                                                         data_store_,
//                                                         default_maid_,
//                                                         unique_user_id_,
//                                                         "",
//                                                         "S:",
//                                                         "MaidSafeDrive",
//                                                         max_space_,
//                                                         used_space_)),
//        directory_handler_(drive_->directory_handler()) {}
//
//
//
// protected:
//  void SetUp() {}
//  void TearDown() {}
//
//  maidsafe::test::TestPath main_test_dir_;
//  passport::Maid default_maid_;
//  routing::Routing routing_;
//  nfs::ClientMaidNfs client_nfs_;
//  DataStore data_store_;
//  Identity unique_user_id_;
//  fs::path relative_root_;
//  MetaData relative_root_meta_data_;
//  fs::path owner_;
//  MetaData owner_meta_data_;
//  int64_t max_space_, used_space_;
//  std::shared_ptr<Drive<Storage>::TestDriveInUserSpace> drive_;
//  std::shared_ptr<detail::DirectoryHandler> directory_handler_;
// };
//
// TEST_F(DriveApiTest, BEH_AddThenGetMetaData) {
//  MetaData new_meta("test", true);
//  SetLastAccessTime(&new_meta);
//  EXPECT_NO_THROW(drive_->AddFile(owner_ / "test",
//                                  new_meta,
//                                  nullptr,
//                                  &(*new_meta.directory_id)));
//  MetaData result_meta_data;
//  EXPECT_NO_THROW(drive_->GetMetaData(owner_ / "test",
//                                      result_meta_data,
//                                      &(*result_meta_data.directory_id),
//                                      nullptr));
//  // Fails due to inaccurate time conversions...
//  // EXPECT_TRUE(LastAccessTimesMatch(new_meta, result_meta_data));
// }
//
// TEST_F(DriveApiTest, BEH_RenameMetaData) {
//  MetaData meta_data("test", true);
//  SetLastAccessTime(&meta_data);
//  EXPECT_NO_THROW(drive_->AddFile(owner_ / "test",
//                                  meta_data,
//                                  nullptr,
//                                  &(*meta_data.directory_id)));
//  int64_t resize_space;
//  EXPECT_NO_THROW(drive_->RenameFile(owner_ / "test",
//                                     owner_ / "new_test",
//                                     meta_data,
//                                     resize_space));
//  MetaData result_meta_data;
//  EXPECT_THROW(drive_->GetMetaData(owner_ / "test",
//                                   result_meta_data,
//                                   &(*result_meta_data.directory_id),
//                                   nullptr),
//               std::exception);
//  EXPECT_NO_THROW(drive_->GetMetaData(owner_ / "new_test",
//                                      result_meta_data,
//                                      &(*result_meta_data.directory_id),
//                                      nullptr));
//  // Fails due to inaccurate time conversions...
//  // EXPECT_TRUE(LastAccessTimesMatch(meta_data, result_meta_data));
// }
//
// TEST_F(DriveApiTest, BEH_Update) {
//  MetaData meta_data("test", true);
//  EXPECT_NO_THROW(drive_->AddFile(owner_ / "test",
//                                  meta_data,
//                                  nullptr,
//                                  &(*owner_meta_data_.directory_id)));
//  FileContext file_context("test", false);
//  file_context.parent_directory_id = *owner_meta_data_.directory_id;
//  file_context.self_encryptor.reset(new encrypt::SelfEncryptor(file_context.meta_data->data_map,
//                                                               client_nfs_,
//                                                               data_store_));
//  SetLastAccessTime(&(*file_context.meta_data));
//  EXPECT_NO_THROW(drive_->UpdateParent(&file_context, owner_));
//  MetaData result_meta_data;
//  EXPECT_NO_THROW(drive_->GetMetaData(owner_ / "test",
//                                      result_meta_data,
//                                      &(*result_meta_data.directory_id),
//                                      nullptr));
//  // Fails due to inaccurate time conversions...
//  // EXPECT_TRUE(LastAccessTimesMatch(*file_context.meta_data, result_meta_data));
// }
//
//// Self encryption tests were using free functions which are now gone...
// TEST_F(DriveApiTest, BEH_WriteReadThenFlush) {
//  std::shared_ptr<encrypt::DataMap> data_map(new encrypt::DataMap);
//  std::string initial_content(RandomString(100));
//  data_map->content = initial_content;
//
//  std::string data_to_write(RandomString(50));
//  uint32_t bytes_to_write(50);
//  boost::shared_array<char> read_result(new char[100]);
//  uint32_t bytes_to_read(100);
//  {
//    SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(data_map,
//                                                               client_nfs_,
//                                                               data_store_));
//    EXPECT_TRUE(self_encryptor->Write(data_to_write.c_str(), bytes_to_write, 0));
//    EXPECT_EQ(0, TotalSize(data_map));
//    EXPECT_TRUE(self_encryptor->Read(read_result.get(), bytes_to_read, 0));
//  }
//  EXPECT_EQ(100, TotalSize(data_map));
//  EXPECT_EQ(100, data_map->content.size());
//  for (uint32_t i = 0; i < 50; ++i)
//    ASSERT_EQ(data_to_write[i], read_result[i]) << "difference at " << i;
//  for (uint32_t i = 50; i < 100; ++i)
//    ASSERT_EQ(initial_content[i], read_result[i]) << "difference at " << i;
// }
//
// TEST_F(DriveApiTest, BEH_Delete) {
//  MetaData meta_data("test", false);
//  {
//    SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(meta_data.data_map,
//                                                               client_nfs_,
//                                                               data_store_));
//
//    std::string data_to_write(RandomString(1024*3*256));
//    uint32_t bytes_to_write(1024*3*256);
//
//    EXPECT_TRUE(self_encryptor->Write(data_to_write.c_str(), 80, 0));
//    EXPECT_TRUE(self_encryptor->Write(data_to_write.c_str(), bytes_to_write, 80));
//    EXPECT_TRUE(self_encryptor->Flush());
//    EXPECT_NO_THROW(drive_->AddFile(owner_ / "test",
//                                    meta_data,
//                                    nullptr,
//                                    &(*meta_data.directory_id)));
//  }
//  EXPECT_NO_THROW(drive_->GetMetaData(owner_ / "test", meta_data, nullptr, nullptr));
//  EXPECT_EQ(3, meta_data.data_map->chunks.size());
//  EXPECT_NO_THROW(drive_->RemoveFile(owner_ / "test"));
//  EXPECT_THROW(drive_->GetMetaData(owner_ / "test", meta_data, nullptr, nullptr),
//               std::exception);
// }
//
// TEST_F(DriveApiTest, BEH_WriteAndReadRandomChunks) {
//  // writes data in random sizes pieces in a random order,
//  // rewrites part overlapping the original piece (may be at the end, increasing
//  // the file size and final chunk size to a non-standard size)
//  // and checks the result of reading the stored result is as expected
//  MetaData meta_data("test", false);
//  uint32_t num_chunks(20);
//  const uint32_t kTestDataSize(encrypt::kDefaultChunkSize * num_chunks);
//  std::string plain_text(RandomString(kTestDataSize));
//  std::vector<std::pair<uint64_t, std::string>> broken_data;
//  std::string extra("amended");
//  uint32_t file_size(kTestDataSize);
//
//  uint32_t i(0);
//  while (i < kTestDataSize) {
//    uint32_t size;
//    if (kTestDataSize - i < (4096))
//      size = kTestDataSize - i;
//    else
//      size = RandomUint32() % (4096);
//    std::pair<uint64_t, std::string> piece(i, plain_text.substr(i, size));
//    broken_data.push_back(piece);
//    i += size;
//  }
//
//  srand(RandomUint32());
//  std::random_shuffle(broken_data.begin(), broken_data.end());
//  std::pair<uint64_t, std::string> overlap(broken_data.back().first,
//                                           (broken_data.back().second + extra));
//  uint32_t position(static_cast<uint32_t>(broken_data.back().first +
//                                          broken_data.back().second.size()));
//
//  if (position / encrypt::kDefaultChunkSize == num_chunks)
//    file_size += static_cast<uint32_t>(extra.size());
//  plain_text.replace(position, extra.size(), extra);
//
//  {
//    SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(meta_data.data_map,
//                                                               client_nfs_,
//                                                               data_store_));
//    uint32_t wtotal(0);
//    for (auto it = broken_data.begin(); it != broken_data.end(); ++it) {
//        EXPECT_TRUE(self_encryptor->Write(it->second.c_str(),
//                                          static_cast<uint32_t>(it->second.size()),
//                                          it->first));
//
//      wtotal += static_cast<uint32_t>(it->second.size());
//    }
//    EXPECT_EQ(wtotal, kTestDataSize);
//    EXPECT_TRUE(self_encryptor->Write(overlap.second.c_str(),
//                                      static_cast<uint32_t>(overlap.second.size()),
//                                      overlap.first));
//  }
//  SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(meta_data.data_map,
//                                                             client_nfs_,
//                                                             data_store_));
//
//  EXPECT_EQ(num_chunks, meta_data.data_map->chunks.size());
//  EXPECT_EQ(file_size, TotalSize(meta_data.data_map));
//  EXPECT_TRUE(meta_data.data_map->content.empty());
//
//  boost::scoped_array<char> original(new char[file_size]);
//  std::copy(plain_text.data(), plain_text.data() + file_size, original.get());
//  boost::shared_array<char> read_result(new char[file_size]);
//  EXPECT_TRUE(self_encryptor->Read(read_result.get(), file_size, 0));
//  for (uint32_t i = 0; i < file_size; ++i)
//    ASSERT_EQ(original[i], read_result[i]) << "difference at " << i;
// }
//
// TEST_F(DriveApiTest, BEH_WriteAndReadRandomContent) {
//  // As per previous WriteAndReadRandomChunks test, but this time with a file
//  // too small to result in chunking - all should be written to datamap's
//  // content
//  MetaData meta_data("test", false);
//  uint32_t num_chunks(2);
//  const uint32_t kTestDataSize(encrypt::kMinChunkSize * num_chunks);
//  std::string plain_text(RandomString(kTestDataSize));
//  std::vector<std::pair<uint64_t, std::string>> broken_data;
//  std::string extra("amended");
//  uint32_t file_size(kTestDataSize);
//
//  uint32_t i(0);
//  while (i < kTestDataSize) {
//    uint32_t size;
//    if (kTestDataSize - i < (50))
//      size = kTestDataSize - i;
//    else
//      size = RandomUint32() % (50);
//    std::pair<uint64_t, std::string> piece(i, plain_text.substr(i, size));
//    broken_data.push_back(piece);
//    i += size;
//  }
//
//  srand(RandomUint32());
//  std::random_shuffle(broken_data.begin(), broken_data.end());
//  std::pair<uint64_t, std::string> overlap(broken_data.back().first,
//                                           (broken_data.back().second + extra));
//  uint32_t position(static_cast<uint32_t>(broken_data.back().first +
//                                          broken_data.back().second.size()));
//
//  if (position / encrypt::kMinChunkSize == num_chunks)
//    file_size += static_cast<uint32_t>(extra.size());
//  plain_text.replace(position, extra.size(), extra);
//
//  {
//    SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(meta_data.data_map,
//                                                               client_nfs_,
//                                                               data_store_));
//    uint32_t wtotal(0);
//    for (auto it = broken_data.begin(); it != broken_data.end(); ++it) {
//      EXPECT_TRUE(self_encryptor->Write(it->second.c_str(),
//                                        static_cast<uint32_t>(it->second.size()),
//                                        it->first));
//
//      wtotal += static_cast<uint32_t>(it->second.size());
//    }
//    EXPECT_EQ(wtotal, kTestDataSize);
//    EXPECT_TRUE(self_encryptor->Write(overlap.second.c_str(),
//                                      static_cast<uint32_t>(overlap.second.size()),
//                                      overlap.first));
//  }
//  SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(meta_data.data_map,
//                                                             client_nfs_,
//                                                             data_store_));
//  EXPECT_EQ(0, meta_data.data_map->chunks.size());
//  EXPECT_EQ(file_size, TotalSize(meta_data.data_map));
//  EXPECT_EQ(file_size, meta_data.data_map->content.size());
//
//  boost::scoped_array<char> original(new char[file_size]);
//  std::copy(plain_text.data(), plain_text.data() + file_size, original.get());
//  boost::shared_array<char> read_result(new char[file_size]);
//  EXPECT_TRUE(self_encryptor->Read(read_result.get(), file_size, 0));
//  for (uint32_t i = 0; i < file_size; ++i)
//    ASSERT_EQ(original[i], read_result[i]) << "difference at " << i;
// }
//
// TEST_F(DriveApiTest, BEH_WriteAndReadIncompressible) {
//  typedef std::chrono::time_point<std::chrono::system_clock> chrono_time_point;
//
//  MetaData meta_data("test", false);
//  const uint32_t kTestDataSize((1024 * 1024 * 20) + 4);
//  std::string plain_text(RandomString(kTestDataSize));
//  boost::scoped_array<char> some_chunks_some_q(new char[kTestDataSize]);
//  {
//    SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(meta_data.data_map,
//                                                               client_nfs_,
//                                                               data_store_));
//    chrono_time_point time = std::chrono::system_clock::now();
//    EXPECT_TRUE(self_encryptor->Write(plain_text.c_str(), kTestDataSize, 0));
//
//    uint64_t duration = std::chrono::duration_cast<std::chrono::microseconds>(
//                          std::chrono::system_clock::now() - time).count();
//    if (duration == 0)
//      duration = 1;
//    uint64_t speed((static_cast<uint64_t>(kTestDataSize) * 1000000) / duration);
//
//    std::cout << "Written " << BytesToBinarySiUnits(kTestDataSize)
//              << " in " << (duration / 1000000.0) << " seconds at a speed of "
//              << BytesToBinarySiUnits(speed) << "/s" << std::endl;
//    EXPECT_TRUE(self_encryptor->Read(some_chunks_some_q.get(), kTestDataSize, 0));
//    for (uint32_t i = 0; i < kTestDataSize; ++i)
//      ASSERT_EQ(plain_text[i], some_chunks_some_q[i]) << "failed @ count " << i;
//  }
//  SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(meta_data.data_map,
//                                                             client_nfs_,
//                                                             data_store_));
//  boost::scoped_array<char> answer(new char[kTestDataSize]);
//  chrono_time_point time = std::chrono::system_clock::now();
//  EXPECT_TRUE(self_encryptor->Read(answer.get(), kTestDataSize, 0));
//
//  uint64_t duration = std::chrono::duration_cast<std::chrono::microseconds>(
//                        std::chrono::system_clock::now() - time).count();
//  if (duration == 0)
//    duration = 1;
//  uint64_t speed((static_cast<uint64_t>(kTestDataSize) * 1000000) / duration);
//
//  std::cout << "Read " << BytesToBinarySiUnits(kTestDataSize)
//            << " in " << (duration / 1000000.0) << " seconds at a speed of "
//            << BytesToBinarySiUnits(speed) << "/s" << std::endl;
//
//  for (uint32_t i = 0; i < kTestDataSize; ++i)
//    ASSERT_EQ(plain_text[i], answer[i]) << "failed at count " << i;
// }
//
// TEST_F(DriveApiTest, BEH_WriteAndReadCompressible) {
//  typedef std::chrono::time_point<std::chrono::system_clock> chrono_time_point;
//
//  MetaData meta_data("test", false);
//  const uint32_t kTestDataSize((1024 * 1024 * 20) + 36);
//  boost::scoped_array<char> plain_data(new char[kTestDataSize]);
//  for (uint32_t i = 0; i < kTestDataSize; ++i) {
//    plain_data[i] = 'a';
//  }
//
//  {
//    SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(meta_data.data_map,
//                                                               client_nfs_,
//                                                               data_store_));
//    chrono_time_point time = std::chrono::system_clock::now();
//
//    EXPECT_TRUE(self_encryptor->Write(plain_data.get(), kTestDataSize, 0));
//
//    uint64_t duration = std::chrono::duration_cast<std::chrono::microseconds>(
//                          std::chrono::system_clock::now() - time).count();
//    if (duration == 0)
//      duration = 1;
//    uint64_t speed((static_cast<uint64_t>(kTestDataSize) * 1000000) / duration);
//
//    std::cout << "Written " << BytesToBinarySiUnits(kTestDataSize)
//              << " in " << (duration / 1000000.0) << " seconds at a speed of "
//              << BytesToBinarySiUnits(speed) << "/s" << std::endl;
//  }
//
//  SelfEncryptorPtr self_encryptor(new encrypt::SelfEncryptor(meta_data.data_map,
//                                                             client_nfs_,
//                                                             data_store_));
//  boost::scoped_array<char> answer(new char[kTestDataSize]);
//  chrono_time_point time = std::chrono::system_clock::now();
//
//  EXPECT_TRUE(self_encryptor->Read(answer.get(), kTestDataSize, 0));
//
//  uint64_t duration = std::chrono::duration_cast<std::chrono::microseconds>(
//                        std::chrono::system_clock::now() - time).count();
//  if (duration == 0)
//    duration = 1;
//  uint64_t speed((static_cast<uint64_t>(kTestDataSize) * 1000000) / duration);
//
//  std::cout << "Read " << BytesToBinarySiUnits(kTestDataSize)
//            << " in " << (duration / 1000000.0) << " seconds at a speed of "
//            << BytesToBinarySiUnits(speed) << "/s" << std::endl;
//
//  for (uint32_t i = 0; i < kTestDataSize; ++i)
//    ASSERT_EQ(plain_data[i], answer[i]) << "failed at count " << i;
// }

}  // namespace test

}  // namespace drive

}  // namespace maidsafe
