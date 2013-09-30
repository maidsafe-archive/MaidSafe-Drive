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
#include <mutex>
#include <string>

#include "boost/filesystem/path.hpp"

#include "maidsafe/common/asio_service.h"
#include "maidsafe/common/crypto.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/data_store/sure_file_store.h"

#include "maidsafe/encrypt/data_map.h"

#include "maidsafe/routing/routing_api.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/tests/test_utils.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace detail {

namespace test {

// TODO(Team): 2013-09-25 - Uncomment and fix or delete
// template<typename Storage>
// class FailDirectoryHandler : public DirectoryHandler<Storage> {
// public:
//  typedef nfs::ClientMaidNfs ClientNfs;
//  typedef data_store::PermanentStore DataStore;
//
//  enum { kValue = DirectoryHandler::DataTagValue::kOwnerDirectoryValue };
//
//  FailDirectoryHandler(ClientNfs& client_nfs,
//                              DataStore& data_store,
//                              const Identity& unique_user_id,
//                              const Identity& drive_root_id,
//                              int fail_for_put,
//                              bool use_real)
//      : DirectoryHandler(client_nfs, data_store, maid, unique_user_id, drive_root_id),
//        fail_for_put_(fail_for_put),
//        fail_count_(0),
//        use_real_(use_real) {}
//
//  ~FailDirectoryHandler() {}
//  Directory GetDirectoryFromStorage(const DirectoryId &pid,
//                                    const DirectoryId &id) const {
//    if (use_real_)
//      return DirectoryHandler::GetFromStorage(pid, id, kValue);
//    else
//      return Directory();
//  }
//  void PutToStorage(Directory data) {
//    if (++fail_count_ == fail_for_put_)
//      ThrowError(CommonErrors::invalid_parameter);
//    else if (use_real_)
//      DirectoryHandler::PutToStorage(std::make_pair(data, kValue));
//    else
//      return;
//  }
//  void DeleteFromStorage(const DirectoryId &pid, const DirectoryId &id) {
//    if (use_real_)
//      DirectoryHandler::DeleteFromStorage(pid, id, kValue);
//    else
//      return;
//  }
//
// private:
//  friend class test::DirectoryHandlerTest;
//  int fail_for_put_;
//  int fail_count_;
//  bool use_real_;
// };

struct TestTreeEntry {
  TestTreeEntry() : path(), leaf(true) {}
  TestTreeEntry(const fs::path fs_path, bool leafness) : path(fs_path), leaf(leafness) {}
  fs::path path;
  bool leaf;
};

class DirectoryHandlerTest : public testing::Test {
 public:
  DirectoryHandlerTest()
      : main_test_dir_(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive")),
        data_store_(new data_store::SureFileStore(*main_test_dir_, DiskUsage(1 << 30))),
        owner_(kRoot / "Owner"),
        owner_meta_data_(owner_, true),
        unique_user_id_(RandomString(64)),
        listing_handler_(),
        created_paths_(),
        created_paths_mutex_() {}
  //
  //  typedef nfs::ClientMaidNfs ClientNfs;
  //  typedef data_store::PermanentStore DataStore;
  //
  // protected:
  //  void SetUp() {
  //    DiskUsage disk_usage(1048576000);
  //    data_store_.reset(new DataStore(*main_test_dir_ / RandomAlphaNumericString(8), disk_usage));
  //    client_nfs_.reset(new ClientNfs(routing_, default_maid_));
  //    listing_handler_.reset(new DirectoryHandler(*client_nfs_,
  //                                                       *data_store_,
  //                                                       default_maid_,
  //                                                       unique_user_id_,
  //                                                       ""));
  //  }
  //
  //  void TearDown() {}
  //
  //  void FullCoverageAddElement() {
  //    // Originally tested cached directories which are now gone...
  //    // Failure to get parent
  //    fs::path test_path(owner_ / "some_path");
  //    MetaData meta_data(test_path.filename(), true);
  //    // Successful addition and failure to add same again
  //    EXPECT_NO_THROW(listing_handler_->AddElement(test_path, meta_data, nullptr, nullptr));
  //    EXPECT_THROW(listing_handler_->AddElement(test_path, meta_data, nullptr, nullptr),
  //                 std::exception);
  //    // Make the add child fail with different path but same meta_data
  //    test_path = fs::path(owner_ / "some_other_path");
  //    EXPECT_THROW(listing_handler_->AddElement(test_path, meta_data, nullptr, nullptr),
  //                 std::exception);
  //    test_path = fs::path(owner_ / "some_path");
  //    EXPECT_NO_THROW(listing_handler_->DeleteElement(test_path, meta_data));
  //    EXPECT_NO_THROW(listing_handler_->AddElement(test_path, meta_data, nullptr, nullptr));
  //    test_path = fs::path(owner_ / "and_yet_one_more");
  //    meta_data = MetaData(test_path.filename(), true);
  //    EXPECT_NO_THROW(listing_handler_->AddElement(test_path, meta_data, nullptr, nullptr));
  //  }
  //
  //  void FullCoverageByPath() {
  //    // Originally tested cached directories which are now gone...
  //    // Get the root listing from storage
  //    auto directory(listing_handler_->GetFromPath("/"));
  //    EXPECT_EQ(directory.first.parent_id, listing_handler_->drive_root_id_);
  //    // Try to get the non-existant dir listing
  //    EXPECT_THROW(directory = listing_handler_->GetFromPath(owner_ / "some_dir"),
  // std::exception);
  //    // Adding some_dir
  //    fs::path path(owner_ / "some_dir");
  //    MetaData meta_data(path.filename(), true);
  // #ifdef MAIDSAFE_WIN32
  //    meta_data.attributes = FILE_ATTRIBUTE_DIRECTORY;
  //    GetSystemTimeAsFileTime(&meta_data.creation_time);
  //    GetSystemTimeAsFileTime(&meta_data.last_access_time);
  //    GetSystemTimeAsFileTime(&meta_data.last_write_time);
  // #else
  //    time(&meta_data.attributes.st_atime);
  //    time(&meta_data.attributes.st_mtime);
  // #endif
  //    DirectoryId grandparent_dir_id, parent_dir_id;
  //    EXPECT_NO_THROW(listing_handler_->AddElement(path, meta_data, nullptr, nullptr));
  //    EXPECT_NO_THROW(listing_handler_->DeleteElement(path, meta_data));
  //    // Try to get the non-nexistant dir listing...
  //    EXPECT_THROW(directory = listing_handler_->GetFromPath(owner_ / "some_dir/another_dir"),
  //                 std::exception);
  //  }
  //
  //  void AddToListing(int id, const uint8_t total_elements) {
  //    fs::path directory;
  //    for (;;) {
  //      boost::this_thread::sleep(boost::posix_time::milliseconds((id + 1) * 50));
  //      {
  //        boost::mutex::scoped_lock loch_easaidh(created_paths_mutex_);
  //        if (created_paths_.size() >= total_elements) {
  //          return;
  //        } else {
  //          if (RandomUint32() % 2 == 0 || created_paths_.size() == 0) {
  //            directory = fs::path(relative_root_ / RandomAlphaNumericString(5));
  //          } else {
  //            size_t index = RandomUint32() % created_paths_.size();
  //            if (created_paths_.at(index).leaf)
  //              created_paths_.at(index).leaf = false;
  //            directory = fs::path(created_paths_.at(index).path / RandomAlphaNumericString(5));
  //          }
  //          created_paths_.push_back(TestTreeEntry(directory, true));
  //        }
  //      }
  //
  //      MetaData meta_data(directory.filename(), true);
  //  #ifdef MAIDSAFE_WIN32
  //      meta_data.attributes = FILE_ATTRIBUTE_DIRECTORY;
  //      GetSystemTimeAsFileTime(&meta_data.creation_time);
  //      GetSystemTimeAsFileTime(&meta_data.last_access_time);
  //      GetSystemTimeAsFileTime(&meta_data.last_write_time);
  //  #else
  //      time(&meta_data.attributes.st_atime);
  //      time(&meta_data.attributes.st_mtime);
  //  #endif
  //      EXPECT_NO_THROW(listing_handler_->AddElement(directory, meta_data, nullptr, nullptr));
  //    }
  //  }
  //
  //  void QueryFromListing(int id, const uint8_t total_queries, uint8_t *queries_so_far) {
  //    fs::path search;
  //    for (;;) {
  //      boost::this_thread::sleep(boost::posix_time::milliseconds((id + 1) * 50));
  //      {
  //        boost::mutex::scoped_lock loch_leitreach(created_paths_mutex_);
  //        if (*queries_so_far >= total_queries) {
  //          return;
  //        } else if (created_paths_.size() > 0) {
  //          search = created_paths_.at(RandomUint32() % created_paths_.size()).path;
  //          ++(*queries_so_far);
  //          auto directory(listing_handler_->GetFromPath(search));
  //        }
  //      }
  //    }
  //  }
  //
  //  void EraseFromListing(int id, const uint8_t total_deletes, uint8_t *deletes_so_far) {
  //    fs::path search;
  //    MetaData meta_data;
  //    for (;;) {
  //      boost::this_thread::sleep(boost::posix_time::milliseconds((id + 1) * 30));
  //      {
  //        boost::mutex::scoped_lock loch_leitreach(created_paths_mutex_);
  //        if (*deletes_so_far >= total_deletes) {
  //          return;
  //        } else if (created_paths_.size() > 0) {
  //          size_t index = RandomUint32() % created_paths_.size();
  //          if (created_paths_.at(index).leaf) {
  //            search = created_paths_.at(index).path;
  //            created_paths_.erase(created_paths_.begin() + index);
  //            ++(*deletes_so_far);
  //            EXPECT_NO_THROW(listing_handler_->DeleteElement(search, meta_data));
  //          }
  //        }
  //      }
  //    }
  //  }
  //
  maidsafe::test::TestPath main_test_dir_;
  std::shared_ptr<data_store::SureFileStore> data_store_;
  fs::path owner_;
  MetaData owner_meta_data_;
  Identity unique_user_id_;
  std::shared_ptr<detail::DirectoryHandler<data_store::SureFileStore>> listing_handler_;
  std::vector<TestTreeEntry> created_paths_;
  std::mutex created_paths_mutex_;

 private:
  DirectoryHandlerTest(const DirectoryHandlerTest&);
  DirectoryHandlerTest& operator=(const DirectoryHandlerTest&);
};

TEST_F(DirectoryHandlerTest, BEH_Construct) {
  // Directory root(Identity(RandomString(64)),
  //               std::make_shared<DirectoryListing>(Identity(RandomString(64))),
  //               nullptr, DataTagValue::kOwnerDirectoryValue);
  listing_handler_.reset(new detail::DirectoryHandler<data_store::SureFileStore>(
      data_store_, DataTagValue::kOwnerDirectoryValue));

  Directory owner(Identity(RandomString(64)),
                  std::make_shared<DirectoryListing>(Identity(RandomString(64))), nullptr,
                  DataTagValue::kOwnerDirectoryValue);
  Directory group(Identity(RandomString(64)),
                  std::make_shared<DirectoryListing>(Identity(RandomString(64))), nullptr,
                  DataTagValue::kGroupDirectoryValue);
  Directory world(Identity(RandomString(64)),
                  std::make_shared<DirectoryListing>(Identity(RandomString(64))), nullptr,
                  DataTagValue::kWorldDirectoryValue);

  PutToStorage(*data_store_, owner);
  PutToStorage(*data_store_, group);
  PutToStorage(*data_store_, world);
  auto owner_recovered(
      GetFromStorage(*data_store_, owner.parent_id, owner.listing->directory_id(), owner.type));
  auto group_recovered(
      GetFromStorage(*data_store_, group.parent_id, group.listing->directory_id(), group.type));
  auto world_recovered(
      GetFromStorage(*data_store_, world.parent_id, world.listing->directory_id(), world.type));
  DeleteFromStorage(*data_store_, owner);
  DeleteFromStorage(*data_store_, group);
  DeleteFromStorage(*data_store_, world);
}

// TODO(Team): 2013-09-25 - Uncomment and fix or delete
// TEST_F(DirectoryHandlerTest, BEH_Construct) {
//  EXPECT_NO_THROW(DirectoryHandler local_listing_handler(*client_nfs_,
//                                                                *data_store_,
//                                                                unique_user_id_,
//                                                                ""));
//  /*EXPECT_THROW(FailDirectoryHandler fail_listing_handler(*client_nfs_,
//                                                                *data_store_,
//                                                                maid,
//                                                                unique_user_id_,
//                                                                "",
//                                                                1,
//                                                                true),
//               std::exception);
//  EXPECT_THROW(FailDirectoryHandler fail_listing_handler(*client_nfs_,
//                                                                *data_store_,
//                                                                maid,
//                                                                unique_user_id_,
//                                                                "",
//                                                                2,
//                                                                true),
//               std::exception);*/
// }
//
// TEST_F(DirectoryHandlerTest, BEH_GetDirectoryDataByPath) {
//  FullCoverageByPath();
// }
//
// TEST_F(DirectoryHandlerTest, BEH_AddElement) {
//  FullCoverageAddElement();
// }
//
// TEST_F(DirectoryHandlerTest, BEH_AddThenDelete) {
//  {
//    // Add then Delete Directory Element
//    MetaData directory_meta("directory_test", true);
//    EXPECT_NO_THROW(listing_handler_->AddElement(owner_ / "test",
//                                                 directory_meta,
//                                                 nullptr,
//                                                 &(*owner_meta_data_.directory_id)));
//    EXPECT_NO_THROW(listing_handler_->DeleteElement(owner_ / "directory_test", directory_meta));
//  }
//  {
//    // Add then Delete File Element
//    MetaData file_meta("file_test", false);
//    EXPECT_NO_THROW(listing_handler_->AddElement(owner_ / "test",
//                                                 file_meta,
//                                                 nullptr,
//                                                 &(*owner_meta_data_.directory_id)));
//    EXPECT_NO_THROW(listing_handler_->DeleteElement(owner_ / "file_test", file_meta));
//  }
// }
//
// TEST_F(DirectoryHandlerTest, BEH_RenameElement) {
//  MetaData directory_meta("test", true);
//  EXPECT_NO_THROW(listing_handler_->AddElement(owner_ / "test",
//                                               directory_meta,
//                                               nullptr,
//                                               &(*owner_meta_data_.directory_id)));
//  int64_t reclaimed_size;
//  EXPECT_NO_THROW(listing_handler_->RenameElement(owner_ / "test",
//                                                  owner_ / "new_test",
//                                                  directory_meta,
//                                                  reclaimed_size));
//  EXPECT_THROW(listing_handler_->DeleteElement(owner_ / "test", directory_meta),
//               std::exception);
//  MetaData new_meta("new_test", true);
//  EXPECT_NO_THROW(listing_handler_->DeleteElement(owner_ / "new_test", new_meta));
// }
//
// TEST_F(DirectoryHandlerTest, BEH_UpdateParentDirectoryListing) {
//  MetaData directory_meta("test", true);
//  EXPECT_NO_THROW(listing_handler_->AddElement(owner_ / "test",
//                                               directory_meta,
//                                               nullptr,
//                                               &(*owner_meta_data_.directory_id)));
//  MetaData non_exists_meta("non_exists", true);
//  EXPECT_THROW(listing_handler_->UpdateParentDirectoryListing(owner_, non_exists_meta),
//               std::exception);
//  MetaData new_meta("test", true);
// #ifdef MAIDSAFE_WIN32
//  GetSystemTimeAsFileTime(&new_meta.last_access_time);
// #else
//  time(&new_meta.attributes.st_atime);
// #endif
//  EXPECT_NO_THROW(listing_handler_->UpdateParentDirectoryListing(owner_, new_meta));
//  MetaData result_temp;
//  EXPECT_NO_THROW(
//    listing_handler_->GetFromPath(owner_).first.listing->GetChild("test", result_temp));
// #ifdef MAIDSAFE_WIN32
//  EXPECT_EQ(new_meta.last_access_time.dwHighDateTime,
//            result_temp.last_access_time.dwHighDateTime);
//  // This fails due to time conversion inaccuracies...
//  /*EXPECT_EQ(new_meta.last_access_time.dwLowDateTime,
//            result_temp.last_access_time.dwLowDateTime);*/
// #else
//  EXPECT_EQ(new_meta.attributes.st_atime, result_temp.attributes.st_atime);
// #endif
// }

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
