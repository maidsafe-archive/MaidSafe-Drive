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

#ifdef USES_WINMAIN
#include <Windows.h>
#include <shellapi.h>
#endif
#include <csignal>

#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#ifndef MAIDSAFE_WIN32
#include <locale>  // NOLINT
#else
#include "boost/locale/generator.hpp"
#endif
#include "boost/filesystem.hpp"
#include "boost/program_options.hpp"
#include "boost/preprocessor/stringize.hpp"
#include "boost/system/error_code.hpp"

#include "maidsafe/common/crypto.h"
#include "maidsafe/common/error.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/process.h"
#include "maidsafe/common/rsa.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/types.h"
#include "maidsafe/nfs/client/fake_store.h"

#ifdef MAIDSAFE_WIN32
#include "maidsafe/drive/win_drive.h"
#else
#include "maidsafe/drive/unix_drive.h"
#endif
#include "maidsafe/drive/tools/launcher.h"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace maidsafe {

namespace drive {

namespace {

#ifdef MAIDSAFE_WIN32
typedef CbfsDrive<nfs::FakeStore> LocalDrive;
#else
typedef FuseDrive<nfs::FakeStore> LocalDrive;
#endif

Drive<nfs::FakeStore>* g_local_drive(nullptr);
std::once_flag g_unmount_flag;
const std::string kConfigFile("maidsafe_local_drive.conf");
std::string g_error_message;
int g_return_code(0);

void Unmount() {
  std::call_once(g_unmount_flag, [&] {
    g_local_drive->Unmount();
    g_local_drive = nullptr;
  });
}

#ifdef MAIDSAFE_WIN32

process::ProcessInfo GetParentProcessInfo(const Options& options) {
  return process::ProcessInfo(options.parent_handle);
}

BOOL CtrlHandler(DWORD control_type) {
  LOG(kInfo) << "Received console control signal " << control_type << ".  Unmounting.";
  if (!g_local_drive)
    return FALSE;
  Unmount();
  return TRUE;
}

void SetSignalHandler() {
  if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(&CtrlHandler), TRUE)) {
    g_error_message = "Failed to set control handler.\n\n";
    g_return_code = 16;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::unknown));
  }
}

#else

process::ProcessInfo GetParentProcessInfo(const Options& /*options*/) {
  return getppid();
}

void SetSignalHandler() {}

#endif

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

po::options_description VisibleOptions() {
  po::options_description options("LocalDrive options");
  options.add_options()
#ifdef MAIDSAFE_WIN32
      ("mount_dir,D", po::value<std::string>(), " virtual drive letter (required)")
#else
      ("mount_dir,D", po::value<std::string>(), " virtual drive mount point (required)")
#endif
      ("storage_dir,S", po::value<std::string>(), " directory to store chunks (required)")
      ("unique_id,U", po::value<std::string>(), " unique identifier (required)")
      ("parent_id,R", po::value<std::string>(), " root parent directory identifier (required)")
      ("drive_name,N", po::value<std::string>(), " virtual drive name")
      ("create,C", " Must be called on first run")
      ("check_data,Z", " check all data in chunkstore");
  return options;
}

po::options_description HiddenOptions() {
  po::options_description options("Hidden options");
  options.add_options()
      ("help,h", "help message")
      ("shared_memory", po::value<std::string>(), "shared memory name (IPC)");
  return options;
}

template <typename Char>
po::variables_map ParseAllOptions(int argc, Char* argv[],
                                  const po::options_description& command_line_options,
                                  const po::options_description& config_file_options) {
  po::variables_map variables_map;
  try {
    // Parse command line
    po::store(po::basic_command_line_parser<Char>(argc, argv).options(command_line_options).
                  allow_unregistered().run(), variables_map);
    po::notify(variables_map);

    // Try to open local or main config files
    std::ifstream local_config_file(kConfigFile.c_str());
    fs::path main_config_path(fs::path(GetUserAppDir() / kConfigFile));
    std::ifstream main_config_file(main_config_path.string().c_str());

    // Try local first for testing
    if (local_config_file) {
      std::cout << "Using local config file \"./" << kConfigFile << "\"";
      po::store(parse_config_file(local_config_file, config_file_options), variables_map);
      po::notify(variables_map);
    } else if (main_config_file) {
      std::cout << "Using main config file \"" << main_config_path << "\"\n";
      po::store(parse_config_file(main_config_file, config_file_options), variables_map);
      po::notify(variables_map);
    }
  }
  catch (const std::exception& e) {
    g_error_message = "Fatal error:\n  " + std::string(e.what()) +
                      "\nRun with -h to see all options.\n\n";
    g_return_code = 32;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
  }
  return variables_map;
}

void HandleHelp(const po::variables_map& variables_map) {
  if (variables_map.count("help")) {
    std::ostringstream stream;
    stream << VisibleOptions() << "\nThese can also be set via a config file at \"./"
           << kConfigFile << "\" or at " << fs::path(GetUserAppDir() / kConfigFile) << "\n\n";
    g_error_message = stream.str();
    g_return_code = 0;
    throw MakeError(CommonErrors::success);
  }
}

bool GetFromIpc(const po::variables_map& variables_map, Options& options) {
  if (variables_map.count("shared_memory")) {
    ReadAndRemoveInitialSharedMemory(variables_map.at("shared_memory").as<std::string>(), options);
    return true;
  }
  return false;
}

void GetFromProgramOptions(const po::variables_map& variables_map, Options& options) {
  options.mount_path = GetStringFromProgramOption("mount_dir", variables_map);
  options.storage_path = GetStringFromProgramOption("storage_dir", variables_map);
  auto unique_id(GetStringFromProgramOption("unique_id", variables_map));
  if (!unique_id.empty())
    options.unique_id = Identity(unique_id);
  auto parent_id(GetStringFromProgramOption("parent_id", variables_map));
  if (!parent_id.empty())
    options.root_parent_id = Identity(parent_id);
  options.drive_name = GetStringFromProgramOption("drive_name", variables_map);
  options.create_store = (variables_map.count("create") != 0);
}

void ValidateOptions(const Options& options) {
  std::string error_message;
  g_return_code = 0;
  if (options.mount_path.empty()) {
    error_message += "  mount_dir must be set\n";
    ++g_return_code;
  }
  if (options.storage_path.empty()) {
    error_message += "  chunk_store must be set\n";
    g_return_code += 2;
  }
  if (!options.unique_id.IsInitialised()) {
    error_message += "  unique_id must be set to a 64 character string\n";
    g_return_code += 4;
  }
  if (!options.root_parent_id.IsInitialised()) {
    error_message += "  parent_id must be set to a 64 character string\n";
    g_return_code += 8;
  }

  if (g_return_code) {
    g_error_message = "Fatal error:\n" + error_message + "\nRun with -h to see all options.\n\n";
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
  }
}

void MonitorParentProcess(const Options& options) {
  auto parent_process_info(GetParentProcessInfo(options));
  while (g_local_drive && process::IsRunning(parent_process_info))
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  Unmount();
}

int MountAndWaitForIpcNotification(const Options& options) {
  fs::path storage_path(options.storage_path / "local_store");
  DiskUsage disk_usage(std::numeric_limits<uint64_t>().max());
  auto storage(std::make_shared<nfs::FakeStore>(storage_path, disk_usage));

  boost::system::error_code error_code;
  if (!fs::exists(options.storage_path, error_code)) {
    LOG(kError) << options.storage_path << " doesn't exist.";
    return error_code.value();
  }
  if (!fs::exists(GetUserAppDir(), error_code)) {
    LOG(kError) << "Creating " << GetUserAppDir();
    if (!fs::create_directories(GetUserAppDir(), error_code)) {
      LOG(kError) << GetUserAppDir() << " creation failed.";
      return error_code.value();
    }
  }

  LocalDrive drive(storage, options.unique_id, options.root_parent_id, options.mount_path,
                   GetUserAppDir(), options.drive_name, options.mount_status_shared_object_name,
                   options.create_store
#ifdef MAIDSAFE_WIN32
                   , BOOST_PP_STRINGIZE(PRODUCT_ID)
#endif
                   );

  g_local_drive = &drive;

  // Start a thread to poll the parent process' continued existence *before* calling drive.Mount().
  std::thread poll_parent([&] { MonitorParentProcess(options); });

  try {
    drive.Mount();
  } catch (const std::exception& e) {
    LOG(kError) << "using VFS caught an exception " << boost::diagnostic_information(e);
  }
  // Drive should already be unmounted by this point, but we need to make 'g_local_drive' null to
  // allow 'poll_parent' to join.
  Unmount();
  poll_parent.join();
  return 0;
}

int MountAndWaitForSignal(const Options& options) {
  fs::path storage_path(options.storage_path / "local_store");
  DiskUsage disk_usage(std::numeric_limits<uint64_t>().max());
  auto storage(std::make_shared<nfs::FakeStore>(storage_path, disk_usage));

  boost::system::error_code error_code;
  if (!fs::exists(options.storage_path, error_code)) {
    LOG(kError) << options.storage_path << " doesn't exist.";
    return error_code.value();
  }
  if (!fs::exists(GetUserAppDir(), error_code)) {
    LOG(kError) << "Creating " << GetUserAppDir();
    if (!fs::create_directories(GetUserAppDir(), error_code)) {
      LOG(kError) << GetUserAppDir() << " creation failed.";
      return error_code.value();
    }
  }

  LocalDrive drive(storage, options.unique_id, options.root_parent_id, options.mount_path,
                   GetUserAppDir(), options.drive_name, "", options.create_store
#ifdef MAIDSAFE_WIN32
                   , BOOST_PP_STRINGIZE(PRODUCT_ID)
#endif
                   );

  g_local_drive = &drive;

  drive.Mount();
  return 0;
}

}  // unnamed namespace

}  // namespace drive

}  // namespace maidsafe

#ifdef USES_WINMAIN
int CALLBACK wWinMain(HINSTANCE /*handle_to_instance*/, HINSTANCE /*handle_to_previous_instance*/,
                      PWSTR /*command_line_args_without_program_name*/, int /*command_show*/) {
  int argc(0);
  LPWSTR* argv(nullptr);
  argv = CommandLineToArgvW(GetCommandLineW(), &argc);
#else
int main(int argc, char* argv[]) {
#endif
#ifdef MAIDSAFE_WIN32
  std::locale::global(boost::locale::generator().generate(""));
#else
  std::locale::global(std::locale(""));
#endif
  maidsafe::log::Logging::Instance().Initialise(argc, argv);
  fs::path::imbue(std::locale());
  boost::system::error_code error_code;
  try {
    // Set up command line options and config file options
    auto visible_options(maidsafe::drive::VisibleOptions());
    po::options_description command_line_options, config_file_options;
    command_line_options.add(visible_options).add(maidsafe::drive::HiddenOptions());
    config_file_options.add(visible_options);

    // Read in options
    auto variables_map(maidsafe::drive::ParseAllOptions(argc, argv, command_line_options,
                                                        config_file_options));
    maidsafe::drive::HandleHelp(variables_map);
    maidsafe::drive::Options options;
    bool using_ipc(maidsafe::drive::GetFromIpc(variables_map, options));
    if (!using_ipc)
      maidsafe::drive::GetFromProgramOptions(variables_map, options);

    // Validate options and run the Drive
    maidsafe::drive::ValidateOptions(options);
    if (using_ipc) {
      return maidsafe::drive::MountAndWaitForIpcNotification(options);
    } else {
      maidsafe::drive::SetSignalHandler();
      return maidsafe::drive::MountAndWaitForSignal(options);
    }
  }
  catch (const std::exception& e) {
    if (!maidsafe::drive::g_error_message.empty()) {
      std::cout << maidsafe::drive::g_error_message;
      return maidsafe::drive::g_return_code;
    }
    LOG(kError) << "Exception: " << e.what();
  }
  catch (...) {
    LOG(kError) << "Exception of unknown type!";
  }
  return 64;
}
