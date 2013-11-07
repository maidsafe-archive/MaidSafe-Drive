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

#include "maidsafe/encrypt/data_map.h"

#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory_listing.h"
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

class DirectoryListingTest {
 public:
  DirectoryListingTest()
      : name_(RandomAlphaNumericString(64)),
        directory_listing_(name_),
        main_test_dir_(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive")),
        relative_root_(fs::path("/").make_preferred()) {}

 protected:
  void GenerateDirectoryListingEntryForDirectory(DirectoryListing& directory_listing,
                                                 fs::path const& path) {
    MetaData meta_data(path.filename(), true);
#ifdef MAIDSAFE_WIN32
    meta_data.attributes = FILE_ATTRIBUTE_DIRECTORY;
    GetSystemTimeAsFileTime(&meta_data.creation_time);
    GetSystemTimeAsFileTime(&meta_data.last_access_time);
    GetSystemTimeAsFileTime(&meta_data.last_write_time);
#else
    time(&meta_data.attributes.st_atime);
    time(&meta_data.attributes.st_mtime);
#endif
    *meta_data.directory_id =
        Identity(crypto::Hash<crypto::SHA512>((*main_test_dir_ / path).string()));
    CHECK_NOTHROW(directory_listing.AddChild(meta_data));
  }

  bool GenerateDirectoryListings(fs::path const& path, fs::path relative_path) {
    fs::directory_iterator itr(path), end;
    // Create directory listing for relative path...
    if (relative_path == fs::path("\\") || relative_path == fs::path("/"))
      relative_path.clear();
    DirectoryListing directory_listing(
        Identity(crypto::Hash<crypto::SHA512>((*main_test_dir_ / relative_path).string())));
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          GenerateDirectoryListingEntryForDirectory(directory_listing, (*itr).path().filename());
          CHECK(GenerateDirectoryListings((*itr).path(), relative_path / (*itr).path().filename()));
        } else if (fs::is_regular_file(*itr)) {
          GenerateDirectoryListingEntryForFile(directory_listing, (*itr).path().filename(),
                                               fs::file_size((*itr).path()));
        } else {
          if (fs::exists(*itr))
            LOG(kInfo) << "Unknown type found.";
          else
            LOG(kInfo) << "Nonexistant type found.";
          return false;
        }
      }
      CHECK(WriteFile(path / "msdir.listing", directory_listing.Serialise()));
    }
    catch (...) {
      LOG(kError) << "Test GenerateDirectoryListings: Failed";
      return false;
    }
    return true;
  }

  bool RemoveDirectoryListingsEntries(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory_listing;
    CHECK(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    // Remove the directory listing file...
    boost::system::error_code error_code;
    CHECK(fs::remove(path / "msdir.listing", error_code));
    fs::directory_iterator itr(path), end;
    try {
      MetaData metadata;
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          CHECK(RemoveDirectoryListingsEntries((*itr).path(),
                                                     relative_path / (*itr).path().filename()));
          CHECK_NOTHROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          CHECK_NOTHROW(directory_listing.RemoveChild(metadata));
          // Remove the disk directory also...
          CheckedRemove((*itr).path());
        } else if (fs::is_regular_file(*itr)) {
          CHECK_NOTHROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          CHECK_NOTHROW(directory_listing.RemoveChild(metadata));
          // Remove the disk file also...
          CheckedRemove((*itr).path());
        } else {
          if (fs::exists(*itr))
            LOG(kInfo) << "Unknown type found.";
          else
            LOG(kInfo) << "Nonexistant type found.";
          return false;
        }
      }
    }
    catch (...) {
      LOG(kError) << "Test RemoveDLE: Failed";
      return false;
    }
    CHECK(directory_listing.empty());
    return true;
  }

  bool RenameDirectoryEntries(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory_listing;
    CHECK(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    boost::system::error_code error_code;
    MetaData metadata;
    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          fs::path new_path(relative_path / (*itr).path().filename());
          CHECK(RenameDirectoryEntries((*itr).path(), new_path));
          CHECK_NOTHROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          CHECK_NOTHROW(directory_listing.RemoveChild(metadata));
          std::string new_name(RandomAlphaNumericString(5));
          metadata.name = fs::path(new_name);
          CHECK_NOTHROW(directory_listing.AddChild(metadata));
          // Rename corresponding directory...
          CheckedRename((*itr).path(), ((*itr).path().parent_path() / new_name));
        } else if (fs::is_regular_file(*itr)) {
          if ((*itr).path().filename().string() != listing) {
            CHECK_NOTHROW(directory_listing.GetChild((*itr).path().filename(), metadata));
            CHECK_NOTHROW(directory_listing.RemoveChild(metadata));
            std::string new_name(RandomAlphaNumericString(5) + ".txt");
            metadata.name = fs::path(new_name);
            CHECK_NOTHROW(directory_listing.AddChild(metadata));
            // Rename corresponding file...
            CheckedRename((*itr).path(), ((*itr).path().parent_path() / new_name));
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
    catch (...) {
      LOG(kError) << "Test RenameDLE: Failed";
      return false;
    }
    return true;
  }

  bool DirectoryHasChild(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory_listing;
    CHECK(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if ((*itr).path().filename().string() != listing) {
          if (fs::is_directory(*itr)) {
            fs::path name((*itr).path().filename());
            CHECK(DirectoryHasChild((*itr).path(), relative_path / name));
            CHECK(directory_listing.HasChild(name));
          } else if (fs::is_regular_file(*itr)) {
            CHECK(directory_listing.HasChild((*itr).path().filename()));
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
    catch (...) {
      LOG(kError) << "Test DLDHC: Failed";
      return false;
    }
    return true;
  }

  bool MatchEntries(fs::path const& path, fs::path relative_path) {
    std::string serialised_directory_listing;
    CHECK(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    MetaData metadata;
    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          CHECK(MatchEntries((*itr).path(), relative_path / (*itr).path().filename()));
          CHECK_NOTHROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          CHECK(metadata.name == (*itr).path().filename());
        } else if (fs::is_regular_file(*itr)) {
          if ((*itr).path().filename().string() != listing) {
            CHECK_NOTHROW(directory_listing.GetChild((*itr).path().filename(), metadata));
            CHECK(metadata.name == (*itr).path().filename());
            CHECK(GetSize(metadata) == fs::file_size((*itr).path()));
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
    catch (...) {
      LOG(kError) << "Test MatchEntries: Failed";
      return false;
    }
    if (relative_path == fs::path("\\") || relative_path == fs::path("/"))
      relative_path.clear();
    CHECK(directory_listing.directory_id().string() ==
          crypto::Hash<crypto::SHA512>((*main_test_dir_ / relative_path).string()).string());
    return true;
  }

  bool MatchEntriesUsingFreeFunctions(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory_listing;
    CHECK(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    std::string listing("msdir.listing");
    MetaData metadata;
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          CHECK(MatchEntriesUsingFreeFunctions((*itr).path(), relative_path));
          CHECK_NOTHROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          CHECK(metadata.name == (*itr).path().filename());
          CHECK(directory_listing.directory_id().string() ==
                    crypto::Hash<crypto::SHA512>((*itr).path().parent_path().string()).string());
        } else if (fs::is_regular_file(*itr)) {
          if ((*itr).path().filename().string() != listing) {
            CHECK_NOTHROW(directory_listing.GetChild((*itr).path().filename(), metadata));
            CHECK(metadata.name == (*itr).path().filename());
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
    catch (...) {
      LOG(kError) << "Test failed";
      return false;
    }
    return true;
  }

  void SortAndResetChildrenIterator() {
    directory_listing_.SortAndResetChildrenIterator();
  }

  Identity name_;
  DirectoryListing directory_listing_;
  maidsafe::test::TestPath main_test_dir_;
  fs::path relative_root_;

 private:
  DirectoryListingTest(const DirectoryListingTest&);
  DirectoryListingTest& operator=(const DirectoryListingTest&);
};

TEST_CASE_METHOD(DirectoryListingTest, "Add children", "[DirectoryListing][behavioural]") {
  REQUIRE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(MatchEntries(*main_test_dir_, relative_root_));
}

TEST_CASE_METHOD(DirectoryListingTest, "Add then remove children",
                 "[DirectoryListing][behavioural]") {
  REQUIRE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(RemoveDirectoryListingsEntries(*main_test_dir_, relative_root_));
}

TEST_CASE_METHOD(DirectoryListingTest, "Add then rename children",
                 "[DirectoryListing][behavioural]") {
  REQUIRE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(RenameDirectoryEntries(*main_test_dir_, relative_root_));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(MatchEntries(*main_test_dir_, relative_root_));
}

TEST_CASE_METHOD(DirectoryListingTest, "Directory has child", "[DirectoryListing][behavioural]") {
  REQUIRE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(DirectoryHasChild(*main_test_dir_, relative_root_));
}

TEST_CASE_METHOD(DirectoryListingTest, "Match entries using free functions",
                 "[DirectoryListing][behavioural]") {
  REQUIRE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  REQUIRE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  REQUIRE(MatchEntriesUsingFreeFunctions(*main_test_dir_, relative_root_));
}

void DirectoriesMatch(const DirectoryListing& lhs, const DirectoryListing& rhs) {
  if (lhs.directory_id() != rhs.directory_id())
    FAIL("Directory ID mismatch.");
  REQUIRE(lhs.children_.size() == rhs.children_.size());
  auto itr1(lhs.children_.begin()), itr2(rhs.children_.begin());
  for (; itr1 != lhs.children_.end(); ++itr1, ++itr2) {
    REQUIRE((*itr1).name == (*itr2).name);
    if (((*itr1).data_map && !(*itr2).data_map) || (!(*itr1).data_map && (*itr2).data_map))
      FAIL("Data map pointer mismatch");
    if ((*itr1).data_map) {
      REQUIRE(TotalSize((*itr1).data_map) == TotalSize((*itr2).data_map));
      REQUIRE((*itr1).data_map->chunks.size() == (*itr2).data_map->chunks.size());
      auto chunk_itr1((*itr1).data_map->chunks.begin());
      auto chunk_itr2((*itr2).data_map->chunks.begin());
      size_t chunk_no(0);
      for (; chunk_itr1 != (*itr1).data_map->chunks.end(); ++chunk_itr1, ++chunk_itr2, ++chunk_no) {
        if ((*chunk_itr1).hash != (*chunk_itr2).hash)
          FAIL("DataMap chunk " << chunk_no << " hash mismatch.");
        if ((*chunk_itr1).pre_hash != (*chunk_itr2).pre_hash)
          FAIL("DataMap chunk " << chunk_no << " pre_hash mismatch.");
        REQUIRE((*chunk_itr1).size == (*chunk_itr2).size);
      }
      if ((*itr1).data_map->content != (*itr2).data_map->content)
        FAIL("DataMap content mismatch.");
      //       if ((*itr1).data_map->self_encryption_type !=
      //           (*itr2).data_map->self_encryption_type)
      //         FAIL("DataMap SE type mismatch.");
    }
    //     if ((*itr1).end_of_file != (*itr2).end_of_file)
    REQUIRE(GetSize(*itr1) == GetSize(*itr2));
#ifdef MAIDSAFE_WIN32
    REQUIRE((*itr1).allocation_size == (*itr2).allocation_size);
    REQUIRE((*itr1).attributes == (*itr2).attributes);
    REQUIRE((*itr1).creation_time.dwHighDateTime == (*itr2).creation_time.dwHighDateTime);
    if ((*itr1).creation_time.dwLowDateTime != (*itr2).creation_time.dwLowDateTime) {
      uint32_t error = 0xA;
      if ((*itr1).creation_time.dwLowDateTime > (*itr2).creation_time.dwLowDateTime + error ||
          (*itr1).creation_time.dwLowDateTime < (*itr2).creation_time.dwLowDateTime - error)
        FAIL("Creation times low: " << (*itr1).creation_time.dwLowDateTime << " != "
             << (*itr2).creation_time.dwLowDateTime);
    }
    REQUIRE((*itr1).last_access_time.dwHighDateTime == (*itr2).last_access_time.dwHighDateTime);
    if ((*itr1).last_access_time.dwLowDateTime != (*itr2).last_access_time.dwLowDateTime) {
      uint32_t error = 0xA;
      if ((*itr1).last_access_time.dwLowDateTime > (*itr2).last_access_time.dwLowDateTime + error ||
          (*itr1).last_access_time.dwLowDateTime < (*itr2).last_access_time.dwLowDateTime - error)
        FAIL("Last access times low: " << (*itr1).last_access_time.dwLowDateTime << " != "
             << (*itr2).last_access_time.dwLowDateTime);
    }
    REQUIRE((*itr1).last_write_time.dwHighDateTime == (*itr2).last_write_time.dwHighDateTime);
    if ((*itr1).last_write_time.dwLowDateTime != (*itr2).last_write_time.dwLowDateTime) {
      uint32_t error = 0xA;
      if ((*itr1).last_write_time.dwLowDateTime > (*itr2).last_write_time.dwLowDateTime + error ||
          (*itr1).last_write_time.dwLowDateTime < (*itr2).last_write_time.dwLowDateTime - error)
        FAIL("Last write times low: " << (*itr1).last_write_time.dwLowDateTime << " != "
             << (*itr2).last_write_time.dwLowDateTime);
    }
#else
    REQUIRE((*itr1).attributes.st_atime != (*itr2).attributes.st_atime);
    REQUIRE((*itr1).attributes.st_mtime != (*itr2).attributes.st_mtime);
#endif
  }
}

TEST_CASE_METHOD(DirectoryListingTest, "Serialise and parse", "[DirectoryListing][behavioural]") {
  maidsafe::test::TestPath testpath(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive"));
  boost::system::error_code error_code;
  int64_t file_size(0);
  CheckedCreateDirectories(*testpath / directory_listing_.directory_id().string());

  RequiredExists(*testpath / directory_listing_.directory_id().string());
  fs::path file(CreateTestFile(*testpath / directory_listing_.directory_id().string(), file_size));

  std::vector<MetaData> meta_datas_before;
  for (int i = 0; i != 10; ++i) {
    bool is_dir((i % 2) == 0);
    std::string child_name("Child " + std::to_string(i));
    MetaData meta_data(child_name, is_dir);
    if (is_dir) {
#ifdef MAIDSAFE_WIN32
      meta_data.attributes = FILE_ATTRIBUTE_DIRECTORY;
      GetSystemTimeAsFileTime(&meta_data.creation_time);
      GetSystemTimeAsFileTime(&meta_data.last_access_time);
      GetSystemTimeAsFileTime(&meta_data.last_write_time);
#else
      time(&meta_data.attributes.st_atime);
      time(&meta_data.attributes.st_mtime);
#endif
    } else {
#ifdef MAIDSAFE_WIN32
      meta_data.end_of_file = RandomUint32();
      // When archiving MetaData the following assumption is made,
      // end_of_file == allocation_size. This is reasonable since when file
      // info is queried or on closing a file we set those values equal. This
      // stemmed from cbfs asserting when end_of_file.QuadPart was less than
      // allocation_size.QuadPart, although they were not always set in an
      // order that avoided this, so, to allow the test to pass...
      // meta_data.allocation_size = RandomUint32();
      meta_data.allocation_size = meta_data.end_of_file;
      meta_data.attributes = FILE_ATTRIBUTE_NORMAL;
      GetSystemTimeAsFileTime(&meta_data.creation_time);
      GetSystemTimeAsFileTime(&meta_data.last_access_time);
      GetSystemTimeAsFileTime(&meta_data.last_write_time);
#else
      time(&meta_data.attributes.st_atime);
      time(&meta_data.attributes.st_mtime);
      meta_data.attributes.st_size = RandomUint32();
#endif
      meta_data.data_map->content = RandomString(10);
    }
    meta_datas_before.push_back(meta_data);
    CHECK_NOTHROW(directory_listing_.AddChild(meta_data));
  }

  std::string serialised_directory_listing(directory_listing_.Serialise());
  DirectoryListing recovered_directory_listing(serialised_directory_listing);
  DirectoriesMatch(directory_listing_, recovered_directory_listing);
}

TEST_CASE_METHOD(DirectoryListingTest, "Iterator reset", "[DirectoryListing][behavioural]") {
  // Add elements
  REQUIRE(directory_listing_.empty());
  const size_t kTestCount(10);
  CHECK(4U < kTestCount);
  char c('A');
  for (size_t i(0); i != kTestCount; ++i, ++c) {
    MetaData metadata(std::string(1, c), ((i % 2) == 0));
    CHECK_NOTHROW(directory_listing_.AddChild(metadata));
  }
  CHECK_FALSE(directory_listing_.empty());

  // Check internal iterator
  MetaData meta_data;
  c = 'A';
  for (size_t i(0); i != kTestCount; ++i, ++c) {
    MetaData metadata(std::string(1, c), ((i % 2) == 0));
    CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
    CHECK(std::string(1, c) == meta_data.name);
    CHECK(((i % 2) == 0) == (meta_data.directory_id.get() != nullptr));
  }
  CHECK_FALSE(directory_listing_.GetChildAndIncrementItr(meta_data));
  SortAndResetChildrenIterator();
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("A" == meta_data.name);
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("B" == meta_data.name);

  // Add another element and check iterator is reset
  ++c;
  meta_data.name = std::string(1, c);
  CHECK_NOTHROW(directory_listing_.AddChild(meta_data));
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("A" == meta_data.name);
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("B" == meta_data.name);

  // Remove an element and check iterator is reset
  meta_data.name = std::string(1, c);
  REQUIRE(directory_listing_.HasChild(meta_data.name));
  CHECK_NOTHROW(directory_listing_.RemoveChild(meta_data));
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("A" == meta_data.name);
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("B" == meta_data.name);

  // Try to remove a non-existent element and check iterator is not reset
  meta_data.name = std::string(1, c);
  REQUIRE_FALSE(directory_listing_.HasChild(meta_data.name));
  CHECK_THROWS_AS(directory_listing_.RemoveChild(meta_data), std::exception);
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("C" == meta_data.name);
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("D" == meta_data.name);

  // Update an element and check iterator is reset
  meta_data.name = "A";
  CHECK_NOTHROW(directory_listing_.GetChild("A", meta_data));
// REQUIRE(kDirectorySize == GetSize(meta_data));
#ifdef MAIDSAFE_WIN32
  meta_data.end_of_file = 1U;
#else
  meta_data.attributes.st_size = 1U;
#endif
  CHECK_NOTHROW(directory_listing_.UpdateChild(meta_data));
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("A" == meta_data.name);
  CHECK(1U == GetSize(meta_data));
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("B" == meta_data.name);

  // Try to update a non-existent element and check iterator is not reset
  meta_data.name = std::string(1, c);
  REQUIRE_FALSE(directory_listing_.HasChild(meta_data.name));
  CHECK_THROWS_AS(directory_listing_.UpdateChild(meta_data), std::exception);
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("C" == meta_data.name);
  CHECK(directory_listing_.GetChildAndIncrementItr(meta_data));
  CHECK("D" == meta_data.name);

  // Check operator<
  DirectoryListing directory_listing1(Identity(crypto::Hash<crypto::SHA512>(std::string("A")))),
      directory_listing2(Identity(crypto::Hash<crypto::SHA512>(std::string("B"))));
  CHECK(directory_listing1 < directory_listing2);
}

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
