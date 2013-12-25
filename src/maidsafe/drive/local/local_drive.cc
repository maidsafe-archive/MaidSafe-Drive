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
#include <signal.h>

#include <functional>
#include <iostream>
#include <sstream>
#include <memory>
#include <string>
#include <fstream>
#include <iterator>

#ifndef MAIDSAFE_WIN32
#include <locale>
#else
#include "boost/locale/generator.hpp"
#endif
#include "boost/filesystem.hpp"
#include "boost/program_options.hpp"
#include "boost/preprocessor/stringize.hpp"
#include "boost/system/error_code.hpp"
#include "boost/interprocess/shared_memory_object.hpp"
#include "boost/interprocess/mapped_region.hpp"
#include "boost/interprocess/sync/scoped_lock.hpp"

#include "maidsafe/common/crypto.h"
#include "maidsafe/common/error.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/rsa.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/ipc.h"
#include "maidsafe/common/types.h"

#include "maidsafe/data_store/local_store.h"

#ifdef MAIDSAFE_WIN32
#include "maidsafe/drive/win_drive.h"
#else
#include "maidsafe/drive/unix_drive.h"
#endif

namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace ip = boost::interprocess;
namespace pt = boost::posix_time;

namespace maidsafe {

namespace drive {

#ifdef MAIDSAFE_WIN32
typedef CbfsDrive<data_store::LocalStore> LocalDrive;
#else
typedef FuseDrive<data_store::LocalStore> LocalDrive;
#endif
LocalDrive* g_local_drive(nullptr);
boost::promise<void> g_unmount;

int Mount(const fs::path &mount_dir, const fs::path &storage_dir, const Identity& unique_id,
          const Identity& parent_id, const fs::path& drive_name, bool create,
          std::shared_ptr<std::string> shared_object_name) {
  typedef ip::shared_memory_object SharedMemoryObject;
  fs::path storage_path(storage_dir / "local_store");
  DiskUsage disk_usage(std::numeric_limits<uint64_t>().max());
  auto storage(std::make_shared<data_store::LocalStore>(storage_path, disk_usage));

  boost::system::error_code error_code;
  if (!fs::exists(storage_dir, error_code)) {
    LOG(kError) << storage_dir << " doesn't exist.";
    return error_code.value();
  }
  if (!fs::exists(GetUserAppDir(), error_code)) {
    LOG(kError) << GetUserAppDir() << " doesn't exist.";
    return error_code.value();
  }

  LocalDrive drive(storage, unique_id, parent_id, mount_dir, GetUserAppDir(), drive_name, create);
  g_local_drive = &drive;
  drive.Mount();

  SharedMemoryObject shared_object(ip::open_only, shared_object_name->c_str(), ip::read_write);
  ip::mapped_region region(shared_object, ip::read_write);
  MountStatus* mount_status = static_cast<MountStatus*>(region.get_address());

  {
    ip::scoped_lock<ip::interprocess_mutex> lock(mount_status->mutex);
    mount_status->mounted = true;
    pt::ptime wait_time(pt::second_clock::universal_time() + pt::seconds(10));
    auto mount_predicate([&] { return mount_status->unmount == false; });
    mount_status->condition.notify_one();
    mount_status->condition.timed_wait(lock, wait_time, mount_predicate);

    if (mount_status->unmount) {
      drive.Unmount();
      return 32;
    }
  }
  {
    ip::scoped_lock<ip::interprocess_mutex> lock(mount_status->mutex);
    auto mount_predicate([&]() { return mount_status->unmount == true; });
    mount_status->condition.wait(lock, mount_predicate);
    assert(mount_status->unmount);
  }

  drive.Unmount();

  {
    ip::scoped_lock<ip::interprocess_mutex> lock(mount_status->mutex);
    mount_status->mounted = false;
    mount_status->condition.notify_one();
  }

  return 0;
}

int Mount(const fs::path &mount_dir, const fs::path &storage_dir, const Identity& unique_id,
          const Identity& parent_id, const fs::path& drive_name, bool create) {
  typedef ip::shared_memory_object SharedMemoryObject;
  fs::path storage_path(storage_dir / "local_store");
  DiskUsage disk_usage(std::numeric_limits<uint64_t>().max());
  auto storage(std::make_shared<data_store::LocalStore>(storage_path, disk_usage));

  boost::system::error_code error_code;
  if (!fs::exists(storage_dir, error_code)) {
    LOG(kError) << storage_dir << " doesn't exist.";
    return error_code.value();
  }
  if (!fs::exists(GetUserAppDir(), error_code)) {
    LOG(kError) << GetUserAppDir() << " doesn't exist.";
    return error_code.value();
  }

  LocalDrive drive(storage, unique_id, parent_id, mount_dir, GetUserAppDir(), drive_name, create);
  g_local_drive = &drive;
  drive.Mount();

  auto wait_until_unmounted(g_unmount.get_future());
  wait_until_unmounted.get();

  return 0;
}

}  // namespace drive

}  // namespace maidsafe

const std::string kConfigFile("maidsafe_local_drive.conf");
std::string g_error_message;
int g_return_code(0);

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
    fs::path main_config_path(fs::path(maidsafe::GetUserAppDir() / kConfigFile));
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
    maidsafe::ThrowError(maidsafe::CommonErrors::invalid_parameter);
  }
  return variables_map;
}

void HandleHelp(const po::variables_map& variables_map) {
  if (variables_map.count("help")) {
    std::ostringstream stream;
    stream << VisibleOptions() << "\nThese can also be set via a config file at \"./"
           << kConfigFile << "\" or at " << fs::path(maidsafe::GetUserAppDir() / kConfigFile)
           << "\n\n";
    g_error_message = stream.str();
    g_return_code = 0;
    throw maidsafe::MakeError(maidsafe::CommonErrors::success);
  }
}

struct Options {
  Options() : mount_dir(), chunk_store(), drive_name(), unique_id(), parent_id(), create(false),
              check_data(false) {}
  fs::path mount_dir, chunk_store, drive_name;
  maidsafe::Identity unique_id, parent_id;
  bool create, check_data;
};

bool GetFromIpc(const po::variables_map& variables_map, Options& options,
                std::shared_ptr<std::string> shared_object_name) {
  if (variables_map.count("shared_memory")) {
    std::string shared_memory_name(variables_map.at("shared_memory").as<std::string>());
    auto hash(maidsafe::crypto::Hash<maidsafe::crypto::SHA512>(shared_memory_name).string());
    shared_object_name.reset(new std::string(maidsafe::HexEncode(hash).substr(0, 32)));
    auto vec_strings = maidsafe::ipc::ReadSharedMemory(shared_memory_name.c_str(), 6);
    options.mount_dir = vec_strings[0];
    options.chunk_store = vec_strings[1];
    options.unique_id = maidsafe::Identity(vec_strings[2]);
    options.parent_id = maidsafe::Identity(vec_strings[3]);
    options.drive_name = vec_strings[4];
    options.create = (std::stoi(vec_strings[5]) != 0);
    maidsafe::ipc::RemoveSharedMemory(shared_memory_name);
    return true;
  }
  return false;
}

void GetFromProgramOptions(const po::variables_map& variables_map, Options& options) {
  options.mount_dir = GetStringFromProgramOption("mount_dir", variables_map);
  options.chunk_store = GetStringFromProgramOption("storage_dir", variables_map);
  auto unique_id(GetStringFromProgramOption("unique_id", variables_map));
  if (!unique_id.empty())
    options.unique_id = maidsafe::Identity(unique_id);
  auto parent_id(GetStringFromProgramOption("parent_id", variables_map));
  if (!parent_id.empty())
    options.parent_id = maidsafe::Identity(parent_id);
  options.drive_name = GetStringFromProgramOption("drive_name", variables_map);
  options.create = (variables_map.count("create") != 0);
}

void ValidateOptions(const Options& options) {
  std::string error_message;
  g_return_code = 0;
  if (options.mount_dir.empty()) {
    error_message += "  mount_dir must be set\n";
    ++g_return_code;
  }
  if (options.chunk_store.empty()) {
    error_message += "  chunk_store must be set\n";
    g_return_code += 2;
  }
  if (!options.unique_id.IsInitialised()) {
    error_message += "  unique_id must be set to a 64 character string\n";
    g_return_code += 4;
  }
  if (!options.parent_id.IsInitialised()) {
    error_message += "  parent_id must be set to a 64 character string\n";
    g_return_code += 8;
  }

  if (g_return_code) {
    g_error_message = "Fatal error:\n" + error_message + "\nRun with -h to see all options.\n\n";
    maidsafe::ThrowError(maidsafe::CommonErrors::invalid_parameter);
  }
}

#ifdef MAIDSAFE_WIN32

BOOL CtrlHandler(DWORD control_type) {
  LOG(kInfo) << "Received console control signal " << control_type << ".  Unmounting.";
  if (!maidsafe::drive::g_local_drive)
    return FALSE;
  maidsafe::drive::g_local_drive->Unmount();
  maidsafe::drive::g_unmount.set_value();
  return TRUE;
}

void SetSignalHandler() {
  if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(&CtrlHandler), TRUE)) {
    g_error_message = "Failed to set control handler.\n\n";
    g_return_code = 16;
    maidsafe::ThrowError(maidsafe::CommonErrors::unknown);
  }
}

#else

void SetSignalHandler() {}

#endif

#ifdef USES_WINMAIN
int CALLBACK wWinMain(HINSTANCE /*handle_to_instance*/, HINSTANCE /*handle_to_previous_instance*/,
                      PWSTR /*command_line_args_without_program_name*/, int /*command_show*/) {
  int argc(0);
  LPWSTR* argv(nullptr);
  argv = CommandLineToArgvW(GetCommandLineW(), &argc);
#else
int main(int argc, char* argv[]) {
  maidsafe::log::Logging::Instance().Initialise(argc, argv);
#endif

#ifdef MAIDSAFE_WIN32
  std::locale::global(boost::locale::generator().generate(""));
#else
  std::locale::global(std::locale(""));
#endif
  fs::path::imbue(std::locale());
  boost::system::error_code error_code;
  try {
    // Set up command line options and config file options
    auto visible_options(VisibleOptions());
    po::options_description command_line_options, config_file_options;
    command_line_options.add(visible_options).add(HiddenOptions());
    config_file_options.add(visible_options);

    // Read in options
    auto variables_map(ParseAllOptions(argc, argv, command_line_options, config_file_options));
    HandleHelp(variables_map);
    Options options;
    std::shared_ptr<std::string> shared_object_name;
    if (!GetFromIpc(variables_map, options, shared_object_name))
      GetFromProgramOptions(variables_map, options);

    // Validate options and run the Drive
    ValidateOptions(options);
    SetSignalHandler();
    if (shared_object_name)
      return maidsafe::drive::Mount(options.mount_dir, options.chunk_store, options.unique_id,
                                    options.parent_id, options.drive_name, options.create,
                                    shared_object_name);
    else
      return maidsafe::drive::Mount(options.mount_dir, options.chunk_store, options.unique_id,
                                    options.parent_id, options.drive_name, options.create);
  }
  catch (const std::exception& e) {
    if (!g_error_message.empty()) {
      std::cout << g_error_message;
      return g_return_code;
    }
    LOG(kError) << "Exception: " << e.what();
  }
  catch (...) {
    LOG(kError) << "Exception of unknown type!";
  }
  return 64;
}
