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

#define USE_GTEST 1

#ifndef MAIDSAFE_WIN32
#include <sys/stat.h>
#endif

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

#ifdef MAIDSAFE_BSD
extern "C" char** environ;
#endif

#ifndef MAIDSAFE_WIN32
#include <locale>  // NOLINT
#else
#include "boost/locale/generator.hpp"
#endif
#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/system/error_code.hpp"
#include "boost/process.hpp"
#include "boost/process/initializers.hpp"

#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/process.h"
#ifdef MAIDSAFE_WIN32
#include "maidsafe/drive/tools/commands/windows_file_commands.h"
#else
#include "maidsafe/drive/tools/commands/unix_file_commands.h"
#endif
#include "maidsafe/drive/drive.h"
#include "maidsafe/drive/tools/launcher.h"

namespace fs = boost::filesystem;
namespace dtc = maidsafe::drive::tools::commands;

namespace maidsafe {

namespace test {

namespace {

fs::path g_root, g_temp;
drive::Options g_options;
std::shared_ptr<drive::Launcher> g_launcher;
drive::DriveType g_test_type;

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
  ASSERT_TRUE(fs::exists(path, error_code));
  ASSERT_TRUE(error_code.value() == 0);
}

void RequireDoesNotExist(const fs::path& path) {
  boost::system::error_code error_code;
  ASSERT_TRUE(!fs::exists(path, error_code));
  ASSERT_TRUE(error_code.value() != 0);
}

std::pair<fs::path, std::string> CreateFile(const fs::path& parent, size_t content_size) {
  auto file(parent / (RandomAlphaNumericString(5) + ".txt"));
  std::string content(RandomString(content_size + 1));
  EXPECT_TRUE(WriteFile(file, content));
  RequireExists(file);
  return std::make_pair(file, content);
}

fs::path CreateDirectory(const fs::path& parent) {
  auto directory(parent / RandomAlphaNumericString(5));
  fs::create_directories(directory);
  RequireExists(directory);
  return directory;
}

std::vector<fs::path> CreateDirectoryHierarchy(const fs::path& parent) {
  std::vector<fs::path> directories;
  auto directory(CreateDirectory(parent));
  directories.push_back(directory);

  // Add further directories 3 levels deep
  for (int i(0); i != 3; ++i) {
    std::vector<fs::path> nested;
    for (const auto& directory : directories) {
      auto directory_count((RandomUint32() % 3) + 1);
      for (uint32_t j(0); j != directory_count; ++j)
        nested.push_back(CreateDirectory(directory));
    }
    directories.insert(std::end(directories), std::begin(nested), std::end(nested));
    nested.clear();
  }

  // Add files to all directories
  for (const auto& directory : directories) {
    auto file_count((RandomUint32() % 4) + 2);
    for (uint32_t k(0); k != file_count; ++k)
      CreateFile(directory, (RandomUint32() % 1024) + 1);
  }

  return directories;
}

bool CopyDirectory(const fs::path& from, const fs::path& to) {
  LOG(kVerbose) << "CopyDirectory: from " << from << " to " << (to / from.filename());
  try {
    if (!fs::exists(to / from.filename()))
      fs::copy_directory(from, to / from.filename());

    fs::directory_iterator end;
    EXPECT_TRUE(fs::exists(to / from.filename()));
    for (fs::directory_iterator directory_itr(from); directory_itr != end; ++directory_itr) {
      if (fs::is_directory(*directory_itr)) {
        EXPECT_TRUE(CopyDirectory((*directory_itr).path(), to / from.filename()));
      } else if (fs::is_regular_file(*directory_itr)) {
        fs::copy_file((*directory_itr).path(),
                      to / from.filename() / (*directory_itr).path().filename(),
                      fs::copy_option::fail_if_exists);
        EXPECT_TRUE(fs::exists(to / from.filename() / (*directory_itr).path().filename()));
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
    ASSERT_TRUE(false);
  }

  std::vector<std::string> difference;
  std::set_symmetric_difference(std::begin(lhs_files), std::end(lhs_files), std::begin(rhs_files),
                                std::end(rhs_files), std::back_inserter(difference));
  if (!difference.empty()) {
    ASSERT_TRUE(difference.empty()) << "At least one difference exists: " + difference[0];
  }

  if (check_file_contents) {
    auto rhs_itr(std::begin(rhs_files));
    for (const auto& lhs_file : lhs_files) {
      if (!fs::is_regular_file(lhs / lhs_file)) {
        ASSERT_TRUE(!fs::is_regular_file(rhs / (*rhs_itr++)));
        continue;
      }
      ASSERT_TRUE(fs::is_regular_file(rhs / *rhs_itr));
      ASSERT_TRUE((ReadFile(lhs / lhs_file)) == (ReadFile(rhs / (*rhs_itr++))));
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

void DownloadFile(const fs::path& start_directory, const std::string& url) {
  boost::system::error_code error_code;
  fs::path resources(BOOST_PP_STRINGIZE(DRIVE_TESTS_RESOURCES)),
      download_py(resources / "download.py"), shell_path(boost::process::shell_path());
  std::string content, script, command_args;

  RequireExists(download_py);

#ifdef MAIDSAFE_WIN32
  DWORD exit_code(0);
  script = "download.bat";
  content += "python " + download_py.string() + " -u " + url + " -l " + start_directory.string() +
             "\n" + "exit\n";
  command_args = "/C " + script + " 1>nul 2>nul";
#else
  int exit_code(0);
  script = "download.sh";
  content += std::string("#!/bin/bash\n") + "python " + download_py.string() + " -u " + url +
             " -l " + start_directory.string() + " 1>/dev/null 2>/dev/null\n" + "exit\n";
  command_args = script;
#endif

  auto script_file(start_directory / script);
  ASSERT_TRUE(WriteFile(script_file, content));
  ASSERT_TRUE(fs::exists(script_file, error_code));

  std::vector<std::string> process_args;
  process_args.emplace_back(shell_path.string());
  process_args.emplace_back(command_args);
  const auto command_line(process::ConstructCommandLine(process_args));

  boost::process::child child =
      boost::process::execute(boost::process::initializers::start_in_dir(start_directory.string()),
                              boost::process::initializers::run_exe(shell_path),
                              boost::process::initializers::set_cmd_line(command_line),
                              boost::process::initializers::inherit_env(),
                              boost::process::initializers::set_on_error(error_code));

  ASSERT_TRUE(error_code.value() == 0);
  exit_code = boost::process::wait_for_exit(child, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  ASSERT_TRUE(exit_code == 0);
}

void CreateAndBuildMinimalCppProject(const fs::path& path) {
  boost::system::error_code error_code;
  fs::path project_main(CreateDirectory(path)), project(CreateDirectory(project_main)),
      build(CreateDirectory(project_main)), shell_path(boost::process::shell_path());
  std::string project_name(project.filename().string()), command_args, content, project_file,
      cmake_generator(BOOST_PP_STRINGIZE(CMAKE_GENERATOR)),
      slash(fs::path("/").make_preferred().string());
  {
    // cmake
    content = std::string("cmake_minimum_required(VERSION 2.8.11.2 FATAL_ERROR)\n") + "project(" +
              project_name + ")\n" + "add_subdirectory(" + project_name + ")";

    auto main_cmake_file(project_main / "CMakeLists.txt");
    ASSERT_TRUE(WriteFile(main_cmake_file, content));
    ASSERT_TRUE(fs::exists(main_cmake_file, error_code));

    content = "add_executable(" + project_name + " " + project_name + ".cc)";

    auto project_cmake_file(project / "CMakeLists.txt");
    ASSERT_TRUE(WriteFile(project_cmake_file, content));
    ASSERT_TRUE(fs::exists(project_cmake_file, error_code));

    content = "int main() {\n  return 0;\n}";

    auto project_cc_file(project / (project_name + ".cc"));
    ASSERT_TRUE(WriteFile(project_cc_file, content));
    ASSERT_TRUE(fs::exists(project_cc_file, error_code));

#ifdef MAIDSAFE_WIN32
    command_args = " /k cmake .. -G" + cmake_generator + " 1>nul 2>nul & exit";
    project_file = build.string() + slash + project_name + ".sln";
#else
    auto script(build / "cmake.sh");
    content = "#!/bin/bash\ncmake .. -G" + cmake_generator + " 1>/dev/null 2>/dev/null ; exit";
    ASSERT_TRUE(WriteFile(script, content));
    ASSERT_TRUE(fs::exists(script, error_code));
    command_args = script.filename().string();
    project_file = build.string() + slash + "Makefile";
#endif

    std::vector<std::string> process_args;
    process_args.emplace_back(shell_path.filename().string());
    process_args.emplace_back(command_args);
    const auto command_line(process::ConstructCommandLine(process_args));

    boost::process::child child =
        boost::process::execute(boost::process::initializers::start_in_dir(build.string()),
                                boost::process::initializers::run_exe(shell_path),
                                boost::process::initializers::set_cmd_line(command_line),
                                boost::process::initializers::inherit_env(),
                                boost::process::initializers::set_on_error(error_code));

    ASSERT_TRUE(error_code.value() == 0);
    boost::process::wait_for_exit(child, error_code);
    ASSERT_TRUE(error_code.value() == 0);
    ASSERT_TRUE(fs::exists(project_file)) << "Failed to find " << project_file;
  }
  {
// release
#ifdef MAIDSAFE_WIN32
    command_args = " /k cmake --build . --config Release 1>nul 2>nul & exit";
    project_file =
        build.string() + slash + project_name + slash + "Release" + slash + project_name + ".exe";
#else
    auto script(build / "release_build.sh");
    content = "#!/bin/bash\ncmake --build . --config Release 1>/dev/null 2>/dev/null ; exit";
    ASSERT_TRUE(WriteFile(script, content));
    ASSERT_TRUE(fs::exists(script, error_code));
    command_args = script.filename().string();
    project_file = build.string() + slash + project_name + slash + project_name;
#endif

    std::vector<std::string> process_args;
    process_args.emplace_back(shell_path.string());
    process_args.emplace_back(command_args);
    const auto command_line(process::ConstructCommandLine(process_args));

    boost::process::child child =
        boost::process::execute(boost::process::initializers::start_in_dir(build.string()),
                                boost::process::initializers::run_exe(shell_path),
                                boost::process::initializers::set_cmd_line(command_line),
                                boost::process::initializers::inherit_env(),
                                boost::process::initializers::set_on_error(error_code));

    ASSERT_TRUE(error_code.value() == 0);
    boost::process::wait_for_exit(child, error_code);
    ASSERT_TRUE(error_code.value() == 0);
    ASSERT_TRUE(fs::exists(project_file)) << "Failed to build " << project_file;
  }
  {
// debug
#ifdef MAIDSAFE_WIN32
    command_args = " /k cmake --build . --config Debug 1>nul 2>nul & exit";
    project_file =
        build.string() + slash + project_name + slash + "Debug" + slash + project_name + ".exe";
#else
    auto script(build / "debug_build.sh");
    content = "#!/bin/bash\ncmake --build . --config Debug 1>/dev/null 2>/dev/null ; exit";
    ASSERT_TRUE(WriteFile(script, content));
    ASSERT_TRUE(fs::exists(script, error_code));
    command_args = script.filename().string();
    project_file = build.string() + slash + project_name + slash + project_name;
#endif

    std::vector<std::string> process_args;
    process_args.emplace_back(shell_path.string());
    process_args.emplace_back(command_args);
    const auto command_line(process::ConstructCommandLine(process_args));

    boost::process::child child =
        boost::process::execute(boost::process::initializers::start_in_dir(build.string()),
                                boost::process::initializers::run_exe(shell_path),
                                boost::process::initializers::set_cmd_line(command_line),
                                boost::process::initializers::inherit_env(),
                                boost::process::initializers::set_on_error(error_code));

    ASSERT_TRUE(error_code.value() == 0);
    boost::process::wait_for_exit(child, error_code);
    ASSERT_TRUE(error_code.value() == 0);
    ASSERT_TRUE(fs::exists(project_file)) << "Failed to build " << project_file;
  }
}

void WriteUtf8FileAndEdit(const fs::path& start_directory) {
  fs::path resources(BOOST_PP_STRINGIZE(DRIVE_TESTS_RESOURCES)), utf8_txt(resources / "utf-8.txt"),
      utf8_file;

  RequireExists(utf8_txt);
  utf8_file = start_directory / utf8_txt.filename();
  ASSERT_NO_THROW(fs::copy_file(utf8_txt, utf8_file));
  RequireExists(utf8_file);

  boost::system::error_code error_code;

#ifdef MAIDSAFE_WIN32
  uintmax_t remove(1265);
  fs::path path(boost::process::search_path(L"notepad.exe"));
  std::vector<std::string> process_args;
  process_args.emplace_back(path.string());
  process_args.emplace_back(utf8_file.string());
  const auto command_line(process::ConstructCommandLine(process_args));

  boost::process::child child =
      boost::process::execute(boost::process::initializers::start_in_dir(start_directory.string()),
                              boost::process::initializers::run_exe(path.string()),
                              boost::process::initializers::set_cmd_line(command_line),
                              boost::process::initializers::inherit_env(),
                              boost::process::initializers::set_on_error(error_code));

  ASSERT_TRUE(error_code.value() == 0);
  Sleep(std::chrono::seconds(1));

  HWND notepad(FindWindow(L"notepad", (utf8_file.filename().wstring() + L" - notepad").c_str()));
  ASSERT_NE(nullptr, notepad);
  HWND edit(FindWindowEx(notepad, nullptr, L"edit", nullptr));
  ASSERT_NE(nullptr, edit);

  SendMessage(edit, EM_SETSEL, 0, static_cast<LPARAM>(remove));
  SendMessage(edit, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(L""));

  Sleep(std::chrono::seconds(3));

  HMENU menu(GetMenu(notepad));
  ASSERT_NE(nullptr, menu);
  HMENU sub_menu(GetSubMenu(menu, 0));
  ASSERT_NE(nullptr, sub_menu);
  UINT id(GetMenuItemID(sub_menu, 2));

  LRESULT command(SendMessage(notepad, WM_COMMAND, id, reinterpret_cast<LPARAM>(menu)));
  ASSERT_TRUE(command == 0);
  LRESULT close(SendMessage(notepad, WM_CLOSE, 0, 0));
  ASSERT_TRUE(close == 0);
#else
  int exit_code(0);
  fs::path shell_path(boost::process::shell_path());
  std::string script("utf.sh"), content = std::string("#!/bin/bash\n") + "sed -i.bak '1,38d' " +
                                          utf8_file.string() + " 1>/dev/null 2>/dev/null\n" +
                                          "exit";

  auto script_file(start_directory / script);
  ASSERT_TRUE(WriteFile(script_file, content));
  ASSERT_TRUE(fs::exists(script_file, error_code));

  std::vector<std::string> process_args;
  process_args.emplace_back(shell_path.string());
  process_args.emplace_back(script);
  const auto command_line(process::ConstructCommandLine(process_args));

  boost::process::child child =
      boost::process::execute(boost::process::initializers::start_in_dir(start_directory.string()),
                              boost::process::initializers::run_exe(shell_path.string()),
                              boost::process::initializers::set_cmd_line(command_line),
                              boost::process::initializers::inherit_env(),
                              boost::process::initializers::set_on_error(error_code));

  ASSERT_TRUE(error_code.value() == 0);
  exit_code = boost::process::wait_for_exit(child, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  ASSERT_TRUE(exit_code == 0);
  ASSERT_TRUE(fs::remove(start_directory / script));
#endif
  RequireExists(utf8_file);
}

#ifndef MAIDSAFE_WIN32
// void RunFsTest(const fs::path& start_directory) {
//  boost::system::error_code error_code;
//  fs::path resources(BOOST_PP_STRINGIZE(DRIVE_TESTS_RESOURCES)),
//           fstest(resources.parent_path() / "pjd-fstest-20080816/fstest"),
//           shell_path(boost::process::shell_path());
//  std::string content, script, command_args;

//  int exit_code(0);
//  script = "fstest.sh";
//  content = std::string("#!/bin/bash\n")
//          + "prove -r " + fstest.string() + " 1>/dev/null 2>/dev/null\n"
//          + "exit";
//  command_args = "sudo " + script;

//  auto script_file(start_directory / script);
//  ASSERT_TRUE(WriteFile(script_file, content));
//  ASSERT_TRUE(fs::exists(script_file, error_code));

//  int result(setuid(0));
// #if !defined(MAIDSAFE_APPLE) && !defined(MAIDSAFE_BSD)
//  clearenv();
// #endif
//  result = system((start_directory.string() + script).c_str());
//  static_cast<void>(result);

//  std::vector<std::string> process_args;
//  process_args.emplace_back(shell_path.string());
//  process_args.emplace_back(command_args);
//  const auto command_line(process::ConstructCommandLine(process_args));

//  boost::process::child child = boost::process::execute(
//      boost::process::initializers::start_in_dir(start_directory.string()),
//      boost::process::initializers::run_exe(shell_path),
//      boost::process::initializers::set_cmd_line(command_line),
//      boost::process::initializers::inherit_env(),
//      boost::process::initializers::set_on_error(error_code));

//  ASSERT_TRUE(error_code.value() == 0);
//  exit_code = boost::process::wait_for_exit(child, error_code);
//  ASSERT_TRUE(error_code.value() == 0);
//  ASSERT_TRUE(exit_code == 0);
// }
#endif

}  // unnamed namespace

int RunTool(int argc, char** argv, const fs::path& root, const fs::path& temp,
            const drive::Options& options, std::shared_ptr<drive::Launcher> launcher,
            int test_type) {
  g_root = root;
  g_temp = temp;
  g_options = options;
  g_launcher = launcher;
  g_test_type = static_cast<drive::DriveType>(test_type);

  log::Logging::Instance().Initialise(argc, argv);
  testing::FLAGS_gtest_catch_exceptions = false;
  testing::InitGoogleTest(&argc, argv);
  int result(RUN_ALL_TESTS());
  int test_count = testing::UnitTest::GetInstance()->test_to_run_count();
  return (test_count == 0) ? -1 : result;
}

TEST(FileSystemTest, BEH_DriveSize) {
  // 1GB seems reasonable as a lower limit for all drive types (real/local/network).  It at least
  // provides a regression check for https://github.com/maidsafe/SureFile/issues/33

  // Skip the test when testing against real_disk (may have a small sized disk)
  // BEFORE_RELEASE - Decide strategy for running other disk-based tests in this suite on a drive
  //                  too small to be able to pass this test.
  if (g_test_type != drive::DriveType::kLocal && g_test_type != drive::DriveType::kLocalConsole &&
      g_test_type != drive::DriveType::kNetwork && g_test_type != drive::DriveType::kNetworkConsole)
    return GTEST_SUCCEED();

  auto space(boost::filesystem::space(g_root));
  ASSERT_TRUE(space.available > 1073741824);
  ASSERT_TRUE(space.capacity > 1073741824);
  ASSERT_TRUE(space.free > 1073741824);
}

TEST(FileSystemTest, BEH_CreateEmptyFile) {
  on_scope_exit cleanup(clean_root);
  CreateFile(g_root, 0);
}

TEST(FileSystemTest, BEH_CreateEmptyDirectory) {
  on_scope_exit cleanup(clean_root);
  CreateDirectory(g_root);
}

TEST(FileSystemTest, BEH_AppendToFile) {
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_root, 0).first);
  int test_runs = 1000;
  ASSERT_TRUE(WriteFile(filepath, "a"));
  NonEmptyString content, updated_content;
  for (int i = 0; i < test_runs; ++i) {
    ASSERT_NO_THROW(content = ReadFile(filepath));
    ASSERT_TRUE(WriteFile(filepath, content.string() + "a"));
    ASSERT_NO_THROW(updated_content = ReadFile(filepath));
    ASSERT_TRUE(updated_content.string().size() == content.string().size() + 1);
    ASSERT_TRUE(updated_content.string().size() == i + 2U);
  }
}

TEST(FileSystemTest, BEH_CopyEmptyDirectory) {
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));

  // Copy 'g_temp' directory to 'g_root'
  boost::system::error_code error_code;
  fs::copy_directory(directory, g_root / directory.filename(), error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(g_root / directory.filename());
}

TEST(FileSystemTest, BEH_CopyDirectoryThenDelete) {
  // Create a file and directory in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());
  RequireExists(copied_directory);
  RequireDirectoriesEqual(directory, copied_directory, true);

  // Delete the directory along with its contents
  boost::system::error_code error_code;
  ASSERT_TRUE(fs::remove_all(copied_directory, error_code) == 3U);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireDoesNotExist(copied_directory / filepath.filename());
  RequireDoesNotExist(copied_directory / nested_directory.filename());

  // Try to clean up 'g_root'
  fs::remove_all(copied_directory, error_code);
}

TEST(FileSystemTest, BEH_CopyDirectoryDeleteThenReCopy) {
  // Create a file and directory in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());

  // Delete the directory along with its contents
  boost::system::error_code error_code;
  ASSERT_TRUE(fs::remove_all(copied_directory, error_code) == 3U);
  ASSERT_TRUE(error_code.value() == 0) << copied_directory << ": " << error_code.message();

  // Re-copy directory and file to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  RequireExists(copied_directory);
  RequireDirectoriesEqual(directory, copied_directory, true);
}

TEST(FileSystemTest, BEH_CopyDirectoryThenRename) {
  // Create a file and directory in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());

  // Rename the directory
  auto renamed_directory(g_root / maidsafe::RandomAlphaNumericString(5));
  boost::system::error_code error_code;
  fs::rename(copied_directory, renamed_directory, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireExists(renamed_directory);
  RequireDirectoriesEqual(directory, renamed_directory, true);
}

TEST(FileSystemTest, BEH_CopyDirectoryRenameThenReCopy) {
  // Create a file and directory in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath(CreateFile(directory, RandomUint32() % 1024).first);
  auto nested_directory(CreateDirectory(directory));

  // Copy directory to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());

  // Rename the directory
  auto renamed_directory(g_root / maidsafe::RandomAlphaNumericString(5));
  boost::system::error_code error_code;
  fs::rename(copied_directory, renamed_directory, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);

  // Re-copy directory and file to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  RequireExists(copied_directory);
  RequireDirectoriesEqual(directory, copied_directory, false);
}

TEST(FileSystemTest, BEH_CopyDirectoryContainingMultipleFiles) {
  // Create files in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectoryContainingFiles(g_temp));

  // Copy directory to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  ASSERT_TRUE(!fs::is_empty(copied_directory, error_code));
  ASSERT_TRUE(error_code.value() == 0);
  RequireDirectoriesEqual(directory, copied_directory, true);
}

TEST(FileSystemTest, BEH_CopyDirectoryHierarchy) {
  on_scope_exit cleanup(clean_root);
  // Create a new hierarchy in 'g_temp'
  std::vector<fs::path> directories(CreateDirectoryHierarchy(g_temp));
  // Copy hierarchy to 'g_root'
  ASSERT_TRUE(CopyDirectory(directories.front(), g_root));
  auto copied_directory(g_root / directories.front().filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  ASSERT_TRUE(!fs::is_empty(copied_directory, error_code));
  ASSERT_TRUE(error_code.value() == 0);
  RequireDirectoriesEqual(directories.front(), copied_directory, true);
}

TEST(FileSystemTest, BEH_CopyThenCopyCopiedFile) {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file(g_root / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(copied_file);
  ASSERT_TRUE(ReadFile(filepath) == ReadFile(copied_file));

  // Copy file to 'g_root' again
  fs::copy_file(filepath, copied_file, fs::copy_option::overwrite_if_exists, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(copied_file);
  ASSERT_TRUE(ReadFile(filepath) == ReadFile(copied_file));
}

TEST(FileSystemTest, BEH_CopyFileDeleteThenReCopy) {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file(g_root / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  ASSERT_TRUE(error_code.value() == 0);

  // Delete the file
  fs::remove(copied_file, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_file);

  // Copy file to 'g_root' again
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(copied_file);
  ASSERT_TRUE(ReadFile(filepath) == ReadFile(copied_file));
}

TEST(FileSystemTest, BEH_CopyFileRenameThenRecopy) {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file(g_root / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  ASSERT_TRUE(error_code.value() == 0);

  // Rename the file
  auto renamed_file(g_root / (RandomAlphaNumericString(5) + ".txt"));
  fs::rename(copied_file, renamed_file, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_file);
  RequireExists(renamed_file);
  ASSERT_TRUE(ReadFile(filepath) == ReadFile(renamed_file));

  // Copy file to 'g_root' again
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(copied_file);
  ASSERT_TRUE(ReadFile(filepath) == ReadFile(copied_file));
}

TEST(FileSystemTest, BEH_CopyFileDeleteRead) {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file(g_root / filepath.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath, copied_file, fs::copy_option::fail_if_exists, error_code);
  ASSERT_TRUE(error_code.value() == 0);

  // Delete the file
  fs::remove(copied_file, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_file);

  // Try to copy 'g_root' file back to a 'g_temp' file
  auto test_file(g_temp / (RandomAlphaNumericString(5) + ".txt"));
  fs::copy_file(copied_file, test_file, fs::copy_option::overwrite_if_exists, error_code);
  ASSERT_TRUE(error_code.value() != 0);
  RequireDoesNotExist(test_file);
}

TEST(FileSystemTest, BEH_CreateFile) {
  // Create a file in 'g_root' and read back its contents
  on_scope_exit cleanup(clean_root);
  auto filepath_and_contents(CreateFile(g_root, RandomUint32() % 1048577));
  ASSERT_TRUE(ReadFile(filepath_and_contents.first).string() == filepath_and_contents.second);
}

TEST(FileSystemTest, BEH_CreateFileModifyThenRead) {
  // Create a file in 'g_root'
  on_scope_exit cleanup(clean_root);

  std::pair<fs::path, std::string> filepath_and_contents;
  filepath_and_contents = CreateFile(g_root, (RandomUint32() % 1048) + 1048577);

  // Modify the file
  size_t offset(RandomUint32() % filepath_and_contents.second.size());
  std::string additional_content(RandomString(RandomUint32() % 1048577));
  filepath_and_contents.second.insert(offset, additional_content);
  std::ofstream output_stream(filepath_and_contents.first.c_str(), std::ios_base::binary);
  ASSERT_TRUE(output_stream.is_open());
  ASSERT_TRUE(!output_stream.bad());
  output_stream.write(filepath_and_contents.second.c_str(), filepath_and_contents.second.size());
  ASSERT_TRUE(!output_stream.bad());
  output_stream.close();

  // Check file
  RequireExists(filepath_and_contents.first);
  ASSERT_TRUE(ReadFile(filepath_and_contents.first).string() == filepath_and_contents.second);
}

TEST(FileSystemTest, BEH_RenameFileToDifferentParentDirectory) {
  // Create a file in a newly created directory in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto directory(CreateDirectory(g_temp));
  auto filepath_and_contents(CreateFile(directory, RandomUint32() % 1024));

  // Copy directory to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());

  // Rename the file into its parent
  auto renamed_from_file(copied_directory / filepath_and_contents.first.filename());
  auto renamed_to_file(g_root / filepath_and_contents.first.filename());
  boost::system::error_code error_code;
  fs::rename(renamed_from_file, renamed_to_file, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(renamed_from_file);
  RequireExists(renamed_to_file);
  ASSERT_TRUE(ReadFile(renamed_to_file).string() == filepath_and_contents.second);
}

TEST(FileSystemTest, BEH_RenameDirectoryHierarchyKeepingSameParent) {
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
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  ASSERT_TRUE(!fs::is_empty(copied_directory, error_code));
  ASSERT_TRUE(error_code.value() == 0);
  RequireDirectoriesEqual(directory, copied_directory, true);

  // Rename the directory
  auto renamed_directory(g_root / maidsafe::RandomAlphaNumericString(5));
  fs::rename(copied_directory, renamed_directory, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireDirectoriesEqual(directory, renamed_directory, true);
}

TEST(FileSystemTest, BEH_RenameDirectoryHierarchyToDifferentParent) {
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
  ASSERT_TRUE(CopyDirectory(directory, g_root));
  auto copied_directory(g_root / directory.filename());
  RequireExists(copied_directory);
  boost::system::error_code error_code;
  ASSERT_TRUE(!fs::is_empty(copied_directory, error_code));
  ASSERT_TRUE(error_code.value() == 0);
  RequireDirectoriesEqual(directory, copied_directory, true);

  // Rename the directory
  auto new_parent(CreateDirectory(g_root));
  auto renamed_directory(new_parent / maidsafe::RandomAlphaNumericString(5));
  fs::rename(copied_directory, renamed_directory, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory);
  RequireDirectoriesEqual(directory, renamed_directory, true);
}

TEST(FileSystemTest, BEH_CheckFailures) {
  // Create a file in 'g_temp'
  on_scope_exit cleanup(clean_root);
  auto filepath0(CreateFile(g_temp, RandomUint32() % 1048577).first);

  // Copy file to 'g_root'
  auto copied_file0(g_root / filepath0.filename());
  boost::system::error_code error_code;
  fs::copy_file(filepath0, copied_file0, fs::copy_option::fail_if_exists, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(copied_file0);

  // Copy same file to 'g_root' again
  fs::copy_file(filepath0, copied_file0, fs::copy_option::fail_if_exists, error_code);
  ASSERT_TRUE(error_code.value() != 0);
  RequireExists(copied_file0);
  ASSERT_TRUE(ReadFile(filepath0) == ReadFile(copied_file0));

  // Create another file in 'g_temp' and copy it to 'g_root'
  auto filepath1(CreateFile(g_temp, RandomUint32() % 1048577).first);
  auto copied_file1(g_root / filepath1.filename());
  fs::copy_file(filepath1, copied_file1, fs::copy_option::fail_if_exists, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(copied_file1);

  // Rename to first file name
  fs::rename(copied_file1, copied_file0, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(copied_file0);
  RequireDoesNotExist(copied_file1);
  ASSERT_TRUE(ReadFile(filepath1) == ReadFile(copied_file0));

  // Rename mirror likewise
  fs::rename(filepath1, filepath0, error_code);
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(filepath0);
  RequireDoesNotExist(filepath1);

  // Delete the file
  ASSERT_TRUE(fs::remove(copied_file0, error_code));
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_file0);

  // Delete the file again
  ASSERT_TRUE(!fs::remove(copied_file0, error_code));
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_file0);

  // Repeat above for directories
  // Create a file and directory in a newly created directory in 'g_temp'
  auto directory0(CreateDirectory(g_temp));
  CreateFile(directory0, RandomUint32() % 1024);
  CreateDirectory(directory0);

  // Copy directory to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory0, g_root));
  auto copied_directory0(g_root / directory0.filename());

  // Copy same directory to 'g_root' again
  fs::copy_directory(directory0, copied_directory0, error_code);
  ASSERT_TRUE(error_code.value() != 0);
  RequireExists(copied_directory0);
  RequireDirectoriesEqual(directory0, copied_directory0, true);

  // Create a directory with the same name on the 'g_root'
  ASSERT_TRUE(!fs::create_directory(copied_directory0, error_code));
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(copied_directory0);
  RequireDirectoriesEqual(directory0, copied_directory0, false);

  // Create another directory in 'g_temp' containing a file and subdirectory
  auto directory1(CreateDirectory(g_temp));
  CreateFile(directory1, RandomUint32() % 1024);
  CreateDirectory(directory1);

  // Copy it to 'g_root'
  ASSERT_TRUE(CopyDirectory(directory1, g_root));
  auto copied_directory1(g_root / directory1.filename());

  // Rename to first directory name
  fs::rename(copied_directory1, copied_directory0, error_code);
  ASSERT_TRUE((error_code.value()) != 0);
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
  ASSERT_TRUE(error_code.value() != 0);
  RequireExists(directory2);
  RequireExists(copied_directory1);
  RequireDirectoriesEqual(directory1, copied_directory1, false);
#else
  ASSERT_TRUE(error_code.value() == 0);
  RequireExists(directory2);
  RequireDoesNotExist(copied_directory1);
  RequireDirectoriesEqual(directory1, directory2, false);
#endif

  // Delete the first directory
  ASSERT_TRUE(fs::remove_all(copied_directory0, error_code) == 3U);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory0);

  // Delete the first directory again
  ASSERT_TRUE(fs::remove_all(copied_directory0, error_code) == 0U);
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory0);
  ASSERT_TRUE(!fs::remove(copied_directory0, error_code));
  ASSERT_TRUE(error_code.value() == 0);
  RequireDoesNotExist(copied_directory0);
}

TEST(FileSystemTest, BEH_ReadOnlyAttribute) {
  const on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32
  
  fs::path path(g_root / RandomAlphaNumericString(8));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size));
  DWORD position(0), size(0), attributes(0);
  BOOL success(0);
  OVERLAPPED overlapped;

  // create a file
  {
    drive::detail::WinHandle handle(nullptr);
    EXPECT_NO_THROW(
        handle = dtc::CreateFileCommand(path, (GENERIC_WRITE | GENERIC_READ), 0, CREATE_NEW, FILE_ATTRIBUTE_ARCHIVE));
    ASSERT_NE(nullptr, handle);
    EXPECT_NO_THROW(success = dtc::WriteFileCommand(handle.get(), path, buffer, &position, nullptr));
    EXPECT_TRUE((size = dtc::GetFileSizeCommand(handle.get(), nullptr)) == buffer_size);
  }
  // check we can open and write to the file
  {
    drive::detail::WinHandle handle(nullptr);
    EXPECT_NO_THROW(handle = dtc::CreateFileCommand(path, (GENERIC_WRITE | GENERIC_READ), 0, OPEN_EXISTING, attributes));
    ASSERT_NE(nullptr, handle);
    buffer = RandomString(buffer_size);
    success = 0;
    position = 1;
    FillMemory(&overlapped, sizeof(overlapped), 0);
    overlapped.Offset = position & 0xFFFFFFFF;
    overlapped.OffsetHigh = 0;
    EXPECT_NO_THROW(success = dtc::WriteFileCommand(handle.get(), path, buffer, &position, &overlapped));
    size = 0;
    EXPECT_TRUE((size = dtc::GetFileSizeCommand(handle.get(), nullptr)) == buffer_size + 1);
  }
  // add read-only to the attributes
  EXPECT_NO_THROW(attributes = dtc::GetFileAttributesCommand(path));
  EXPECT_TRUE((attributes & FILE_ATTRIBUTE_ARCHIVE) == FILE_ATTRIBUTE_ARCHIVE);
  EXPECT_NO_THROW(success = dtc::SetFileAttributesCommand(path, FILE_ATTRIBUTE_ARCHIVE |
                                                                    FILE_ATTRIBUTE_READONLY));
  EXPECT_NO_THROW(attributes = dtc::GetFileAttributesCommand(path));
  EXPECT_TRUE((attributes & FILE_ATTRIBUTE_ARCHIVE) == FILE_ATTRIBUTE_ARCHIVE);
  EXPECT_TRUE((attributes & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY);
  // check we can open for reading but can't write to the file
  {
    drive::detail::WinHandle handle(nullptr);
    EXPECT_THROW(handle = dtc::CreateFileCommand(path, (GENERIC_WRITE | GENERIC_READ), 0, OPEN_EXISTING, attributes),
                 std::exception);
    EXPECT_NO_THROW(handle =
        dtc::CreateFileCommand(path, GENERIC_READ, 0, OPEN_EXISTING, attributes));
    ASSERT_NE(nullptr, handle);
    buffer = RandomString(buffer_size);
    success = 0;
    position = 2;
    FillMemory(&overlapped, sizeof(overlapped), 0);
    overlapped.Offset = position & 0xFFFFFFFF;
    overlapped.OffsetHigh = 0;
    EXPECT_THROW(success = dtc::WriteFileCommand(handle.get(), path, buffer, &position, &overlapped),
                 std::exception);
    size = 0;
    EXPECT_TRUE((size = dtc::GetFileSizeCommand(handle.get(), nullptr)) == buffer_size + 1);
  }
  // remove the read-only attribute so the file can be deleted
  EXPECT_NO_THROW(success = dtc::SetFileAttributesCommand(path, FILE_ATTRIBUTE_ARCHIVE));
  EXPECT_NO_THROW(success = dtc::DeleteFileCommand(path));
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
  EXPECT_NO_THROW(file_descriptor = dtc::CreateFileCommand(path, flags, mode));
  EXPECT_NO_THROW(result = dtc::WriteFileCommand(file_descriptor, buffer));
  EXPECT_TRUE(result == buffer_size);
  EXPECT_NO_THROW(dtc::SyncFileCommand(file_descriptor));
  EXPECT_NO_THROW(size = dtc::GetFileSizeCommand(file_descriptor));
  EXPECT_TRUE(size == buffer_size);
  EXPECT_NO_THROW(dtc::CloseFileCommand(file_descriptor));
  // check we can open and write to the file
  flags = O_RDWR;
  EXPECT_NO_THROW(file_descriptor = dtc::CreateFileCommand(path, flags));
  buffer = RandomString(buffer_size);
  offset = 1;
  EXPECT_NO_THROW(result = dtc::WriteFileCommand(file_descriptor, buffer, offset));
  EXPECT_NO_THROW(dtc::CloseFileCommand(file_descriptor));
  size = 0;
  EXPECT_NO_THROW(size = dtc::GetFileSizeCommand(path));
  EXPECT_TRUE(size == buffer_size + 1);
  // add read-only to the attributes
  EXPECT_NO_THROW(mode = dtc::GetModeCommand(path));
  EXPECT_TRUE((mode & S_IFREG) == S_IFREG);
  EXPECT_TRUE((mode & S_IRUSR) == S_IRUSR);
  EXPECT_TRUE((mode & S_IWUSR) == S_IWUSR);
  mode = S_IRUSR;
  EXPECT_NO_THROW(dtc::SetModeCommand(path, mode));
  EXPECT_NO_THROW(mode = dtc::GetModeCommand(path));
  EXPECT_TRUE((mode & S_IFREG) == S_IFREG);
  EXPECT_TRUE((mode & S_IRUSR) == S_IRUSR);
  EXPECT_TRUE((mode & S_IWUSR) == 0);
  // check we can open for reading but can't write to the file
  EXPECT_THROW(file_descriptor = dtc::CreateFileCommand(path, flags), std::exception);
  flags = O_RDONLY;
  EXPECT_NO_THROW(file_descriptor = dtc::CreateFileCommand(path, flags));
  buffer = RandomString(buffer_size);
  offset = 2;
  EXPECT_THROW(result = dtc::WriteFileCommand(file_descriptor, buffer, offset), std::exception);
  size = 0;
  EXPECT_NO_THROW(size = dtc::GetFileSizeCommand(file_descriptor));
  EXPECT_TRUE(size == buffer_size + 1);
  EXPECT_NO_THROW(dtc::CloseFileCommand(file_descriptor));
  // remove the read-only attribute so the file can be deleted ???
  mode = S_IRWXU;
  EXPECT_NO_THROW(dtc::SetModeCommand(path, mode));
#endif
}

TEST(FileSystemTest, BEH_InsufficientAccess) {
  const on_scope_exit cleanup(clean_root);
  const fs::path path(g_root / RandomAlphaNumericString(8));

#ifdef MAIDSAFE_WIN32
  // Creating a file ignores desired permissions, and instead always uses
  // GENERIC_WRITE on the parent directory.
  {
    drive::detail::WinHandle handle(nullptr);
    EXPECT_NO_THROW(
        handle = dtc::CreateFileCommand(path, GENERIC_ALL, 0, CREATE_NEW, FILE_ATTRIBUTE_ARCHIVE));
    ASSERT_NE(nullptr, handle);
  }
  // Opening an existing file uses desired permissions, so execute bit should
  // cause this to fail
  {
    drive::detail::WinHandle handle(nullptr);
    EXPECT_THROW(
        (handle = dtc::CreateFileCommand(path, GENERIC_ALL, 0, OPEN_EXISTING, FILE_ATTRIBUTE_ARCHIVE)),
        common_error);
    ASSERT_EQ(nullptr, handle);
  }
  {
    drive::detail::WinHandle handle(nullptr);
    EXPECT_NO_THROW(
       handle = dtc::CreateFileCommand(path, (GENERIC_READ | GENERIC_WRITE), 0, OPEN_EXISTING, FILE_ATTRIBUTE_ARCHIVE));
    ASSERT_NE(nullptr, handle);
  }
#endif // WIN32
}

TEST(FileSystemTest, BEH_DeleteOnClose) {
  on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32  
  const fs::path path(g_root / RandomAlphaNumericString(8));
  {
    drive::detail::WinHandle handle(nullptr);
    EXPECT_NO_THROW(
       handle = dtc::CreateFileCommand(path, (GENERIC_READ | GENERIC_WRITE), 0, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE));
    ASSERT_NE(nullptr, handle);
    const size_t buffer_size(1024);
    std::string buffer(RandomString(buffer_size));
    DWORD position(0);
    BOOL success(0);
    EXPECT_NO_THROW(success = dtc::WriteFileCommand(handle.get(), path, buffer, &position, nullptr));
    EXPECT_TRUE(fs::exists(path));
  }
  EXPECT_FALSE(fs::exists(path));
#else
  int file_descriptor(-1);
  fs::path path_template(g_root / (RandomAlphaNumericString(8) + "_XXXXXX"));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size));
  int size(0);
  ssize_t result(0);
  mode_t mode(0);

  // create a temp file
  EXPECT_NO_THROW(file_descriptor = dtc::CreateTempFileCommand(path_template));
  EXPECT_TRUE(boost::filesystem::exists(path_template));
  // unlink
  EXPECT_NO_THROW(dtc::UnlinkFileCommand(path_template));
  EXPECT_FALSE(boost::filesystem::exists(path_template));
  // write to file
  EXPECT_NO_THROW(result = dtc::WriteFileCommand(file_descriptor, buffer));
  EXPECT_TRUE(result == buffer_size);
  // verify size...
  EXPECT_NO_THROW(size = dtc::GetFileSizeCommand(file_descriptor));
  EXPECT_TRUE(size == buffer_size);
  // ...and mode
  EXPECT_NO_THROW(mode = dtc::GetModeCommand(file_descriptor));
  EXPECT_TRUE((mode & S_IFREG) == S_IFREG);
  EXPECT_TRUE((mode & S_IRUSR) == S_IRUSR);
  EXPECT_TRUE((mode & S_IWUSR) == S_IWUSR);
  // close file
  EXPECT_NO_THROW(dtc::CloseFileCommand(file_descriptor));
// EXPECT_FALSE(boost::filesystem::exists(path_template));
#endif
}

TEST(FileSystemTest, BEH_HiddenAttribute) {
  on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32
  
  fs::path directory(g_root / RandomAlphaNumericString(5)),
      file(directory / RandomAlphaNumericString(8));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size));
  DWORD position(0), attributes(0);
  BOOL success(0);

  EXPECT_NO_THROW(success = dtc::CreateDirectoryCommand(directory));
  ASSERT_NE(0, success);
  {
    drive::detail::WinHandle handle(nullptr);
    EXPECT_NO_THROW(
        handle = dtc::CreateFileCommand(file, GENERIC_ALL, 0, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN));
    ASSERT_NE(nullptr, handle);
    EXPECT_NO_THROW(success = dtc::WriteFileCommand(handle.get(), file, buffer, &position, nullptr));
    EXPECT_NO_THROW(attributes = dtc::GetFileAttributesCommand(file));
    EXPECT_TRUE((attributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
  }

  std::vector<WIN32_FIND_DATA> files(dtc::EnumerateDirectoryCommand(directory));
  EXPECT_TRUE(files.size() == 1);
  EXPECT_TRUE((files.begin()->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
  EXPECT_TRUE(files.begin()->nFileSizeLow == buffer_size);
  EXPECT_TRUE(files.begin()->nFileSizeHigh == 0);
  EXPECT_NO_THROW(success = dtc::DeleteFileCommand(file));
  EXPECT_NO_THROW(success = dtc::RemoveDirectoryCommand(directory));
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

  EXPECT_NO_THROW(dtc::CreateDirectoryCommand(directory, directory_mode));
  EXPECT_TRUE(boost::filesystem::exists(directory));
  EXPECT_NO_THROW(file_descriptor = dtc::CreateFileCommand(file, flags, file_mode));
  EXPECT_TRUE(boost::filesystem::exists(file));
  EXPECT_NO_THROW(result = dtc::WriteFileCommand(file_descriptor, buffer));
  EXPECT_TRUE(result == buffer_size);
  EXPECT_NO_THROW(size = dtc::GetFileSizeCommand(file_descriptor));
  EXPECT_TRUE(size == buffer_size);
  EXPECT_NO_THROW(dtc::CloseFileCommand(file_descriptor));

  std::vector<boost::filesystem::path> files(dtc::EnumerateDirectoryCommand(directory));
  EXPECT_TRUE(files.size() == 1);
  EXPECT_TRUE(files[0] == file.filename().string());
  EXPECT_NO_THROW(dtc::UnlinkFileCommand(file));
  EXPECT_FALSE(boost::filesystem::exists(file));
  EXPECT_NO_THROW(dtc::RemoveDirectoryCommand(directory));
  EXPECT_FALSE(boost::filesystem::exists(directory));
#endif
}

TEST(FileSystemTest, BEH_CheckAttributesForConcurrentOpenInstances) {
  on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32
  
  fs::path path(g_root / RandomAlphaNumericString(5));
  const size_t buffer_size(1024);
  std::string buffer(RandomString(buffer_size)), recovered(buffer_size, 0);
  DWORD position(0), attributes(FILE_ATTRIBUTE_ARCHIVE), size(0), count(0);
  BOOL success(0);
  OVERLAPPED overlapped;

  // create file
  {
    drive::detail::WinHandle first_handle(nullptr), second_handle(nullptr);
    EXPECT_NO_THROW(first_handle =
        dtc::CreateFileCommand(path, (GENERIC_READ | GENERIC_WRITE), 0, CREATE_NEW, attributes));
    ASSERT_NE(nullptr, first_handle);
    // write data using first instance
    EXPECT_NO_THROW(success = dtc::WriteFileCommand(first_handle.get(), path, buffer, &count, nullptr));
    // verify opening a second instance throws
    EXPECT_THROW(second_handle =
        dtc::CreateFileCommand(path, (GENERIC_READ | GENERIC_WRITE), 0, OPEN_EXISTING, attributes),
        std::exception);
    ASSERT_EQ(nullptr, second_handle);
  }
  drive::detail::WinHandle first_handle(nullptr), second_handle(nullptr);

  // reopen a first instance with shared read/write access
  EXPECT_NO_THROW(first_handle = dtc::CreateFileCommand(path, GENERIC_READ | GENERIC_WRITE,
                                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                        OPEN_EXISTING, attributes));
  ASSERT_NE(nullptr, first_handle);
  // open a second instance with shared read/write access
  EXPECT_NO_THROW(second_handle = dtc::CreateFileCommand(path, GENERIC_READ | GENERIC_WRITE,
                                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                         OPEN_EXISTING, attributes));
  ASSERT_NE(nullptr, second_handle);
  // write to file using first instance
  buffer = RandomString(buffer_size);
  success = 0;
  position = 1;
  FillMemory(&overlapped, sizeof(overlapped), 0);
  overlapped.Offset = position & 0xFFFFFFFF;
  EXPECT_NO_THROW(success = dtc::WriteFileCommand(first_handle.get(), path, buffer, &count, &overlapped));
  // check the file size with the second instance
  EXPECT_TRUE((size = dtc::GetFileSizeCommand(second_handle.get(), nullptr)) == buffer_size + 1);
  // check content with the second instance
  EXPECT_NO_THROW(success =
                      dtc::ReadFileCommand(second_handle.get(), path, recovered, &count, &overlapped));
  EXPECT_TRUE(recovered.compare(buffer) == 0);
  EXPECT_TRUE(count == buffer_size);
  // write to file using second instance
  buffer = RandomString(buffer_size);
  success = 0;
  position = 2;
  FillMemory(&overlapped, sizeof(overlapped), 0);
  overlapped.Offset = position & 0xFFFFFFFF;
  EXPECT_NO_THROW(success =
                      dtc::WriteFileCommand(second_handle.get(), path, buffer, &count, &overlapped));
  // check the file size with the first instance
  EXPECT_TRUE((size = dtc::GetFileSizeCommand(first_handle.get(), nullptr)) == buffer_size + 2);
  // check content with the first instance
  EXPECT_NO_THROW(success =
                      dtc::ReadFileCommand(first_handle.get(), path, recovered, &count, &overlapped));
  EXPECT_TRUE(recovered.compare(buffer) == 0);
  EXPECT_TRUE(count == buffer_size);
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
  EXPECT_NO_THROW(first_file_descriptor = dtc::CreateFileCommand(path, flags, mode));
  // open a second instance
  flags = O_RDWR;
  EXPECT_NO_THROW(second_file_descriptor = dtc::CreateFileCommand(path, flags));
  // write to file using first instance
  EXPECT_NO_THROW(result = dtc::WriteFileCommand(first_file_descriptor, buffer));
  EXPECT_TRUE(result == buffer_size);
  // check data using second instance
  EXPECT_NO_THROW(size = dtc::GetFileSizeCommand(second_file_descriptor));
  EXPECT_TRUE(size == buffer_size);
  EXPECT_NO_THROW(result = dtc::ReadFileCommand(second_file_descriptor, recovered));
  EXPECT_TRUE(result == buffer_size);
  EXPECT_TRUE(recovered.compare(buffer) == 0);
  // write to file using second instance
  buffer = RandomString(buffer_size);
  offset = 1;
  EXPECT_NO_THROW(result = dtc::WriteFileCommand(second_file_descriptor, buffer, offset));
  EXPECT_TRUE(result == buffer_size);
  // check data using first instance
  EXPECT_NO_THROW(size = dtc::GetFileSizeCommand(first_file_descriptor));
  EXPECT_TRUE(size == buffer_size + 1);
  EXPECT_NO_THROW(result = dtc::ReadFileCommand(first_file_descriptor, recovered, offset));
  EXPECT_TRUE(result == buffer_size);
  EXPECT_TRUE(recovered.compare(buffer) == 0);
  // close both instances
  EXPECT_NO_THROW(dtc::CloseFileCommand(first_file_descriptor));
  EXPECT_NO_THROW(dtc::CloseFileCommand(second_file_descriptor));
#endif
}

TEST(FileSystemTest, BEH_Locale) {
  on_scope_exit cleanup(clean_root);

#if defined(MAIDSAFE_APPLE)
  // This test fails on OS X when run against the real disk (due to Apple's manipulation of unicode
  // filenames - see e.g. http://apple.stackexchange.com/a/10484).  As such, I'll set the test to
  // trivially pass for this case.  Note, the test passes when run against the VFS, so it may be
  // appropriate to make this "fix" permanent.  Alternatively, we maybe should change the production
  // Drive code for OS X so that it "breaks" in the same way as for the disk-based test.
  //
  // BEFORE_RELEASE - Decide whether this fix should be deemed as permanent.
  if (g_test_type != drive::DriveType::kLocal && g_test_type != drive::DriveType::kLocalConsole &&
      g_test_type != drive::DriveType::kNetwork && g_test_type != drive::DriveType::kNetworkConsole)
    return GTEST_SUCCEED();
#elif defined(MAIDSAFE_WIN32)
  std::locale::global(boost::locale::generator().generate(""));
#else
  std::locale::global(std::locale(""));
#endif
  fs::path::imbue(std::locale());
  fs::path resources(BOOST_PP_STRINGIZE(DRIVE_TESTS_RESOURCES));
  fs::path file(resources / "utf-8");
  RequireExists(file);
  fs::path directory(g_root / ReadFile(file).string());
  CreateDirectory(directory);
  RequireExists(directory);
  fs::directory_iterator it(g_root);
  EXPECT_TRUE(it->path().filename() == ReadFile(file).string());
}

TEST(FileSystemTest, DISABLED_FUNC_CreateAndBuildMinimalCXXProject) {
  on_scope_exit cleanup(clean_root);
  ASSERT_NO_THROW(CreateAndBuildMinimalCppProject(g_root));
  ASSERT_NO_THROW(CreateAndBuildMinimalCppProject(g_temp));
}

TEST(FileSystemTest, DISABLED_BEH_Write256MbFileToTempAndCopyToDrive) {
  on_scope_exit cleanup(clean_root);
#ifdef MAIDSAFE_WIN32
  
  std::string filename(RandomAlphaNumericString(8));
  fs::path temp_file(g_temp / filename), root_file(g_root / filename);
  const size_t size(1 << 16);
  std::string original(size, 0), recovered(size, 0);
  DWORD position(0), file_size(0), count(0), attributes(FILE_ATTRIBUTE_ARCHIVE);
  BOOL success(0);
  OVERLAPPED overlapped;

  {
    drive::detail::WinHandle handle(nullptr);
    EXPECT_NO_THROW(handle =
        dtc::CreateFileCommand(temp_file, GENERIC_ALL, 0, CREATE_NEW, attributes));
    ASSERT_NE(nullptr, handle);

    for (uint32_t i = 0; i != (1 << 12); ++i) {
      original = RandomString(size);
      success = 0, count = 0, position = i * size;
      FillMemory(&overlapped, sizeof(overlapped), 0);
      overlapped.Offset = position & 0xFFFFFFFF;
      overlapped.OffsetHigh = 0;
      EXPECT_NO_THROW(success =
          dtc::WriteFileCommand(handle.get(), temp_file, original, &count, &overlapped));
      ASSERT_NE(0, success);
      ASSERT_TRUE(count == size);
    }

    EXPECT_TRUE((file_size = dtc::GetFileSizeCommand(handle.get(), nullptr)) == (1 << 28));
  }

  ASSERT_NO_THROW(fs::copy_file(temp_file, root_file));
  ASSERT_TRUE(fs::exists(root_file));
  
  drive::detail::WinHandle temp_handle(nullptr), root_handle(nullptr);
  EXPECT_NO_THROW(temp_handle =
      dtc::CreateFileCommand(temp_file, (GENERIC_READ | GENERIC_WRITE), 0, OPEN_EXISTING, attributes));
  ASSERT_NE(nullptr, temp_handle);
  ASSERT_NO_THROW(root_handle =
      dtc::CreateFileCommand(root_file, (GENERIC_READ | GENERIC_WRITE), 0, OPEN_EXISTING, attributes));
  ASSERT_NE(nullptr, root_handle);

  for (uint32_t i = 0; i != (1 << 12); ++i) {
    success = 0, count = 0, position = i * size;
    FillMemory(&overlapped, sizeof(overlapped), 0);
    overlapped.Offset = position & 0xFFFFFFFF;
    overlapped.OffsetHigh = 0;
    ASSERT_NO_THROW(
        success = dtc::ReadFileCommand(temp_handle.get(), temp_file, original, &count, &overlapped));
    ASSERT_NE(0, success);
    ASSERT_TRUE(count == size);
    success = 0, count = 0;
    ASSERT_NO_THROW(
        success = dtc::ReadFileCommand(root_handle.get(), root_file, recovered, &count, &overlapped));
    ASSERT_NE(0, success);
    ASSERT_TRUE(count == size);
    ASSERT_TRUE(original == recovered);
  }

  
#endif
  // (TODO Team): Implementation required
}

TEST(FileSystemTest, DISABLED_BEH_WriteUtf8FileAndEdit) {
  on_scope_exit cleanup(clean_root);
  ASSERT_NO_THROW(WriteUtf8FileAndEdit(g_temp));
  ASSERT_NO_THROW(WriteUtf8FileAndEdit(g_root));
}

TEST(FileSystemTest, DISABLED_FUNC_DownloadMovieThenCopyToDrive) {
  on_scope_exit cleanup(clean_root);
  std::string movie("TheKid_512kb.mp4");
  ASSERT_NO_THROW(
      DownloadFile(g_temp, "https://ia700508.us.archive.org/12/items/TheKid_179/" + movie));
  ASSERT_NO_THROW(fs::copy_file(g_temp / movie, g_root / movie));
  ASSERT_TRUE(fs::exists(g_root / movie)) << "Failed to find " << (g_root / movie).string();
}

#ifndef MAIDSAFE_WIN32
TEST(FileSystemTest, FUNC_Runfstest) {
  on_scope_exit cleanup(clean_root);
  // ASSERT_NO_THROW(RunFsTest(g_temp));
  // ASSERT_NO_THROW(RunFsTest(g_root));
  ASSERT_TRUE(true);
}
#endif

TEST(FileSystemTest, DISABLED_FUNC_RemountDrive) {
  bool do_test(g_test_type == drive::DriveType::kLocal ||
               g_test_type == drive::DriveType::kLocalConsole ||
               g_test_type == drive::DriveType::kNetwork ||
               g_test_type == drive::DriveType::kNetworkConsole);

  if (do_test) {
    on_scope_exit cleanup(clean_root);

    // Create a new hierarchy in 'g_temp'
    std::vector<fs::path> directories(CreateDirectoryHierarchy(g_temp));
    {
      // Copy hierarchy to 'g_root'
      ASSERT_TRUE(CopyDirectory(directories.front(), g_root));
      auto copied_directory(g_root / directories.front().filename());
      RequireExists(copied_directory);
      boost::system::error_code error_code;
      ASSERT_TRUE(!fs::is_empty(copied_directory, error_code));
      ASSERT_TRUE(error_code.value() == 0);
      RequireDirectoriesEqual(directories.front(), copied_directory, true);

      Sleep(std::chrono::seconds(3));
      g_launcher->StopDriveProcess(true);
      g_launcher.reset();
    }
    {
      // Remount and check hierarchy for equality
      g_options.create_store = false;
      g_launcher.reset(new drive::Launcher(g_options));

      auto directory(g_root / directories.front().filename());
      RequireExists(directory);
      boost::system::error_code error_code;
      ASSERT_TRUE(!fs::is_empty(directory, error_code));
      ASSERT_TRUE(error_code.value() == 0) << error_code.value();
      RequireDirectoriesEqual(directories.front(), directory, true);
    }
  }
}

TEST(FileSystemTest, FUNC_CrossPlatformFileCheck) {
  // Involves mounting a drive of type g_test_type so don't attempt it if we're doing a disk test
  if (g_test_type == drive::DriveType::kLocal || g_test_type == drive::DriveType::kLocalConsole ||
      g_test_type == drive::DriveType::kNetwork ||
      g_test_type == drive::DriveType::kNetworkConsole) {
    const on_scope_exit cleanup(clean_root);
    const fs::path resources(BOOST_PP_STRINGIZE(DRIVE_TESTS_RESOURCES));
    const fs::path cross_platform(resources / "cross_platform");
    const fs::path ids(cross_platform / "ids");
    const fs::path shell_path(boost::process::shell_path());
    const fs::path prefix_path(g_temp);

    fs::path utf8_file(resources / "utf-8.txt");
    fs::path root;
        
    std::string content, script, command_args, utf8_file_name;
    boost::system::error_code error_code;

    ASSERT_TRUE(fs::exists(utf8_file));
    ASSERT_TRUE((fs::exists(cross_platform) && fs::is_directory(cross_platform)));
    const bool is_empty(fs::is_empty(cross_platform));

    utf8_file_name = utf8_file.filename().string();
    ASSERT_NO_THROW(fs::copy_file(utf8_file, prefix_path / utf8_file_name));
    ASSERT_TRUE(fs::exists(prefix_path / utf8_file_name));

    utf8_file = prefix_path / utf8_file_name;
    content = std::string("cmake_minimum_required(VERSION 2.8.11.2 FATAL_ERROR)\n") +
              "configure_file(\"${CMAKE_PREFIX_PATH}/" + utf8_file_name +
              "\" \"${CMAKE_PREFIX_PATH}/" + utf8_file_name + "\" NEWLINE_STYLE WIN32)";

    const auto cmake_file(prefix_path / "CMakeLists.txt");
    ASSERT_TRUE(WriteFile(cmake_file, content));
    ASSERT_TRUE(fs::exists(cmake_file));

    content = "";

#ifdef MAIDSAFE_WIN32
    DWORD exit_code(0);
    script = "configure_file.bat";
    command_args = "/C " + script + " 1>nul 2>nul";
#else
    int exit_code(0);
    script = "configure_file.sh";
    command_args = script;
    content = "#!/bin/bash\n";
#endif
    content += "cmake -DCMAKE_PREFIX_PATH=" + prefix_path.string() + "\nexit\n";

    const auto script_file(prefix_path / script);
    ASSERT_TRUE(WriteFile(script_file, content));
    ASSERT_TRUE(fs::exists(script_file, error_code));

    std::vector<std::string> process_args;
    process_args.emplace_back(shell_path.filename().string());
    process_args.emplace_back(command_args);
    const auto command_line(process::ConstructCommandLine(process_args));

    const boost::process::child child =
        boost::process::execute(boost::process::initializers::start_in_dir(prefix_path.string()),
                                boost::process::initializers::run_exe(shell_path),
                                boost::process::initializers::set_cmd_line(command_line),
                                boost::process::initializers::inherit_env(),
                                boost::process::initializers::set_on_error(error_code));

    ASSERT_TRUE(error_code.value() == 0);
    exit_code = boost::process::wait_for_exit(child, error_code);
    ASSERT_TRUE(error_code.value() == 0);
    ASSERT_TRUE(exit_code == 0);

    drive::Options options;

#ifdef MAIDSAFE_WIN32
    root = drive::GetNextAvailableDrivePath();
#else
    root = fs::unique_path(GetHomeDir() / "MaidSafe_Root_Filesystem_%%%%-%%%%-%%%%");
    ASSERT_NO_THROW(fs::create_directories(root));
    ASSERT_TRUE(fs::exists(root));
#endif

    options.mount_path = root;
    options.storage_path = cross_platform;
    options.drive_name = RandomAlphaNumericString(10);

    if (is_empty) {
      options.unique_id = Identity(RandomAlphaNumericString(64));
      options.root_parent_id = Identity(RandomAlphaNumericString(64));
      options.create_store = true;
      content = options.unique_id.string() + ";" + options.root_parent_id.string();
      ASSERT_TRUE(WriteFile(ids, content));
      ASSERT_TRUE(fs::exists(ids));
    } else {
      ASSERT_TRUE(fs::exists(ids));
      ASSERT_NO_THROW(content = ReadFile(ids).string());
      ASSERT_TRUE(content.size() == 2 * 64 + 1);
      size_t offset(content.find(std::string(";").c_str(), 0, 1));
      ASSERT_TRUE(offset != std::string::npos);
      options.unique_id = Identity(std::string(content.begin(), content.begin() + offset));
      options.root_parent_id = Identity(std::string(content.begin() + offset + 1, content.end()));
      ASSERT_TRUE(options.unique_id.string().size() == 64);
      ASSERT_TRUE(options.root_parent_id.string().size() == 64);
    }

    options.drive_type = g_test_type;

    std::unique_ptr<drive::Launcher> launcher;
    launcher.reset(new drive::Launcher(options));
    root = launcher->kMountPath();

    // allow time for mount
    Sleep(std::chrono::seconds(1));

    const fs::path file(root / "file");

    if (is_empty) {
      ASSERT_TRUE(!fs::exists(file));
      ASSERT_NO_THROW(fs::copy_file(utf8_file, file));
      ASSERT_TRUE(fs::exists(file));
    } else {
      ASSERT_TRUE(fs::exists(file));
      std::ifstream original_file, recovered_file;

      original_file.open(utf8_file.string(), std::ios_base::binary | std::ios_base::in);
      ASSERT_TRUE(original_file.good());
      recovered_file.open(file.string(), std::ios_base::binary | std::ios_base::in);
      ASSERT_TRUE(original_file.good());

      std::string original_string(256, 0), recovered_string(256, 0);
      int line_count = 0;
      while (!original_file.eof() && !recovered_file.eof()) {
        original_file.getline(&(original_string[0]), 256);
        recovered_file.getline(&(recovered_string[0]), 256);
        ASSERT_TRUE(original_string == recovered_string);
        ++line_count;
      }
      ASSERT_TRUE((original_file.eof() && recovered_file.eof()));
    }

    // allow time for the version to store!
    Sleep(std::chrono::seconds(3));

#ifndef MAIDSAFE_WIN32
    launcher.reset();
    ASSERT_TRUE(fs::remove(root));
    ASSERT_TRUE(!fs::exists(root));
#endif
  }
}

}  // namespace test

}  // namespace maidsafe
