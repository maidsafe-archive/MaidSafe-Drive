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
#endif

#include <fstream>
#include <string>
#include "boost/filesystem.hpp"
#include "boost/thread.hpp"
#include "boost/random/mersenne_twister.hpp"
#include "boost/random/uniform_int.hpp"
#include "boost/random/variate_generator.hpp"

#include "maidsafe/common/crypto.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/asio_service.h"

#include "maidsafe/encrypt/data_map.h"

#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/tests/test_utils.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace detail {

namespace test {

inline uint64_t GetSize(const MetaData& meta_data) {
#ifdef MAIDSAFE_WIN32
  return meta_data.end_of_file;
#else
  return meta_data.attributes.st_size;
#endif
}

class DirectoryTestListener
  : public std::enable_shared_from_this<DirectoryTestListener>,
    public Path::Listener {
 public:
  // Directory::Listener
  virtual void PathPut(std::shared_ptr<Path> path) {
    LOG(kInfo) << "Putting directory.";
    ImmutableData contents(NonEmptyString(path->Serialise()));
    std::static_pointer_cast<Directory>(path)->AddNewVersion(contents.name());
  }
  virtual void PathPutChunk(const ImmutableData&) {
    LOG(kInfo) << "Putting chunk.";
  }
  virtual void PathIncrementChunks(const std::vector<ImmutableData::Name>&) {
    LOG(kInfo) << "Incrementing chunks.";
  }
};

class DirectoryTest : public testing::Test {
 public:
  DirectoryTest()
      : main_test_dir_(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive")),
        relative_root_(kRoot),
        unique_id_(RandomAlphaNumericString(64)),
        parent_id_(crypto::Hash<crypto::SHA512>(main_test_dir_->string())),
        directory_id_(RandomAlphaNumericString(64)),
        asio_service_(1) {
      listener = std::make_shared<DirectoryTestListener>();
  }

  ~DirectoryTest() {
    asio_service_.Stop();
  }

 protected:
  std::shared_ptr<Directory::Listener> GetListener() {
    return listener;
  }

  void GenerateDirectoryListingEntryForDirectory(std::shared_ptr<Directory> directory,
                                                 fs::path const& path) {
    auto file(File::Create(path.filename(), true));
    file->meta_data.creation_time
        = file->meta_data.last_access_time
        = file->meta_data.last_write_time
        = common::Clock::now();
#ifdef MAIDSAFE_WIN32
    file.meta_data.attributes = FILE_ATTRIBUTE_DIRECTORY;
#endif
    *file->meta_data.directory_id =
        Identity(crypto::Hash<crypto::SHA512>((*main_test_dir_ / path).string()));
    EXPECT_NO_THROW(directory->AddChild(file));
  }

  bool GenerateDirectoryListings(fs::path const& path, fs::path relative_path) {
    // Create directory listing for relative path
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    DirectoryId directory_id(crypto::Hash<crypto::SHA512>(absolute_path.string()));
    auto directory(Directory::Create(parent_id,
                                     directory_id,
                                     asio_service_.service(),
                                     GetListener(),
                                     relative_path));
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          GenerateDirectoryListingEntryForDirectory(directory, itr->path().filename());
          EXPECT_TRUE(
              GenerateDirectoryListings(itr->path(), relative_path / itr->path().filename()));
        } else if (fs::is_regular_file(*itr)) {
          GenerateDirectoryListingEntryForFile(directory, itr->path().filename(),
                                               fs::file_size(itr->path()));
        } else {
          if (fs::exists(*itr))
            LOG(kInfo) << "Unknown type found.";
          else
            LOG(kInfo) << "Nonexistant type found.";
          return false;
        }
      }
      ImmutableData contents(NonEmptyString(directory->Serialise()));
      EXPECT_TRUE(WriteFile(path / "msdir.listing", contents.data().string()));
      directory->AddNewVersion(contents.name());
    }
    catch (const std::exception& e) {
      LOG(kError) << "GenerateDirectoryListings test failed: " << e.what();
      return false;
    }
    return true;
  }

  bool RemoveDirectoryListingsEntries(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory;
    EXPECT_TRUE(ReadFile(path / "msdir.listing", &serialised_directory));
    std::vector<StructuredDataVersions::VersionName> versions;
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    auto directory(Directory::Create(parent_id,
                                     serialised_directory,
                                     versions,
                                     asio_service_.service(),
                                     GetListener(),
                                     relative_path));

    std::shared_ptr<Path> file;
    // Remove the directory listing file
    boost::system::error_code error_code;
    EXPECT_TRUE(fs::remove(path / "msdir.listing", error_code));
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          EXPECT_TRUE(
              RemoveDirectoryListingsEntries(itr->path(), relative_path / itr->path().filename()));
          EXPECT_NO_THROW(file = directory->GetMutableChild(itr->path().filename()));
          EXPECT_NO_THROW(directory->RemoveChild(file->meta_data.name));
          // Remove the disk directory also
          CheckedRemove(itr->path());
        } else if (fs::is_regular_file(*itr)) {
          EXPECT_NO_THROW(file = directory->GetMutableChild(itr->path().filename()));
          EXPECT_NO_THROW(directory->RemoveChild(file->meta_data.name));
          // Remove the disk file also
          CheckedRemove(itr->path());
        } else {
          if (fs::exists(*itr))
            LOG(kInfo) << "Unknown type found.";
          else
            LOG(kInfo) << "Nonexistant type found.";
          return false;
        }
      }
    }
    catch (const std::exception& e) {
      LOG(kError) << "RemoveDirectoryListingsEntries test failed: " << e.what();
      return false;
    }
    EXPECT_TRUE(directory->empty());
    return true;
  }

  bool RenameDirectoryEntries(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory;
    EXPECT_TRUE(ReadFile(path / "msdir.listing", &serialised_directory));
    std::vector<StructuredDataVersions::VersionName> versions;
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    auto directory(Directory::Create(parent_id,
                                     serialised_directory,
                                     versions,
                                     asio_service_.service(),
                                     GetListener(),
                                     relative_path));

    std::shared_ptr<Path> file;
    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          fs::path new_path(relative_path / itr->path().filename());
          EXPECT_TRUE(RenameDirectoryEntries(itr->path(), new_path));
          EXPECT_NO_THROW(file = directory->GetMutableChild(itr->path().filename()));
          std::shared_ptr<Path> removed_context;
          EXPECT_NO_THROW(removed_context = directory->RemoveChild(file->meta_data.name));
          std::string new_name(RandomAlphaNumericString(5));
          removed_context->meta_data.name = fs::path(new_name);
          EXPECT_NO_THROW(directory->AddChild(removed_context));
          // Rename corresponding directory
          CheckedRename(itr->path(), (itr->path().parent_path() / new_name));
        } else if (fs::is_regular_file(*itr)) {
          if (itr->path().filename().string() != listing) {
            EXPECT_NO_THROW(file = directory->GetMutableChild(itr->path().filename()));
            std::shared_ptr<Path> removed_context;
            EXPECT_NO_THROW(removed_context = directory->RemoveChild(file->meta_data.name));
            std::string new_name(RandomAlphaNumericString(5) + ".txt");
            removed_context->meta_data.name = fs::path(new_name);
            EXPECT_NO_THROW(directory->AddChild(removed_context));
            // Rename corresponding file
            CheckedRename(itr->path(), (itr->path().parent_path() / new_name));
          }
        } else {
          if (fs::exists(*itr))
            LOG(kInfo) << "Unknown type found.";
          else
            LOG(kInfo) << "Nonexistant type found.";
          return false;
        }
      }
    }
    catch (const std::exception& e) {
      LOG(kError) << "RenameDirectoryEntries test failed: " << e.what();
      return false;
    }
    return true;
  }

  bool DirectoryHasChild(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory;
    EXPECT_TRUE(ReadFile(path / "msdir.listing", &serialised_directory));
    std::vector<StructuredDataVersions::VersionName> versions;
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    auto directory(Directory::Create(parent_id,
                                     serialised_directory,
                                     versions,
                                     asio_service_.service(),
                                     GetListener(),
                                     relative_path));

    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (itr->path().filename().string() != listing) {
          if (fs::is_directory(*itr)) {
            fs::path name(itr->path().filename());
            EXPECT_TRUE(DirectoryHasChild(itr->path(), relative_path / name));
            EXPECT_TRUE(directory->HasChild(name));
          } else if (fs::is_regular_file(*itr)) {
            EXPECT_TRUE(directory->HasChild(itr->path().filename()));
          } else {
            if (fs::exists(*itr))
              LOG(kInfo) << "Unknown type found.";
            else
              LOG(kInfo) << "Nonexistant type found.";
            return false;
          }
        }
      }
    }
    catch (const std::exception& e) {
      LOG(kError) << "DirectoryHasChild test failed: " << e.what();
      return false;
    }
    return true;
  }

  bool MatchEntries(fs::path const& path, fs::path relative_path) {
    std::string serialised_directory;
    EXPECT_TRUE(ReadFile(path / "msdir.listing", &serialised_directory));
    std::vector<StructuredDataVersions::VersionName> versions;
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    auto directory(Directory::Create(parent_id,
                                     serialised_directory,
                                     versions,
                                     asio_service_.service(),
                                     GetListener(),
                                     relative_path));

    std::shared_ptr<const Path> file;
    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;

    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          EXPECT_TRUE(MatchEntries(itr->path(), relative_path / itr->path().filename()));
          EXPECT_NO_THROW(file = directory->GetChild(itr->path().filename()));
          EXPECT_TRUE(file->meta_data.name == itr->path().filename());
        } else if (fs::is_regular_file(*itr)) {
          if (itr->path().filename().string() != listing) {
            EXPECT_NO_THROW(file = directory->GetChild(itr->path().filename()));
            EXPECT_TRUE(file->meta_data.name == itr->path().filename());
            // EXPECT_TRUE(GetSize(file->meta_data) ==
            // fs::file_size(itr->path()));
          }
        } else {
          if (fs::exists(*itr))
            LOG(kInfo) << "Unknown type found.";
          else
            LOG(kInfo) << "Nonexistant type found.";
          return false;
        }
      }
    }
    catch (const std::exception& e) {
      LOG(kError) << "MatchEntries test failed: " << e.what();
      return false;
    }

    EXPECT_TRUE(directory->directory_id().string() ==
                crypto::Hash<crypto::SHA512>(absolute_path.string()).string());
    return true;
  }

  void SortAndResetChildrenCounter(std::shared_ptr<Directory> directory) {
    test::SortAndResetChildrenCounter(*directory);
  }

  void ResetChildrenCounter(std::shared_ptr<Directory> directory) {
    directory->ResetChildrenCounter();
  }

  maidsafe::test::TestPath main_test_dir_;
  fs::path relative_root_;
  Identity unique_id_, parent_id_, directory_id_;
  AsioService asio_service_;
  std::shared_ptr<DirectoryTestListener> listener;

 private:
  DirectoryTest(const DirectoryTest&) = delete;
  DirectoryTest& operator=(const DirectoryTest&) = delete;
};

TEST_F(DirectoryTest, BEH_AddChildren) {
  ASSERT_TRUE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(MatchEntries(*main_test_dir_, relative_root_));
}

TEST_F(DirectoryTest, BEH_AddThenRemoveChildren) {
  ASSERT_TRUE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(RemoveDirectoryListingsEntries(*main_test_dir_, relative_root_));
}

TEST_F(DirectoryTest, BEH_AddThenRenameChildren) {
  ASSERT_TRUE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(RenameDirectoryEntries(*main_test_dir_, relative_root_));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(MatchEntries(*main_test_dir_, relative_root_));
}

TEST_F(DirectoryTest, BEH_DirectoryHasChild) {
  ASSERT_TRUE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(DirectoryHasChild(*main_test_dir_, relative_root_));
}

void DirectoriesMatch(const Directory& lhs, const Directory& rhs) {
  std::lock_guard<std::mutex> lhsLock(lhs.mutex_);
  std::lock_guard<std::mutex> rhsLock(rhs.mutex_);
  // Do not call functions on lhs and rhs, otherwise they will deadlock
  ASSERT_TRUE(lhs.directory_id_ == rhs.directory_id_) << "Directory ID mismatch.";
  ASSERT_TRUE(lhs.children_.size() == rhs.children_.size());
  auto itr1(lhs.children_.begin()), itr2(rhs.children_.begin());
  for (; itr1 != lhs.children_.end(); ++itr1, ++itr2) {
    ASSERT_TRUE((*itr1)->meta_data.name == (*itr2)->meta_data.name);
    EXPECT_FALSE((*itr1)->meta_data.data_map == nullptr &&
                 (*itr2)->meta_data.directory_id == nullptr);
    if ((*itr1)->meta_data.data_map) {
      ASSERT_TRUE(TotalSize(*(*itr1)->meta_data.data_map) ==
                  TotalSize(*(*itr2)->meta_data.data_map));
      ASSERT_TRUE((*itr1)->meta_data.data_map->chunks.size() ==
                  (*itr2)->meta_data.data_map->chunks.size());
      auto chunk_itr1((*itr1)->meta_data.data_map->chunks.begin());
      auto chunk_itr2((*itr2)->meta_data.data_map->chunks.begin());
      size_t chunk_no(0);
      for (; chunk_itr1 != (*itr1)->meta_data.data_map->chunks.end();
           ++chunk_itr1, ++chunk_itr2, ++chunk_no) {
        ASSERT_TRUE((*chunk_itr1).hash != (*chunk_itr2).hash) << "DataMap chunk " << chunk_no
                                                              << " hash mismatch.";
        ASSERT_TRUE((*chunk_itr1).pre_hash != (*chunk_itr2).pre_hash)
            << "DataMap chunk " << chunk_no << " pre_hash mismatch.";
        ASSERT_TRUE((*chunk_itr1).size == (*chunk_itr2).size);
      }
      ASSERT_TRUE((*itr1)->meta_data.data_map->content == (*itr2)->meta_data.data_map->content)
          << "DataMap content mismatch.";
    }
    //     if ((*itr1).end_of_file != (*itr2).end_of_file)
    ASSERT_TRUE(GetSize((*itr1)->meta_data) == GetSize((*itr2)->meta_data));
    ASSERT_EQ((*itr1)->meta_data.creation_time, (*itr2)->meta_data.creation_time);
    ASSERT_EQ((*itr1)->meta_data.last_access_time, (*itr2)->meta_data.last_access_time);
    ASSERT_EQ((*itr1)->meta_data.last_write_time, (*itr2)->meta_data.last_write_time);
#ifdef MAIDSAFE_WIN32
    ASSERT_TRUE((*itr1)->meta_data.allocation_size == (*itr2)->meta_data.allocation_size);
    ASSERT_TRUE((*itr1)->meta_data.attributes == (*itr2)->meta_data.attributes);
#endif
  }
}

void SortAndResetChildrenCounter(Directory& lhs) {
    lhs.SortAndResetChildrenCounter();
}

TEST_F(DirectoryTest, BEH_SerialiseAndParse) {
  maidsafe::test::TestPath testpath(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive"));
  auto directory(Directory::Create(ParentId(unique_id_),
                                   parent_id_,
                                   asio_service_.service(),
                                   GetListener(),
                                   ""));
  boost::system::error_code error_code;
  int64_t file_size(0);
  std::string name(RandomAlphaNumericString(10));
  CheckedCreateDirectories(*testpath / name);

  RequiredExists(*testpath / name);
  fs::path file(CreateTestFile(*testpath / name, file_size));

  // std::vector<File> files_before;
  for (int i = 0; i != 10; ++i) {
    bool is_dir((i % 2) == 0);
    std::string child_name("Child " + std::to_string(i));
    auto file(File::Create(child_name, is_dir));
    file->meta_data.creation_time
        = file->meta_data.last_access_time
        = file->meta_data.last_write_time
        = common::Clock::now();
    if (is_dir) {
#ifdef MAIDSAFE_WIN32
      file.meta_data.attributes = FILE_ATTRIBUTE_DIRECTORY;
#endif
    } else {
#ifdef MAIDSAFE_WIN32
      file->meta_data.end_of_file = RandomUint32();
      // When archiving MetaData the following assumption is made: end_of_file == allocation_size.
      // This is reasonable since when file info is queried or on closing a file we set those values
      // equal.  This stemmed from cbfs asserting when end_of_file.QuadPart was less than
      // allocation_size.QuadPart, although they were not always set in an order that avoided this,
      // so, to allow the test to pass: meta_data.allocation_size = RandomUint32();
      file->meta_data.allocation_size = file->meta_data.end_of_file;
      file->meta_data.attributes = FILE_ATTRIBUTE_NORMAL;
#else
      file->meta_data.attributes.st_size = RandomUint32();
#endif
      file->meta_data.data_map->content = GetRandomString<encrypt::ByteVector>(10);
    }
    // files_before.emplace_back(std::move(file));
    EXPECT_NO_THROW(directory->AddChild(file));
  }

  directory->StoreImmediatelyIfPending();

  std::string serialised_directory(directory->Serialise());
  std::vector<StructuredDataVersions::VersionName> versions;
  auto recovered_directory(Directory::Create(directory->parent_id(),
                                             serialised_directory,
                                             versions,
                                             asio_service_.service(),
                                             GetListener(),
                                             ""));
  DirectoriesMatch(*directory, *recovered_directory);
}

TEST_F(DirectoryTest, BEH_IteratorReset) {
  auto directory(Directory::Create(ParentId(unique_id_),
                                   parent_id_,
                                   asio_service_.service(),
                                   GetListener(),
                                   ""));
  // Add elements
  const size_t kTestCount(10);
  ResetChildrenCounter(directory);
  EXPECT_TRUE(4U < kTestCount);
  char c('A');
  for (size_t i(0); i != kTestCount; ++i, ++c) {
    auto file(File::Create(std::string(1, c), ((i % 2) == 0)));
    EXPECT_NO_THROW(directory->AddChild(std::move(file)));
  }
  EXPECT_FALSE(directory->empty());

  // Check internal iterator
  std::shared_ptr<const Path> file;
  c = 'A';
  for (size_t i(0); i != kTestCount; ++i, ++c) {
    EXPECT_NO_THROW(file = directory->GetChildAndIncrementCounter());
    EXPECT_TRUE(std::string(1, c) == file->meta_data.name);
    EXPECT_TRUE(((i % 2) == 0) == (file->meta_data.directory_id != nullptr));
  }

  SortAndResetChildrenCounter(directory);

  EXPECT_NO_THROW(file = directory->GetChildAndIncrementCounter());
  EXPECT_TRUE("A" == file->meta_data.name);
  EXPECT_NO_THROW(file = directory->GetChildAndIncrementCounter());
  EXPECT_TRUE("B" == file->meta_data.name);

  // Add another element and check iterator is reset
  ++c;
  auto new_file(File::Create(std::string(1, c), false));
  EXPECT_NO_THROW(directory->AddChild(new_file));
  EXPECT_NO_THROW(file = directory->GetChildAndIncrementCounter());
  EXPECT_TRUE("A" == file->meta_data.name);
  EXPECT_NO_THROW(file = directory->GetChildAndIncrementCounter());
  EXPECT_TRUE("B" == file->meta_data.name);

  // Remove an element and check iterator is reset
  ASSERT_TRUE(directory->HasChild("C"));
  EXPECT_NO_THROW(directory->RemoveChild("C"));
  EXPECT_NO_THROW(file = directory->GetChildAndIncrementCounter());
  EXPECT_TRUE("A" == file->meta_data.name);
  EXPECT_NO_THROW(file = directory->GetChildAndIncrementCounter());
  EXPECT_TRUE("B" == file->meta_data.name);

  // Try to remove a non-existent element and check iterator is not reset
  ASSERT_FALSE(directory->HasChild("C"));
  EXPECT_THROW(directory->RemoveChild("C"), std::exception);
  EXPECT_NO_THROW(file = directory->GetChildAndIncrementCounter());
  EXPECT_TRUE("D" == file->meta_data.name);
  EXPECT_NO_THROW(file = directory->GetChildAndIncrementCounter());
  EXPECT_TRUE("E" == file->meta_data.name);

  // Check operator<
  // DirectoryListing directory_listing1(Identity(crypto::Hash<crypto::SHA512>(std::string("A")))),
  //     directory_listing2(Identity(crypto::Hash<crypto::SHA512>(std::string("B"))));
  // EXPECT_TRUE(directory_listing1 < directory_listing2);
}

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
