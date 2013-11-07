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

#include "maidsafe/data_store/local_store.h"

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

class DirectoryHandlerTest {
 public:
  DirectoryHandlerTest()
      : main_test_dir_(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive")),
        data_store_(new data_store::LocalStore(*main_test_dir_, DiskUsage(1 << 30))),
        unique_user_id_(RandomString(64)),
        root_parent_id_(RandomString(64)),
        listing_handler_() {}

 protected:
  maidsafe::test::TestPath main_test_dir_;
  std::shared_ptr<data_store::LocalStore> data_store_;
  Identity unique_user_id_, root_parent_id_;
  std::shared_ptr<detail::DirectoryHandler<data_store::LocalStore>> listing_handler_;

 private:
  DirectoryHandlerTest(const DirectoryHandlerTest&);
  DirectoryHandlerTest& operator=(const DirectoryHandlerTest&);
};

TEST_CASE_METHOD(DirectoryHandlerTest, "Construct", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  Directory recovered_directory;
  MetaData recovered_meta_data;

  CHECK_NOTHROW(recovered_directory = listing_handler_->Get(""));
  CHECK(recovered_directory.parent_id == unique_user_id_);
  CHECK(recovered_directory.listing->directory_id() == root_parent_id_);
  CHECK(!recovered_directory.listing->empty());
  CHECK_NOTHROW(recovered_directory.listing->GetChild(kRoot, recovered_meta_data));
  CHECK(kRoot == recovered_meta_data.name);
  CHECK_NOTHROW(recovered_directory = listing_handler_->Get(kRoot));
  CHECK(recovered_directory.parent_id == root_parent_id_);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "AddDirectory", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string directory_name("Directory");
  MetaData meta_data(directory_name, true), recovered_meta_data;
  Directory directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / directory_name, meta_data, unique_user_id_,
                                      root_parent_id_));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / directory_name));
  CHECK(directory.listing->directory_id() == *meta_data.directory_id);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(directory.listing->GetChild(directory_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "AddSameDirectory", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string directory_name("Directory");
  MetaData meta_data(directory_name, true), recovered_meta_data;
  Directory directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / directory_name, meta_data, unique_user_id_,
                                      root_parent_id_));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / directory_name));
  CHECK(directory.listing->directory_id() == *meta_data.directory_id);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(directory.listing->GetChild(directory_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);

  CHECK_THROWS_AS(listing_handler_->Add(kRoot / directory_name, meta_data, unique_user_id_,
                  root_parent_id_), std::exception);
  CHECK_NOTHROW(directory.listing->GetChild(directory_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "AddFile", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string file_name("File");
  MetaData meta_data(file_name, false), recovered_meta_data;
  Directory directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / file_name, meta_data, unique_user_id_,
                                      root_parent_id_));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / file_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK(directory.listing->HasChild(file_name));
  CHECK_NOTHROW(directory.listing->GetChild(file_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "AddSameFile", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string file_name("File");
  MetaData meta_data(file_name, false), recovered_meta_data;
  Directory directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / file_name, meta_data, unique_user_id_,
                                      root_parent_id_));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / file_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK(directory.listing->HasChild(file_name));
  CHECK_NOTHROW(directory.listing->GetChild(file_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);

  CHECK_THROWS_AS(listing_handler_->Add(kRoot / file_name, meta_data, unique_user_id_,
                  root_parent_id_), std::exception);
  CHECK(directory.listing->HasChild(file_name));
  CHECK_NOTHROW(directory.listing->GetChild(file_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "DeleteDirectory", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string directory_name("Directory");
  MetaData meta_data(directory_name, true), recovered_meta_data;
  Directory directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / directory_name, meta_data, unique_user_id_,
                                      root_parent_id_));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / directory_name));
  CHECK(directory.listing->directory_id() == *meta_data.directory_id);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(directory.listing->GetChild(directory_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);

  CHECK_NOTHROW(listing_handler_->Delete(kRoot / directory_name));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / directory_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_THROWS_AS(directory.listing->GetChild(directory_name, recovered_meta_data),
                  std::exception);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "DeleteSameDirectory", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string directory_name("Directory");
  MetaData meta_data(directory_name, true), recovered_meta_data;
  Directory directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / directory_name, meta_data, unique_user_id_,
                                      root_parent_id_));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / directory_name));
  CHECK(directory.listing->directory_id() == *meta_data.directory_id);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(directory.listing->GetChild(directory_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);

  CHECK_NOTHROW(listing_handler_->Delete(kRoot / directory_name));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / directory_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_THROWS_AS(directory.listing->GetChild(directory_name, recovered_meta_data),
                  std::exception);

  CHECK_THROWS_AS(listing_handler_->Delete(kRoot / directory_name), std::exception);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "DeleteFile", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string file_name("File");
  MetaData meta_data(file_name, false), recovered_meta_data;
  Directory directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / file_name, meta_data, unique_user_id_,
                                      root_parent_id_));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / file_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(directory.listing->GetChild(file_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);

  CHECK_NOTHROW(listing_handler_->Delete(kRoot / file_name));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_THROWS_AS(directory.listing->GetChild(file_name, recovered_meta_data), std::exception);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "DeleteSameFile", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string file_name("File");
  MetaData meta_data(file_name, false), recovered_meta_data;
  Directory directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / file_name, meta_data, unique_user_id_,
                                      root_parent_id_));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / file_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(directory.listing->GetChild(file_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);

  CHECK_NOTHROW(listing_handler_->Delete(kRoot / file_name));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_THROWS_AS(directory.listing->GetChild(file_name, recovered_meta_data), std::exception);

  CHECK_THROWS_AS(listing_handler_->Delete(kRoot / file_name), std::exception);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "RenameMoveDirectory", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string first_directory_name("Directory1"), second_directory_name("Directory2"),
              old_directory_name("OldName"), new_directory_name("NewName");
  MetaData first_meta_data(first_directory_name, true),
           second_meta_data(second_directory_name, true),
           meta_data(old_directory_name, true), recovered_meta_data;
  Directory old_parent_directory, new_parent_directory, directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / first_directory_name, first_meta_data,
                                      unique_user_id_, root_parent_id_));
  CHECK_NOTHROW(listing_handler_->Add(kRoot / second_directory_name, second_meta_data,
                                      unique_user_id_, root_parent_id_));
  CHECK_NOTHROW(listing_handler_->Add(kRoot / first_directory_name / old_directory_name,
                                      meta_data, root_parent_id_, *first_meta_data.directory_id));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_NOTHROW(old_parent_directory.listing->GetChild(old_directory_name, recovered_meta_data));
  CHECK(old_directory_name == recovered_meta_data.name);
  CHECK_THROWS_AS(old_parent_directory.listing->GetChild(new_directory_name, recovered_meta_data),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(old_directory_name, recovered_meta_data),
                  std::exception);
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(new_directory_name, recovered_meta_data),
                  std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / first_directory_name /
                                                  old_directory_name));
  CHECK(directory.parent_id == old_parent_directory.listing->directory_id());
  CHECK(directory.listing->directory_id() == *meta_data.directory_id);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name /
                  new_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  old_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  new_directory_name), std::exception);

  CHECK_NOTHROW(listing_handler_->Rename(kRoot / first_directory_name / old_directory_name,
                                         kRoot / first_directory_name / new_directory_name,
                                         meta_data));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_THROWS_AS(old_parent_directory.listing->GetChild(old_directory_name, recovered_meta_data),
                  std::exception);
  CHECK_NOTHROW(old_parent_directory.listing->GetChild(new_directory_name, recovered_meta_data));
  CHECK(new_directory_name == recovered_meta_data.name);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(old_directory_name, recovered_meta_data),
                  std::exception);
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(new_directory_name, recovered_meta_data),
                  std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name /
                  old_directory_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / first_directory_name /
                                                  new_directory_name));
  CHECK(directory.parent_id == old_parent_directory.listing->directory_id());
  CHECK(directory.listing->directory_id() == *meta_data.directory_id);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  old_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  new_directory_name), std::exception);

  CHECK_THROWS_AS(listing_handler_->Rename(kRoot / first_directory_name / old_directory_name, kRoot
                  / second_directory_name / new_directory_name, meta_data), std::exception);
  CHECK_NOTHROW(listing_handler_->Rename(kRoot / first_directory_name / new_directory_name, kRoot /
                second_directory_name / new_directory_name, meta_data));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_THROWS_AS(old_parent_directory.listing->GetChild(old_directory_name, recovered_meta_data),
                  std::exception);
  CHECK_THROWS_AS(old_parent_directory.listing->GetChild(new_directory_name, recovered_meta_data),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(old_directory_name, recovered_meta_data),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory.listing->GetChild(new_directory_name, recovered_meta_data));
  CHECK(new_directory_name == recovered_meta_data.name);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name /
                  old_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name /
                  new_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  old_directory_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / second_directory_name /
                                                  new_directory_name));
  CHECK(directory.parent_id == new_parent_directory.listing->directory_id());
  CHECK(directory.listing->directory_id() == *meta_data.directory_id);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "RenameMoveFile", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string first_directory_name("Directory1"), second_directory_name("Directory2"),
              old_file_name("OldName"), new_file_name("NewName");
  MetaData first_meta_data(first_directory_name, true),
           second_meta_data(second_directory_name, true),
           meta_data(old_file_name, false), recovered_meta_data;
  Directory old_parent_directory, new_parent_directory, directory;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / first_directory_name, first_meta_data,
                                      unique_user_id_, root_parent_id_));
  CHECK_NOTHROW(listing_handler_->Add(kRoot / second_directory_name, second_meta_data,
                                      unique_user_id_, root_parent_id_));
  CHECK_NOTHROW(listing_handler_->Add(kRoot / first_directory_name / old_file_name,
                                      meta_data, root_parent_id_, *first_meta_data.directory_id));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_NOTHROW(old_parent_directory.listing->GetChild(old_file_name, recovered_meta_data));
  CHECK(old_file_name == recovered_meta_data.name);
  CHECK_THROWS_AS(old_parent_directory.listing->GetChild(new_file_name, recovered_meta_data),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(old_file_name, recovered_meta_data),
                  std::exception);
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(new_file_name, recovered_meta_data),
                  std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name / old_file_name),
                  std::exception);

  CHECK_NOTHROW(listing_handler_->Rename(kRoot / first_directory_name / old_file_name,
                                         kRoot / first_directory_name / new_file_name,
                                         meta_data));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_THROWS_AS(old_parent_directory.listing->GetChild(old_file_name, recovered_meta_data),
                  std::exception);
  CHECK_NOTHROW(old_parent_directory.listing->GetChild(new_file_name, recovered_meta_data));
  CHECK(new_file_name == recovered_meta_data.name);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(old_file_name, recovered_meta_data),
                  std::exception);
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(new_file_name, recovered_meta_data),
                  std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name / new_file_name),
                  std::exception);

  CHECK_NOTHROW(recovered_meta_data.name = old_file_name);
  CHECK(recovered_meta_data.name == old_file_name);
  CHECK_THROWS_AS(listing_handler_->Rename(kRoot / first_directory_name / old_file_name, kRoot /
                  second_directory_name / new_file_name, recovered_meta_data), std::exception);
  CHECK_NOTHROW(recovered_meta_data.name = new_file_name);
  CHECK(recovered_meta_data.name == new_file_name);
  CHECK_NOTHROW(listing_handler_->Rename(kRoot / first_directory_name / new_file_name, kRoot /
                second_directory_name / new_file_name, recovered_meta_data));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_THROWS_AS(old_parent_directory.listing->GetChild(old_file_name, recovered_meta_data),
                  std::exception);
  CHECK_THROWS_AS(old_parent_directory.listing->GetChild(new_file_name, recovered_meta_data),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(new_parent_directory.listing->GetChild(old_file_name, recovered_meta_data),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory.listing->GetChild(new_file_name, recovered_meta_data));
  CHECK(new_file_name == recovered_meta_data.name);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  new_file_name), std::exception);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "UpdateParent", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_store::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, true));
  std::string file_name("File"), file_content;
  MetaData meta_data(file_name, false), recovered_meta_data;
  Directory directory;
  encrypt::DataMap data_map;

  CHECK_NOTHROW(listing_handler_->Add(kRoot / file_name, meta_data, unique_user_id_,
                root_parent_id_));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / file_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(directory.listing->GetChild(file_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);
  CHECK(recovered_meta_data.data_map->content == file_content);

  CHECK_NOTHROW(file_content = "A");
  CHECK_NOTHROW(data_map.content = file_content);
  CHECK_NOTHROW(*meta_data.data_map = data_map);
  CHECK_NOTHROW(listing_handler_->UpdateParent(kRoot, meta_data));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(directory.listing->GetChild(file_name, recovered_meta_data));
  CHECK(meta_data.name == recovered_meta_data.name);
  CHECK(recovered_meta_data.data_map->content == file_content);
}

}  // namespace test
}  // namespace detail
}  // namespace drive
}  // namespace maidsafe
