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
#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/data_stores/local_store.h"

#include "maidsafe/encrypt/data_map.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory.h"
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
        data_store_(new data_stores::LocalStore(*main_test_dir_, DiskUsage(1 << 30))),
        unique_user_id_(RandomString(64)),
        root_parent_id_(RandomString(64)),
        asio_service_(2),
        listing_handler_() {}
  ~DirectoryHandlerTest() { asio_service_.Stop(); }

 protected:
  maidsafe::test::TestPath main_test_dir_;
  std::shared_ptr<data_stores::LocalStore> data_store_;
  Identity unique_user_id_, root_parent_id_;
  AsioService asio_service_;
  std::shared_ptr<detail::DirectoryHandler<data_stores::LocalStore>> listing_handler_;

 private:
  DirectoryHandlerTest(const DirectoryHandlerTest&);
  DirectoryHandlerTest& operator=(const DirectoryHandlerTest&);
};

TEST_CASE_METHOD(DirectoryHandlerTest, "Construct", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  Directory* recovered_directory(nullptr);
  const FileContext* recovered_file_context(nullptr);

  CHECK_NOTHROW(recovered_directory = listing_handler_->Get(""));
  CHECK(recovered_directory->parent_id().data == unique_user_id_);
  CHECK(recovered_directory->directory_id() == root_parent_id_);
  CHECK(!recovered_directory->empty());
  CHECK_NOTHROW(recovered_file_context = recovered_directory->GetChild(kRoot));
  CHECK(kRoot == recovered_file_context->meta_data.name);
  CHECK_NOTHROW(recovered_directory = listing_handler_->Get(kRoot));
  CHECK(recovered_directory->parent_id().data == root_parent_id_);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Add directory", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string directory_name("Directory");
  FileContext file_context(directory_name, true);
  const FileContext* recovered_file_context(nullptr);
  Directory* directory(nullptr);
  DirectoryId dir(*file_context.meta_data.directory_id);
  CHECK_NOTHROW(listing_handler_->Add(kRoot / directory_name, std::move(file_context)));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / directory_name));
  CHECK(directory->directory_id() == dir);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(directory_name));
  CHECK(file_context.meta_data.name == recovered_file_context->meta_data.name);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Add same directory", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string directory_name("Directory");
  FileContext file_context(directory_name, true);
  DirectoryId dir(*file_context.meta_data.directory_id);
  const FileContext* recovered_file_context(nullptr);
  Directory* directory(nullptr);
  boost::filesystem::path meta_data_name(file_context.meta_data.name);
  CHECK_NOTHROW(listing_handler_->Add(kRoot / directory_name, std::move(file_context)));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / directory_name));
  CHECK(directory->directory_id() == dir);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(directory_name));
  CHECK(meta_data_name == recovered_file_context->meta_data.name);

  CHECK_THROWS_AS(listing_handler_->Add(kRoot / directory_name, FileContext(directory_name, true)),
                  std::exception);
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(directory_name));
  CHECK(meta_data_name == recovered_file_context->meta_data.name);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Add file", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string file_name("File");
  FileContext file_context(file_name, false);
  const FileContext* recovered_file_context(nullptr);
  Directory* directory(nullptr);

  CHECK_NOTHROW(listing_handler_->Add(kRoot / file_name, std::move(file_context)));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / file_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK(directory->HasChild(file_name));
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(file_name));
  CHECK(file_context.meta_data.name == recovered_file_context->meta_data.name);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Add same file", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string file_name("File");
  FileContext file_context(file_name, false);
  const FileContext* recovered_file_context(nullptr);
  Directory* directory(nullptr);

  CHECK_NOTHROW(listing_handler_->Add(kRoot / file_name, std::move(file_context)));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / file_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK(directory->HasChild(file_name));
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(file_name));
  CHECK(file_context.meta_data.name == recovered_file_context->meta_data.name);

  CHECK(directory->HasChild(file_name));
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(file_name));
  CHECK(file_context.meta_data.name == recovered_file_context->meta_data.name);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Delete directory", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string directory_name("Directory");
  FileContext file_context(directory_name, true);
  const FileContext* recovered_file_context(nullptr);
  Directory* directory(nullptr);
  DirectoryId dir(*file_context.meta_data.directory_id);

  CHECK_NOTHROW(listing_handler_->Add(kRoot / directory_name, std::move(file_context)));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / directory_name));
  CHECK(directory->directory_id() == dir);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(directory_name));
  CHECK(file_context.meta_data.name == recovered_file_context->meta_data.name);

  CHECK_NOTHROW(listing_handler_->Delete(kRoot / directory_name));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / directory_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_THROWS_AS(recovered_file_context = directory->GetChild(directory_name),
                  std::exception);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Delete same directory", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string directory_name("Directory");
  FileContext file_context(directory_name, true);
  const FileContext* recovered_file_context(nullptr);
  Directory* directory(nullptr);
  DirectoryId dir(*file_context.meta_data.directory_id);

  CHECK_NOTHROW(listing_handler_->Add(kRoot / directory_name, std::move(file_context)));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / directory_name));
  CHECK(directory->directory_id() == dir);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(directory_name));
  CHECK(file_context.meta_data.name == recovered_file_context->meta_data.name);

  CHECK_NOTHROW(listing_handler_->Delete(kRoot / directory_name));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / directory_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_THROWS_AS(recovered_file_context = directory->GetChild(directory_name),
                  std::exception);
  CHECK_THROWS_AS(listing_handler_->Delete(kRoot / directory_name), std::exception);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Delete file", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string file_name("File");
  FileContext file_context(file_name, false);
  const FileContext* recovered_file_context(nullptr);
  Directory* directory(nullptr);

  CHECK_NOTHROW(listing_handler_->Add(kRoot / file_name, std::move(file_context)));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / file_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(file_name));
  CHECK(file_context.meta_data.name == recovered_file_context->meta_data.name);

  CHECK_NOTHROW(listing_handler_->Delete(kRoot / file_name));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_THROWS_AS(recovered_file_context = directory->GetChild(file_name), std::exception);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Delete same file", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string file_name("File");
  FileContext file_context(file_name, false);
  const FileContext* recovered_file_context(nullptr);
  Directory* directory(nullptr);

  CHECK_NOTHROW(listing_handler_->Add(kRoot / file_name, std::move(file_context)));
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / file_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_NOTHROW(recovered_file_context = directory->GetChild(file_name));
  CHECK(file_context.meta_data.name == recovered_file_context->meta_data.name);

  CHECK_NOTHROW(listing_handler_->Delete(kRoot / file_name));
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot));
  CHECK_THROWS_AS(recovered_file_context = directory->GetChild(file_name), std::exception);

  CHECK_THROWS_AS(listing_handler_->Delete(kRoot / file_name), std::exception);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Rename and move directory",
                 "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string first_directory_name("Directory1"), second_directory_name("Directory2"),
              old_directory_name("OldName"), new_directory_name("NewName");
  FileContext first_file_context(first_directory_name, true),
              second_file_context(second_directory_name, true),
              file_context(old_directory_name, true);
  const FileContext* recovered_file_context(nullptr);
  Directory *old_parent_directory(nullptr), *new_parent_directory(nullptr), *directory(nullptr);

  CHECK_NOTHROW(listing_handler_->Add(kRoot / first_directory_name,
                std::move(first_file_context)));
  CHECK_NOTHROW(listing_handler_->Add(kRoot / second_directory_name,
                std::move(second_file_context)));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_NOTHROW(file_context.parent = old_parent_directory);
  DirectoryId dir(*file_context.meta_data.directory_id);
  CHECK_NOTHROW(listing_handler_->Add(kRoot / first_directory_name / old_directory_name,
                std::move(file_context)));

  CHECK_NOTHROW(recovered_file_context = old_parent_directory->GetChild(old_directory_name));
  CHECK(old_directory_name == recovered_file_context->meta_data.name);

  CHECK_THROWS_AS(recovered_file_context = old_parent_directory->GetChild(new_directory_name),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(old_directory_name),
                  std::exception);
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(new_directory_name),
                  std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / first_directory_name /
                                                  old_directory_name));
  CHECK(directory->parent_id().data == old_parent_directory->directory_id());
  CHECK(directory->directory_id() == dir);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name /
                  new_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  old_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  new_directory_name), std::exception);

  CHECK_NOTHROW(listing_handler_->Rename(kRoot / first_directory_name / old_directory_name,
                                         kRoot / first_directory_name / new_directory_name));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_THROWS_AS(recovered_file_context = old_parent_directory->GetChild(old_directory_name),
                  std::exception);
  CHECK_NOTHROW(recovered_file_context = old_parent_directory->GetChild(new_directory_name));
  CHECK(new_directory_name == recovered_file_context->meta_data.name);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(old_directory_name),
                  std::exception);
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(new_directory_name),
                  std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name /
                  old_directory_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / first_directory_name /
                                                  new_directory_name));
  CHECK(directory->parent_id().data == old_parent_directory->directory_id());
  CHECK(directory->directory_id() == *recovered_file_context->meta_data.directory_id);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  old_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  new_directory_name), std::exception);

  CHECK_THROWS_AS(listing_handler_->Rename(kRoot / first_directory_name / old_directory_name, kRoot
                  / second_directory_name / new_directory_name), std::exception);
  CHECK_NOTHROW(listing_handler_->Rename(kRoot / first_directory_name / new_directory_name, kRoot /
                second_directory_name / new_directory_name));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_THROWS_AS(recovered_file_context = old_parent_directory->GetChild(old_directory_name),
                  std::exception);
  CHECK_THROWS_AS(recovered_file_context = old_parent_directory->GetChild(new_directory_name),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(old_directory_name),
                  std::exception);
  CHECK_NOTHROW(recovered_file_context = new_parent_directory->GetChild(new_directory_name));
  CHECK(new_directory_name == recovered_file_context->meta_data.name);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name /
                  old_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / first_directory_name /
                  new_directory_name), std::exception);
  CHECK_THROWS_AS(directory = listing_handler_->Get(kRoot / second_directory_name /
                  old_directory_name), std::exception);
  CHECK_NOTHROW(directory = listing_handler_->Get(kRoot / second_directory_name /
                                                  new_directory_name));
  CHECK(directory->parent_id().data == new_parent_directory->directory_id());
  CHECK(directory->directory_id() == dir);
}

TEST_CASE_METHOD(DirectoryHandlerTest, "Rename and move file", "[DirectoryHandler][behavioural]") {
  listing_handler_.reset(new detail::DirectoryHandler<data_stores::LocalStore>(
      data_store_, unique_user_id_, root_parent_id_, boost::filesystem::unique_path(GetUserAppDir()
      / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true, asio_service_.service()));
  std::string first_directory_name("Directory1"), second_directory_name("Directory2"),
              old_file_name("OldName"), new_file_name("NewName");
  FileContext first_file_context(first_directory_name, true),
              second_file_context(second_directory_name, true),
              file_context(old_file_name, false);
  const FileContext* recovered_file_context(nullptr);
  Directory *old_parent_directory(nullptr), *new_parent_directory(nullptr);
  CHECK_NOTHROW(listing_handler_->Add(kRoot / first_directory_name,
                std::move(first_file_context)));
  CHECK_NOTHROW(listing_handler_->Add(kRoot / second_directory_name,
                std::move(second_file_context)));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_NOTHROW(file_context.parent = old_parent_directory);
  CHECK_NOTHROW(listing_handler_->Add(kRoot / first_directory_name / old_file_name,
                std::move(file_context)));

  CHECK_NOTHROW(recovered_file_context = old_parent_directory->GetChild(old_file_name));
  CHECK(old_file_name == recovered_file_context->meta_data.name);
  CHECK_THROWS_AS(recovered_file_context = old_parent_directory->GetChild(new_file_name),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(old_file_name),
                  std::exception);
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(new_file_name),
                  std::exception);

  CHECK_THROWS_AS(listing_handler_->Get(kRoot / first_directory_name / old_file_name),
                  std::exception);

  CHECK_NOTHROW(listing_handler_->Rename(kRoot / first_directory_name / old_file_name,
                                         kRoot / first_directory_name / new_file_name));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_THROWS_AS(recovered_file_context = old_parent_directory->GetChild(old_file_name),
                  std::exception);
  CHECK_NOTHROW(recovered_file_context = old_parent_directory->GetChild(new_file_name));
  CHECK(new_file_name == recovered_file_context->meta_data.name);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(old_file_name),
                  std::exception);
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(new_file_name),
                  std::exception);
  CHECK_THROWS_AS(listing_handler_->Get(kRoot / first_directory_name / new_file_name),
                  std::exception);

  CHECK_THROWS_AS(listing_handler_->Rename(kRoot / first_directory_name / old_file_name, kRoot /
                  second_directory_name / new_file_name), std::exception);

  CHECK_NOTHROW(listing_handler_->Rename(kRoot / first_directory_name / new_file_name, kRoot /
                second_directory_name / new_file_name));

  CHECK_NOTHROW(old_parent_directory = listing_handler_->Get(kRoot / first_directory_name));
  CHECK_THROWS_AS(recovered_file_context = old_parent_directory->GetChild(old_file_name),
                  std::exception);
  CHECK_THROWS_AS(recovered_file_context = old_parent_directory->GetChild(new_file_name),
                  std::exception);
  CHECK_NOTHROW(new_parent_directory = listing_handler_->Get(kRoot / second_directory_name));
  CHECK_THROWS_AS(recovered_file_context = new_parent_directory->GetChild(old_file_name),
                  std::exception);
  CHECK_NOTHROW(recovered_file_context = new_parent_directory->GetChild(new_file_name));
  CHECK(new_file_name == recovered_file_context->meta_data.name);
  CHECK_THROWS_AS(listing_handler_->Get(kRoot / second_directory_name / new_file_name),
                  std::exception);
}

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
