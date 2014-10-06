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

#include "maidsafe/encrypt/data_map.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/tests/test_utils.h"

#include "maidsafe/nfs/client/fake_store.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace detail {

namespace test {

class DirectoryHandlerTest : public testing::Test {
 public:
  DirectoryHandlerTest()
      : main_test_dir_(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive")),
        data_store_(new nfs::FakeStore(*main_test_dir_, DiskUsage(1 << 30))),
        unique_user_id_(RandomString(64)),
        root_parent_id_(RandomString(64)),
        asio_service_(2),
        listing_handler_() {}
  ~DirectoryHandlerTest() { asio_service_.Stop(); }
  DirectoryHandlerTest(const DirectoryHandlerTest&) = delete;
  DirectoryHandlerTest& operator=(const DirectoryHandlerTest&) = delete;

 protected:
  maidsafe::test::TestPath main_test_dir_;
  std::shared_ptr<nfs::FakeStore> data_store_;
  Identity unique_user_id_, root_parent_id_;
  AsioService asio_service_;
  std::shared_ptr<detail::DirectoryHandler<nfs::FakeStore>> listing_handler_;
};

TEST_F(DirectoryHandlerTest, BEH_Construct) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::shared_ptr<Directory> recovered_directory;
  std::shared_ptr<const Path> recovered_file;

  EXPECT_NO_THROW(recovered_directory = listing_handler_->Get<Directory>(""));
  EXPECT_TRUE(recovered_directory->parent_id().data == unique_user_id_);
  EXPECT_TRUE(recovered_directory->directory_id() == root_parent_id_);
  EXPECT_TRUE(!recovered_directory->empty());
  EXPECT_NO_THROW(recovered_file = recovered_directory->GetChild(kRoot));
  EXPECT_TRUE(kRoot == recovered_file->meta_data.name());
  EXPECT_NO_THROW(recovered_directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_TRUE(recovered_directory->parent_id().data == root_parent_id_);
}

TEST_F(DirectoryHandlerTest, BEH_AddDirectory) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string directory_name("Directory");
  auto file(File::Create(directory_name, true));
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> directory;
  DirectoryId dir(*file->meta_data.directory_id());
  EXPECT_NO_THROW(listing_handler_->Add(kRoot / directory_name, file));
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot / directory_name));
  EXPECT_TRUE(directory->directory_id() == dir);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_NO_THROW(recovered_file = directory->GetChild(directory_name));
  EXPECT_TRUE(directory_name == recovered_file->meta_data.name());
}

TEST_F(DirectoryHandlerTest, BEH_AddSameDirectory) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string directory_name("Directory");
  auto file(File::Create(directory_name, true));
  DirectoryId dir(*file->meta_data.directory_id());
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> directory;
  boost::filesystem::path meta_data_name(file->meta_data.name());
  EXPECT_NO_THROW(listing_handler_->Add(kRoot / directory_name, file));
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot / directory_name));
  EXPECT_TRUE(directory->directory_id() == dir);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_NO_THROW(recovered_file = directory->GetChild(directory_name));
  EXPECT_TRUE(meta_data_name == recovered_file->meta_data.name());

  EXPECT_THROW(listing_handler_->Add(kRoot / directory_name, File::Create(directory_name, true)),
               std::exception);
  EXPECT_NO_THROW(recovered_file = directory->GetChild(directory_name));
  EXPECT_TRUE(meta_data_name == recovered_file->meta_data.name());
}

TEST_F(DirectoryHandlerTest, BEH_AddFile) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string file_name("File");
  auto file(File::Create(file_name, false));
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> directory;

  EXPECT_NO_THROW(listing_handler_->Add(kRoot / file_name, file));
  EXPECT_THROW(directory = listing_handler_->Get<Directory>(kRoot / file_name), std::exception);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_TRUE(directory->HasChild(file_name));
  EXPECT_NO_THROW(recovered_file = directory->GetChild(file_name));
  EXPECT_TRUE(file_name == recovered_file->meta_data.name());
}

TEST_F(DirectoryHandlerTest, BEH_AddSameFile) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string file_name("File");
  auto file(File::Create(file_name, false));
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> directory;

  EXPECT_NO_THROW(listing_handler_->Add(kRoot / file_name, file));
  EXPECT_THROW(directory = listing_handler_->Get<Directory>(kRoot / file_name), std::exception);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_TRUE(directory->HasChild(file_name));
  EXPECT_NO_THROW(recovered_file = directory->GetChild(file_name));
  EXPECT_TRUE(file_name == recovered_file->meta_data.name());

  EXPECT_TRUE(directory->HasChild(file_name));
  EXPECT_NO_THROW(recovered_file = directory->GetChild(file_name));
  EXPECT_TRUE(file_name == recovered_file->meta_data.name());
}

TEST_F(DirectoryHandlerTest, BEH_DeleteDirectory) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string directory_name("Directory");
  auto file(File::Create(directory_name, true));
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> directory;
  DirectoryId dir(*file->meta_data.directory_id());

  EXPECT_NO_THROW(listing_handler_->Add(kRoot / directory_name, file));
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot / directory_name));
  EXPECT_TRUE(directory->directory_id() == dir);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_NO_THROW(recovered_file = directory->GetChild(directory_name));
  EXPECT_TRUE(directory_name == recovered_file->meta_data.name());

  EXPECT_NO_THROW(listing_handler_->Delete(kRoot / directory_name));
  EXPECT_THROW(directory = listing_handler_->Get<Directory>(kRoot / directory_name), std::exception);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_THROW(recovered_file = directory->GetChild(directory_name), std::exception);
}

TEST_F(DirectoryHandlerTest, BEH_DeleteSameDirectory) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string directory_name("Directory");
  auto file(File::Create(directory_name, true));
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> directory;
  DirectoryId dir(*file->meta_data.directory_id());

  EXPECT_NO_THROW(listing_handler_->Add(kRoot / directory_name, file));
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot / directory_name));
  EXPECT_TRUE(directory->directory_id() == dir);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_NO_THROW(recovered_file = directory->GetChild(directory_name));
  EXPECT_TRUE(directory_name == recovered_file->meta_data.name());

  EXPECT_NO_THROW(listing_handler_->Delete(kRoot / directory_name));
  EXPECT_THROW(directory = listing_handler_->Get<Directory>(kRoot / directory_name), std::exception);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_THROW(recovered_file = directory->GetChild(directory_name), std::exception);
  EXPECT_THROW(listing_handler_->Delete(kRoot / directory_name), std::exception);
}

TEST_F(DirectoryHandlerTest, BEH_DeleteFile) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string file_name("File");
  auto file(File::Create(file_name, false));
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> directory;

  EXPECT_NO_THROW(listing_handler_->Add(kRoot / file_name, file));
  EXPECT_THROW(directory = listing_handler_->Get<Directory>(kRoot / file_name), std::exception);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_NO_THROW(recovered_file = directory->GetChild(file_name));
  EXPECT_TRUE(file_name == recovered_file->meta_data.name());

  EXPECT_NO_THROW(listing_handler_->Delete(kRoot / file_name));
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_THROW(recovered_file = directory->GetChild(file_name), std::exception);
}

TEST_F(DirectoryHandlerTest, BEH_DeleteSameFile) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string file_name("File");
  auto file(File::Create(file_name, false));
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> directory;

  EXPECT_NO_THROW(listing_handler_->Add(kRoot / file_name, file));
  EXPECT_THROW(directory = listing_handler_->Get<Directory>(kRoot / file_name), std::exception);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_NO_THROW(recovered_file = directory->GetChild(file_name));
  EXPECT_TRUE(file_name == recovered_file->meta_data.name());

  EXPECT_NO_THROW(listing_handler_->Delete(kRoot / file_name));
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>(kRoot));
  EXPECT_THROW(recovered_file = directory->GetChild(file_name), std::exception);

  EXPECT_THROW(listing_handler_->Delete(kRoot / file_name), std::exception);
}

TEST_F(DirectoryHandlerTest, BEH_RenameAndMoveDirectory) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string first_directory_name("Directory1"), second_directory_name("Directory2"),
      old_directory_name("OldName"), new_directory_name("NewName");
  auto first_file(File::Create(first_directory_name, true));
  auto second_file(File::Create(second_directory_name, true));
  auto file(File::Create(old_directory_name, true));
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> old_parent_directory, new_parent_directory, directory;

  EXPECT_NO_THROW(
      listing_handler_->Add(kRoot / first_directory_name, first_file));
  EXPECT_NO_THROW(
      listing_handler_->Add(kRoot / second_directory_name, second_file));

  EXPECT_NO_THROW(old_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / first_directory_name));
  EXPECT_NO_THROW(file->SetParent(old_parent_directory));
  DirectoryId dir(*file->meta_data.directory_id());
  EXPECT_NO_THROW(listing_handler_->Add(kRoot / first_directory_name / old_directory_name,
                                        file));

  EXPECT_NO_THROW(recovered_file = old_parent_directory->GetChild(old_directory_name));
  EXPECT_TRUE(old_directory_name == recovered_file->meta_data.name());

  EXPECT_THROW(recovered_file = old_parent_directory->GetChild(new_directory_name),
               std::exception);
  EXPECT_NO_THROW(new_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / second_directory_name));
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(old_directory_name),
               std::exception);
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(new_directory_name),
               std::exception);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>
                  (kRoot / first_directory_name / old_directory_name));
  EXPECT_TRUE(directory->parent_id().data == old_parent_directory->directory_id());
  EXPECT_TRUE(directory->directory_id() == dir);
  EXPECT_THROW(directory = listing_handler_->Get<Directory>
               (kRoot / first_directory_name / new_directory_name),
               std::exception);
  EXPECT_THROW(directory = listing_handler_->Get<Directory>
               (kRoot / second_directory_name / old_directory_name),
               std::exception);
  EXPECT_THROW(directory = listing_handler_->Get<Directory>
               (kRoot / second_directory_name / new_directory_name),
               std::exception);

  EXPECT_NO_THROW(listing_handler_->Rename(kRoot / first_directory_name / old_directory_name,
                                           kRoot / first_directory_name / new_directory_name));

  EXPECT_NO_THROW(old_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / first_directory_name));
  EXPECT_THROW(recovered_file = old_parent_directory->GetChild(old_directory_name),
               std::exception);
  EXPECT_NO_THROW(recovered_file = old_parent_directory->GetChild(new_directory_name));
  EXPECT_TRUE(new_directory_name == recovered_file->meta_data.name());
  EXPECT_NO_THROW(new_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / second_directory_name));
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(old_directory_name),
               std::exception);
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(new_directory_name),
               std::exception);
  EXPECT_THROW(directory = listing_handler_->Get<Directory>
               (kRoot / first_directory_name / old_directory_name),
               std::exception);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>
                  (kRoot / first_directory_name / new_directory_name));
  EXPECT_TRUE(directory->parent_id().data == old_parent_directory->directory_id());
  EXPECT_TRUE(directory->directory_id() == *recovered_file->meta_data.directory_id());
  EXPECT_THROW(directory = listing_handler_->Get<Directory>
               (kRoot / second_directory_name / old_directory_name),
               std::exception);
  EXPECT_THROW(directory = listing_handler_->Get<Directory>
               (kRoot / second_directory_name / new_directory_name),
               std::exception);

  EXPECT_THROW(listing_handler_->Rename(kRoot / first_directory_name / old_directory_name,
                                        kRoot / second_directory_name / new_directory_name),
               std::exception);
  EXPECT_NO_THROW(listing_handler_->Rename(kRoot / first_directory_name / new_directory_name,
                                           kRoot / second_directory_name / new_directory_name));

  EXPECT_NO_THROW(old_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / first_directory_name));
  EXPECT_THROW(recovered_file = old_parent_directory->GetChild(old_directory_name),
               std::exception);
  EXPECT_THROW(recovered_file = old_parent_directory->GetChild(new_directory_name),
               std::exception);
  EXPECT_NO_THROW(new_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / second_directory_name));
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(old_directory_name),
               std::exception);
  EXPECT_NO_THROW(recovered_file = new_parent_directory->GetChild(new_directory_name));
  EXPECT_TRUE(new_directory_name == recovered_file->meta_data.name());
  EXPECT_THROW(directory = listing_handler_->Get<Directory>
               (kRoot / first_directory_name / old_directory_name),
               std::exception);
  EXPECT_THROW(directory = listing_handler_->Get<Directory>
               (kRoot / first_directory_name / new_directory_name),
               std::exception);
  EXPECT_THROW(directory = listing_handler_->Get<Directory>
               (kRoot / second_directory_name / old_directory_name),
               std::exception);
  EXPECT_NO_THROW(directory = listing_handler_->Get<Directory>
                  (kRoot / second_directory_name / new_directory_name));
  EXPECT_TRUE(directory->parent_id().data == new_parent_directory->directory_id());
  EXPECT_TRUE(directory->directory_id() == dir);
}

TEST_F(DirectoryHandlerTest, BEH_RenameAndMoveFile) {
  listing_handler_ = detail::DirectoryHandler<nfs::FakeStore>::Create(
      data_store_, unique_user_id_, root_parent_id_,
      boost::filesystem::unique_path(GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"), true,
      asio_service_.service());
  std::string first_directory_name("Directory1"), second_directory_name("Directory2"),
      old_file_name("OldName"), new_file_name("NewName");
  auto first_file(File::Create(first_directory_name, true));
  auto second_file(File::Create(second_directory_name, true));
  auto file(File::Create(old_file_name, false));
  std::shared_ptr<const Path> recovered_file;
  std::shared_ptr<Directory> old_parent_directory, new_parent_directory;
  EXPECT_NO_THROW(
      listing_handler_->Add(kRoot / first_directory_name, first_file));
  EXPECT_NO_THROW(
      listing_handler_->Add(kRoot / second_directory_name, second_file));

  EXPECT_NO_THROW(old_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / first_directory_name));
  EXPECT_NO_THROW(file->SetParent(old_parent_directory));
  EXPECT_NO_THROW(
      listing_handler_->Add(kRoot / first_directory_name / old_file_name, file));

  EXPECT_NO_THROW(recovered_file = old_parent_directory->GetChild(old_file_name));
  EXPECT_TRUE(old_file_name == recovered_file->meta_data.name());
  EXPECT_THROW(recovered_file = old_parent_directory->GetChild(new_file_name),
               std::exception);
  EXPECT_NO_THROW(new_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / second_directory_name));
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(old_file_name),
               std::exception);
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(new_file_name),
               std::exception);

  EXPECT_THROW(listing_handler_->Get<Directory>(kRoot / first_directory_name / old_file_name),
               std::exception);

  EXPECT_NO_THROW(listing_handler_->Rename(kRoot / first_directory_name / old_file_name,
                                           kRoot / first_directory_name / new_file_name));

  EXPECT_NO_THROW(old_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / first_directory_name));
  EXPECT_THROW(recovered_file = old_parent_directory->GetChild(old_file_name),
               std::exception);
  EXPECT_NO_THROW(recovered_file = old_parent_directory->GetChild(new_file_name));
  EXPECT_TRUE(new_file_name == recovered_file->meta_data.name());
  EXPECT_NO_THROW(new_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / second_directory_name));
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(old_file_name),
               std::exception);
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(new_file_name),
               std::exception);
  EXPECT_THROW(listing_handler_->Get<Directory>(kRoot / first_directory_name / new_file_name),
               std::exception);

  EXPECT_THROW(listing_handler_->Rename(kRoot / first_directory_name / old_file_name,
                                        kRoot / second_directory_name / new_file_name),
               std::exception);

  EXPECT_NO_THROW(listing_handler_->Rename(kRoot / first_directory_name / new_file_name,
                                           kRoot / second_directory_name / new_file_name));

  EXPECT_NO_THROW(old_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / first_directory_name));
  EXPECT_THROW(recovered_file = old_parent_directory->GetChild(old_file_name),
               std::exception);
  EXPECT_THROW(recovered_file = old_parent_directory->GetChild(new_file_name),
               std::exception);
  EXPECT_NO_THROW(new_parent_directory = listing_handler_->Get<Directory>
                  (kRoot / second_directory_name));
  EXPECT_THROW(recovered_file = new_parent_directory->GetChild(old_file_name),
               std::exception);
  EXPECT_NO_THROW(recovered_file = new_parent_directory->GetChild(new_file_name));
  EXPECT_TRUE(new_file_name == recovered_file->meta_data.name());
  EXPECT_THROW(listing_handler_->Get<Directory>(kRoot / second_directory_name / new_file_name),
               std::exception);
}

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
