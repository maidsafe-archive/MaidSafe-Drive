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
#include "maidsafe/common/error.h"
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

fs::path g_root, g_temp, g_storage_path;
std::unique_ptr<bp::child> g_child;
std::string g_error_message;
int g_return_code(0);
enum class TestType { kDisk, kLocal, kNetwork } g_test_type;

void CreateDir(const fs::path& dir) {
  boost::system::error_code error_code;
  if (!fs::create_directories(dir, error_code) || error_code) {
    g_error_message = std::string("Failed to create ") + dir.string() + ": " + error_code.message();
    g_return_code = error_code.value();
    ThrowError(CommonErrors::filesystem_io_error);
  }
}

void SetUpTempDirectory() {
  boost::system::error_code error_code;
  g_temp = fs::unique_path(fs::temp_directory_path() / "MaidSafe_Test_Filesystem_%%%%-%%%%-%%%%");
  CreateDir(g_temp);
  LOG(kInfo) << "Created temp directory " << g_temp;
}

void RemoveTempDirectory() {
  boost::system::error_code error_code;
  if (fs::remove_all(g_temp, error_code) == 0 || error_code)
    LOG(kWarning) << "Failed to remove g_temp " << g_temp << ": " << error_code.message();
  else
    LOG(kInfo) << "Removed " << g_temp;
}

void SetUpStorageDirectory() {
  boost::system::error_code error_code;
  g_storage_path =
      fs::unique_path(fs::temp_directory_path() / "MaidSafe_Test_ChunkStore_%%%%-%%%%-%%%%");
  CreateDir(g_storage_path);
  LOG(kInfo) << "Created g_storage_path " << g_storage_path;
}

void RemoveStorageDirectory() {
  boost::system::error_code error_code;
  if (fs::remove_all(g_storage_path, error_code) == 0 || error_code) {
    LOG(kWarning) << "Failed to remove g_storage_path " << g_storage_path << ": "
                  << error_code.message();
  } else {
    LOG(kInfo) << "Removed " << g_storage_path;
  }
}

void SetUpRootDirectory(fs::path base_dir) {
#ifdef MAIDSAFE_WIN32
  if (g_test_type == TestType::kDisk) {
    g_root = fs::unique_path(base_dir / "MaidSafe_Root_Filesystem_%%%%-%%%%-%%%%");
    CreateDir(g_root);
  } else {
    g_root = drive::GetNextAvailableDrivePath();
  }
#else
  g_root = fs::unique_path(base_dir / "MaidSafe_Root_Filesystem_%%%%-%%%%-%%%%");
  CreateDir(g_root);

#endif
  LOG(kInfo) << "Created root directory " << g_root;
}

void RemoveRootDirectory() {
  boost::system::error_code error_code;
  fs::remove_all(g_root, error_code);
  if (error_code)
    LOG(kWarning) << "Failed to remove root directory " << g_root << ": " << error_code.message();
  else
    LOG(kInfo) << "Removed " << g_root;
}

std::function<void()> remove_test_dirs([] {
  RemoveTempDirectory();
  if (fs::exists(g_storage_path))
    RemoveStorageDirectory();
  RemoveRootDirectory();
});

po::options_description CommandLineOptions() {
  po::options_description command_line_options(
      "Filesystem Tool Options:\nYou must pass exactly one of '--disk', '--local' or '--network'");
  command_line_options.add_options()("help,h", "Show help message.")
                                    ("disk", "Perform all tests/benchmarks on native hard disk.")
                                    ("local", "Perform all tests/benchmarks on local VFS.")
                                    ("network", "Perform all tests/benchmarks on network VFS.");
  return command_line_options;
}

po::variables_map ParseAllOptions(int& argc, char* argv[],
                                  std::vector<std::string>& unused_options) {
  auto command_line_options(CommandLineOptions());
  po::parsed_options parsed(po::command_line_parser(unused_options).options(command_line_options).
                            allow_unregistered().run());

  po::variables_map variables_map;
  po::store(parsed, variables_map);
  po::notify(variables_map);

  // Strip used command line options before passing to RunTool function.
  unused_options = po::collect_unrecognized(parsed.options, po::include_positional);
  argc = static_cast<int>(unused_options.size() + 1);
  int position(1);
  for (const auto& unused_option : unused_options)
    std::strcpy(argv[position++], unused_option.c_str());  // NOLINT

  return variables_map;
}

void HandleHelp(const po::variables_map& variables_map) {
  if (variables_map.count("help")) {
    std::ostringstream stream;
    stream << CommandLineOptions() << "\n\n";
    g_error_message = stream.str();
    g_return_code = 0;
    throw MakeError(CommonErrors::success);
  }
}

void GetTestType(const po::variables_map& variables_map) {
  int option_count(0);
  if (variables_map.count("disk")) {
    ++option_count;
    g_test_type = TestType::kDisk;
  }
  if (variables_map.count("local")) {
    ++option_count;
    g_test_type = TestType::kLocal;
  }
  if (variables_map.count("network")) {
    ++option_count;
    g_test_type = TestType::kNetwork;
  }
  if (option_count != 1) {
    std::ostringstream stream;
    stream << "You must pass exactly one of '--disk', '--local' or '--network'.  "
           << "For all options, run '--help'\n\n";
    g_error_message = stream.str();
    g_return_code = 1;
    ThrowError(CommonErrors::invalid_parameter);
  }
}

void SignalChildProcessAndWaitToExit() {
  if (!g_child)
    return;
#ifdef MAIDSAFE_WIN32
  SetConsoleCtrlHandler([](DWORD control_type)->BOOL {
        LOG(kInfo) << "Received console control signal " << control_type << ".";
        return (control_type == CTRL_BREAK_EVENT ? TRUE : FALSE);
      }, TRUE);
  assert(g_child->proc_info.dwProcessId);
  GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, g_child->proc_info.dwProcessId);
#else
  assert(g_child->pid);
  int result(kill(g_child->pid, SIGINT));
  if (result) {
    LOG(kError) << "Sending signal to drive process ID " << g_child->pid << " returned result "
                << result;
  } else {
    LOG(kInfo) << "Sending signal to drive process ID " << g_child->pid << " returned result "
               << result;
  }
#endif
  boost::system::error_code error_code;
  auto exit_code = bp::wait_for_exit(*g_child, error_code);
  if (error_code)
    LOG(kError) << "Error waiting for child to exit: " << error_code.message();
  else
    LOG(kInfo) << "Drive process has completed with exit code " << exit_code;
}

void PrepareDisk() {
  SetUpTempDirectory();
  SetUpRootDirectory(fs::temp_directory_path());
}

void PrepareLocalVfs() {
  SetUpTempDirectory();
  SetUpStorageDirectory();
  SetUpRootDirectory(GetHomeDir());

  std::vector<std::string> shared_memory_args;
  shared_memory_args.push_back(g_root.string());
  shared_memory_args.push_back(g_storage_path.string());
  shared_memory_args.push_back(RandomString(64));  // unique_id
  shared_memory_args.push_back(RandomString(64));  // root_parent_id
  shared_memory_args.push_back(RandomAlphaNumericString(10));  // drive_name
  shared_memory_args.push_back("1");  // Create flag set to "true".

  auto shared_memory_name(RandomAlphaNumericString(32));
  ipc::CreateSharedMemory(shared_memory_name, shared_memory_args);

  // Set up boost::process args
  std::vector<std::string> process_args;
  const auto kExePath(process::GetLocalDriveLocation());
  process_args.push_back(kExePath);
  std::string shared_memory_opt("--shared_memory " + shared_memory_name);
  process_args.push_back(shared_memory_opt);
  process_args.push_back("--log_* I --log_colour_mode 2");
  const auto kCommandLine(process::ConstructCommandLine(process_args));

  boost::system::error_code error_code;
  g_child.reset(new bp::child(bp::execute(bp::initializers::run_exe(kExePath),
                                          bp::initializers::set_cmd_line(kCommandLine),
                                          bp::initializers::set_on_error(error_code))));
#ifdef MAIDSAFE_WIN32
  g_root /= boost::filesystem::path("/").make_preferred();
  while (!fs::exists(g_root, error_code))
    Sleep(std::chrono::milliseconds(100));
#else
  Sleep(std::chrono::seconds(3));
#endif
}

void PrepareNetworkVfs() {
  g_error_message = "Network test is unimplemented just now.";
  g_return_code = 10;
  ThrowError(CommonErrors::invalid_parameter);
}

void PrepareTest() {
  switch (g_test_type) {
    case TestType::kDisk:
      PrepareDisk();
      break;
    case TestType::kLocal:
      PrepareLocalVfs();
      break;
    case TestType::kNetwork:
      PrepareNetworkVfs();
      break;
    default:
      break;
  }
}

}  // unnamed namespace

}  // namespace test

}  // namespace maidsafe

int main(int argc, char** argv) {
  try {
    auto unused_options(maidsafe::log::Logging::Instance().Initialise(argc, argv));
    auto variables_map(maidsafe::test::ParseAllOptions(argc, argv, unused_options));
    maidsafe::test::HandleHelp(variables_map);
    maidsafe::test::GetTestType(variables_map);

    maidsafe::on_scope_exit cleanup_on_exit(maidsafe::test::remove_test_dirs);
    maidsafe::test::PrepareTest();

    auto tests_result(maidsafe::test::RunTool(argc, argv, maidsafe::test::g_root,
                                              maidsafe::test::g_temp));
    maidsafe::test::SignalChildProcessAndWaitToExit();
    return tests_result;
  }
  catch (const std::exception& e) {
    if (!maidsafe::test::g_error_message.empty()) {
      std::cout << maidsafe::test::g_error_message;
      return maidsafe::test::g_return_code;
    }
    LOG(kError) << "Exception: " << e.what();
  }
  catch (...) {
    LOG(kError) << "Exception of unknown type!";
  }
  return 64;
}
