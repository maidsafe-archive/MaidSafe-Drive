/*  Copyright 2014 MaidSafe.net limited

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
#include <csignal>
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
#include "boost/system/error_code.hpp"

#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/error.h"
#include "maidsafe/common/ipc.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/drive/drive.h"
#include "maidsafe/drive/tools/launcher.h"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace maidsafe {

namespace test {

namespace {

#ifdef _MSC_VER
// This function is needed to avoid use of po::bool_switch causing MSVC warning C4505:
// 'boost::program_options::typed_value<bool>::name' : unreferenced local function has been removed.
void UseUnreferenced() {
  auto dummy = po::typed_value<bool>(nullptr);
  static_cast<void>(dummy);
}
#endif

fs::path g_root, g_temp, g_storage;
std::unique_ptr<drive::Launcher> g_launcher;
std::string g_error_message;
int g_return_code(0);
bool g_enable_vfs_logging(false);
bool g_running(true);
std::shared_ptr<passport::Anmaid> g_anmaid;
std::shared_ptr<passport::Anpmid> g_anpmid;

void CreateDir(const fs::path& dir) {
  boost::system::error_code error_code;
  if (!fs::create_directories(dir, error_code) || error_code) {
    g_error_message = std::string("Failed to create ") + dir.string() + ": " + error_code.message();
    g_return_code = error_code.value();
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
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

void SetUpRootDirectory(fs::path base_dir) {
#ifdef MAIDSAFE_WIN32
  g_root = drive::GetNextAvailableDrivePath();
#else
  g_root = fs::unique_path(base_dir / "MaidSafe_Root_Filesystem_%%%%-%%%%-%%%%");
  CreateDir(g_root);
#endif
  LOG(kInfo) << "Set up g_root at " << g_root;
}

void RemoveRootDirectory() {
  boost::system::error_code error_code;
  if (fs::exists(g_root, error_code)) {
    if (fs::remove_all(g_root, error_code) == 0 || error_code) {
      LOG(kWarning) << "Failed to remove root directory " << g_root << ": "
                    << error_code.message();
    } else {
      LOG(kInfo) << "Removed " << g_root;
    }
  }
}

fs::path SetUpStorageDirectory() {
  boost::system::error_code error_code;
  fs::path storage_path(
      fs::unique_path(fs::temp_directory_path() / "MaidSafe_Test_ChunkStore_%%%%-%%%%-%%%%"));
  CreateDir(storage_path);
  g_storage = storage_path;
  LOG(kInfo) << "Created storage_path " << storage_path;
  return storage_path;
}

void RemoveStorageDirectory(const fs::path& storage_path) {
  boost::system::error_code error_code;
  if (fs::remove_all(storage_path, error_code) == 0 || error_code) {
    LOG(kWarning) << "Failed to remove storage_path " << storage_path << ": "
                  << error_code.message();
  } else {
    LOG(kInfo) << "Removed " << storage_path;
  }
}

po::options_description CommandLineOptions() {
  boost::system::error_code error_code;
  po::options_description command_line_options(std::string("SafeStorage Tool Options:\n"));
  command_line_options.add_options()("help,h", "Show help message.")
      ("peer", po::value<std::string>(), "Endpoint of peer, for connection to SAFE network")
      ("enable_vfs_logging", po::bool_switch(&g_enable_vfs_logging), "Enable logging on the VFS");
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
  argc = static_cast<int>(unused_options.size());
  int position(0);
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

std::string GetStringFromProgramOption(const std::string& option_name,
                                       const po::variables_map& variables_map) {
  if (variables_map.count(option_name)) {
    std::string option_string(variables_map.at(option_name).as<std::string>());
    LOG(kInfo) << option_name << " set to " << option_string;
    return option_string;
  } else {
    return "";
  }
}

std::function<void()> PrepareNetworkVfs(drive::Options& options, bool create_account) {
  SetUpTempDirectory();
  SetUpRootDirectory(GetHomeDir());

  options.mount_path = g_root;
  options.storage_path = SetUpStorageDirectory();
  options.drive_name = RandomAlphaNumericString(10);
  options.monitor_parent = false;
  options.create_store = false;
  if (g_enable_vfs_logging)
    options.drive_logging_args = "--log_* V --log_colour_mode 2 --log_no_async";

  if (create_account)
    g_launcher.reset(new drive::Launcher(options, *g_anmaid, *g_anpmid));
  else
    g_launcher.reset(new drive::Launcher(options));

  g_root = g_launcher->kMountPath();

  return [options] {  // NOLINT
    RemoveTempDirectory();
    RemoveStorageDirectory(options.storage_path);
    RemoveRootDirectory();
  };
}

}  // unnamed namespace

}  // namespace test

}  // namespace maidsafe

int main(int argc, char** argv) {
  try {
    auto unuseds(maidsafe::log::Logging::Instance().Initialise(argc, argv));
    std::vector<std::string> unused_options;
    for (const auto& unused : unuseds)
      unused_options.emplace_back(&unused[0]);
    auto variables_map(maidsafe::test::ParseAllOptions(argc, argv, unused_options));
    maidsafe::test::HandleHelp(variables_map);
    maidsafe::test::g_anmaid.reset(new maidsafe::passport::Anmaid());
    maidsafe::test::g_anpmid.reset(new maidsafe::passport::Anpmid());
    bool create_account(true);
    maidsafe::drive::Options options;
    options.peer_endpoint = maidsafe::test::GetStringFromProgramOption("peer", variables_map);

    while (maidsafe::test::g_running) {
      auto cleanup_functor(maidsafe::test::PrepareNetworkVfs(options, create_account));
      create_account = false;
      maidsafe::on_scope_exit cleanup_on_exit(cleanup_functor);
      std::cout << " (enter \"1\" to logout and re-login; \"0\" to stop): ";
      std::string choice;
      std::getline(std::cin, choice);
      if (choice == "1") {
        maidsafe::test::g_launcher->StopDriveProcess(true);
        std::this_thread::sleep_for(std::chrono::seconds(5));
        maidsafe::test::g_launcher.reset();
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
      if (choice == "0")
        maidsafe::test::g_running = false;
    }
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
  return 0;
}
