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
#  include <windows.h>
#endif

#include <fstream>  // NOLINT
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

class DirectoryListingTest : public testing::Test {
 public:
  DirectoryListingTest()
      : name_(RandomAlphaNumericString(64)),
        directory_listing_(name_),
        main_test_dir_(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive")),
        relative_root_(fs::path("/").make_preferred()) {}

 protected:
  void SetUp() override {}

  void TearDown() override {}

  void GenerateDirectoryListingEntryForDirectory(DirectoryListing& directory_listing,
                                                 fs::path const& path) {
    MetaData meta_data(path.filename(), true);
#ifdef WIN32
    meta_data.attributes = FILE_ATTRIBUTE_DIRECTORY;
    GetSystemTimeAsFileTime(&meta_data.creation_time);
    GetSystemTimeAsFileTime(&meta_data.last_access_time);
    GetSystemTimeAsFileTime(&meta_data.last_write_time);
#else
    time(&meta_data.attributes.st_atime);
    time(&meta_data.attributes.st_mtime);
#endif
    *meta_data.directory_id = Identity(
        crypto::Hash<crypto::SHA512>((*main_test_dir_ / path).string()));
    EXPECT_NO_THROW(directory_listing.AddChild(meta_data));
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
          EXPECT_TRUE(GenerateDirectoryListings((*itr).path(),
                                                relative_path / (*itr).path().filename()));
        } else if (fs::is_regular_file(*itr)) {
          GenerateDirectoryListingEntryForFile(directory_listing,
                                               (*itr).path().filename(),
                                               fs::file_size((*itr).path()));
        } else {
          if (fs::exists(*itr))
            LOG(kInfo) << "Unknown type found.";
          else
            LOG(kInfo) << "Nonexistant type found.";
          return false;
        }
      }
      EXPECT_TRUE(WriteFile(path / "msdir.listing", directory_listing.Serialise()));
    }
    catch(...) {
      LOG(kError) << "Test GenerateDirectoryListings: Failed";
      return false;
    }
    return true;
  }

  bool RemoveDirectoryListingsEntries(fs::path const& path,
                                      fs::path const& relative_path) {
    std::string serialised_directory_listing;
    EXPECT_TRUE(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    // Remove the directory listing file...
    boost::system::error_code error_code;
    EXPECT_TRUE(fs::remove(path / "msdir.listing", error_code)) << error_code.message();
    fs::directory_iterator itr(path), end;
    try {
      MetaData metadata;
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          EXPECT_TRUE(RemoveDirectoryListingsEntries((*itr).path(),
                                                     relative_path / (*itr).path().filename()));
          EXPECT_NO_THROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          EXPECT_NO_THROW(directory_listing.RemoveChild(metadata));
          // Remove the disk directory also...
          EXPECT_TRUE(fs::remove((*itr).path(), error_code)) << error_code.message();
          EXPECT_EQ(error_code.value(), 0) << error_code.message();
        } else if (fs::is_regular_file(*itr)) {
          EXPECT_NO_THROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          EXPECT_NO_THROW(directory_listing.RemoveChild(metadata));
          // Remove the disk file also...
          EXPECT_TRUE(fs::remove((*itr).path(), error_code)) << error_code.message();
          EXPECT_EQ(error_code.value(), 0) << error_code.message();
        } else {
          if (fs::exists(*itr))
            LOG(kInfo) << "Unknown type found.";
          else
            LOG(kInfo) << "Nonexistant type found.";
          return false;
        }
      }
    }
    catch(...) {
      LOG(kError) << "Test RemoveDLE: Failed";
      return false;
    }
    EXPECT_TRUE(directory_listing.empty());
    return true;
  }

  bool RenameDirectoryEntries(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory_listing;
    EXPECT_TRUE(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    boost::system::error_code error_code;
    MetaData metadata;
    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          fs::path new_path(relative_path / (*itr).path().filename());
          EXPECT_TRUE(RenameDirectoryEntries((*itr).path(), new_path));
          EXPECT_NO_THROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          EXPECT_NO_THROW(directory_listing.RemoveChild(metadata));
          std::string new_name(RandomAlphaNumericString(5));
          metadata.name = fs::path(new_name);
          EXPECT_NO_THROW(directory_listing.AddChild(metadata));
          // Rename corresponding directory...
          fs::rename((*itr).path(), ((*itr).path().parent_path() / new_name), error_code);
          EXPECT_EQ(error_code.value(), 0) << error_code.message();
        } else if (fs::is_regular_file(*itr)) {
          if ((*itr).path().filename().string() != listing) {
            EXPECT_NO_THROW(directory_listing.GetChild((*itr).path().filename(), metadata));
            EXPECT_NO_THROW(directory_listing.RemoveChild(metadata));
            std::string new_name(RandomAlphaNumericString(5) + ".txt");
            metadata.name = fs::path(new_name);
            EXPECT_NO_THROW(directory_listing.AddChild(metadata));
            // Rename corresponding file...
            fs::rename((*itr).path(), ((*itr).path().parent_path() / new_name), error_code);
            EXPECT_EQ(error_code.value(), 0) << error_code.message();
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
    catch(...) {
      LOG(kError) << "Test RenameDLE: Failed";
      return false;
    }
    return true;
  }

  bool DirectoryHasChild(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory_listing;
    EXPECT_TRUE(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if ((*itr).path().filename().string() != listing) {
          if (fs::is_directory(*itr)) {
            fs::path name((*itr).path().filename());
            EXPECT_TRUE(DirectoryHasChild((*itr).path(), relative_path / name));
            EXPECT_TRUE(directory_listing.HasChild(name));
          } else if (fs::is_regular_file(*itr)) {
            EXPECT_TRUE(directory_listing.HasChild((*itr).path().filename()));
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
    catch(...) {
      LOG(kError) << "Test DLDHC: Failed";
      return false;
    }
    return true;
  }

  bool MatchEntries(fs::path const& path, fs::path relative_path) {
    std::string serialised_directory_listing;
    EXPECT_TRUE(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    MetaData metadata;
    std::string listing("msdir.listing");
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          EXPECT_TRUE(MatchEntries((*itr).path(), relative_path / (*itr).path().filename()));
          EXPECT_NO_THROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          EXPECT_TRUE(metadata.name == (*itr).path().filename());
        } else if (fs::is_regular_file(*itr)) {
          if ((*itr).path().filename().string() != listing) {
            EXPECT_NO_THROW(directory_listing.GetChild((*itr).path().filename(), metadata));
            EXPECT_TRUE(metadata.name == (*itr).path().filename());
            EXPECT_EQ(GetSize(metadata), fs::file_size((*itr).path()));
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
    catch(...) {
      LOG(kError) << "Test MatchEntries: Failed";
      return false;
    }
    if (relative_path == fs::path("\\") || relative_path == fs::path("/"))
      relative_path.clear();
    EXPECT_EQ(directory_listing.directory_id().string(),
              crypto::Hash<crypto::SHA512>((*main_test_dir_ / relative_path).string()).string());
    return true;
  }

  bool MatchEntriesUsingFreeFunctions(fs::path const& path, fs::path const& relative_path) {
    std::string serialised_directory_listing;
    EXPECT_TRUE(ReadFile(path / "msdir.listing", &serialised_directory_listing));
    DirectoryListing directory_listing(serialised_directory_listing);

    std::string listing("msdir.listing");
    MetaData metadata;
    fs::directory_iterator itr(path), end;
    try {
      for (; itr != end; ++itr) {
        if (fs::is_directory(*itr)) {
          EXPECT_TRUE(MatchEntriesUsingFreeFunctions((*itr).path(), relative_path));
          EXPECT_NO_THROW(directory_listing.GetChild((*itr).path().filename(), metadata));
          EXPECT_EQ(metadata.name, (*itr).path().filename());
          EXPECT_EQ(directory_listing.directory_id().string(),
                    crypto::Hash<crypto::SHA512>((*itr).path().parent_path().string()).string());
        } else if (fs::is_regular_file(*itr)) {
          if ((*itr).path().filename().string() != listing) {
            EXPECT_NO_THROW(directory_listing.GetChild((*itr).path().filename(), metadata));
            EXPECT_EQ(metadata.name, (*itr).path().filename());
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
    catch(...) {
      LOG(kError) << "Test MEUFF: Failed";
      return false;
    }
    return true;
  }

  Identity name_;
  DirectoryListing directory_listing_;
  maidsafe::test::TestPath main_test_dir_;
  fs::path relative_root_;

 private:
  DirectoryListingTest(const DirectoryListingTest&);
  DirectoryListingTest& operator=(const DirectoryListingTest&);
};

TEST_F(DirectoryListingTest, BEH_AddChildren) {
  ASSERT_TRUE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(MatchEntries(*main_test_dir_, relative_root_));
}

TEST_F(DirectoryListingTest, BEH_AddThenRemoveChildren) {
  ASSERT_TRUE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(RemoveDirectoryListingsEntries(*main_test_dir_, relative_root_));
}

TEST_F(DirectoryListingTest, BEH_AddThenRenameChildren) {
  ASSERT_TRUE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(RenameDirectoryEntries(*main_test_dir_, relative_root_));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(MatchEntries(*main_test_dir_, relative_root_));
}

TEST_F(DirectoryListingTest, BEH_DirectoryHasChild) {
  ASSERT_TRUE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(DirectoryHasChild(*main_test_dir_, relative_root_));
}

TEST_F(DirectoryListingTest, BEH_MatchEntriesUsingFreeFunctions) {
  ASSERT_TRUE(fs::exists(CreateTestDirectoriesAndFiles(*main_test_dir_)));
  ASSERT_TRUE(GenerateDirectoryListings(*main_test_dir_, relative_root_));
  ASSERT_TRUE(MatchEntriesUsingFreeFunctions(*main_test_dir_, relative_root_));
}

testing::AssertionResult DirectoriesMatch(const DirectoryListing& lhs,
                                          const DirectoryListing& rhs) {
  if (lhs.directory_id() != rhs.directory_id())
    return testing::AssertionFailure() << "Directory ID mismatch.";
  if (lhs.children_.size() != rhs.children_.size())
    return testing::AssertionFailure() << "Children size mismatch.";
  auto itr1(lhs.children_.begin()), itr2(rhs.children_.begin());
  for (; itr1 != lhs.children_.end(); ++itr1, ++itr2) {
    if ((*itr1).name != (*itr2).name)
      return testing::AssertionFailure() << "Names: " << (*itr1).name
          << " != " << (*itr2).name;
    if (((*itr1).data_map && !(*itr2).data_map) ||
        (!(*itr1).data_map && (*itr2).data_map))
      return testing::AssertionFailure() << "Data map pointer mismatch";
    if ((*itr1).data_map) {
      if (TotalSize((*itr1).data_map) !=
          TotalSize((*itr2).data_map))
        return testing::AssertionFailure() << "DataMap sizes: "
            << TotalSize((*itr1).data_map) << " != "
            << TotalSize((*itr2).data_map);
      if ((*itr1).data_map->chunks.size() !=
          (*itr2).data_map->chunks.size())
        return testing::AssertionFailure() << "DataMap chunks' sizes: "
            << (*itr1).data_map->chunks.size() << " != "
            << (*itr2).data_map->chunks.size();
      auto chunk_itr1((*itr1).data_map->chunks.begin());
      auto chunk_itr2((*itr2).data_map->chunks.begin());
      size_t chunk_no(0);
      for (; chunk_itr1 != (*itr1).data_map->chunks.end();
           ++chunk_itr1, ++chunk_itr2, ++chunk_no) {
        if ((*chunk_itr1).hash != (*chunk_itr2).hash)
          return testing::AssertionFailure() << "DataMap chunk " << chunk_no
              << " hash mismatch.";
        if ((*chunk_itr1).pre_hash != (*chunk_itr2).pre_hash)
          return testing::AssertionFailure() << "DataMap chunk " << chunk_no
              << " pre_hash mismatch.";
        if ((*chunk_itr1).size != (*chunk_itr2).size)
          return testing::AssertionFailure() << "DataMap chunk " << chunk_no
              << " pre_size mismatch.";
      }
      if ((*itr1).data_map->content !=
          (*itr2).data_map->content)
        return testing::AssertionFailure() << "DataMap content mismatch.";
//       if ((*itr1).data_map->self_encryption_type !=
//           (*itr2).data_map->self_encryption_type)
//         return testing::AssertionFailure() << "DataMap SE type mismatch.";
    }
//     if ((*itr1).end_of_file != (*itr2).end_of_file)
    if (GetSize(*itr1) != GetSize(*itr2))
      return testing::AssertionFailure() << "EOFs: " << GetSize(*itr1)
          << " != " << GetSize(*itr2);
#ifdef MAIDSAFE_WIN32
    if ((*itr1).allocation_size != (*itr2).allocation_size)
      return testing::AssertionFailure() << "Allocation sizes: "
          << (*itr1).allocation_size << " != "
          << (*itr2).allocation_size;
    if ((*itr1).attributes != (*itr2).attributes)
      return testing::AssertionFailure() << "Attributes: "
          << (*itr1).attributes << " != " << (*itr2).attributes;
    if ((*itr1).creation_time.dwHighDateTime !=
        (*itr2).creation_time.dwHighDateTime)
      return testing::AssertionFailure() << "Creation times high: "
          << (*itr1).creation_time.dwHighDateTime << " != "
          << (*itr2).creation_time.dwHighDateTime;
    if ((*itr1).creation_time.dwLowDateTime !=
        (*itr2).creation_time.dwLowDateTime) {
      uint32_t error = 0xA;
      if ((*itr1).creation_time.dwLowDateTime >
            (*itr2).creation_time.dwLowDateTime + error
          || (*itr1).creation_time.dwLowDateTime <
            (*itr2).creation_time.dwLowDateTime - error)
        return testing::AssertionFailure() << "Creation times low: "
            << (*itr1).creation_time.dwLowDateTime << " != "
            << (*itr2).creation_time.dwLowDateTime;
    }
    if ((*itr1).last_access_time.dwHighDateTime !=
        (*itr2).last_access_time.dwHighDateTime)
      return testing::AssertionFailure() << "Last access times high: "
          << (*itr1).last_access_time.dwHighDateTime << " != "
          << (*itr2).last_access_time.dwHighDateTime;
    if ((*itr1).last_access_time.dwLowDateTime !=
        (*itr2).last_access_time.dwLowDateTime) {
      uint32_t error = 0xA;
      if ((*itr1).last_access_time.dwLowDateTime >
            (*itr2).last_access_time.dwLowDateTime + error
          || (*itr1).last_access_time.dwLowDateTime <
            (*itr2).last_access_time.dwLowDateTime - error)
        return testing::AssertionFailure() << "Last access times low: "
            << (*itr1).last_access_time.dwLowDateTime << " != "
            << (*itr2).last_access_time.dwLowDateTime;
    }
    if ((*itr1).last_write_time.dwHighDateTime !=
        (*itr2).last_write_time.dwHighDateTime)
      return testing::AssertionFailure() << "Last write times high: "
          << (*itr1).last_write_time.dwHighDateTime << " != "
          << (*itr2).last_write_time.dwHighDateTime;
    if ((*itr1).last_write_time.dwLowDateTime !=
        (*itr2).last_write_time.dwLowDateTime) {
      uint32_t error = 0xA;
      if ((*itr1).last_write_time.dwLowDateTime >
            (*itr2).last_write_time.dwLowDateTime + error
          || (*itr1).last_write_time.dwLowDateTime <
            (*itr2).last_write_time.dwLowDateTime - error)
        return testing::AssertionFailure() << "Last write times low: "
            << (*itr1).last_write_time.dwLowDateTime << " != "
            << (*itr2).last_write_time.dwLowDateTime;
    }
#else
    if ((*itr1).attributes.st_atime != (*itr2).attributes.st_atime)
      return testing::AssertionFailure() << "Last access time mismatch: "
          << (*itr1).attributes.st_atime << " != "
          << (*itr2).attributes.st_atime;
    if ((*itr1).attributes.st_mtime != (*itr2).attributes.st_mtime)
      return testing::AssertionFailure() << "Last modification time mismatch: "
          << (*itr1).attributes.st_mtime << " != "
          << (*itr2).attributes.st_mtime;
#endif
  }

  return testing::AssertionSuccess();
}

TEST_F(DirectoryListingTest, BEH_SerialiseDeserialise) {
  maidsafe::test::TestPath testpath(maidsafe::test::CreateTestPath("MaidSafe_Test_Drive"));
  boost::system::error_code error_code;
  int64_t file_size(0);
  ASSERT_TRUE(fs::create_directories(*testpath
              / directory_listing_.directory_id().string(), error_code))
                  << error_code.message();
  ASSERT_EQ(error_code.value(), 0) << error_code.message();
  ASSERT_TRUE(fs::exists(*testpath / directory_listing_.directory_id().string(), error_code))
                  << error_code.message();
  ASSERT_EQ(error_code.value(), 0) << error_code.message();
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
    EXPECT_NO_THROW(directory_listing_.AddChild(meta_data));
  }

  std::string serialised_directory_listing(directory_listing_.Serialise());
  DirectoryListing recovered_directory_listing(serialised_directory_listing);
  EXPECT_TRUE(DirectoriesMatch(directory_listing_, recovered_directory_listing));
}

TEST_F(DirectoryListingTest, BEH_IteratorResetAndFailures) {
  // Add elements
  ASSERT_TRUE(directory_listing_.empty());
  const size_t kTestCount(10);
  ASSERT_LE(4U, kTestCount) << "kTestCount must be > 4";
  char c('A');
  for (size_t i(0); i != kTestCount; ++i, ++c) {
    MetaData metadata(std::string(1, c), ((i % 2) == 0));
    EXPECT_NO_THROW(directory_listing_.AddChild(metadata));
  }
  EXPECT_FALSE(directory_listing_.empty());

  // Check internal iterator
  MetaData meta_data;
  c = 'A';
  for (size_t i(0); i != kTestCount; ++i, ++c) {
    MetaData metadata(std::string(1, c), ((i % 2) == 0));
    EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
    EXPECT_EQ(std::string(1, c), meta_data.name);
    EXPECT_EQ(((i % 2) == 0), (meta_data.directory_id.get() != nullptr));
  }
  EXPECT_FALSE(directory_listing_.GetChildAndIncrementItr(meta_data));
  directory_listing_.SortAndResetChildrenIterator();
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("A", meta_data.name);
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("B", meta_data.name);

  // Add another element and check iterator is reset
  ++c;
  meta_data.name = std::string(1, c);
  EXPECT_NO_THROW(directory_listing_.AddChild(meta_data));
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("A", meta_data.name);
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("B", meta_data.name);

  // Remove an element and check iterator is reset
  meta_data.name = std::string(1, c);
  ASSERT_TRUE(directory_listing_.HasChild(meta_data.name));
  EXPECT_NO_THROW(directory_listing_.RemoveChild(meta_data));
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("A", meta_data.name);
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("B", meta_data.name);

  // Try to remove a non-existent element and check iterator is not reset
  meta_data.name = std::string(1, c);
  ASSERT_FALSE(directory_listing_.HasChild(meta_data.name));
  EXPECT_THROW(directory_listing_.RemoveChild(meta_data), std::exception);
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("C", meta_data.name);
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("D", meta_data.name);

  // Update an element and check iterator is reset
  meta_data.name = "A";
  EXPECT_NO_THROW(directory_listing_.GetChild("A", meta_data));
  // ASSERT_EQ(kDirectorySize, GetSize(meta_data));
#ifdef MAIDSAFE_WIN32
  meta_data.end_of_file = 1U;
#else
  meta_data.attributes.st_size = 1U;
#endif
  EXPECT_NO_THROW(directory_listing_.UpdateChild(meta_data));
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("A", meta_data.name);
  EXPECT_EQ(1U, GetSize(meta_data));
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("B", meta_data.name);

  // Try to update a non-existent element and check iterator is not reset
  meta_data.name = std::string(1, c);
  ASSERT_FALSE(directory_listing_.HasChild(meta_data.name));
  EXPECT_THROW(directory_listing_.UpdateChild(meta_data), std::exception);
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("C", meta_data.name);
  EXPECT_TRUE(directory_listing_.GetChildAndIncrementItr(meta_data));
  EXPECT_EQ("D", meta_data.name);

  // Check operator<
  DirectoryListing directory_listing1(Identity(crypto::Hash<crypto::SHA512>(std::string("A")))),
                   directory_listing2(Identity(crypto::Hash<crypto::SHA512>(std::string("B"))));
  EXPECT_TRUE(directory_listing1 < directory_listing2);
  EXPECT_FALSE(directory_listing2 < directory_listing1);
}

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
