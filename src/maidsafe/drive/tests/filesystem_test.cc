/*  Copyright 2013 MaidSafe.net limited

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

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/program_options.hpp"
#include "boost/process/child.hpp"
#include "boost/process/execute.hpp"
#include "boost/process/initializers.hpp"
#include "boost/process/wait_for_exit.hpp"
#include "boost/process/terminate.hpp"
#include "boost/process/search_path.hpp"
#include "boost/system/error_code.hpp"


#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "maidsafe/common/log.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/ipc.h"
#include "maidsafe/common/application_support_directories.h"

#include "maidsafe/drive/drive.h"

#include "local_drive_location.h"  // NOLINT
#include "network_drive_location.h"  // NOLINT

namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace bp = boost::process;


namespace maidsafe {

namespace test {

// namespace {

fs::path root_, temp_, chunk_store_;
std::string root_parent_, user_id_, shm_name_;
std::function<void()> child_;
#ifdef MAIDSAFE_WIN32
HANDLE child_handle_;
#else
pid_t child_pid_(0);
#endif

int RunCatch(int argc, char** argv) {
  Catch::Session session;
  auto command_line_result(
      session.applyCommandLine(argc, argv, Catch::Session::OnUnusedOptions::Ignore));
  if (command_line_result != 0)
    LOG(kWarning) << "Catch command line parsing error: " << command_line_result;
  return session.run();
}

std::function<void()> clean_root([] {
  boost::system::error_code error_code;
  fs::directory_iterator end;
  for (fs::directory_iterator directory_itr(root_); directory_itr != end; ++directory_itr)
    fs::remove_all(*directory_itr, error_code);
});

void RequireExists(const fs::path& path) {
  boost::system::error_code error_code;
  REQUIRE(fs::exists(path, error_code));
  REQUIRE(error_code.value() == 0);
}

void RequireDoesNotExist(const fs::path& path) {
  boost::system::error_code error_code;
  REQUIRE(!fs::exists(path, error_code));
  REQUIRE(error_code.value() != 0);
}

std::pair<fs::path, std::string> CreateFile(const fs::path& parent, size_t content_size) {
  auto file(parent / (RandomAlphaNumericString(5) + ".txt"));
  std::string content(RandomString(content_size + 1));
  REQUIRE(WriteFile(file, content));
  RequireExists(file);
  return std::make_pair(file, content);
}

fs::path CreateDirectory(const fs::path& parent) {
  auto directory(parent / RandomAlphaNumericString(5));
  fs::create_directories(directory);
  RequireExists(directory);
  return directory;
}

bool CopyDirectory(const fs::path& from, const fs::path& to) {
  LOG(kVerbose) << "CopyDirectory: from " << from << " to " << (to / from.filename());
  try {
    if (!fs::exists(to / from.filename()))
      fs::copy_directory(from, to / from.filename());

    fs::directory_iterator end;
    REQUIRE(fs::exists(to / from.filename()));
    for (fs::directory_iterator directory_itr(from); directory_itr != end; ++directory_itr) {
      if (fs::is_directory(*directory_itr)) {
        REQUIRE(CopyDirectory((*directory_itr).path(), to / from.filename()));
      } else if (fs::is_regular_file(*directory_itr)) {
        fs::copy_file((*directory_itr).path(),
                      to / from.filename() / (*directory_itr).path().filename(),
                      fs::copy_option::fail_if_exists);
        REQUIRE(fs::exists(to / from.filename() / (*directory_itr).path().filename()));
      } else {
        if (fs::exists(*directory_itr))
          LOG(kInfo) << "CopyDirectory: unknown type found.";
        else
          LOG(kInfo) << "CopyDirectory: nonexistant type found.";
        return false;
      }
    }
  }
  catch (const boost::system::system_error& error) {
    LOG(kError) << "CopyDirectory failed: " << error.what();
    return false;
  }
  return true;
}

void RequireDirectoriesEqual(const fs::path& lhs, const fs::path& rhs, bool check_file_contents) {
  std::set<std::string> lhs_files, rhs_files;
  try {
    fs::recursive_directory_iterator end;
    for (fs::recursive_directory_iterator lhs_itr(lhs); lhs_itr != end; ++lhs_itr)
      lhs_files.insert((*lhs_itr).path().string().substr(lhs.string().size()));
    for (fs::recursive_directory_iterator rhs_itr(rhs); rhs_itr != end; ++rhs_itr)
      rhs_files.insert((*rhs_itr).path().string().substr(rhs.string().size()));
  }
  catch (const boost::system::system_error& error) {
    LOG(kError) << "RequireDirectoriesEqual failed: " << error.what();
    REQUIRE(false);
  }

  std::vector<std::string> difference;
  std::set_symmetric_difference(std::begin(lhs_files), std::end(lhs_files), std::begin(rhs_files),
                                std::end(rhs_files), std::back_inserter(difference));
  REQUIRE(difference.empty());

  if (check_file_contents) {
    auto rhs_itr(std::begin(rhs_files));
    for (const auto& lhs_file : lhs_files) {
      CAPTURE(lhs_file);
      CAPTURE(*rhs_itr);
      if (!fs::is_regular_file(lhs / lhs_file)) {
        REQUIRE(!fs::is_regular_file(rhs / (*rhs_itr++)));
        continue;
      }
        REQUIRE(fs::is_regular_file(rhs / *rhs_itr));
        REQUIRE((ReadFile(lhs / lhs_file)) == (ReadFile(rhs / (*rhs_itr++))));
    }
  }
}

fs::path CreateDirectoryContainingFiles(const fs::path& parent) {
  auto directory(CreateDirectory(parent));
  auto file_count((RandomUint32() % 4) + 2);
  for (uint32_t i(0); i != file_count; ++i)
    CreateFile(directory, (RandomUint32() % 1024) + 1);
  return directory;
}

bool SetUpTempDirectory() {
    maidsafe::test::temp_ =
        fs::unique_path(fs::temp_directory_path() / "MaidSafe_Test_Filesystem_%%%%-%%%%-%%%%");
    if (!fs::create_directories(maidsafe::test::temp_)) {
      LOG(kWarning) << "Failed to create test directory " << maidsafe::test::temp_;
      return false;
    }
    LOG(kInfo) << "Created test directory " << maidsafe::test::temp_;
    return true;
}

void RemoveTempDirectory() {
  boost::system::error_code error_code;
  if (fs::remove_all(maidsafe::test::temp_, error_code) == 0) {
    LOG(kWarning) << "Failed to remove " << maidsafe::test::temp_;
  }
  if (error_code.value() != 0) {
    LOG(kWarning) << "Error removing " << maidsafe::test::temp_ << "  " << error_code.message();
  }else {
    LOG(kInfo) << "Removed " << maidsafe::test::temp_ << "  " << error_code.message();
  }
}

bool SetUpChunkStore() {
    maidsafe::test::chunk_store_ =
        fs::unique_path(fs::temp_directory_path() / "MaidSafe_Test_ChunkStore%%%%-%%%%-%%%%");
    if (!fs::create_directories(maidsafe::test::chunk_store_)) {
      LOG(kWarning) << "Failed to create chunk_store_ directory " << maidsafe::test::chunk_store_;
      return false;
    }
    LOG(kInfo) << "Created chunk_store directory " << maidsafe::test::chunk_store_;
    return true;
}

void RemoveChunkStore() {
  boost::system::error_code error_code;
  if (fs::remove_all(maidsafe::test::chunk_store_, error_code) == 0) {
    LOG(kWarning) << "Failed to remove chunk_store" << maidsafe::test::chunk_store_;
  }
  if (error_code.value() != 0) {
    LOG(kWarning) << "Error removing " << maidsafe::test::chunk_store_ << "  " 
                                       << error_code.message();
  }else {
    LOG(kInfo) << "Removed " << maidsafe::test::chunk_store_ << "  " << error_code.message();
  }
}

bool SetUpRootDirectory(fs::path base_dir) {
#ifdef MAIDSAFE_WIN32
  static_cast<void>(base_dir);
  maidsafe::test::root_ = drive::GetNextAvailableDrivePath();
#else
  maidsafe::test::root_ =
     fs::unique_path(base_dir / "MaidSafe_Root_Filesystem_%%%%-%%%%-%%%%");
  if (!fs::create_directories(maidsafe::test::root_)) {
    LOG(kWarning) << "Failed to create root directory " << maidsafe::test::root_;
    return false;
  }
#endif
  LOG(kInfo) << "Created test directory " << maidsafe::test::root_;
  return true;
}

void RemoveRootDirectory() {
  boost::system::error_code error_code;
  if (fs::remove_all(maidsafe::test::root_, error_code) == 0) {
    LOG(kWarning) << "Failed to remove root directory " << maidsafe::test::root_;
  }
  if (error_code.value() != 0) {
    LOG(kWarning) << "Error removing " << maidsafe::test::root_ << "  " << error_code.message();
  } else {
    LOG(kInfo) << "Removed " << maidsafe::test::root_ << "  " << error_code.message();
  }
}

#ifdef MAIDSAFE_WIN32
std::wstring StringToWstring(const std::string& input) {
  std::unique_ptr<wchar_t[]> buffer(new wchar_t[input.size()]);
  size_t num_chars = mbstowcs(buffer.get(), input.c_str(), input.size());
  return std::wstring(buffer.get(), num_chars);
}

std::wstring ConstructCommandLine(std::vector<std::string> process_args) {
  std::string args;
  for (auto arg : process_args)
    args += (arg + " ");
  return StringToWstring(args);
}
#else
std::string ConstructCommandLine(std::vector<std::string> process_args) {
  std::string args;
  for (auto arg : process_args)
    args += (arg + " ");
  return args;
}
#endif


// }  // unnamed namespace

TEST_CASE("Create empty file", "[Filesystem]") {
  on_scope_exit cleanup(clean_root);
  CreateFile(root_, 0);
}

TEST_CASE("Create empty directory", "[Filesystem]") {
  on_scope_exit cleanup(clean_root);
  CreateDirectory(root_);
}

TEST_CASE("Append to file", "[Filesystem]") {
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(root_, 0).first);
  int test_runs = 1000;
  WriteFile(filepath, "a");
  for (int i = 0; i < test_runs; ++i) {
    auto content(ReadFile(filepath));
    WriteFile(filepath, content.string() + "a");
    auto updated_content(ReadFile(filepath));
    REQUIRE(updated_content.string().size() == content.string().size() + 1);
    REQUIRE(updated_content.string().size() == i + 2U);
  }
}

TEST_CASE("Copy empty directory", "[Filesystem]") {
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(temp_));

  // Copy 'temp_' directory to 'root_'
  boost::system::error_code error_code;
  fs::copy_directory(directory, root_ / directory.filename(), error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(root_ / directory.filename());
}

TEST_CASE("Copy directory then delete", "[Filesystem]") {
  // Create a file and directory in a newly created directory in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(temp_));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'root_'
  REQUIRE(CopyDirectory(directory, root_));
  auto copied_directory(root_ / directory.filename());
  RequireExists(copied_directory);
  RequireDirectoriesEqual(directory, copied_directory, true);

  // Delete the directory along with its contents
  boost::system::error_code error_code;
  REQUIRE(fs::remove_all(copied_directory, error_code) == 3U);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireDoesNotExist(copied_directory / filepath.filename());
  RequireDoesNotExist(copied_directory / nested_directory.filename());

  // Try to clean up 'root_'
  fs::remove_all(copied_directory, error_code);
}

TEST_CASE("Copy directory, delete then re-copy", "[Filesystem]") {
  // Create a file and directory in a newly created directory in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(temp_));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'root_'
  REQUIRE(CopyDirectory(directory, root_));
  auto copied_directory(root_ / directory.filename());

  // Delete the directory along with its contents
  boost::system::error_code error_code;
  REQUIRE(fs::remove_all(copied_directory, error_code) == 3U);
  REQUIRE(error_code.value() == 0);

  // Re-copy directory and file to 'root_'
  REQUIRE(CopyDirectory(directory, root_));
  RequireExists(copied_directory);
  RequireDirectoriesEqual(directory, copied_directory, true);
}

TEST_CASE("Copy directory then rename", "[Filesystem]") {
  // Create a file and directory in a newly created directory in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(temp_));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'root_'
  REQUIRE(CopyDirectory(directory, root_));
  auto copied_directory(root_ / directory.filename());

  // Rename the directory
  auto renamed_directory(root_ / maidsafe::RandomAlphaNumericString(5));
  boost::system::error_code error_code;
  fs::rename(copied_directory, renamed_directory, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireExists(renamed_directory);
  RequireDirectoriesEqual(directory, renamed_directory, true);
}

TEST_CASE("Copy directory, rename then re-copy", "[Filesystem]") {
  // Create a file and directory in a newly created directory in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(temp_));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'root_'
  REQUIRE(CopyDirectory(directory, root_));
  auto copied_directory(root_ / directory.filename());

  // Rename the directory
  auto renamed_directory(root_ / maidsafe::RandomAlphaNumericString(5));
  boost::system::error_code error_code;
  fs::rename(copied_directory, renamed_directory, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);

  // Re-copy directory and file to 'root_'
  REQUIRE(CopyDirectory(directory, root_));
  RequireExists(copied_directory);
  RequireDirectoriesEqual(directory, copied_directory, false);
}

TEST_CASE("Copy directory containing multiple files", "[Filesystem]") {
  // Create files in a newly created directory in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectoryContainingFiles(temp_));

  // Copy directory to 'root_'
  REQUIRE(CopyDirectory(directory, root_));
  auto copied_directory(root_ / directory.filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  REQUIRE(!fs::is_empty(copied_directory, error_code));
  REQUIRE(error_code.value() == 0);
  RequireDirectoriesEqual(directory, copied_directory, true);
}

TEST_CASE("Copy directory hierarchy", "[Filesystem]") {
  // Create a new directory in 'temp_'
  on_scope_exit cleanup(clean_root);
  std::vector<fs::path> directories;
  auto directory(CreateDirectory(temp_));
  directories.push_back(directory);

  // Add further directories 3 levels deep
  for (int i(0); i != 3; ++i) {
    std::vector<fs::path> nested;
    for (const auto& dir : directories) {
      auto dir_count((RandomUint32() % 3) + 1);
      for (uint32_t j(0); j != dir_count; ++j)
        nested.push_back(CreateDirectory(dir));
    }
    directories.insert(std::end(directories), std::begin(nested), std::end(nested));
    nested.clear();
  }

  // Add files to all directories
  for (const auto& dir : directories) {
    auto file_count((RandomUint32() % 4) + 2);
    for (uint32_t k(0); k != file_count; ++k)
      CreateFile(dir, (RandomUint32() % 1024) + 1);
  }

  // Copy hierarchy to 'root_'
  REQUIRE(CopyDirectory(directory, root_));
  auto copied_directory(root_ / directory.filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  REQUIRE(!fs::is_empty(copied_directory, error_code));
  REQUIRE(error_code.value() == 0);
  RequireDirectoriesEqual(directory, copied_directory, true);
}

TEST_CASE("Copy then copy copied file", "[Filesystem]") {
  // Create a file in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(temp_, RandomUint32() % 1048577).first);

  // Copy file to 'root_'
  auto copied_file(root_ / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file);
  REQUIRE(ReadFile(filepath) == ReadFile(copied_file));

  // Copy file to 'root_' again
  fs::copy_file(filepath, copied_file, fs::copy_option::overwrite_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file);
  REQUIRE(ReadFile(filepath) == ReadFile(copied_file));
}

TEST_CASE("Copy file, delete then re-copy", "[Filesystem]") {
  // Create a file in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(temp_, RandomUint32() % 1048577).first);

  // Copy file to 'root_'
  auto copied_file(root_ / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);

  // Delete the file
  fs::remove(copied_file, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_file);

  // Copy file to 'root_' again
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file);
  REQUIRE(ReadFile(filepath) == ReadFile(copied_file));
}

TEST_CASE("Copy file, rename then re-copy", "[Filesystem]") {
  // Create a file in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(temp_, RandomUint32() % 1048577).first);

  // Copy file to 'root_'
  auto copied_file(root_ / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);

  // Rename the file
  auto renamed_file(root_ / (RandomAlphaNumericString(5) + ".txt"));
  fs::rename(copied_file, renamed_file, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_file);
  RequireExists(renamed_file);
  REQUIRE(ReadFile(filepath) == ReadFile(renamed_file));

  // Copy file to 'root_' again
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file);
  REQUIRE(ReadFile(filepath) == ReadFile(copied_file));
}

TEST_CASE("Copy file, delete then try to read", "[Filesystem]") {
  // Create a file in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(temp_, RandomUint32() % 1048577).first);

  // Copy file to 'root_'
  auto copied_file(root_ / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);

  // Delete the file
  fs::remove(copied_file, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_file);

  // Try to copy 'root_' file back to a 'temp_' file
  auto test_file(temp_ / (RandomAlphaNumericString(5) + ".txt"));
  fs::copy_file(copied_file, test_file, fs::copy_option::overwrite_if_exists, error_code);
  REQUIRE(error_code.value() != 0);
  RequireDoesNotExist(test_file);
}

TEST_CASE("Create file", "[Filesystem]") {
  // Create a file in 'root_' and read back its contents
  on_scope_exit cleanup(clean_root);
  auto filepath_and_contents(CreateFile(root_, RandomUint32() % 1048577));
  REQUIRE(ReadFile(filepath_and_contents.first).string() == filepath_and_contents.second);
}

TEST_CASE("Create file, modify then read", "[Filesystem]") {
  // Create a file in 'root_'
  on_scope_exit cleanup(clean_root);
  auto filepath_and_contents(CreateFile(root_, RandomUint32() % 1048577));

  // Modify the file
  size_t offset(RandomUint32() % filepath_and_contents.second.size());
  std::string additional_content(RandomString(RandomUint32() % 1048577));
  filepath_and_contents.second.insert(offset, additional_content);
  std::ofstream output_stream(filepath_and_contents.first.c_str(), std::ios_base::binary);
  REQUIRE(output_stream.is_open());
  REQUIRE(!output_stream.bad());
  output_stream.write(filepath_and_contents.second.c_str(), filepath_and_contents.second.size());
  REQUIRE(!output_stream.bad());
  output_stream.close();

  // Check file
  RequireExists(filepath_and_contents.first);
  REQUIRE(ReadFile(filepath_and_contents.first).string() == filepath_and_contents.second);
}

TEST_CASE("Rename file to different parent directory", "[Filesystem]") {
  // Create a file in a newly created directory in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(temp_));
  auto filepath_and_contents(CreateFile(directory, RandomUint32() % 1024));

  // Copy directory to 'root_'
  REQUIRE(CopyDirectory(directory, root_));
  auto copied_directory(root_ / directory.filename());

  // Rename the file into its parent
  auto renamed_from_file(copied_directory / filepath_and_contents.first.filename());
  auto renamed_to_file(root_ / filepath_and_contents.first.filename());
  boost::system::error_code error_code;
  fs::rename(renamed_from_file, renamed_to_file, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(renamed_from_file);
  RequireExists(renamed_to_file);
  REQUIRE(ReadFile(renamed_to_file).string() == filepath_and_contents.second);
}

TEST_CASE("Check failures", "[Filesystem]") {
  // Create a file in 'temp_'
  on_scope_exit cleanup(clean_root);
  auto filepath0(CreateFile(temp_, RandomUint32() % 1048577).first);

  // Copy file to 'root_'
  auto copied_file0(root_ / filepath0.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath0, copied_file0, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file0);

  // Copy same file to 'root_' again
  fs::copy_file(filepath0, copied_file0, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() != 0);
  RequireExists(copied_file0);
  REQUIRE(ReadFile(filepath0) == ReadFile(copied_file0));

  // Create another file in 'temp_' and copy it to 'root_'
  auto filepath1(CreateFile(temp_, RandomUint32() % 1048577).first);
  auto copied_file1(root_ / filepath1.filename());
  fs::copy_file(filepath1, copied_file1, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file1);

  // Rename to first file name
  fs::rename(copied_file1, copied_file0, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file0);
  RequireDoesNotExist(copied_file1);
  REQUIRE(ReadFile(filepath1) == ReadFile(copied_file0));

  // Rename mirror likewise
  fs::rename(filepath1, filepath0, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(filepath0);
  RequireDoesNotExist(filepath1);

  // Delete the file
  REQUIRE(fs::remove(copied_file0, error_code));
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_file0);

  // Delete the file again
  REQUIRE(!fs::remove(copied_file0, error_code));
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_file0);

  // Repeat above for directories
  // Create a file and directory in a newly created directory in 'temp_'
  auto directory0(CreateDirectory(temp_));
  CreateFile(directory0, RandomUint32() % 1024);
  CreateDirectory(directory0);

  // Copy directory to 'root_'
  REQUIRE(CopyDirectory(directory0, root_));
  auto copied_directory0(root_ / directory0.filename());

  // Copy same directory to 'root_' again
  fs::copy_directory(directory0, copied_directory0, error_code);
  REQUIRE(error_code.value() != 0);
  RequireExists(copied_directory0);
  RequireDirectoriesEqual(directory0, copied_directory0, true);

  // Create a directory with the same name on the 'root_'
  REQUIRE(!fs::create_directory(copied_directory0, error_code));
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_directory0);
  RequireDirectoriesEqual(directory0, copied_directory0, false);

  // Create another directory in 'temp_' containing a file and subdirectory
  auto directory1(CreateDirectory(temp_));
  CreateFile(directory1, RandomUint32() % 1024);
  CreateDirectory(directory1);

  // Copy it to 'root_'
  REQUIRE(CopyDirectory(directory1, root_));
  auto copied_directory1(root_ / directory1.filename());

  // Rename to first directory name
  fs::rename(copied_directory1, copied_directory0, error_code);
  REQUIRE(error_code.value() != 0);
  RequireExists(copied_directory0);
  RequireExists(copied_directory1);
  RequireDirectoriesEqual(directory0, copied_directory0, false);
  RequireDirectoriesEqual(directory1, copied_directory1, false);

  // Create an empty directory in 'root_'
  auto directory2(CreateDirectory(temp_));

  // Rename copied directory to empty directory
  fs::rename(copied_directory1, directory2, error_code);

// From http://www.boost.org/doc/libs/release/libs/filesystem/doc/reference.html#rename:
// if new_p resolves to an existing directory, it is removed if empty on POSIX but is an error on
// Windows.
#ifdef MAIDSAFE_WIN32
  REQUIRE(error_code.value() != 0);
  RequireExists(directory2);
  RequireExists(copied_directory1);
  RequireDirectoriesEqual(directory1, copied_directory1, false);
#else
  REQUIRE(error_code.value() == 0);
  RequireExists(directory2);
  RequireDoesNotExist(copied_directory1);
  RequireDirectoriesEqual(directory1, directory2, false);
#endif

  // Delete the first directory
  REQUIRE(fs::remove_all(copied_directory0, error_code) == 3U);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory0);

  // Delete the first directory again
  REQUIRE(fs::remove_all(copied_directory0, error_code) == 0U);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory0);
  REQUIRE(!fs::remove(copied_directory0, error_code));
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory0);
}

}  // namespace test

}  // namespace maidsafe

int main(int argc, char** argv) {
  auto unused_options(maidsafe::log::Logging::Instance().Initialise(argc, argv));
  auto tests_result(0);
  // Handle passing path to test root via command line
  
  po::options_description filesystem_options("Filesystem Test Options /n Only a single option will be performed per test run");
  
  filesystem_options.add_options()("help,h", "Show help message.")
                                  ("disk,d", "Perform all tests on native hard disk")
                                  ("local,l", "Perform all tests on local vfs ")
                                  ("network,n", "Perform all tests on network vfs ");
  po::parsed_options parsed(
      po::command_line_parser(unused_options).options(filesystem_options).allow_unregistered().run());

  po::variables_map variables_map;
  po::store(parsed, variables_map);
  po::notify(variables_map);

  // Strip used command line options before passing to Catch
  unused_options = po::collect_unrecognized(parsed.options, po::include_positional);
  argc = static_cast<int>(unused_options.size() + 1);
  int position(1);
  for (const auto& unused_option : unused_options)
    std::strcpy(argv[position++], unused_option.c_str());  // NOLINT

  if (variables_map.count("help")) {
    std::cout << filesystem_options << '\n';
    ++argc;
    std::strcpy(argv[position], "--help");  // NOLINT
    return 0;
  } else if (variables_map.count("disk")) {
    maidsafe::test::SetUpRootDirectory(fs::unique_path(fs::temp_directory_path()));
    maidsafe::test::SetUpTempDirectory();
    tests_result = maidsafe::test::RunCatch(argc, argv);
  } else if (variables_map.count("local")) {
    maidsafe::test::shm_name_ = maidsafe::RandomAlphaNumericString(32);
    // maidsafe::test::root_parent_ = maidsafe::RandomAlphaNumericString(64);
    std::vector<std::string> shm_args;
    maidsafe::test::SetUpRootDirectory(fs::unique_path(fs::path(maidsafe::GetHomeDir())));
    maidsafe::test::SetUpTempDirectory();
    maidsafe::test::SetUpChunkStore();
    shm_args.push_back(maidsafe::test::root_.string());
    shm_args.push_back(maidsafe::test::chunk_store_.string());
    shm_args.push_back(maidsafe::test::root_parent_);
    maidsafe::ipc::CreateSharedMemory(maidsafe::test::shm_name_, shm_args);
     // Set up boost::process args 
    std::vector<std::string> process_args;
    const auto kExePath(maidsafe::process::GetLocalDriveLocation());
    process_args.push_back(kExePath);
    std::string shm_opt("-S" +  maidsafe::test::shm_name_);
    process_args.push_back(shm_opt);
    const auto kCommandLine(maidsafe::test::ConstructCommandLine(process_args));
    boost::system::error_code error_code;

    bp::child child = bp::child(bp::execute(bp::initializers::run_exe(kExePath),
                                            bp::initializers::set_cmd_line(kCommandLine),
                                            bp::initializers::set_on_error(error_code)));

    // REQUIRE_FALSE(error_code);
    maidsafe::Sleep(std::chrono::seconds(3));

#ifdef WIN32
    maidsafe::test::child_handle_ = child.process_handle();
#else
    maidsafe::test::child_pid_ = child.pid;
#endif
  tests_result = maidsafe::test::RunCatch(argc, argv);
  } else if (variables_map.count("network")) {
  }
  
  maidsafe::test::RemoveRootDirectory();
  maidsafe::test::RemoveTempDirectory();
  if (fs::exists(maidsafe::test::chunk_store_))
    maidsafe::test::RemoveChunkStore();
#ifdef MAIDSAFE_WIN32
  if (maidsafe::test::child_handle_ != 0) {
    DWORD pid(GetProcessId(maidsafe::test::child_handle_));
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid);
  }
#else
std::cout << "pid set at " << maidsafe::test::child_pid_  << "/n";
  if (maidsafe::test::child_pid_ != 0) {
#include "signal.h"
//    kill(maidsafe::test::child_pid_, SIGKILL);
  }
#endif
  
  return tests_result;
}
