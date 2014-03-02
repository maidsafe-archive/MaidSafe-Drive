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
#include "boost/system/error_code.hpp"
#ifdef MAIDSAFE_WIN32
#include "boost/locale/generator.hpp"
#else
#include <locale>  // NOLINT
#endif
#include "boost/process.hpp"
#include "boost/process/initializers.hpp"

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "maidsafe/common/log.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/process.h"
#ifdef MAIDSAFE_WIN32
#include "maidsafe/drive/tools/commands/windows_file_commands.h"
#else
#include <sys/stat.h>
#include "maidsafe/drive/tools/commands/linux_file_commands.h"
#endif

namespace fs = boost::filesystem;
namespace dtc = maidsafe::drive::tools::commands;

namespace maidsafe {

namespace test {

namespace {

fs::path g_root, g_temp, g_storage;

std::function<void()> clean_root([] {
  // On Windows, this frequently fails on the first attempt due to lingering open handles in the
  // VFS, so we make several attempts to clean up the root dir before failing.
  int attempts(0);
  std::string error_message;
  while (attempts < 50) {
    try {
      ++attempts;
      fs::directory_iterator end;
      for (fs::directory_iterator directory_itr(g_root); directory_itr != end; ++directory_itr)
        fs::remove_all(*directory_itr);
      return;
    }
    catch (const std::exception& e) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      error_message = e.what();
    }
  }
  std::cout << "Failed to cleanup " << g_root << " - " << error_message << '\n';
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

void GetUsedSpace(const fs::path& path, uintmax_t& size) {
  fs::directory_iterator it(path), end;
  while (it != end) {
    if (fs::is_regular_file(it->path()))
      size += fs::file_size(it->path());
    else if (fs::is_directory(it->path()))
      GetUsedSpace(it->path(), size);
    else
      boost::throw_exception(std::invalid_argument("Invalid Path Element"));
    ++it;
  }
}

std::pair<fs::path, fs::path> CreateMinimalCppProject(const fs::path& path) {
  boost::system::error_code error_code;
  fs::path project_root(CreateDirectory(path));
  fs::path project(CreateDirectory(project_root));
  fs::path build(CreateDirectory(project_root));
  std::string project_name(project.filename().string());
  auto main_cmake_file(project_root / "CMakeLists.txt");
  std::string content("cmake_minimum_required(VERSION 2.8.12.1 FATAL_ERROR)\n");
  content += "project(" + project_name + ")\n";
  content += "add_subdirectory(" + project_name + ")";
  REQUIRE(WriteFile(main_cmake_file, content));
  REQUIRE(fs::exists(main_cmake_file, error_code));
  auto project_cmake_file(project / "CMakeLists.txt");
  content = "add_executable(" + project_name + " " + project_name + ".cc)";
  REQUIRE(WriteFile(project_cmake_file, content));
  REQUIRE(fs::exists(project_cmake_file, error_code));
  auto project_cc_file(project / (project_name + ".cc"));
  content = "int main() {\n  return 0;\n}";
  REQUIRE(WriteFile(project_cc_file, content));
  REQUIRE(fs::exists(project_cc_file, error_code));

#ifdef MAIDSAFE_WIN32
  std::string architecture(BOOST_PP_STRINGIZE(TARGET_ARCHITECTURE)), command_args;
  if (architecture == "x86_64")
    command_args = " /k cmake .. -G\"Visual Studio 11 Win64\" 2>nul 1>nul & exit";
  else
    command_args = " /k cmake .. -G\"Visual Studio 11\" 2>nul 1>nul & exit";
#else
#endif

  fs::path shell_path = boost::process::shell_path();
  std::vector<std::string> process_args;
  process_args.emplace_back(shell_path.string());
  process_args.emplace_back(command_args);
  const auto command_line(process::ConstructCommandLine(process_args));
  boost::process::child child = boost::process::execute(
      boost::process::initializers::start_in_dir(build),
      boost::process::initializers::run_exe(shell_path),
      boost::process::initializers::set_cmd_line(command_line),
      boost::process::initializers::set_on_error(error_code));

  REQUIRE(error_code.value() == 0);
  boost::process::wait_for_exit(child, error_code);
  REQUIRE(error_code.value() == 0);

  std::string slash(fs::path("/").make_preferred().string());
  INFO("Failed to find " << build.string() + slash + project_name + ".sln");
#ifdef MAIDSAFE_WIN32
  REQUIRE(fs::exists(build / (project_name + ".sln")));
#else
#endif

  return std::make_pair(project, build);
}

void BuildMinimalCppProject(const fs::path& project, const fs::path& build) {
  boost::system::error_code error_code;
  std::string command_args;
  fs::path shell_path = boost::process::shell_path();

  {
    // release
    std::vector<std::string> process_args;
    process_args.emplace_back(shell_path.string());
    command_args = " /k cmake . && cmake --build . --config Release 2>nul 1>nul & exit";
    process_args.emplace_back(command_args);
    const auto command_line(process::ConstructCommandLine(process_args));

    boost::process::child child = boost::process::execute(
        boost::process::initializers::start_in_dir(build),
        boost::process::initializers::run_exe(shell_path),
        boost::process::initializers::set_cmd_line(command_line),
        boost::process::initializers::set_on_error(error_code));

    REQUIRE(error_code.value() == 0);
    boost::process::wait_for_exit(child, error_code);
    REQUIRE(error_code.value() == 0);

    std::string slash(fs::path("/").make_preferred().string());
    std::string project_name(project.filename().string());
    fs::path project_exe(build / project_name / "Release" / (project_name + ".exe"));
    INFO("Failed to build " << project_exe.string());
#ifdef MAIDSAFE_WIN32
    REQUIRE(fs::exists(project_exe));
#else
#endif
  }
  {
    // debug
    std::vector<std::string> process_args;
    process_args.emplace_back(shell_path.string());
    command_args = " /k cmake . && cmake --build . --config Debug 2>nul 1>nul & exit";
    process_args.emplace_back(command_args);
    const auto command_line(process::ConstructCommandLine(process_args));

    boost::process::child child = boost::process::execute(
        boost::process::initializers::start_in_dir(build),
        boost::process::initializers::run_exe(shell_path),
        boost::process::initializers::set_cmd_line(command_line),
        boost::process::initializers::set_on_error(error_code));

    REQUIRE(error_code.value() == 0);
    boost::process::wait_for_exit(child, error_code);
    REQUIRE(error_code.value() == 0);

    std::string slash(fs::path("/").make_preferred().string());
    std::string project_name(project.filename().string());
    fs::path project_exe(build / project_name / "Debug" / (project_name + ".exe"));
    INFO("Failed to build " << project_exe.string());
#ifdef MAIDSAFE_WIN32
    REQUIRE(fs::exists(project_exe));
#else
#endif
  }
}

void CloneProject(const fs::path& start_directory, const std::string& url) {
  boost::system::error_code error_code;
  std::string command_args;
  fs::path shell_path = boost::process::shell_path();

  std::vector<std::string> process_args;
  process_args.emplace_back(shell_path.string());
  command_args = " /k git clone " + url + " 2>nul 1>nul & exit";
  process_args.emplace_back(command_args);
  const auto command_line(process::ConstructCommandLine(process_args));

  boost::process::child child = boost::process::execute(
      boost::process::initializers::start_in_dir(start_directory),
      boost::process::initializers::run_exe(shell_path),
      boost::process::initializers::set_cmd_line(command_line),
      boost::process::initializers::set_on_error(error_code));

  REQUIRE(error_code.value() == 0);
  boost::process::wait_for_exit(child, error_code);
  REQUIRE(error_code.value() == 0);
}

void InitialiseSubmodulesInProject(const fs::path& start_directory) {
  boost::system::error_code error_code;
  std::string command_args;
  fs::path shell_path = boost::process::shell_path();

  std::vector<std::string> process_args;
  process_args.emplace_back(shell_path.string());
  command_args = " /k git submodule init 2>nul 1>nul & exit";
  process_args.emplace_back(command_args);
  const auto command_line(process::ConstructCommandLine(process_args));

  boost::process::child child = boost::process::execute(
      boost::process::initializers::start_in_dir(start_directory),
      boost::process::initializers::run_exe(shell_path),
      boost::process::initializers::set_cmd_line(command_line),
      boost::process::initializers::set_on_error(error_code));

  REQUIRE(error_code.value() == 0);
  boost::process::wait_for_exit(child, error_code);
  REQUIRE(error_code.value() == 0);
}

void UpdateSubmodulesInProject(const fs::path& start_directory) {
  boost::system::error_code error_code;
  std::string command_args;
  fs::path shell_path = boost::process::shell_path();

  std::vector<std::string> process_args;
  process_args.emplace_back(shell_path.string());
  command_args = " /k git submodule update 2>nul 1>nul & exit";
  process_args.emplace_back(command_args);
  const auto command_line(process::ConstructCommandLine(process_args));

  boost::process::child child = boost::process::execute(
      boost::process::initializers::start_in_dir(start_directory),
      boost::process::initializers::run_exe(shell_path),
      boost::process::initializers::set_cmd_line(command_line),
      boost::process::initializers::set_on_error(error_code));

  REQUIRE(error_code.value() == 0);
  boost::process::wait_for_exit(child, error_code);
  REQUIRE(error_code.value() == 0);
}

void CheckoutNextBranchesForWholeProject(const fs::path& start_directory) {
  boost::system::error_code error_code;
  std::string command_args;
  fs::path shell_path = boost::process::shell_path();

  std::vector<std::string> process_args;
  process_args.emplace_back(shell_path.string());
  command_args = " /k git checkout next 2>nul 1>nul & git pull 2>nul 1>nul";
  command_args += " & git submodule foreach git checkout next 2>nul 1>nul";
  command_args += " & git submodule foreach git pull 2>nul 1>nul & exit";
  process_args.emplace_back(command_args);
  const auto command_line(process::ConstructCommandLine(process_args));

  boost::process::child child = boost::process::execute(
      boost::process::initializers::start_in_dir(start_directory),
      boost::process::initializers::run_exe(shell_path),
      boost::process::initializers::set_cmd_line(command_line),
      boost::process::initializers::set_on_error(error_code));

  REQUIRE(error_code.value() == 0);
  boost::process::wait_for_exit(child, error_code);
  REQUIRE(error_code.value() == 0);
}

}  // unnamed namespace

int RunTool(int argc, char** argv, const fs::path& root, const fs::path& temp,
            const fs::path& storage) {
  g_root = root;
  g_temp = temp;
  g_storage = storage;
  Catch::Session session;
  auto command_line_result(
      session.applyCommandLine(argc, argv, Catch::Session::OnUnusedOptions::Ignore));
  if (command_line_result != 0)
    LOG(kWarning) << "Catch command line parsing error: " << command_line_result;
  return session.run();
}

TEST_CASE("Drive size", "[Filesystem]") {
  // 1GB seems reasonable as a lower limit for all drive types (real/local/network).  It at least
  // provides a regression check for https://github.com/maidsafe/SureFile/issues/33
  auto space(boost::filesystem::space(g_root));
  REQUIRE(space.available > 1073741824);
  REQUIRE(space.capacity > 1073741824);
  REQUIRE(space.free > 1073741824);
}

TEST_CASE("Create empty file", "[Filesystem]") {
  on_scope_exit cleanup(clean_root);
  CreateFile(g_root, 0);
}

TEST_CASE("Create empty directory", "[Filesystem]") {
  on_scope_exit cleanup(clean_root);
  CreateDirectory(g_root);
}

TEST_CASE("Append to file", "[Filesystem]") {
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_root, 0).first);
  int test_runs = 1000;
  WriteFile(filepath, "a");
  NonEmptyString content, updated_content;
  for (int i = 0; i < test_runs; ++i) {
    REQUIRE_NOTHROW(content = ReadFile(filepath));
    REQUIRE(WriteFile(filepath, content.string() + "a"));
    REQUIRE_NOTHROW(updated_content = ReadFile(filepath));
    REQUIRE(updated_content.string().size() == content.string().size() + 1);
    REQUIRE(updated_content.string().size() == i + 2U);
  }
}

TEST_CASE("Copy empty directory", "[Filesystem]") {
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));

  // Copy 'g_temp' directory to 'g_root'
  boost::system::error_code error_code;
  fs::copy_directory(directory, g_root / directory.filename(), error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(g_root / directory.filename());
}

TEST_CASE("Copy directory then delete", "[Filesystem]") {
  // Create a file and directory in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());
  RequireExists(copied_directory);
  RequireDirectoriesEqual(directory, copied_directory, true);

  // Delete the directory along with its contents
  boost::system::error_code error_code;
  REQUIRE(fs::remove_all(copied_directory, error_code) == 3U);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireDoesNotExist(copied_directory / filepath.filename());
  RequireDoesNotExist(copied_directory / nested_directory.filename());

  // Try to clean up 'g_root'
  fs::remove_all(copied_directory, error_code);
}

TEST_CASE("Copy directory, delete then re-copy", "[Filesystem]") {
  // Create a file and directory in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());

  // Delete the directory along with its contents
  boost::system::error_code error_code;
  REQUIRE(fs::remove_all(copied_directory, error_code) == 3U);
  INFO(copied_directory << ": " << error_code.message());
  REQUIRE(error_code.value() == 0);

  // Re-copy directory and file to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  RequireExists(copied_directory);
  RequireDirectoriesEqual(directory, copied_directory, true);
}

TEST_CASE("Copy directory then rename", "[Filesystem]") {
  // Create a file and directory in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());

  // Rename the directory
  auto renamed_directory(g_root / maidsafe::RandomAlphaNumericString(5));
  boost::system::error_code error_code;
  fs::rename(copied_directory, renamed_directory, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireExists(renamed_directory);
  RequireDirectoriesEqual(directory, renamed_directory, true);
}

TEST_CASE("Copy directory, rename then re-copy", "[Filesystem]") {
  // Create a file and directory in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());

  // Rename the directory
  auto renamed_directory(g_root / maidsafe::RandomAlphaNumericString(5));
  boost::system::error_code error_code;
  fs::rename(copied_directory, renamed_directory, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);

  // Re-copy directory and file to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  RequireExists(copied_directory);
  RequireDirectoriesEqual(directory, copied_directory, false);
}

TEST_CASE("Copy directory containing multiple files", "[Filesystem]") {
  // Create files in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectoryContainingFiles(g_temp));

  // Copy directory to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  REQUIRE(!fs::is_empty(copied_directory, error_code));
  REQUIRE(error_code.value() == 0);
  RequireDirectoriesEqual(directory, copied_directory, true);
}

TEST_CASE("Copy directory hierarchy", "[Filesystem]") {
  // Create a new directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  std::vector<fs::path> directories;
  auto directory(CreateDirectory(g_temp));
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

  // Copy hierarchy to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  REQUIRE(!fs::is_empty(copied_directory, error_code));
  REQUIRE(error_code.value() == 0);
  RequireDirectoriesEqual(directory, copied_directory, true);
}

TEST_CASE("Copy then copy copied file", "[Filesystem]") {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file(g_root / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file);
  REQUIRE(ReadFile(filepath) == ReadFile(copied_file));

  // Copy file to 'g_root' again
  fs::copy_file(filepath, copied_file, fs::copy_option::overwrite_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file);
  REQUIRE(ReadFile(filepath) == ReadFile(copied_file));
}

TEST_CASE("Copy file, delete then re-copy", "[Filesystem]") {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file(g_root / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);

  // Delete the file
  fs::remove(copied_file, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_file);

  // Copy file to 'g_root' again
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file);
  REQUIRE(ReadFile(filepath) == ReadFile(copied_file));
}

TEST_CASE("Copy file, rename then re-copy", "[Filesystem]") {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file(g_root / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);

  // Rename the file
  auto renamed_file(g_root / (RandomAlphaNumericString(5) + ".txt"));
  fs::rename(copied_file, renamed_file, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_file);
  RequireExists(renamed_file);
  REQUIRE(ReadFile(filepath) == ReadFile(renamed_file));

  // Copy file to 'g_root' again
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file);
  REQUIRE(ReadFile(filepath) == ReadFile(copied_file));
}

TEST_CASE("Copy file, delete then try to read", "[Filesystem]") {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file(g_root / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);

  // Delete the file
  fs::remove(copied_file, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_file);

  // Try to copy 'g_root' file back to a 'g_temp' file
  auto test_file(g_temp / (RandomAlphaNumericString(5) + ".txt"));
  fs::copy_file(copied_file, test_file, fs::copy_option::overwrite_if_exists, error_code);
  REQUIRE(error_code.value() != 0);
  RequireDoesNotExist(test_file);
}

TEST_CASE("Create file", "[Filesystem]") {
  // Create a file in 'g_root' and read back its contents
  on_scope_exit cleanup(clean_root);
  auto filepath_and_contents(CreateFile(g_root, RandomUint32() % 1048577));
  REQUIRE(ReadFile(filepath_and_contents.first).string() == filepath_and_contents.second);
}

TEST_CASE("Create file, modify then read", "[Filesystem]") {
  // Create a file in 'g_root'
  on_scope_exit cleanup(clean_root);

  std::pair<fs::path, std::string> filepath_and_contents;
  SECTION("Smaller file") {
    filepath_and_contents = CreateFile(g_root, RandomUint32() % 1048);
  }
  SECTION("Larger file") {
    filepath_and_contents = CreateFile(g_root, (RandomUint32() % 1048) + 1048577);
  }
  if (filepath_and_contents.second.empty())  // first run-through before SECTIONs are executed.
    return;

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
  // Create a file in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath_and_contents(CreateFile(directory, RandomUint32() % 1024));

  // Copy directory to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());

  // Rename the file into its parent
  auto renamed_from_file(copied_directory / filepath_and_contents.first.filename());
  auto renamed_to_file(g_root / filepath_and_contents.first.filename());
  boost::system::error_code error_code;
  fs::rename(renamed_from_file, renamed_to_file, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(renamed_from_file);
  RequireExists(renamed_to_file);
  REQUIRE(ReadFile(renamed_to_file).string() == filepath_and_contents.second);
}

TEST_CASE("Rename directory hierarchy keeping same parent", "[Filesystem]") {
  // Create a new directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  std::vector<fs::path> directories;
  auto directory(CreateDirectory(g_temp));
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

  // Copy hierarchy to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  REQUIRE(!fs::is_empty(copied_directory, error_code));
  REQUIRE(error_code.value() == 0);
  RequireDirectoriesEqual(directory, copied_directory, true);

  // Rename the directory
  auto renamed_directory(g_root / maidsafe::RandomAlphaNumericString(5));
  fs::rename(copied_directory, renamed_directory, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireDirectoriesEqual(directory, renamed_directory, true);
}

TEST_CASE("Rename directory hierarchy to different parent", "[Filesystem]") {
  // Create a new directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  std::vector<fs::path> directories;
  auto directory(CreateDirectory(g_temp));
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

  // Copy hierarchy to 'g_root'
  REQUIRE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  REQUIRE(!fs::is_empty(copied_directory, error_code));
  REQUIRE(error_code.value() == 0);
  RequireDirectoriesEqual(directory, copied_directory, true);

  // Rename the directory
  auto new_parent(CreateDirectory(g_root));
  auto renamed_directory(new_parent / maidsafe::RandomAlphaNumericString(5));
  fs::rename(copied_directory, renamed_directory, error_code);
  REQUIRE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireDirectoriesEqual(directory, renamed_directory, true);
}

TEST_CASE("Check failures", "[Filesystem]") {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath0(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file0(g_root / filepath0.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath0, copied_file0, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_file0);

  // Copy same file to 'g_root' again
  fs::copy_file(filepath0, copied_file0, fs::copy_option::fail_if_exists, error_code);
  REQUIRE(error_code.value() != 0);
  RequireExists(copied_file0);
  REQUIRE(ReadFile(filepath0) == ReadFile(copied_file0));

  // Create another file in 'g_temp' and copy it to 'g_root'
  auto filepath1(CreateFile(g_temp, RandomUint32() % 1048577).first);
  auto copied_file1(g_root / filepath1.filename());
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
  // Create a file and directory in a newly created directory in 'g_temp'
  auto directory0(CreateDirectory(g_temp));
  CreateFile(directory0, RandomUint32() % 1024);
  CreateDirectory(directory0);

  // Copy directory to 'g_root'
  REQUIRE(CopyDirectory(directory0, g_root));
  auto copied_directory0(g_root / directory0.filename());

  // Copy same directory to 'g_root' again
  fs::copy_directory(directory0, copied_directory0, error_code);
  REQUIRE(error_code.value() != 0);
  RequireExists(copied_directory0);
  RequireDirectoriesEqual(directory0, copied_directory0, true);

  // Create a directory with the same name on the 'g_root'
  REQUIRE(!fs::create_directory(copied_directory0, error_code));
  REQUIRE(error_code.value() == 0);
  RequireExists(copied_directory0);
  RequireDirectoriesEqual(directory0, copied_directory0, false);

  // Create another directory in 'g_temp' containing a file and subdirectory
  auto directory1(CreateDirectory(g_temp));
  CreateFile(directory1, RandomUint32() % 1024);
  CreateDirectory(directory1);

  // Copy it to 'g_root'
  REQUIRE(CopyDirectory(directory1, g_root));
  auto copied_directory1(g_root / directory1.filename());

  // Rename to first directory name
  fs::rename(copied_directory1, copied_directory0, error_code);
  REQUIRE((error_code.value()) != 0);
  RequireExists(copied_directory0);
  RequireExists(copied_directory1);
  RequireDirectoriesEqual(directory0, copied_directory0, false);
  RequireDirectoriesEqual(directory1, copied_directory1, false);

  // Create an empty directory in 'g_root'
  auto directory2(CreateDirectory(g_root));

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

TEST_CASE("Read only attribute", "[Filesystem][behavioural]") {
  on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32
  HANDLE handle(nullptr);
  fs::path path(g_root / RandomAlphaNumericString(8));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size));
  DWORD position(0), size(0), attributes(0);
  BOOL success(0);
  OVERLAPPED overlapped;

  // create a file
  CHECK_NOTHROW(handle = dtc::CreateFileCommand(path, GENERIC_ALL, 0, CREATE_NEW,
                                                FILE_ATTRIBUTE_ARCHIVE));
  REQUIRE(handle);
  CHECK_NOTHROW(success = dtc::WriteFileCommand(handle, path, buffer, &position, nullptr));
  CHECK((size = dtc::GetFileSizeCommand(handle, nullptr)) == buffer_size);
  CHECK_NOTHROW(success = dtc::CloseHandleCommand(handle));
  // check we can open and write to the file
  CHECK_NOTHROW(handle = dtc::CreateFileCommand(path, GENERIC_ALL, 0, OPEN_EXISTING, attributes));
  REQUIRE(handle);
  buffer = RandomString(buffer_size);
  success = 0;
  position = 1;
  FillMemory(&overlapped, sizeof(overlapped), 0);
  overlapped.Offset = position & 0xFFFFFFFF;
  overlapped.OffsetHigh = 0;
  CHECK_NOTHROW(success = dtc::WriteFileCommand(handle, path, buffer, &position, &overlapped));
  size = 0;
  CHECK((size = dtc::GetFileSizeCommand(handle, nullptr)) == buffer_size + 1);
  CHECK_NOTHROW(success = dtc::CloseHandleCommand(handle));
  // add read-only to the attributes
  CHECK_NOTHROW(attributes = dtc::GetFileAttributesCommand(path));
  CHECK((attributes & FILE_ATTRIBUTE_ARCHIVE) == FILE_ATTRIBUTE_ARCHIVE);
  CHECK_NOTHROW(success = dtc::SetFileAttributesCommand(
      path, FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY));
  CHECK_NOTHROW(attributes = dtc::GetFileAttributesCommand(path));
  CHECK((attributes & FILE_ATTRIBUTE_ARCHIVE) == FILE_ATTRIBUTE_ARCHIVE);
  CHECK((attributes & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY);
  // check we can open for reading but can't write to the file
  CHECK_THROWS_AS(handle = dtc::CreateFileCommand(path, GENERIC_ALL, 0, OPEN_EXISTING, attributes),
                  std::exception);
  CHECK_NOTHROW(handle = dtc::CreateFileCommand(path, GENERIC_READ, 0, OPEN_EXISTING, attributes));
  REQUIRE(handle);
  buffer = RandomString(buffer_size);
  success = 0;
  position = 2;
  FillMemory(&overlapped, sizeof(overlapped), 0);
  overlapped.Offset = position & 0xFFFFFFFF;
  overlapped.OffsetHigh = 0;
  CHECK_THROWS_AS(success = dtc::WriteFileCommand(handle, path, buffer, &position, &overlapped),
                  std::exception);
  size = 0;
  CHECK((size = dtc::GetFileSizeCommand(handle, nullptr)) == buffer_size + 1);
  CHECK_NOTHROW(success = dtc::CloseHandleCommand(handle));
  // remove the read-only attribute so the file can be deleted
  CHECK_NOTHROW(success = dtc::SetFileAttributesCommand(path, FILE_ATTRIBUTE_ARCHIVE));
  CHECK_NOTHROW(success = dtc::DeleteFileCommand(path));
#else
  int file_descriptor(-1);
  fs::path path(g_root / RandomAlphaNumericString(8));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size));
  int flags(O_CREAT | O_RDWR), size(0);
  ssize_t result(0);
  mode_t mode(S_IRWXU);
  off_t offset(0);

  // create a file
  CHECK_NOTHROW(file_descriptor = dtc::CreateFileCommand(path, flags, mode));
  CHECK_NOTHROW(result = dtc::WriteFileCommand(file_descriptor, buffer));
  CHECK(result == buffer_size);
  CHECK_NOTHROW(dtc::SyncFileCommand(file_descriptor));
  CHECK_NOTHROW(size = dtc::GetFileSizeCommand(file_descriptor));
  CHECK(size == buffer_size);
  CHECK_NOTHROW(dtc::CloseFileCommand(file_descriptor));
  // check we can open and write to the file
  flags = O_RDWR;
  CHECK_NOTHROW(file_descriptor = dtc::CreateFileCommand(path, flags));
  buffer = RandomString(buffer_size);
  offset = 1;
  CHECK_NOTHROW(result = dtc::WriteFileCommand(file_descriptor, buffer, offset));
  CHECK_NOTHROW(dtc::CloseFileCommand(file_descriptor));
  size = 0;
  CHECK_NOTHROW(size = dtc::GetFileSizeCommand(path));
  CHECK(size == buffer_size + 1);
  // add read-only to the attributes
  CHECK_NOTHROW(mode = dtc::GetModeCommand(path));
  CHECK((mode & S_IFREG) == S_IFREG);
  CHECK((mode & S_IRUSR) == S_IRUSR);
  CHECK((mode & S_IWUSR) == S_IWUSR);
  mode = S_IRUSR;
  CHECK_NOTHROW(dtc::SetModeCommand(path, mode));
  CHECK_NOTHROW(mode = dtc::GetModeCommand(path));
  CHECK((mode & S_IFREG) == S_IFREG);
  CHECK((mode & S_IRUSR) == S_IRUSR);
  CHECK((mode & S_IWUSR) == 0);
  // check we can open for reading but can't write to the file
  CHECK_THROWS_AS(file_descriptor = dtc::CreateFileCommand(path, flags), std::exception);
  flags = O_RDONLY;
  CHECK_NOTHROW(file_descriptor = dtc::CreateFileCommand(path, flags));
  buffer = RandomString(buffer_size);
  offset = 2;
  CHECK_THROWS_AS(result = dtc::WriteFileCommand(file_descriptor, buffer, offset), std::exception);
  size = 0;
  CHECK_NOTHROW(size = dtc::GetFileSizeCommand(file_descriptor));
  CHECK(size == buffer_size + 1);
  CHECK_NOTHROW(dtc::CloseFileCommand(file_descriptor));
  // remove the read-only attribute so the file can be deleted ???
  mode = S_IRWXU;
  CHECK_NOTHROW(dtc::SetModeCommand(path, mode));
#endif
}

TEST_CASE("Delete on close", "[Filesystem][behavioural]") {
  on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32
  HANDLE handle(nullptr);
  fs::path path(g_root / RandomAlphaNumericString(8));
  CHECK_NOTHROW(handle = dtc::CreateFileCommand(path, GENERIC_ALL, 0, CREATE_NEW,
                                                FILE_FLAG_DELETE_ON_CLOSE));
  REQUIRE(handle);
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size));
  DWORD position(0);
  BOOL success(0);
  CHECK_NOTHROW(success = dtc::WriteFileCommand(handle, path, buffer, &position, nullptr));
  DWORD attributes(0);
  CHECK_NOTHROW(attributes = dtc::GetFileAttributesCommand(path));
  CHECK((attributes & FILE_FLAG_DELETE_ON_CLOSE) == FILE_FLAG_DELETE_ON_CLOSE);
  CHECK_NOTHROW(success = dtc::CloseHandleCommand(handle));
  attributes = 0;
  CHECK_THROWS_AS(attributes = dtc::GetFileAttributesCommand(path), std::exception);
  CHECK(attributes == 0);
#else
  int file_descriptor(-1);
  fs::path path_template(g_root / (RandomAlphaNumericString(8) + "_XXXXXX"));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size));
  int size(0);
  ssize_t result(0);
  mode_t mode(0);

  // create a temp file
  CHECK_NOTHROW(file_descriptor = dtc::CreateTempFileCommand(path_template));
  CHECK(boost::filesystem::exists(path_template));
  // unlink
  CHECK_NOTHROW(dtc::UnlinkFileCommand(path_template));
  CHECK_FALSE(boost::filesystem::exists(path_template));
  // write to file
  CHECK_NOTHROW(result = dtc::WriteFileCommand(file_descriptor, buffer));
  CHECK(result == buffer_size);
  // verify size...
  CHECK_NOTHROW(size = dtc::GetFileSizeCommand(file_descriptor));
  CHECK(size == buffer_size);
  // ...and mode
  CHECK_NOTHROW(mode = dtc::GetModeCommand(file_descriptor));
  CHECK((mode & S_IFREG) == S_IFREG);
  CHECK((mode & S_IRUSR) == S_IRUSR);
  CHECK((mode & S_IWUSR) == S_IWUSR);
  // close file
  CHECK_NOTHROW(dtc::CloseFileCommand(file_descriptor));
#endif
}

TEST_CASE("Hidden attribute", "[Filesystem][behavioural]") {
  on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32
  HANDLE handle(nullptr);
  fs::path directory(g_root / RandomAlphaNumericString(5)),
           file(directory / RandomAlphaNumericString(8));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size));
  DWORD position(0), attributes(0);
  BOOL success(0);

  CHECK_NOTHROW(success = dtc::CreateDirectoryCommand(directory));
  CHECK(success);
  CHECK_NOTHROW(handle = dtc::CreateFileCommand(file, GENERIC_ALL, 0, CREATE_NEW,
                                                FILE_ATTRIBUTE_HIDDEN));
  REQUIRE(handle);
  CHECK_NOTHROW(success = dtc::WriteFileCommand(handle, file, buffer, &position, nullptr));
  CHECK_NOTHROW(attributes = dtc::GetFileAttributesCommand(file));
  CHECK((attributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
  CHECK_NOTHROW(success = dtc::CloseHandleCommand(handle));

  std::vector<WIN32_FIND_DATA> files(dtc::EnumerateDirectoryCommand(directory));
  CHECK(files.size() == 1);
  CHECK((files.begin()->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
  CHECK(files.begin()->nFileSizeLow == buffer_size);
  CHECK(files.begin()->nFileSizeHigh == 0);
  CHECK_NOTHROW(success = dtc::DeleteFileCommand(file));
  CHECK_NOTHROW(success = dtc::RemoveDirectoryCommand(directory));
#else
  int file_descriptor(-1);
  std::string dot(".");
  fs::path directory(g_root / RandomAlphaNumericString(5)),
           file(directory / (dot + RandomAlphaNumericString(8)));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size));
  int flags(O_CREAT | O_EXCL | O_RDWR), size(0);
  ssize_t result(0);
  mode_t directory_mode = 0777, file_mode(S_IRWXU | S_IRGRP | S_IROTH);

  CHECK_NOTHROW(dtc::CreateDirectoryCommand(directory, directory_mode));
  CHECK(boost::filesystem::exists(directory));
  CHECK_NOTHROW(file_descriptor = dtc::CreateFileCommand(file, flags, file_mode));
  CHECK(boost::filesystem::exists(file));
  CHECK_NOTHROW(result = dtc::WriteFileCommand(file_descriptor, buffer));
  CHECK(result == buffer_size);
  CHECK_NOTHROW(size = dtc::GetFileSizeCommand(file_descriptor));
  CHECK(size == buffer_size);
  CHECK_NOTHROW(dtc::CloseFileCommand(file_descriptor));

  std::vector<boost::filesystem::path> files(dtc::EnumerateDirectoryCommand(directory));
  CHECK(files.size() == 1);
  CHECK(files[0] == file.filename().string());
  CHECK_NOTHROW(dtc::UnlinkFileCommand(file));
  CHECK_FALSE(boost::filesystem::exists(file));
  CHECK_NOTHROW(dtc::RemoveDirectoryCommand(directory));
  CHECK_FALSE(boost::filesystem::exists(directory));
#endif
}

TEST_CASE("Check attributes for concurrent open instances", "[Filesystem][behavioural]") {
  on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32
  HANDLE first_handle(nullptr), second_handle(nullptr);
  fs::path path(g_root / RandomAlphaNumericString(5));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size)), recovered(buffer_size, 0);
  DWORD position(0), attributes(FILE_ATTRIBUTE_ARCHIVE), size(0), count;
  BOOL success(0);
  OVERLAPPED overlapped;

  // create file
  CHECK_NOTHROW(first_handle = dtc::CreateFileCommand(
      path, GENERIC_ALL, 0, CREATE_NEW, attributes));
  REQUIRE(first_handle);
  // write data using first instance
  CHECK_NOTHROW(success = dtc::WriteFileCommand(first_handle, path, buffer, &count, nullptr));
  // verify opening a second instance throws
  CHECK_THROWS_AS(second_handle = dtc::CreateFileCommand(
      path, GENERIC_ALL, 0, OPEN_EXISTING, attributes), std::exception);
  REQUIRE(!second_handle);
  // close first instance
  CHECK_NOTHROW(success = dtc::CloseHandleCommand(first_handle));
  // reopen a first instance with shared read/write access
  CHECK_NOTHROW(first_handle = dtc::CreateFileCommand(path, GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, attributes));
  REQUIRE(first_handle);
  // open a second instance with shared read/write access
  CHECK_NOTHROW(second_handle = dtc::CreateFileCommand(path, GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, attributes));
  REQUIRE(second_handle);
  // write to file using first instance
  buffer = RandomString(buffer_size);
  success = 0;
  position = 1;
  FillMemory(&overlapped, sizeof(overlapped), 0);
  overlapped.Offset = position & 0xFFFFFFFF;
  CHECK_NOTHROW(success = dtc::WriteFileCommand(
      first_handle, path, buffer, &count, &overlapped));
  // check the file size with the second instance
  CHECK((size = dtc::GetFileSizeCommand(second_handle, nullptr)) == buffer_size + 1);
  // check content with the second instance
  CHECK_NOTHROW(success = dtc::ReadFileCommand(second_handle, path, recovered, &count,
                                               &overlapped));
  CHECK(recovered.compare(buffer) == 0);
  CHECK(count == buffer_size);
  // write to file using second instance
  buffer = RandomString(buffer_size);
  success = 0;
  position = 2;
  FillMemory(&overlapped, sizeof(overlapped), 0);
  overlapped.Offset = position & 0xFFFFFFFF;
  CHECK_NOTHROW(success = dtc::WriteFileCommand(
      second_handle, path, buffer, &count, &overlapped));
  // check the file size with the first instance
  CHECK((size = dtc::GetFileSizeCommand(first_handle, nullptr)) == buffer_size + 2);
  // check content with the first instance
  CHECK_NOTHROW(success = dtc::ReadFileCommand(first_handle, path, recovered, &count,
                                               &overlapped));
  CHECK(recovered.compare(buffer) == 0);
  CHECK(count == buffer_size);
  // close both instances
  CHECK_NOTHROW(success = dtc::CloseHandleCommand(first_handle));
  CHECK_NOTHROW(success = dtc::CloseHandleCommand(second_handle));
#else
  int first_file_descriptor(-1), second_file_descriptor(-1);
  fs::path path(g_root / RandomAlphaNumericString(5));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size)), recovered(buffer_size, 0);
  int flags(O_CREAT | O_RDWR), size(0);
  ssize_t result(0);
  mode_t mode(S_IRWXU);
  off_t offset(0);

  // create file
  CHECK_NOTHROW(first_file_descriptor = dtc::CreateFileCommand(path, flags, mode));
  // open a second instance
  flags = O_RDWR;
  CHECK_NOTHROW(second_file_descriptor = dtc::CreateFileCommand(path, flags));
  // write to file using first instance
  CHECK_NOTHROW(result = dtc::WriteFileCommand(first_file_descriptor, buffer));
  CHECK(result == buffer_size);
  // check data using second instance
  CHECK_NOTHROW(size = dtc::GetFileSizeCommand(second_file_descriptor));
  CHECK(size == buffer_size);
  CHECK_NOTHROW(result = dtc::ReadFileCommand(second_file_descriptor, recovered));
  CHECK(result == buffer_size);
  CHECK(recovered.compare(buffer) == 0);
  // write to file using second instance
  buffer = RandomString(buffer_size);
  offset = 1;
  CHECK_NOTHROW(result = dtc::WriteFileCommand(second_file_descriptor, buffer, offset));
  CHECK(result == buffer_size);
  // check data using first instance
  CHECK_NOTHROW(size = dtc::GetFileSizeCommand(first_file_descriptor));
  CHECK(size == buffer_size + 1);
  CHECK_NOTHROW(result = dtc::ReadFileCommand(first_file_descriptor, recovered, offset));
  CHECK(result == buffer_size);
  CHECK(recovered.compare(buffer) == 0);
  // close both instances
  CHECK_NOTHROW(dtc::CloseFileCommand(first_file_descriptor));
  CHECK_NOTHROW(dtc::CloseFileCommand(second_file_descriptor));
#endif
}

TEST_CASE("Locale", "[Filesystem][behavioural]") {
  on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32
  std::locale::global(boost::locale::generator().generate(""));
#else
  std::locale::global(std::locale(""));
#endif
  fs::path::imbue(std::locale());
  fs::path file(process::GetOtherExecutablePath("filesystem_test"));
  while (file.filename().string() != "MaidSafe" && file.filename().string() != "")
    file = file.parent_path();
  if (file.filename().string() == "")
    REQUIRE(false);
#ifdef MAIDSAFE_WIN32
  file /= "\\src\\drive\\src\\maidsafe\\drive\\tools\\UTF-8";
#else
  file /= "src/drive/src/maidsafe/drive/tools/UTF-8";
#endif
  fs::path directory(g_root / ReadFile(file).string());
  CreateDirectory(directory);
  RequireExists(directory);
  fs::directory_iterator it(g_root);
  CHECK(it->path().filename() == ReadFile(file).string());
}

TEST_CASE("Storage path chunks not deleted", "[Filesystem][behavioural]") {
  // Related to SureFile Issue#50, the test should be reworked/removed when the implementation of
  // versions is complete and some form of communication is available to handle them. The test is
  // currently setup to highlight the issue and thus to fail.
  on_scope_exit cleanup(clean_root);
  boost::system::error_code error_code;
  size_t file_size(1024 * 1024);
  uintmax_t initial_size(0), first_update_size(0), second_update_size(0);
  GetUsedSpace(g_storage, initial_size);
  auto test_file(CreateFile(g_root, file_size));
  GetUsedSpace(g_storage, first_update_size);
  fs::remove(test_file.first, error_code);
  GetUsedSpace(g_storage, second_update_size);
  CHECK(second_update_size < first_update_size);
  CHECK(initial_size == second_update_size);
}

TEST_CASE("Create a minimal C++ project", "[Filesystem][functional]") {
  on_scope_exit cleanup(clean_root);
  // create in temp directory...
  REQUIRE_NOTHROW(CreateMinimalCppProject(g_temp));
  // ...now in drive root
  REQUIRE_NOTHROW(CreateMinimalCppProject(g_root));
}

TEST_CASE("Build a minimal C++ project", "[Filesystem][functional]") {
  on_scope_exit cleanup(clean_root);
  std::pair<fs::path, fs::path> project_paths;
  // first in temp directory...
  REQUIRE_NOTHROW(project_paths = CreateMinimalCppProject(g_temp));
  REQUIRE_NOTHROW(BuildMinimalCppProject(project_paths.first, project_paths.second));
  // ...now in drive root
  REQUIRE_NOTHROW(project_paths = CreateMinimalCppProject(g_root));
  REQUIRE_NOTHROW(BuildMinimalCppProject(project_paths.first, project_paths.second));
}

TEST_CASE("Clone MaidSafe", "[Filesystem][functional]") {
  on_scope_exit cleanup(clean_root);
  std::string url("git@github.com:maidsafe/MaidSafe.git");
  fs::path temp_maidsafe_directory(g_temp / "MaidSafe"),
           root_maidsafe_directory(g_root / "MaidSafe");
  // first to temp directory...
  REQUIRE_NOTHROW(CloneProject(g_temp, url));
  REQUIRE_NOTHROW(InitialiseSubmodulesInProject(temp_maidsafe_directory));
  REQUIRE_NOTHROW(UpdateSubmodulesInProject(temp_maidsafe_directory));
  REQUIRE_NOTHROW(CheckoutNextBranchesForWholeProject(temp_maidsafe_directory));
  // ...now to drive root
  REQUIRE_NOTHROW(CloneProject(g_root, url));
  REQUIRE_NOTHROW(InitialiseSubmodulesInProject(root_maidsafe_directory));
  REQUIRE_NOTHROW(UpdateSubmodulesInProject(root_maidsafe_directory));
  REQUIRE_NOTHROW(CheckoutNextBranchesForWholeProject(root_maidsafe_directory));
  // compare repositories, may fail if next has been updated during the test
  RequireDirectoriesEqual(temp_maidsafe_directory, root_maidsafe_directory, false);
}

}  // namespace test

}  // namespace maidsafe
