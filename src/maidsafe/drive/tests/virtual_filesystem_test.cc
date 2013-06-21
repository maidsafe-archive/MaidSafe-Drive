/*******************************************************************************
 *  Copyright 2011 maidsafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 *******************************************************************************
 */

#include <memory>
#include <cstdio>

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/fstream.hpp"

#include "maidsafe/common/crypto.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"

// #include "maidsafe/data_store/data_store.h"
#include "maidsafe/data_store/permanent_store.h"
#include "maidsafe/nfs/nfs.h"

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

namespace maidsafe {

namespace drive {

namespace test {

namespace {

fs::path g_test_mirror, g_mount_dir;
std::shared_ptr<DerivedDriveInUserSpace> g_drive;
bool g_virtual_filesystem_test;
passport::Maid* g_default_maid;

}  // unnamed namespace

class ApiTestEnvironment : public testing::Environment {
 public:
  // typedef data_store::DataStore<data_store::DataBuffer> DataStore;
  typedef data_store::PermanentStore DataStore;

  explicit ApiTestEnvironment(std::string test_directory)
      : root_test_dir_(new fs::path(test_directory)),
        main_test_dir_(maidsafe::test::CreateTestPath((*root_test_dir_).string())),
        virtual_filesystem_test_(test_directory == "MaidSafe_Test_Drive"),
        default_maid_(GetSigner()),
        routing_(default_maid_),
        client_nfs_(routing_, default_maid_),
        /*data_store_(MemoryUsage(0),
                    DiskUsage(1073741824),
                    DataStore::PopFunctor(),
                    *main_test_dir_ / "local"),*/
        data_store_(*main_test_dir_ / "local", DiskUsage(1073741824)),
        max_space_(1073741824),  // 2^30
        used_space_(0),
        unique_user_id_(Identity(crypto::Hash<crypto::SHA512>(main_test_dir_->string()))),
#ifdef WIN32
        drive_(std::make_shared<DerivedDriveInUserSpace>(client_nfs_,
                                                         data_store_,
                                                         default_maid_,
                                                         unique_user_id_,
                                                         "",
                                                         "S:",
                                                         "MaidSafeDrive",
                                                         max_space_,
                                                         used_space_)) {}
#else
        drive_() {}
#endif

 protected:
  void SetUp() {
#ifdef WIN32
    if (virtual_filesystem_test_) {
      g_mount_dir = "S:";
    } else {
      g_mount_dir = *main_test_dir_ / "TestMount";
    }
    g_test_mirror = *main_test_dir_ / "TestMirror";
#else
    g_mount_dir = *main_test_dir_ / "MaidSafeDrive";
    g_test_mirror = *main_test_dir_ / "Temp";
#endif

    boost::system::error_code error_code;
#ifndef WIN32
    fs::create_directories(g_mount_dir, error_code);
    ASSERT_EQ(0, error_code.value());
#else
    if (!virtual_filesystem_test_) {
      fs::create_directories(g_mount_dir, error_code);
      ASSERT_EQ(0, error_code.value());
    }
#endif
    fs::create_directories(g_test_mirror, error_code);
    ASSERT_EQ(0, error_code.value());
    if (virtual_filesystem_test_) {
#ifdef WIN32
      g_mount_dir /= "\\Owner";
#else
      drive_ = std::make_shared<DerivedDriveInUserSpace>(client_nfs_,
                                                         data_store_,
                                                         default_maid_,
                                                         unique_user_id_,
                                                         "",
                                                         g_mount_dir,
                                                         "MaidSafeDrive",
                                                         max_space_,
                                                         used_space_);
      // TODO(Team): Find out why, if the mount is put on the asio service,
      //             unmount hangs
      boost::thread th(std::bind(&DerivedDriveInUserSpace::Mount, drive_));
      if (!drive_->WaitUntilMounted()) {
        LOG(kError) << "Drive failed to mount";
//         asio_service_.Stop();
        ASSERT_TRUE(false);
      }
      g_mount_dir /= "Owner";
#endif
    }
    g_drive = drive_;
    g_virtual_filesystem_test = virtual_filesystem_test_;
    g_default_maid = &default_maid_;
  }

  void TearDown() {
    if (virtual_filesystem_test_) {
      int64_t max_space, used_space;
#ifdef WIN32
      std::static_pointer_cast<DerivedDriveInUserSpace>(drive_)->Unmount(max_space, used_space);
#else
      drive_->Unmount(max_space, used_space);
      drive_->WaitUntilUnMounted();
#endif
      BOOST_ASSERT(max_space == 1073741824);
    }
//    g_drive.reset();
    main_test_dir_.reset();
  }

  maidsafe::passport::Maid::signer_type GetSigner() {
    return maidsafe::passport::Maid::signer_type();
  }

 private:
  ApiTestEnvironment(const ApiTestEnvironment&);
  ApiTestEnvironment& operator=(const ApiTestEnvironment&);

  maidsafe::test::TestPath root_test_dir_;
  maidsafe::test::TestPath main_test_dir_;
  bool virtual_filesystem_test_;
  passport::Maid default_maid_;
  routing::Routing routing_;
  nfs::ClientMaidNfs client_nfs_;
  DataStore data_store_;
  int64_t max_space_, used_space_;
  Identity unique_user_id_;
  std::shared_ptr<DerivedDriveInUserSpace> drive_;
};

class CallbacksApiTest : public testing::Test {
 protected:
  void TearDown() {
    // if (!RemoveDirectories(g_mount_dir)) {
    //  // be persistent...
    //  if (!RemoveDirectories(g_mount_dir)) {
    //    // ok give up...
    //    LOG(kError) << "Failed to remove directories from " << g_mount_dir;
    //  }
    // }
    // if (!RemoveDirectories(g_test_mirror)) {
    //    // do it next time...
    //    LOG(kError) << "Failed to remove directories from " << g_test_mirror;
    // }
     fs::directory_iterator end;
     try {
      fs::directory_iterator begin1(g_test_mirror), end1;
      for (; begin1 != end1; ++begin1)
        fs::remove_all((*begin1).path());
     }
     catch(const std::exception &e) {
      LOG(kError) << e.what();
     }
     try {
      fs::directory_iterator begin2(g_mount_dir), end2;
      for (; begin2 != end2; ++begin2)
        fs::remove_all((*begin2).path());
     }
     catch(const std::exception &e) {
      LOG(kError) << e.what();
     }
  }

  fs::path CreateEmptyFile(fs::path const& path) {
    fs::path file(path / (RandomAlphaNumericString(5) + ".txt"));
    std::ofstream ofs;
    ofs.open(file.native().c_str(), std::ios_base::out | std::ios_base::binary);
    if (ofs.bad()) {
      LOG(kError) << "Can't open " << file;
    } else {
      ofs.close();
    }
    boost::system::error_code ec;
    EXPECT_TRUE(fs::exists(file, ec)) << file;
    EXPECT_EQ(0, ec.value());
    return file;
  }

  bool CreateFileAt(fs::path const& path) {
    EXPECT_TRUE(fs::exists(path));
    size_t size = RandomUint32() % 1048576;  // 2^20

    LOG(kInfo) << "CreateFileAt: filename = " << path << " size " << size;

    std::string file_content = maidsafe::RandomAlphaNumericString(size);
    std::ofstream ofs;
    ofs.open(path.native().c_str(), std::ios_base::out | std::ios_base::binary);
    if (ofs.bad()) {
      LOG(kError) << "Can't open " << path;
      return false;
    } else {
      ofs << file_content;
      ofs.close();
    }
    EXPECT_TRUE(fs::exists(path));
    return true;
  }

  fs::path CreateDirectoryContainingFiles(fs::path const& path) {
    int64_t file_size(0);
    LOG(kInfo) << "CreateDirectoryContainingFiles: directory = " << path;
    try {
      size_t r1 = 0;
      do {
        r1 = RandomUint32() % 5;
      } while (r1 < 2);

      fs::path directory(CreateTestDirectory(path)), check;

      for (size_t i = 0; i != r1; ++i) {
        check = CreateTestFile(directory, file_size);
        EXPECT_TRUE(fs::exists(check));
      }
      return directory;
    }
    catch(const std::exception &e) {
      LOG(kError) << e.what();
      return "";
    }
  };

  bool RemoveDirectories(fs::path const& path) {
    LOG(kInfo) << "RemoveDirectories: " << path;
    boost::system::error_code error_code;
    fs::directory_iterator begin(path), end;
    try {
      for (; begin != end; ++begin) {
        if (fs::is_directory(*begin)) {
          EXPECT_TRUE(RemoveDirectories((*begin).path()));
          EXPECT_TRUE(fs::remove((*begin).path(), error_code));
          if (error_code.value() != 0) {
            // try again...
            EXPECT_TRUE(fs::remove((*begin).path(), error_code));
            if (error_code.value() != 0) {
              LOG(kError) << "Failed to remove " << (*begin).path();
              return false;
            }
          }
        } else if (fs::is_regular_file(*begin)) {
          EXPECT_TRUE(fs::remove((*begin).path(), error_code));
          if (error_code.value() != 0) {
            // may as well...
            EXPECT_TRUE(fs::remove((*begin).path(), error_code));
            if (error_code.value() != 0) {
              LOG(kError) << "Failed to remove " << (*begin).path();
              return false;
            }
          }
        } else {
          // try removing it anyway...
          EXPECT_TRUE(fs::remove((*begin).path(), error_code));
          if (error_code.value() != 0) {
            // and again...
            EXPECT_TRUE(fs::remove((*begin).path(), error_code));
            if (error_code.value() != 0) {
              LOG(kError) << "Failed to remove " << (*begin).path();
              return false;
            }
          }
        }
      }
    } catch(...) {
      LOG(kError) << "RemoveDirectories: Failed";
      return false;
    }
    return true;
  };

  bool CopyDirectories(fs::path const& from, fs::path const& to) {
    LOG(kInfo) << "CopyDirectories: from " << from << " to " << (to / from.filename());

    fs::directory_iterator begin(from), end;

    if (!fs::exists(to / from.filename()))
      fs::create_directory(to / from.filename());
    EXPECT_TRUE(fs::exists(to / from.filename()));
    try {
      for (; begin != end; ++begin) {
        if (fs::is_directory(*begin)) {
          EXPECT_TRUE(CopyDirectories((*begin).path(), to / from.filename()));
        } else if (fs::is_regular_file(*begin)) {
          fs::copy_file((*begin).path(),
                        to / from.filename() / (*begin).path().filename(),
                        fs::copy_option::fail_if_exists);
          EXPECT_TRUE(fs::exists(to / from.filename() / (*begin).path().filename()));
        } else {
          if (fs::exists(*begin))
            LOG(kInfo) << "CopyDirectories: unknown type found.";
          else
            LOG(kInfo) << "CopyDirectories: nonexistant type found.";
          return false;
        }
      }
    }
    catch(...) {
      LOG(kError) << "CopyDirectories: Failed";
      return false;
    }
    return true;
  };

  bool CompareDirectoryEntries(fs::path const& drive_path, fs::path const& disk_path) {
    typedef std::set<fs::path>::iterator iterator;

    std::set<fs::path> drive_files, disk_files;
    try {
      fs::recursive_directory_iterator actual(drive_path), compare(disk_path), end;
      for (; actual != end; ++actual)
        drive_files.insert((*actual).path().filename());
      for (; compare != end; ++compare)
        disk_files.insert((*compare).path().filename());
    }
    catch(...) {
      LOG(kError) << "CompareDirectoryEntries: Failed";
      return false;
    }
    std::size_t drive_files_total(drive_files.size()), disk_files_total(disk_files.size());
    if (drive_files_total == disk_files_total) {
      iterator first1 = drive_files.begin(), last1 = drive_files.end(),
               first2 = disk_files.begin();
      for (; first1 != last1; ++first1, ++first2)
        EXPECT_EQ(*first1, *first2);
    } else if (drive_files_total > disk_files_total) {
      iterator first = disk_files.begin(), last = disk_files.end(), found;
      for (; first != last; ++first) {
        found = drive_files.find(*first);
        if (found != drive_files.end())
          EXPECT_EQ(*found, *first);
        else
          return false;
      }
    } else {
      iterator first = drive_files.begin(), last = drive_files.end(), found;
      for (; first != last; ++first) {
        found = disk_files.find(*first);
        if (found == drive_files.end())
          return false;
      }
    }
    return true;
  };

  bool CompareFileContents(fs::path const& path1, fs::path const& path2) {
    std::ifstream efile, ofile;
    efile.open(path1.c_str());
    ofile.open(path2.c_str());

    if (efile.bad() || ofile.bad())
      return false;
    while (efile.good() && ofile.good())
      if (efile.get() != ofile.get())
        return false;
    if (!(efile.eof() && ofile.eof()))
      return false;
    return true;
  }

  fs::path LocateNthFile(fs::path const& path, size_t n) {
    fs::recursive_directory_iterator begin(path), end;
    fs::path temp_path;
    size_t m = 0;
    try {
      for (; begin != end; ++begin) {
        if (fs::is_regular_file(*begin)) {
          temp_path = (*begin).path();
          if (++m == n)
            return temp_path;
        }
      }
    }
    catch(...) {
      LOG(kError) << "Test LocateNthFile: Failed";
      return fs::path();
    }
    // Return a potentially empty path whose index is less than n...
    return temp_path;
  }

  fs::path LocateNthDirectory(fs::path const& path, size_t n) {
    fs::recursive_directory_iterator begin(path), end;
    fs::path temp_path;
    size_t m = 0;
    try {
      for (; begin != end; ++begin) {
        if (fs::is_directory(*begin)) {
          temp_path = (*begin).path();
          if (++m == n)
            return temp_path;
        }
      }
    }
    catch(...) {
      LOG(kError) << "Test LocateNthDirectory: Failed";
      return fs::path();
    }
    // Return a potentially empty path whose index is less than n...
    return temp_path;
  }

  fs::path FindDirectoryOrFile(fs::path const& path,
                               fs::path const& find) {
    fs::recursive_directory_iterator begin(path), end;
    try {
      for (; begin != end; ++begin) {
          if ((*begin).path().filename() == find)
            return (*begin).path();
      }
    }
    catch(...) {
      LOG(kError) << "Test FindDirectoryOrFile: Failed";
      return fs::path();
    }
    // Failed to find 'find'...
    return fs::path();
  }

  bool DoRandomEvents() {
    LOG(kInfo) << "DoRandomEvents";
    // Values assigned to events, randomly chosen of course, are given by,
    //  1. Create directories hierarchy on disk containing arbitrary number of
    //     files then copy to virtual drive.
    //  2. Create a file on virtual drive then copy to mirror.
    //  3. Create a directory containing some files in mirror then copy to
    //     virtual drive.
    //  4. Delete a file on virtual drive and its corresponding mirror.
    //  5. Delete a directory on virtual drive and its corresponding mirror.
    //  6. Create a directory containing some files on virtual drive then copy
    //     to mirror.
    //  7. Create a file in mirror then copy do virtual drive.
    //  8. Unmount then remount virtual drive and compare contents of
    //     directories and files with those in mirror.
    //  9. Copy an existing file to new location on the virtual drive repeat for
    //     mirror.
    // 10. Find any file on the virtual drive then rename it and its mirror
    //     equivalently.
    // 11. Search for a file and compare contents with mirror.

    boost::system::error_code error_code;
    size_t count(15 + RandomUint32() % 5);
    int64_t file_size(0);

    for (size_t i = 0; i != count; ++i) {
      switch (RandomUint32() % 10) {
        case 0: {
          fs::path directories(CreateTestDirectoriesAndFiles(g_test_mirror));
          EXPECT_TRUE(fs::exists(directories, error_code));
          EXPECT_TRUE(CopyDirectories(directories, g_mount_dir));
          EXPECT_TRUE(fs::exists(g_mount_dir / directories.filename(), error_code));
          EXPECT_EQ(error_code.value(), 0);
          break;
        }
        case 1: {
          fs::path file(CreateTestFile(g_mount_dir, file_size));
          EXPECT_TRUE(fs::exists(file, error_code));
          fs::copy_file(file, g_test_mirror / file.filename());
          EXPECT_TRUE(fs::exists(g_test_mirror / file.filename(), error_code));
          EXPECT_EQ(error_code.value(), 0);
          break;
        }
        case 2: {
          fs::path directory(CreateDirectoryContainingFiles(g_test_mirror));
          EXPECT_FALSE(directory.empty());
          if (!directory.empty()) {
            EXPECT_TRUE(CopyDirectories(directory, g_mount_dir));
            EXPECT_TRUE(fs::exists(g_mount_dir / directory.filename(), error_code));
            EXPECT_EQ(error_code.value(), 0);
          }
          break;
        }
        case 3: {
          fs::path file(LocateNthFile(g_mount_dir, RandomUint32() % 30));
          if (file != fs::path()) {
            fs::path found(FindDirectoryOrFile(g_test_mirror, file.filename()));
            EXPECT_NE(found, fs::path());
            fs::remove(file, error_code);
            EXPECT_FALSE(fs::exists(file, error_code));
            EXPECT_EQ(error_code.value(), 2);
            fs::remove(found, error_code);
            EXPECT_FALSE(fs::exists(found, error_code));
            EXPECT_EQ(error_code.value(), 2);
          }
          break;
        }
        case 4: {
          // as above...
          fs::path directory(LocateNthDirectory(g_mount_dir, RandomUint32() % 30));
          if (directory != fs::path()) {
            fs::path found(FindDirectoryOrFile(g_test_mirror, directory.filename()));
            EXPECT_NE(found, fs::path());
            fs::remove_all(directory, error_code);
            EXPECT_FALSE(fs::exists(directory, error_code));
            EXPECT_EQ(error_code.value(), 2);
            fs::remove_all(found, error_code);
            EXPECT_FALSE(fs::exists(found, error_code));
            EXPECT_EQ(error_code.value(), 2);
          }
          break;
        }
        case 5: {
          boost::system::error_code error_code;
          // Create directory with random number of files...
          fs::path directory(CreateDirectoryContainingFiles(g_mount_dir));
          EXPECT_FALSE(directory.empty());
          if (!directory.empty()) {
            // Copy directory to disk...
            EXPECT_TRUE(CopyDirectories(directory, g_test_mirror));
            EXPECT_TRUE(fs::exists(g_test_mirror / directory.filename(), error_code));
            EXPECT_EQ(error_code.value(), 0);
          }
          break;
        }
        case 6: {
          typedef fs::copy_option copy_option;
          boost::system::error_code error_code;
          // Create file on disk...
          fs::path file(CreateTestFile(g_test_mirror, file_size));
          EXPECT_TRUE(fs::exists(file, error_code));
          EXPECT_EQ(error_code.value(), 0);
          // Copy file to virtual drive...
          fs::copy_file(file,
                        g_mount_dir / file.filename(),
                        copy_option::fail_if_exists,
                        error_code);
          EXPECT_EQ(error_code.value(), 0);
          EXPECT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
          break;
        }
//        case 7: {
//          if (mount_test_) {
// #ifdef WIN32
//            // Unmount...
//            EXPECT_EQ(0, std::static_pointer_cast<TestDriveInUserSpace>(
//                      drive_)->Unmount());
//            // Remount...
//            EXPECT_EQ(0, std::static_pointer_cast<TestDriveInUserSpace>(
//                      drive_)->Init(registration_key_));
// #endif
//          EXPECT_EQ(0, drive_->Mount(g_mount_dir, "TestDrive"));
//            EXPECT_TRUE(CompareDirectoryEntries(g_mount_dir, g_test_mirror));
//          }
//          break;
//        }
        case 7: {
          typedef fs::copy_option copy_option;
          boost::system::error_code error_code;
          fs::path file(LocateNthFile(g_mount_dir, RandomUint32() % 21));
          if (file != fs::path()) {
            fs::path found(FindDirectoryOrFile(g_test_mirror, file.filename()));
            EXPECT_NE(found, fs::path());
            fs::copy_file(found,
                          g_mount_dir / found.filename(),
                          copy_option::fail_if_exists,
                          error_code);
            EXPECT_TRUE(fs::exists(g_mount_dir / found.filename(),
                                   error_code));
            EXPECT_EQ(error_code.value(), 0);
            fs::copy_file(file,
                          g_test_mirror / file.filename(),
                          copy_option::fail_if_exists,
                          error_code);
            EXPECT_TRUE(fs::exists(g_test_mirror / file.filename(), error_code));
            EXPECT_EQ(error_code.value(), 0);
          }
          break;
        }
        case 8: {
          fs::path file(LocateNthFile(g_mount_dir, RandomUint32() % 30));
          if (file != fs::path()) {
            fs::path found(FindDirectoryOrFile(g_test_mirror, file.filename()));
            EXPECT_NE(found, fs::path());
            std::string new_name(maidsafe::RandomAlphaNumericString(5) + ".txt");
            fs::rename(found, found.parent_path() / new_name, error_code);
            EXPECT_TRUE(fs::exists(found.parent_path() / new_name, error_code));
            EXPECT_EQ(error_code.value(), 0);
            fs::rename(file, file.parent_path() / new_name, error_code);
            EXPECT_TRUE(fs::exists(file.parent_path() / new_name, error_code));
            EXPECT_EQ(error_code.value(), 0);
          }
          break;
        }
        case 9: {
          fs::path file(LocateNthFile(g_mount_dir, RandomUint32() % 30));
          if (file != fs::path()) {
            fs::path found(FindDirectoryOrFile(g_test_mirror, file.filename()));
            EXPECT_NE(found, fs::path());
            EXPECT_TRUE(CompareFileContents(file, found));
          }
          break;
        }
        default:
          LOG(kInfo) << "Can't reach here!";
      }
    }
    return true;
  }
};

TEST_F(CallbacksApiTest, BEH_CreateDirectoryOnDrive) {
  // Create empty directory on virtual drive...
  fs::path directory(CreateTestDirectory(g_mount_dir));
  ASSERT_TRUE(fs::exists(directory)) << directory;
}

#ifdef __MSVC__
#  pragma warning(push, 1)
#  pragma warning(disable: 4996)
#endif

TEST_F(CallbacksApiTest, BEH_AppendToFileTest) {
  fs::path file(g_mount_dir / (RandomAlphaNumericString(5) + ".txt"));
  FILE *test_file(NULL);
  int this_char(0);
  int num_of_a_chars = 0;
  int test_runs = 1000;

  for (int i = 0; i < test_runs; ++i) {
    test_file = fopen(file.string().c_str(), "a");
    ASSERT_TRUE(test_file != NULL);
    fputc('a', test_file);
    fclose(test_file);
    test_file = fopen(file.string().c_str(), "r");
    ASSERT_TRUE(test_file != NULL);
    while (this_char != EOF) {
      this_char = getc(test_file);
      if (this_char == 'a')
        ++num_of_a_chars;
    }
    ASSERT_EQ(num_of_a_chars, i + 1);
    fclose(test_file);
    num_of_a_chars = 0;
    this_char = 0;
  }
}

#ifdef __MSVC__
#  pragma warning(pop)
#endif

TEST_F(CallbacksApiTest, BEH_CopyEmptyDirectoryToDrive) {
  // Create empty directory on disk...
  fs::path directory(CreateTestDirectory(g_test_mirror));
  ASSERT_TRUE(fs::exists(directory));
  // Copy disk directory to virtual drive...
  fs::copy_directory(directory, g_mount_dir / directory.filename());
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename()));
}

TEST_F(CallbacksApiTest, BEH_CopyNonemptyDirectoryToDriveThenDelete) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create empty directory on disk...
  fs::path directory(CreateTestDirectory(g_test_mirror));
  ASSERT_TRUE(fs::exists(directory, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Create a file in newly created directory...
  fs::path file(CreateTestFile(directory, file_size));
  // Check used space...
  ASSERT_EQ(0, g_drive->GetUsedSpace());
  // Copy directory and file to virtual drive...
  ASSERT_TRUE(CopyDirectories(directory, g_mount_dir));
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename() / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  ASSERT_EQ(CalculateUsedSpace(g_test_mirror), CalculateUsedSpace(g_mount_dir));
  if (g_virtual_filesystem_test)
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
  // Delete the directory along with its contents...
  ASSERT_EQ(2U, fs::remove_all(g_mount_dir / directory.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / directory.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / directory.filename() / file.filename()));
  // Check used space...
  ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
  ASSERT_EQ(0, g_drive->GetUsedSpace());
}

TEST_F(CallbacksApiTest, BEH_CopyNonemptyDirectoryToDriveDeleteThenRecopy) {
  int64_t file_size(0);
  // Create empty directory on disk...
  fs::path directory(CreateTestDirectory(g_test_mirror));
  ASSERT_TRUE(fs::exists(directory));
  // Create a file in newly created directory...
  fs::path file(CreateTestFile(directory, file_size));
  // Copy directory and file to virtual drive...
  ASSERT_TRUE(CopyDirectories(directory, g_mount_dir));
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename()));
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename() / file.filename()));
  // Check used space...
  if (g_virtual_filesystem_test)
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
  // Delete the directory along with its contents...
  boost::system::error_code error_code;
  ASSERT_EQ(2U, fs::remove_all(g_mount_dir / directory.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / directory.filename()));
  ASSERT_FALSE(fs::exists(g_mount_dir / directory.filename() / file.filename()));
  // Check used space again...
  ASSERT_EQ(0, g_drive->GetUsedSpace());
  // Re-copy directory and file to virtual drive...
  ASSERT_TRUE(CopyDirectories(directory, g_mount_dir));
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename()));
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename() / file.filename()));
  // and again...
  if (g_virtual_filesystem_test) {
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
    ASSERT_EQ(file_size + kDirectorySize, g_drive->GetUsedSpace());
  }
}

TEST_F(CallbacksApiTest, BEH_CopyNonemptyDirectoryThenRename) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create empty directory on disk...
  fs::path directory(CreateTestDirectory(g_test_mirror));
  ASSERT_TRUE(fs::exists(directory, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Create a file in newly created directory...
  fs::path file(CreateTestFile(directory, file_size));
  // Copy directory and file to virtual drive...
  ASSERT_TRUE(CopyDirectories(directory, g_mount_dir));
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename() / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  ASSERT_EQ(CalculateUsedSpace(g_test_mirror), CalculateUsedSpace(g_mount_dir));
  if (g_virtual_filesystem_test)
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
  // Rename the directory...
  fs::path new_directory_name(g_mount_dir / maidsafe::RandomAlphaNumericString(5));
  fs::rename(g_mount_dir / directory.filename(), new_directory_name, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / directory.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(new_directory_name, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  ASSERT_EQ(file_size + kDirectorySize, CalculateUsedSpace(g_test_mirror));
  if (g_virtual_filesystem_test)
    ASSERT_EQ(file_size + kDirectorySize, g_drive->GetUsedSpace());
}

TEST_F(CallbacksApiTest, BEH_CopyNonemptyDirectoryRenameThenRecopy) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create empty directory on disk...
  fs::path directory(CreateTestDirectory(g_test_mirror));
  ASSERT_TRUE(fs::exists(directory, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Create a file in newly created directory...
  fs::path file(CreateTestFile(directory, file_size));
  // Copy directory and file to virtual drive...
  ASSERT_TRUE(CopyDirectories(directory, g_mount_dir));
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename() / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  if (g_virtual_filesystem_test)
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
  // Rename the directory...
  fs::path new_directory_name(g_mount_dir / maidsafe::RandomAlphaNumericString(5));
  fs::rename(g_mount_dir / directory.filename(), new_directory_name, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / directory.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(new_directory_name));
  // Re-copy disk directory and file to virtual drive...
  ASSERT_TRUE(CopyDirectories(directory, g_mount_dir));
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename() / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space again...
  if (g_virtual_filesystem_test) {
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
    ASSERT_EQ(2 * file_size + 2 * kDirectorySize, g_drive->GetUsedSpace());
  }
}

TEST_F(CallbacksApiTest, FUNC_CopyDirectoryContainingFiles) {
  boost::system::error_code error_code;
  // Create directory with random number of files...
  fs::path directory(CreateDirectoryContainingFiles(g_test_mirror));
  ASSERT_FALSE(directory.empty());
  // Copy directory to virtual drive...
  ASSERT_TRUE(CopyDirectories(directory, g_mount_dir));
  ASSERT_TRUE(fs::exists(g_mount_dir / directory.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  ASSERT_EQ(CalculateUsedSpace(g_test_mirror), CalculateUsedSpace(g_mount_dir));
  if (g_virtual_filesystem_test)
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
}

TEST_F(CallbacksApiTest, FUNC_CopyDirectoryContainingFilesAndDirectories) {
  boost::system::error_code error_code;
  // Create directories hierarchy some of which containing files...
  fs::path directories(CreateTestDirectoriesAndFiles(g_test_mirror));
  ASSERT_TRUE(fs::exists(directories));
  // Copy hierarchy to virtual drive...
  ASSERT_TRUE(CopyDirectories(directories, g_mount_dir));
  ASSERT_TRUE(fs::exists(g_mount_dir / directories.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  if (g_virtual_filesystem_test)
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
}

TEST_F(CallbacksApiTest, FUNC_CopyFileThenCopyCopiedFile) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create file on disk...
  fs::path file(CreateTestFile(g_test_mirror, file_size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive...
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive again...
  fs::copy_file(file,
                g_mount_dir / file.filename(),
                fs::copy_option::overwrite_if_exists,
                error_code);
  ASSERT_EQ(error_code.value(), 0) << error_code.message();
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  if (g_virtual_filesystem_test) {
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
    ASSERT_EQ(file_size, g_drive->GetUsedSpace());
  }
}

TEST_F(CallbacksApiTest, FUNC_CopyFileDeleteThenRecopy) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create file on disk...
  fs::path file(CreateTestFile(g_test_mirror, file_size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive...
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Delete the file...
  fs::remove(g_mount_dir / file.filename(), error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);
  // Copy file to virtual drive again...
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  if (g_virtual_filesystem_test) {
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
    ASSERT_EQ(file_size, g_drive->GetUsedSpace());
  }
}

TEST_F(CallbacksApiTest, FUNC_CopyFileRenameThenRecopy) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create file on disk...
  fs::path file(CreateTestFile(g_test_mirror, file_size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive...
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  if (g_virtual_filesystem_test)
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
  // Rename the file...
  fs::path new_file_name(g_mount_dir / (RandomAlphaNumericString(5) + ".txt"));
  fs::rename(g_mount_dir / file.filename(), new_file_name, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(new_file_name, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive again...
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_test_mirror / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  if (g_virtual_filesystem_test)
    ASSERT_EQ(2 * file_size, g_drive->GetUsedSpace());
}

TEST_F(CallbacksApiTest, BEH_CopyFileThenRead) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create file on disk...
  fs::path file(CreateTestFile(g_test_mirror, file_size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive...
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Write virtual drive file back to a disk file...
  fs::path test_file(g_test_mirror / (RandomAlphaNumericString(5) + ".txt"));
  fs::copy_file(g_mount_dir / file.filename(), test_file, fs::copy_option::overwrite_if_exists);
  ASSERT_TRUE(fs::exists(test_file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Compare content in the two files...
  ASSERT_EQ(fs::file_size(test_file), fs::file_size(file));
  ASSERT_TRUE(CompareFileContents(test_file, file));
  // Check used space...
  if (g_virtual_filesystem_test)
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
}

TEST_F(CallbacksApiTest, FUNC_CopyFileRenameThenRead) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create file on disk...
  fs::path file(CreateTestFile(g_test_mirror, file_size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive...
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Rename the file...
  fs::path new_file_name(g_mount_dir / (RandomAlphaNumericString(5) + ".txt"));
  fs::rename(g_mount_dir / file.filename(), new_file_name, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(new_file_name, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Write virtual drive file back to a disk file...
  fs::path test_file(g_test_mirror / new_file_name.filename());
  fs::copy_file(new_file_name, test_file, fs::copy_option::overwrite_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(test_file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Compare content in the two files...
  ASSERT_TRUE(CompareFileContents(test_file, file));
}

TEST_F(CallbacksApiTest, FUNC_CopyFileDeleteThenTryToRead) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create file on disk...
  fs::path file(CreateTestFile(g_test_mirror, file_size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive...
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Delete the file...
  fs::remove(g_mount_dir / file.filename(), error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);
  // Write virtual drive file back to a disk file...
  fs::path test_file(g_test_mirror / (RandomAlphaNumericString(5) + ".txt"));
  fs::copy_file(g_mount_dir / file.filename(),
                test_file,
                fs::copy_option::overwrite_if_exists,
                error_code);
  ASSERT_NE(error_code.value(), 0);
  // Compare content in the two files...
  ASSERT_FALSE(CompareFileContents(test_file, file));
}

TEST_F(CallbacksApiTest, BEH_CreateFileOnDriveThenRead) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create file on virtual drive...
  fs::path file(CreateTestFile(g_mount_dir, file_size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Write virtual drive file out to disk...
  fs::path test_file(g_test_mirror / file.filename());
  fs::copy_file(file, test_file, fs::copy_option::overwrite_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  if (g_virtual_filesystem_test) {
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
    ASSERT_EQ(file_size, g_drive->GetUsedSpace());
  }
}

TEST_F(CallbacksApiTest, BEH_CopyFileModifyThenRead) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create file on disk...
  fs::path file(CreateTestFile(g_test_mirror, file_size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive...
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space...
  if (g_virtual_filesystem_test) {
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
    ASSERT_EQ(file_size, g_drive->GetUsedSpace());
  }
  // Modify the file...
  ASSERT_TRUE(ModifyFile(g_mount_dir / file.filename(), file_size));
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Check used space again...
  if (g_virtual_filesystem_test) {
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
    ASSERT_EQ(file_size, g_drive->GetUsedSpace());
  }
  // Write virtual drive file back to a disk file...
  fs::path test_file(g_test_mirror / (RandomAlphaNumericString(5) + ".txt"));
  fs::copy_file(g_mount_dir / file.filename(),
                test_file,
                fs::copy_option::overwrite_if_exists,
                error_code);
  ASSERT_EQ(error_code.value(), 0);
  // Compare content in the two files...
  ASSERT_FALSE(CompareFileContents(test_file, file));
}

TEST_F(CallbacksApiTest, FUNC_CheckFailures) {
  boost::system::error_code error_code;
  int64_t file_size(0);
  // Create file on disk...
  fs::path file0(CreateTestFile(g_test_mirror, file_size));
  ASSERT_TRUE(fs::exists(file0, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy file to virtual drive...
  fs::copy_file(file0, g_mount_dir / file0.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file0.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy same file to virtual drive again...
  fs::copy_file(file0, g_mount_dir / file0.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_NE(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file0.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Create a file with the same name on the virtual drive...
  ASSERT_TRUE(CreateFileAt(g_mount_dir / file0.filename()));
  ASSERT_TRUE(fs::exists(file0, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Create another file on disk...
  fs::path file1(CreateTestFile(g_test_mirror, file_size));
  ASSERT_TRUE(fs::exists(file1, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy it to virtual drive...
  fs::copy_file(file1, g_mount_dir / file1.filename(), fs::copy_option::fail_if_exists, error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file1.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);

  // Rename to first file name...
  fs::rename(g_mount_dir / file1.filename(), g_mount_dir / file0.filename(), error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file0.filename(), error_code));
  ASSERT_FALSE(fs::exists(g_mount_dir / file1.filename(), error_code));
  ASSERT_EQ(crypto::HashFile<crypto::Tiger>(file1),
            crypto::HashFile<crypto::Tiger>(g_mount_dir / file0.filename()));
  // Rename mirror likewise...
  fs::rename(g_test_mirror / file1.filename(), g_test_mirror / file0.filename(), error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_test_mirror / file0.filename(), error_code));
  ASSERT_FALSE(fs::exists(g_test_mirror / file1.filename(), error_code));
  // Delete the first file...
  ASSERT_TRUE(fs::remove(g_mount_dir / file0.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / file0.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);
  // Delete the first file again...
  ASSERT_FALSE(fs::remove(g_mount_dir / file0.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / file0.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);

  // Repeat above for directories
  // Create directory on disk...
  fs::path directory0(CreateTestDirectory(g_test_mirror));
  ASSERT_TRUE(fs::exists(directory0, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy directory to virtual drive...
  fs::copy_directory(directory0, g_mount_dir / directory0.filename(), error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / directory0.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy same directory to virtual drive again...
  fs::copy_directory(directory0, g_mount_dir / directory0.filename(), error_code);
  ASSERT_NE(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / directory0.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Create a directory with the same name on the virtual drive...
  ASSERT_FALSE(fs::create_directory(g_mount_dir / directory0.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(directory0, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Create another directory on disk...
  fs::path directory1(CreateTestDirectory(g_test_mirror));
  ASSERT_TRUE(fs::exists(directory1, error_code));
  ASSERT_EQ(error_code.value(), 0);
  // Copy it to virtual drive...
  fs::copy_directory(directory1, g_mount_dir / directory1.filename(), error_code);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / directory1.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);

  // Rename to first directory name...
  fs::rename(g_mount_dir / directory1.filename(), g_mount_dir / directory0.filename(), error_code);

  // From boost filesystem docs: if new_p resolves to an existing directory,
  // it is removed if empty on POSIX but is an error on Windows.
#ifdef WIN32
  ASSERT_NE(error_code.value(), 0);
#else
  ASSERT_EQ(error_code.value(), 0);
#endif
  ASSERT_TRUE(fs::exists(g_mount_dir / directory0.filename(), error_code));
  // Delete the first directory...
  ASSERT_TRUE(fs::remove(g_mount_dir / directory0.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / directory0.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);

  // Delete the first directory again...
  ASSERT_FALSE(fs::remove(g_mount_dir / directory0.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_FALSE(fs::exists(g_mount_dir / directory0.filename(), error_code));
  ASSERT_NE(error_code.value(), 0);
  // TODO(Fraser#5#): 2011-05-30 - Add similar test for non-empty directory.
}

TEST_F(CallbacksApiTest, FUNC_FunctionalTest) {
  ASSERT_TRUE(DoRandomEvents());
  // Check used space...
  if (g_virtual_filesystem_test)
    ASSERT_EQ(g_drive->GetUsedSpace(), CalculateUsedSpace(g_mount_dir));
}

namespace {

fs::path GenerateFile(const fs::path &path,
                      std::uint32_t size = 0,
                      const std::string &content = "") {
  if ((size == 0 && content.empty()) || (size != 0 && !content.empty()))
    return fs::path();

  size_t filename_size(RandomUint32() % 4 + 4);
  fs::path file_name(RandomAlphaNumericString(filename_size) + ".txt");
#ifndef WIN32
  while (ExcludedFilename(file_name))
    file_name = RandomAlphaNumericString(filename_size);
#endif
  file_name = path / file_name;
  fs::ofstream ofs(file_name.c_str(), std::ios::out);
  if (!ofs.is_open())
    return fs::path();

  if (size != 0) {
    std::string random_string(RandomString(size % 1024));
    size_t rounds = size / 1024, count = 0;
    while (count++ < rounds)
      ofs << random_string;
  } else {
    ofs << content;
  }

  ofs.close();
  return file_name;
}

fs::path GenerateDirectory(const fs::path &path) {
  size_t directory_name_size(RandomUint32() % 8 + 1);
  fs::path file_name(RandomAlphaNumericString(directory_name_size));
#ifndef WIN32
  while (ExcludedFilename(file_name))
    file_name = RandomAlphaNumericString(directory_name_size);
#endif
  file_name = path / file_name;
  boost::system::error_code ec;
  fs::create_directory(file_name, ec);
  if (ec)
    return fs::path();
  return file_name;
}

void GenerateFileSizes(std::uint32_t max_size,
                       std::uint32_t min_size,
                       size_t count,
                       std::vector<std::uint32_t> *file_sizes) {
  while (file_sizes->size() < count)
    file_sizes->push_back(RandomUint32() % max_size + min_size);
}

std::uint32_t CreateTestTreeStructure(const fs::path &base_path,
                                      std::vector<fs::path> *directories,
                                      std::set<fs::path> *files,
                                      std::uint32_t directory_node_count,
                                      std::uint32_t file_node_count = 100,
                                      std::uint32_t max_filesize = 5 * 1024 * 1024,
                                      std::uint32_t min_size = 1024) {
  fs::path directory(GenerateDirectory(base_path));
  directories->reserve(directory_node_count);
  directories->push_back(directory);
  while (directories->size() < directory_node_count) {
    size_t random_element(RandomUint32() % directories->size());
    fs::path p = GenerateDirectory(directories->at(random_element));
    if (!p.empty())
      directories->push_back(p);
  }

  std::vector<std::uint32_t> file_sizes;
  GenerateFileSizes(max_filesize, min_size, 20, &file_sizes);
  std::uint32_t total_file_size(0);
  while (files->size() < file_node_count) {
    size_t random_element(RandomUint32() % directory_node_count);
    std::uint32_t file_size = file_sizes.at(files->size() % file_sizes.size());
    fs::path p = GenerateFile(directories->at(random_element), file_size);
    if (!p.empty()) {
      total_file_size += file_size;
      files->insert(p);
    }
  }
  return total_file_size;
}

void CopyRecursiveDirectory(const fs::path &src, const fs::path &dest) {
  boost::system::error_code ec;
  fs::copy_directory(src, dest / src.filename(), ec);
  for (fs::recursive_directory_iterator end, current(src); current != end; ++current) {
    std::string str = current->path().string();
    std::string str2 = src.string();
    boost::algorithm::replace_last(str2, src.filename().string(), "");
    boost::algorithm::replace_first(str, str2, dest.string() + "/");
    EXPECT_TRUE(fs::exists(current->path()));
    if (fs::is_directory(*current)) {
      fs::copy_directory(current->path(), str, ec);
    } else {
      fs::copy_file(current->path(), str, fs::copy_option::overwrite_if_exists, ec);
    }
    EXPECT_TRUE(fs::exists(str));
  }
}

}  // namespace

TEST_F(CallbacksApiTest, FUNC_BENCHMARK_CopyThenReadLargeFile) {
  boost::system::error_code error_code;

  // Create file on disk...
  size_t size = 300 * 1024 * 1024;
  fs::path file(CreateTestFileWithSize(g_test_mirror, size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);

  // Copy file to virtual drive...
  bptime::ptime copy_start_time(bptime::microsec_clock::universal_time());
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  bptime::ptime copy_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(copy_start_time, copy_stop_time, size, kCopy);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);

  // Read the file back to a disk file...
  // Because of the system caching, the pure read can't reflect the real speed
  fs::path test_file(g_test_mirror / (RandomAlphaNumericString(5) + ".txt"));
  bptime::ptime read_start_time(bptime::microsec_clock::universal_time());
  fs::copy_file(g_mount_dir / file.filename(), test_file, fs::copy_option::overwrite_if_exists);
  bptime::ptime read_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(read_start_time, read_stop_time, size, kRead);
  ASSERT_TRUE(fs::exists(test_file, error_code));
  ASSERT_EQ(error_code.value(), 0);

  // Compare content in the two files...
  ASSERT_EQ(fs::file_size(g_mount_dir / file.filename()), fs::file_size(file));
  bptime::ptime compare_start_time(bptime::microsec_clock::universal_time());
  ASSERT_TRUE(CompareFileContents(g_mount_dir / file.filename(), file));
  bptime::ptime compare_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(compare_start_time, compare_stop_time, size, kCompare);
}

TEST_F(CallbacksApiTest, FUNC_BENCHMARK_CopyThenReadManySmallFiles) {
  std::vector<fs::path> directories;
  std::set<fs::path> files;
  // The changed values that follow don't affect effectiveness or
  // benchmarkability, but do reduce running time significantly...
  std::uint32_t num_of_directories(100);  // 1000);
  std::uint32_t num_of_files(300);  // 3000);
  std::uint32_t max_filesize(256 * 1024);
  std::uint32_t min_filesize(1);
  std::cout << "Creating a test tree with " << num_of_directories
            << " directories holding " << num_of_files
            << " files with file size range from "
            << BytesToBinarySiUnits(min_filesize)
            << " to " << BytesToBinarySiUnits(max_filesize) << std::endl;
  std::uint32_t total_data_size = CreateTestTreeStructure(g_test_mirror, &directories, &files,
                                                          num_of_directories, num_of_files,
                                                          max_filesize, min_filesize);

  // Copy test_tree to virtual drive...
  bptime::ptime copy_start_time(bptime::microsec_clock::universal_time());
  CopyRecursiveDirectory(directories.at(0), g_mount_dir);
  bptime::ptime copy_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(copy_start_time, copy_stop_time, total_data_size, kCopy);

  // Read the test_tree back to a disk file...
  std::string str = directories.at(0).string();
  boost::algorithm::replace_first(str, g_test_mirror.string(), g_mount_dir.string());
  fs::path from_directory(str);
  fs::path read_back_directory(GenerateDirectory(g_test_mirror));
  bptime::ptime read_start_time(bptime::microsec_clock::universal_time());
  CopyRecursiveDirectory(from_directory, read_back_directory);
  bptime::ptime read_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(read_start_time, read_stop_time, total_data_size, kRead);

  // Compare content in the two test_trees...
  bptime::ptime compare_start_time(bptime::microsec_clock::universal_time());
  for (auto it = files.begin(); it != files.end(); ++it) {
    std::string str = (*it).string();
    boost::algorithm::replace_first(str, g_test_mirror.string(), g_mount_dir.string());
    if (!fs::exists(str))
      Sleep(bptime::seconds(1));
    ASSERT_TRUE(fs::exists(str))  << "Missing " << str;
    ASSERT_TRUE(CompareFileContents(*it, str)) << "Comparing " << *it << " with " << str;
  }
  bptime::ptime compare_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(compare_start_time, compare_stop_time, total_data_size, kCompare);

  for (size_t i = 0; i < directories.size(); ++i) {
    std::string str = directories[i].string();
    boost::algorithm::replace_first(str, g_test_mirror.string(), g_mount_dir.string());
    ASSERT_TRUE(fs::exists(str)) << "Missing " << str;
  }
}

TEST_F(CallbacksApiTest, BEH_GetAndInsertDataMap) {
  if (!g_virtual_filesystem_test)
    return SUCCEED() << "Can't test on real filesystem.";
  int64_t file_size(0);
  // Create file on virtual drive
  fs::path dir(CreateTestDirectory(g_mount_dir)), dir_relative_path(RelativePath(g_mount_dir, dir));
  fs::path file(CreateTestFile(dir, file_size)),
           file_relative_path(fs::path("/Owner") / RelativePath(g_mount_dir, file));
  boost::system::error_code error_code;
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);

  // Try getting with invalid parameters
  EXPECT_THROW(g_drive->GetDataMap(file_relative_path, nullptr), std::exception);
  std::string data_map;
  EXPECT_THROW(g_drive->GetDataMap("", &data_map), std::exception);
  EXPECT_THROW(g_drive->GetDataMap(dir_relative_path, &data_map), std::exception);
  EXPECT_THROW(g_drive->GetDataMap(dir_relative_path / "Rubbish", &data_map), std::exception);

  // Get the serialised DataMap
  EXPECT_NO_THROW(g_drive->GetDataMap(file_relative_path, &data_map));

  // Try inserting with invalid parameters
  fs::path new_file(dir / (RandomAlphaNumericString(5) + ".txt")),
           new_file_rel_path(fs::path("/Owner") / RelativePath(g_mount_dir, new_file));
  EXPECT_THROW(g_drive->InsertDataMap(new_file_rel_path, "Rubbish"), std::exception);
  EXPECT_THROW(g_drive->InsertDataMap("", data_map), std::exception);

  // Insert the file and compare to the previous one
  EXPECT_NO_THROW(g_drive->InsertDataMap(new_file_rel_path, data_map));
  ASSERT_TRUE(CompareFileContents(file, new_file));
}

TEST_F(CallbacksApiTest, BEH_GetAndInsertHiddenDataMap) {
  if (!g_virtual_filesystem_test)
    return SUCCEED() << "Can't test on real filesystem.";

  std::string content(RandomAlphaNumericString(128));
  for (int i = 0; i < 10; ++i)
    content += content + RandomAlphaNumericString(10);

  fs::path dir(CreateTestDirectory(g_mount_dir)), dir_relative_path(RelativePath(g_mount_dir, dir));
  // Create file on virtual drive, one hidden and one normal, with same content
  std::string hidden_file_name(RandomAlphaNumericString(5) + ".ms_hidden");
  fs::path hidden_file(dir_relative_path / hidden_file_name);
  EXPECT_NO_THROW(g_drive->WriteHiddenFile(hidden_file, content, true));
  fs::path file(CreateTestFileWithContent(dir, content)),
           file_relative_path(RelativePath(g_mount_dir, file));
  boost::system::error_code error_code;
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);
  Sleep(boost::posix_time::seconds(5));

  std::string hidden_data_map;
  std::string normal_data_map;
  EXPECT_THROW(g_drive->GetDataMap(hidden_file, &hidden_data_map), std::exception);
  EXPECT_NO_THROW(g_drive->GetDataMapHidden(hidden_file, &hidden_data_map));
  EXPECT_FALSE(hidden_data_map.empty());
  EXPECT_NO_THROW(g_drive->GetDataMapHidden(file_relative_path, &normal_data_map));
  EXPECT_EQ(hidden_data_map, normal_data_map);
}

// TEST_F(CallbacksApiTest, BEH_MoveDirectory) {
//  if (!g_virtual_filesystem_test)
//    return SUCCEED() << "Can't test on real filesystem.";
//
//  boost::system::error_code error_code;
//  // create a directory on the drive...
//  fs::path directory1(CreateDirectoryContainingFiles(g_mount_dir));
//  // create a directory in newly created directory...
//  fs::path directory2(CreateDirectoryContainingFiles(directory1));
//  EXPECT_TRUE(fs::exists(directory2, error_code));
//  // create another directory on the drive...
//  fs::path directory3(CreateDirectoryContainingFiles(g_mount_dir));
//  EXPECT_FALSE(fs::exists(directory3 / directory2.filename(), error_code));
//  // move directory2 to directory3...
//  EXPECT_EQ(kSuccess, g_drive->MoveDirectory(directory2, directory3 / directory2.filename()));
//  int count(0);
//  while (fs::exists(directory2, error_code) && count++ < 20)
//    Sleep(bptime::millisec(100));
//  EXPECT_FALSE(fs::exists(directory2, error_code));
//  EXPECT_TRUE(fs::exists(directory3 / directory2.filename(), error_code));
// }

}  // namespace test

}  // namespace drive

}  // namespace maidsafe

int main(int argc, char **argv) {
  maidsafe::log::Logging::Instance().Initialise(argc, argv);

  testing::FLAGS_gtest_catch_exceptions = false;
  testing::InitGoogleTest(&argc, argv);
#ifdef DISK_TEST
  testing::AddGlobalTestEnvironment(
      new maidsafe::drive::test::ApiTestEnvironment("MaidSafe_Test_Disk"));
#else
  testing::AddGlobalTestEnvironment(
      new maidsafe::drive::test::ApiTestEnvironment("MaidSafe_Test_Drive"));
#endif
  int result(RUN_ALL_TESTS());
  int test_count = testing::UnitTest::GetInstance()->test_to_run_count();
  return (test_count == 0) ? -1 : result;
}
