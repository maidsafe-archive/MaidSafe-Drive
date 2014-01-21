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

inline uint64_t GetSize(MetaData meta_data) {
#ifdef MAIDSAFE_WIN32
  return meta_data.end_of_file;
#else
  return meta_data.attributes.st_size;
#endif
}

class DirectoryTest {
 public:
  DirectoryTest()
      : main_test_dir_(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive")),
        relative_root_(kRoot),
        unique_id_(RandomAlphaNumericString(64)),
        parent_id_(crypto::Hash<crypto::SHA512>(main_test_dir_->string())),
        directory_id_(RandomAlphaNumericString(64)),
        asio_service_(1),
        put_chunk_functor_([](const ImmutableData&) { LOG(kInfo) << "Putting chunk."; }),
        increment_chunks_functor_([](const std::vector<ImmutableData::Name>&) {
          LOG(kInfo) << "Incrementing chunks.";
        }),
        put_functor_([&](Directory* directory) {
          LOG(kInfo) << "Putting directory.";
          ImmutableData contents(
              NonEmptyString(directory->Serialise(put_chunk_functor_, increment_chunks_functor_)));
          directory->AddNewVersion(contents.name());
        }),
        directory_(ParentId(unique_id_), parent_id_, asio_service_.service(), put_functor_, "") {}

 protected:
  void GenerateDirectoryListingEntryForDirectory(Directory& directory, fs::path const& path) {
    FileContext file_context(path.filename(), true);
#ifdef MAIDSAFE_WIN32
    file_context.meta_data.attributes = FILE_ATTRIBUTE_DIRECTORY;
    GetSystemTimeAsFileTime(&file_context.meta_data.creation_time);
    GetSystemTimeAsFileTime(&file_context.meta_data.last_access_time);
    GetSystemTimeAsFileTime(&file_context.meta_data.last_write_time);
#else
    time(&file_context.meta_data.attributes.st_atime);
    time(&file_context.meta_data.attributes.st_mtime);
#endif
    *file_context.meta_data.directory_id =
        Identity(crypto::Hash<crypto::SHA512>((*main_test_dir_ / path).string()));
    CHECK_NOTHROW(directory.AddChild(std::move(file_context)));
  }

  bool GenerateDirectoryListings(fs::path const& path, fs::path relative_path) {
    // Create directory listing for relative path
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    DirectoryId directory_id(crypto::Hash<crypto::SHA512>(absolute_path.string()));
    Directory directory(parent_id, directory_id, asio_service_.service(), put_functor_,
                        relative_path);
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          GenerateDirectoryListingEntryForDirectory(directory, itr->path().filename());
          CHECK(GenerateDirectoryListings(itr->path(), relative_path / itr->path().filename()));
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
      ImmutableData contents(
          NonEmptyString(directory.Serialise(put_chunk_functor_, increment_chunks_functor_)));
      CHECK(WriteFile(path / "msdir.listing", contents.data().string()));
      directory.AddNewVersion(contents.name());
    }
    catch (const std::exception& e) {
      LOG(kError) << "GenerateDirectoryListings test failed: " << e.what();
      return false;
    }
    return true;
  }

  bool RemoveDirectoryListingsEntries(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory;
    CHECK(ReadFile(path / "msdir.listing", &serialised_directory));
    std::vector<StructuredDataVersions::VersionName> versions;
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    Directory directory(parent_id, serialised_directory, versions, asio_service_.service(),
                        put_functor_, relative_path);

    FileContext* file_context(nullptr);
    // Remove the directory listing file
    boost::system::error_code error_code;
    CHECK(fs::remove(path / "msdir.listing", error_code));
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          CHECK(RemoveDirectoryListingsEntries(itr->path(),
                                                     relative_path / itr->path().filename()));
          CHECK_NOTHROW(file_context = directory.GetMutableChild(itr->path().filename()));
          CHECK_NOTHROW(FileContext context(directory.RemoveChild(file_context->meta_data.name)));
          // Remove the disk directory also
          CheckedRemove(itr->path());
        } else if (fs::is_regular_file(*itr)) {
          CHECK_NOTHROW(file_context = directory.GetMutableChild(itr->path().filename()));
          CHECK_NOTHROW(FileContext context(directory.RemoveChild(file_context->meta_data.name)));
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
    CHECK(directory.empty());
    return true;
  }

  bool RenameDirectoryEntries(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory;
    CHECK(ReadFile(path / "msdir.listing", &serialised_directory));
    std::vector<StructuredDataVersions::VersionName> versions;
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    Directory directory(parent_id, serialised_directory, versions, asio_service_.service(),
                        put_functor_, relative_path);

    FileContext* file_context(nullptr);
    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          fs::path new_path(relative_path / itr->path().filename());
          CHECK(RenameDirectoryEntries(itr->path(), new_path));
          CHECK_NOTHROW(file_context = directory.GetMutableChild(itr->path().filename()));
          FileContext removed_context;
          CHECK_NOTHROW(removed_context = directory.RemoveChild(file_context->meta_data.name));
          std::string new_name(RandomAlphaNumericString(5));
          removed_context.meta_data.name = fs::path(new_name);
          CHECK_NOTHROW(directory.AddChild(std::move(removed_context)));
          // Rename corresponding directory
          CheckedRename(itr->path(), (itr->path().parent_path() / new_name));
        } else if (fs::is_regular_file(*itr)) {
          if (itr->path().filename().string() != listing) {
            CHECK_NOTHROW(file_context = directory.GetMutableChild(itr->path().filename()));
            FileContext removed_context;
            CHECK_NOTHROW(removed_context = directory.RemoveChild(file_context->meta_data.name));
            std::string new_name(RandomAlphaNumericString(5) + ".txt");
            removed_context.meta_data.name = fs::path(new_name);
            CHECK_NOTHROW(directory.AddChild(std::move(removed_context)));
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
    CHECK(ReadFile(path / "msdir.listing", &serialised_directory));
    std::vector<StructuredDataVersions::VersionName> versions;
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    Directory directory(parent_id, serialised_directory, versions, asio_service_.service(),
                        put_functor_, relative_path);

    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (itr->path().filename().string() != listing) {
          if (fs::is_directory(*itr)) {
            fs::path name(itr->path().filename());
            CHECK(DirectoryHasChild(itr->path(), relative_path / name));
            CHECK(directory.HasChild(name));
          } else if (fs::is_regular_file(*itr)) {
            CHECK(directory.HasChild(itr->path().filename()));
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
    CHECK(ReadFile(path / "msdir.listing", &serialised_directory));
    std::vector<StructuredDataVersions::VersionName> versions;
    fs::path absolute_path((*main_test_dir_ / relative_path));
    ParentId parent_id(crypto::Hash<crypto::SHA512>(absolute_path.parent_path().string()));
    Directory directory(parent_id, serialised_directory, versions, asio_service_.service(),
                        put_functor_, relative_path);

    const FileContext* file_context(nullptr);
    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;

    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          CHECK(MatchEntries(itr->path(), relative_path / itr->path().filename()));
          CHECK_NOTHROW(file_context = directory.GetChild(itr->path().filename()));
          CHECK(file_context->meta_data.name == itr->path().filename());
        } else if (fs::is_regular_file(*itr)) {
          if (itr->path().filename().string() != listing) {
            CHECK_NOTHROW(file_context = directory.GetChild(itr->path().filename()));
            CHECK(file_context->meta_data.name == itr->path().filename());
            // CHECK(GetSize(std::move(file_context->meta_data)) == fs::file_size(itr->path()));
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

    CHECK(directory.directory_id().string() ==
      crypto::Hash<crypto::SHA512>(absolute_path.string()).string());
    return true;
  }

  void SortAndResetChildrenCounter() {
    directory_.SortAndResetChildrenCounter();
  }

  void ResetChildrenCounter() {
    directory_.ResetChildrenCounter();
  }

  maidsafe::test::TestPath main_test_dir_;
  fs::path relative_root_;
  Identity unique_id_, parent_id_, directory_id_;
  AsioService asio_service_;
  std::function<void(const ImmutableData&)> put_chunk_functor_;
  std::function<void(const std::vector<ImmutableData::Name>&)> increment_chunks_functor_;
  std::function<void(Directory*)> put_functor_;  // NOLINT
  Directory directory_;

 private:
  DirectoryTest(const DirectoryTest&);
  DirectoryTest& operator=(const DirectoryTest&);
};

TEST_CASE_METHOD(DirectoryTest, "Add children", "[Directory][behavioural]") {
  REQUIRE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(MatchEntries(*main_test_dir_, relative_root_));
}

TEST_CASE_METHOD(DirectoryTest, "Add then remove children", "[Directory][behavioural]") {
  REQUIRE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(RemoveDirectoryListingsEntries(*main_test_dir_, relative_root_));
}

TEST_CASE_METHOD(DirectoryTest, "Add then rename children", "[Directory][behavioural]") {
  REQUIRE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(RenameDirectoryEntries(*main_test_dir_, relative_root_));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(MatchEntries(*main_test_dir_, relative_root_));
}

TEST_CASE_METHOD(DirectoryTest, "Directory has child", "[Directory][behavioural]") {
  REQUIRE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(DirectoryHasChild(*main_test_dir_, relative_root_));
}

void DirectoriesMatch(const Directory& lhs, const Directory& rhs) {
  if (lhs.directory_id() != rhs.directory_id())
    FAIL("Directory ID mismatch.");
  REQUIRE(lhs.children_.size() == rhs.children_.size());
  auto itr1(lhs.children_.begin()), itr2(rhs.children_.begin());
  for (; itr1 != lhs.children_.end(); ++itr1, ++itr2) {
    REQUIRE((*itr1)->meta_data.name == (*itr2)->meta_data.name);
    if (((*itr1)->meta_data.data_map && !(*itr2)->meta_data.data_map) ||
        (!(*itr1)->meta_data.data_map && (*itr2)->meta_data.data_map))
      FAIL("Data map pointer mismatch");
    if ((*itr1)->meta_data.data_map) {
      REQUIRE(TotalSize(*(*itr1)->meta_data.data_map) == TotalSize(*(*itr2)->meta_data.data_map));
      REQUIRE((*itr1)->meta_data.data_map->chunks.size() ==
              (*itr2)->meta_data.data_map->chunks.size());
      auto chunk_itr1((*itr1)->meta_data.data_map->chunks.begin());
      auto chunk_itr2((*itr2)->meta_data.data_map->chunks.begin());
      size_t chunk_no(0);
      for (; chunk_itr1 != (*itr1)->meta_data.data_map->chunks.end();
           ++chunk_itr1, ++chunk_itr2, ++chunk_no) {
        if ((*chunk_itr1).hash != (*chunk_itr2).hash)
          FAIL("DataMap chunk " << chunk_no << " hash mismatch.");
        if ((*chunk_itr1).pre_hash != (*chunk_itr2).pre_hash)
          FAIL("DataMap chunk " << chunk_no << " pre_hash mismatch.");
        REQUIRE((*chunk_itr1).size == (*chunk_itr2).size);
      }
      if ((*itr1)->meta_data.data_map->content != (*itr2)->meta_data.data_map->content)
        FAIL("DataMap content mismatch.");
      //       if ((*itr1).data_map->self_encryption_type !=
      //           (*itr2).data_map->self_encryption_type)
      //         FAIL("DataMap SE type mismatch.");
    }
    //     if ((*itr1).end_of_file != (*itr2).end_of_file)
    REQUIRE(GetSize(std::move((*itr1)->meta_data)) == GetSize(std::move((*itr2)->meta_data)));
#ifdef MAIDSAFE_WIN32
    REQUIRE((*itr1)->meta_data.allocation_size == (*itr2)->meta_data.allocation_size);
    REQUIRE((*itr1)->meta_data.attributes == (*itr2)->meta_data.attributes);
    REQUIRE((*itr1)->meta_data.creation_time.dwHighDateTime ==
            (*itr2)->meta_data.creation_time.dwHighDateTime);
    if ((*itr1)->meta_data.creation_time.dwLowDateTime !=
        (*itr2)->meta_data.creation_time.dwLowDateTime) {
      uint32_t error = 0xA;
      if ((*itr1)->meta_data.creation_time.dwLowDateTime >
          (*itr2)->meta_data.creation_time.dwLowDateTime + error ||
          (*itr1)->meta_data.creation_time.dwLowDateTime <
          (*itr2)->meta_data.creation_time.dwLowDateTime - error)
        FAIL("Creation times low: " << (*itr1)->meta_data.creation_time.dwLowDateTime << " != "
             << (*itr2)->meta_data.creation_time.dwLowDateTime);
    }
    REQUIRE((*itr1)->meta_data.last_access_time.dwHighDateTime ==
            (*itr2)->meta_data.last_access_time.dwHighDateTime);
    if ((*itr1)->meta_data.last_access_time.dwLowDateTime !=
        (*itr2)->meta_data.last_access_time.dwLowDateTime) {
      uint32_t error = 0xA;
      if ((*itr1)->meta_data.last_access_time.dwLowDateTime >
          (*itr2)->meta_data.last_access_time.dwLowDateTime + error ||
          (*itr1)->meta_data.last_access_time.dwLowDateTime <
          (*itr2)->meta_data.last_access_time.dwLowDateTime - error)
        FAIL("Last access times low: " << (*itr1)->meta_data.last_access_time.dwLowDateTime
             << " != " << (*itr2)->meta_data.last_access_time.dwLowDateTime);
    }
    REQUIRE((*itr1)->meta_data.last_write_time.dwHighDateTime ==
            (*itr2)->meta_data.last_write_time.dwHighDateTime);
    if ((*itr1)->meta_data.last_write_time.dwLowDateTime !=
        (*itr2)->meta_data.last_write_time.dwLowDateTime) {
      uint32_t error = 0xA;
      if ((*itr1)->meta_data.last_write_time.dwLowDateTime >
          (*itr2)->meta_data.last_write_time.dwLowDateTime + error ||
          (*itr1)->meta_data.last_write_time.dwLowDateTime <
          (*itr2)->meta_data.last_write_time.dwLowDateTime - error)
        FAIL("Last write times low: " << (*itr1)->meta_data.last_write_time.dwLowDateTime << " != "
             << (*itr2)->meta_data.last_write_time.dwLowDateTime);
    }
#else
    REQUIRE((*itr1)->meta_data.attributes.st_atime == (*itr2)->meta_data.attributes.st_atime);
    REQUIRE((*itr1)->meta_data.attributes.st_mtime == (*itr2)->meta_data.attributes.st_mtime);
#endif
  }
}

TEST_CASE_METHOD(DirectoryTest, "Serialise and parse", "[Directory][behavioural]") {
  maidsafe::test::TestPath testpath(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive"));
  boost::system::error_code error_code;
  int64_t file_size(0);
  std::string name(RandomAlphaNumericString(10));
  CheckedCreateDirectories(*testpath / name);

  RequiredExists(*testpath / name);
  fs::path file(CreateTestFile(*testpath / name, file_size));

  // std::vector<FileContext> file_contexts_before;
  for (int i = 0; i != 10; ++i) {
    bool is_dir((i % 2) == 0);
    std::string child_name("Child " + std::to_string(i));
    FileContext file_context(child_name, is_dir);
    if (is_dir) {
#ifdef MAIDSAFE_WIN32
      file_context.meta_data.attributes = FILE_ATTRIBUTE_DIRECTORY;
      GetSystemTimeAsFileTime(&file_context.meta_data.creation_time);
      GetSystemTimeAsFileTime(&file_context.meta_data.last_access_time);
      GetSystemTimeAsFileTime(&file_context.meta_data.last_write_time);
#else
      time(&file_context.meta_data.attributes.st_atime);
      time(&file_context.meta_data.attributes.st_mtime);
#endif
    } else {
#ifdef MAIDSAFE_WIN32
      file_context.meta_data.end_of_file = RandomUint32();
      // When archiving MetaData the following assumption is made: end_of_file == allocation_size.
      // This is reasonable since when file info is queried or on closing a file we set those values
      // equal.  This stemmed from cbfs asserting when end_of_file.QuadPart was less than
      // allocation_size.QuadPart, although they were not always set in an order that avoided this,
      // so, to allow the test to pass: meta_data.allocation_size = RandomUint32();
      file_context.meta_data.allocation_size = file_context.meta_data.end_of_file;
      file_context.meta_data.attributes = FILE_ATTRIBUTE_NORMAL;
      GetSystemTimeAsFileTime(&file_context.meta_data.creation_time);
      GetSystemTimeAsFileTime(&file_context.meta_data.last_access_time);
      GetSystemTimeAsFileTime(&file_context.meta_data.last_write_time);
#else
      time(&file_context.meta_data.attributes.st_atime);
      time(&file_context.meta_data.attributes.st_mtime);
      file_context.meta_data.attributes.st_size = RandomUint32();
#endif
      file_context.meta_data.data_map->content = RandomString(10);
    }
    // file_contexts_before.emplace_back(std::move(file_context));
    CHECK_NOTHROW(directory_.AddChild(std::move(file_context)));
  }

  std::string serialised_directory(directory_.Serialise(put_chunk_functor_,
                                   increment_chunks_functor_));
  ImmutableData contents(
      NonEmptyString(directory_.Serialise(put_chunk_functor_, increment_chunks_functor_)));
  directory_.AddNewVersion(contents.name());

  std::vector<StructuredDataVersions::VersionName> versions;
  Directory recovered_directory(directory_.parent_id(), serialised_directory, versions,
                                asio_service_.service(), put_functor_, "");
  DirectoriesMatch(directory_, recovered_directory);
}

TEST_CASE_METHOD(DirectoryTest, "Iterator reset", "[Directory][behavioural]") {
  // Add elements
  REQUIRE(directory_.empty());
  const size_t kTestCount(10);
  ResetChildrenCounter();
  CHECK(4U < kTestCount);
  char c('A');
  for (size_t i(0); i != kTestCount; ++i, ++c) {
    FileContext file_context(std::string(1, c), ((i % 2) == 0));
    CHECK_NOTHROW(directory_.AddChild(std::move(file_context)));
  }
  CHECK_FALSE(directory_.empty());

  // Check internal iterator
  const FileContext* file_context(nullptr);
  c = 'A';
  for (size_t i(0); i != kTestCount; ++i, ++c) {
    CHECK_NOTHROW(file_context = directory_.GetChildAndIncrementCounter());
    CHECK(std::string(1, c) == file_context->meta_data.name);
    CHECK(((i % 2) == 0) == (file_context->meta_data.directory_id != nullptr));
  }

  SortAndResetChildrenCounter();

  CHECK_NOTHROW(file_context = directory_.GetChildAndIncrementCounter());
  CHECK("A" == file_context->meta_data.name);
  CHECK_NOTHROW(file_context = directory_.GetChildAndIncrementCounter());
  CHECK("B" == file_context->meta_data.name);

  // Add another element and check iterator is reset
  ++c;
  FileContext new_file_context(std::string(1, c), false);
  CHECK_NOTHROW(directory_.AddChild(std::move(new_file_context)));
  CHECK_NOTHROW(file_context = directory_.GetChildAndIncrementCounter());
  CHECK("A" == file_context->meta_data.name);
  CHECK_NOTHROW(file_context = directory_.GetChildAndIncrementCounter());
  CHECK("B" == file_context->meta_data.name);

  // Remove an element and check iterator is reset
  REQUIRE(directory_.HasChild(new_file_context.meta_data.name));
  CHECK_NOTHROW(FileContext context(directory_.RemoveChild(new_file_context.meta_data.name)));
  CHECK_NOTHROW(file_context = directory_.GetChildAndIncrementCounter());
  CHECK("A" == file_context->meta_data.name);
  CHECK_NOTHROW(file_context = directory_.GetChildAndIncrementCounter());
  CHECK("B" == file_context->meta_data.name);

  // Try to remove a non-existent element and check iterator is not reset
  REQUIRE_FALSE(directory_.HasChild(new_file_context.meta_data.name));
  CHECK_THROWS_AS(FileContext context(directory_.RemoveChild(new_file_context.meta_data.name)),
                  std::exception);
  CHECK_NOTHROW(file_context = directory_.GetChildAndIncrementCounter());
  CHECK("C" == file_context->meta_data.name);
  CHECK_NOTHROW(file_context = directory_.GetChildAndIncrementCounter());
  CHECK("D" == file_context->meta_data.name);

  // Check operator<
  // DirectoryListing directory_listing1(Identity(crypto::Hash<crypto::SHA512>(std::string("A")))),
  //     directory_listing2(Identity(crypto::Hash<crypto::SHA512>(std::string("B"))));
  // CHECK(directory_listing1 < directory_listing2);
}

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
