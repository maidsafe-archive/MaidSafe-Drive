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


#ifndef MAIDSAFE_WIN32
#include <signal.h>
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

#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/ipc.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/process.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/drive/drive.h"

#include "local_drive_location.h"  // NOLINT
#include "network_drive_location.h"  // NOLINT

namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace bp = boost::process;


namespace maidsafe {

namespace test {

int RunTool(int argc, char** argv, const fs::path& root, const fs::path& temp);

namespace {

fs::path root_, temp_, storage_path_;
std::string unique_id_, root_parent_id_, drive_name_, shared_memory_name_;
std::unique_ptr<bp::child> child_;

std::function<void()> clean_root([] {
  boost::system::error_code error_code;
  fs::directory_iterator end;
  for (fs::directory_iterator directory_itr(root_); directory_itr != end; ++directory_itr)
    fs::remove_all(*directory_itr, error_code);
});

bool SetUpTempDirectory() {
  boost::system::error_code error_code;
  temp_ = fs::unique_path(fs::temp_directory_path() / "MaidSafe_Test_Filesystem_%%%%-%%%%-%%%%");
  if (!fs::create_directories(temp_, error_code) || error_code) {
    LOG(kWarning) << "Failed to create temp directory " << temp_ << ": " << error_code.message();
    return false;
  }
  LOG(kInfo) << "Created temp directory " << temp_;
  return true;
}

void RemoveTempDirectory() {
  boost::system::error_code error_code;
  if (fs::remove_all(temp_, error_code) == 0 || error_code)
    LOG(kWarning) << "Failed to remove temp_ " << temp_ << ": " << error_code.message();
  else
    LOG(kInfo) << "Removed " << temp_;
}

bool SetUpStorageDirectory() {
  boost::system::error_code error_code;
  storage_path_ =
      fs::unique_path(fs::temp_directory_path() / "MaidSafe_Test_ChunkStore%%%%-%%%%-%%%%");
  if (!fs::create_directories(storage_path_, error_code) || error_code) {
    LOG(kWarning) << "Failed to create storage_path_ " << storage_path_ << ": "
                  << error_code.message();
    return false;
  }
  LOG(kInfo) << "Created storage_path_ " << storage_path_;
  return true;
}

void RemoveStorageDirectory() {
  boost::system::error_code error_code;
  if (fs::remove_all(storage_path_, error_code) == 0 || error_code) {
    LOG(kWarning) << "Failed to remove storage_path_ " << storage_path_ << ": "
                  << error_code.message();
  } else {
    LOG(kInfo) << "Removed " << storage_path_;
  }
}

bool SetUpRootDirectory(fs::path base_dir) {
#ifdef MAIDSAFE_WIN32
  static_cast<void>(base_dir);
  root_ = drive::GetNextAvailableDrivePath();
#else
  root_ = fs::unique_path(base_dir / "MaidSafe_Root_Filesystem_%%%%-%%%%-%%%%");
  boost::system::error_code error_code;
  if (!fs::create_directories(root_, error_code)) {
    LOG(kWarning) << "Failed to create root directory " << root_ << ": " << error_code.message();
    return false;
  }
#endif
  LOG(kInfo) << "Created root directory " << root_;
  return true;
}

void RemoveRootDirectory() {
  boost::system::error_code error_code;
  fs::remove_all(root_, error_code);
  if (error_code)
    LOG(kWarning) << "Failed to remove root directory " << root_ << ": " << error_code.message();
  else
    LOG(kInfo) << "Removed " << root_;
}

}  // unnamed namespace

}  // namespace test

}  // namespace maidsafe

int main(int argc, char** argv) {
  auto unused_options(maidsafe::log::Logging::Instance().Initialise(argc, argv));
  auto tests_result(0);
  // Handle passing path to test root via command line
  po::options_description filesystem_options(
      "Filesystem Test Options /n Only a single option will be performed per test run");

  filesystem_options.add_options()("help,h", "Show help message.")
                                  ("disk", "Perform all tests on native hard disk")
                                  ("local", "Perform all tests on local vfs ")
                                  ("network", "Perform all tests on network vfs ");
  po::parsed_options parsed(po::command_line_parser(unused_options).options(filesystem_options).
                            allow_unregistered().run());

  po::variables_map variables_map;
  po::store(parsed, variables_map);
  po::notify(variables_map);

  // Strip used command line options before passing to Catch
  unused_options = po::collect_unrecognized(parsed.options, po::include_positional);
  argc = static_cast<int>(unused_options.size() + 1);
  int position(1);
  for (const auto& unused_option : unused_options)
    std::strcpy(argv[position++], unused_option.c_str());  // NOLINT

  boost::system::error_code error_code;
  if (variables_map.count("help")) {
    std::cout << filesystem_options << '\n';
    ++argc;
    std::strcpy(argv[position], "--help");  // NOLINT
    return 0;
  } else if (variables_map.count("disk")) {
    if (!maidsafe::test::SetUpRootDirectory(fs::temp_directory_path()))
      return -1;
    if (!maidsafe::test::SetUpTempDirectory())
      return -2;
  } else if (variables_map.count("local")) {
    maidsafe::test::shared_memory_name_ = maidsafe::RandomAlphaNumericString(32);
    maidsafe::test::unique_id_ = maidsafe::RandomString(64);
    maidsafe::test::root_parent_id_ = maidsafe::RandomString(64);
    maidsafe::test::drive_name_ = maidsafe::RandomAlphaNumericString(10);
    if (!maidsafe::test::SetUpRootDirectory(maidsafe::GetHomeDir()))
      return -1;
    if (!maidsafe::test::SetUpTempDirectory())
      return -2;
    if (!maidsafe::test::SetUpStorageDirectory())
      return -3;

    std::vector<std::string> shared_memory_args;
    shared_memory_args.push_back(maidsafe::test::root_.string());
    shared_memory_args.push_back(maidsafe::test::storage_path_.string());
    shared_memory_args.push_back(maidsafe::test::unique_id_);
    shared_memory_args.push_back(maidsafe::test::root_parent_id_);
    shared_memory_args.push_back(maidsafe::test::drive_name_);
    shared_memory_args.push_back("1");  // Create flag set to "true".
    maidsafe::ipc::CreateSharedMemory(maidsafe::test::shared_memory_name_, shared_memory_args);

    // Set up boost::process args
    std::vector<std::string> process_args;
    const auto kExePath(maidsafe::process::GetLocalDriveLocation());
    process_args.push_back(kExePath);
    std::string shared_memory_opt("--shared_memory " + maidsafe::test::shared_memory_name_);
    process_args.push_back(shared_memory_opt);
    process_args.push_back("--log_* I --log_colour_mode 2");
    const auto kCommandLine(maidsafe::process::ConstructCommandLine(process_args));

    maidsafe::test::child_.reset(
        new bp::child(bp::execute(bp::initializers::run_exe(kExePath),
                                  bp::initializers::set_cmd_line(kCommandLine),
                                  bp::initializers::set_on_error(error_code))));
    maidsafe::Sleep(std::chrono::seconds(3));

#ifdef MAIDSAFE_WIN32
    maidsafe::test::root_ /= boost::filesystem::path("/").make_preferred();
#endif
  } else if (variables_map.count("network")) {
  } else {
    std::cout << "You must run with '--disk', '--local', or '--network'.  To see all options, "
              << "run with '--help'.\n\n";
    return 1;
  }

  tests_result = maidsafe::test::RunTool(argc, argv, maidsafe::test::root_, maidsafe::test::temp_);

#ifdef MAIDSAFE_WIN32
  SetConsoleCtrlHandler([](DWORD control_type)->BOOL {
        LOG(kInfo) << "Received console control signal " << control_type << ".";
        return (control_type == CTRL_BREAK_EVENT ? TRUE : FALSE);
      }, TRUE);
  assert(maidsafe::test::child_->proc_info.dwProcessId);
  GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, maidsafe::test::child_->proc_info.dwProcessId);
#else
  assert(maidsafe::test::child_->pid);
  int result(kill(maidsafe::test::child_->pid, SIGINT));
  if (result) {
    LOG(kError) << "Sending signal to drive process ID " << maidsafe::test::child_->pid
                << " returned result " << result;
  } else {
    LOG(kInfo) << "Sending signal to drive process ID " << maidsafe::test::child_->pid
               << " returned result " << result;
  }
#endif
  auto exit_code = bp::wait_for_exit(*maidsafe::test::child_, error_code);
  if (error_code)
    LOG(kError) << "Error waiting for child to exit: " << error_code.message();
  else
    LOG(kInfo) << "Drive process has completed with exit code " << exit_code;

  maidsafe::test::RemoveTempDirectory();
  if (fs::exists(maidsafe::test::storage_path_))
    maidsafe::test::RemoveStorageDirectory();
  maidsafe::test::RemoveRootDirectory();

  return tests_result;
}
