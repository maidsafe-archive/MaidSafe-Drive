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

#include <signal.h>

#include <functional>
#include <iostream>  // NOLINT
#include <memory>
#include <string>
#include <fstream>  // NOLINT
#include <iterator>

#include "boost/filesystem.hpp"
#include "boost/program_options.hpp"
#include "boost/preprocessor/stringize.hpp"
#include "boost/system/error_code.hpp"

#include "maidsafe/common/crypto.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/rsa.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/ipc.h"
#include "maidsafe/common/types.h"

#include "maidsafe/data_store/local_store.h"

#ifdef MAIDSAFE_WIN32
#include "maidsafe/drive/win_drive.h"

BOOL CtrlHandler(DWORD control_type) {
  switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      exit(control_type);
    default:
      exit(0);
  }
}

#else
#include "maidsafe/drive/unix_drive.h"
#endif

#undef APPLICATION_NAME
#undef COMPANY_NAME
#define APPLICATION_NAME LocalDrive
#define COMPANY_NAME MaidSafe

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace maidsafe {

namespace drive {

template<typename Storage>
#ifdef MAIDSAFE_WIN32
struct GetDrive {
  typedef CbfsDrive<Storage> type;
};
#else
struct GetDrive {
  typedef FuseDrive<Storage> type;
};
#endif

int Mount(const fs::path &mount_dir, const fs::path &chunk_dir, const Identity& unique_id,
          const Identity& parent_id, const std::string& drive_name) {
  fs::path storage_path(chunk_dir / "local_store");
  DiskUsage disk_usage(std::numeric_limits<uint64_t>().max());
  auto storage(std::make_shared<maidsafe::data_store::LocalStore>(storage_path, disk_usage));

  boost::system::error_code error_code;
  if (!fs::exists(chunk_dir, error_code))
    return error_code.value();

  typedef GetDrive<maidsafe::data_store::LocalStore>::type Drive;

  Drive drive(storage, unique_id, parent_id, mount_dir, drive_name);
  drive.Mount();

  return 0;
}

}  // namespace drive

}  // namespace maidsafe

std::string GetStringFromProgramOption(const std::string &option_name,
                                       po::variables_map *variables_map) {
  if (variables_map->count(option_name)) {
    std::string option_string(variables_map->at(option_name).as<std::string>());
    LOG(kInfo) << option_name << " set to " << option_string;
    return option_string;
  } else {
    LOG(kWarning) << "You must set the " << option_name << " option to a string.";
    return std::string();
  }
}

int main(int argc, char *argv[]) {
  maidsafe::log::Logging::Instance().Initialise(argc, argv);
  boost::system::error_code error_code;
  try {
    std::string help_text("Please note that SharedMemory is a stand alone option.\n");
    help_text += "Setting any other option requires all options for your system must be set \n";
    help_text += "with the exception of ShareMemory of course. The config file obeys the same rules.\n";
    po::options_description options_description(help_text);
    options_description.add_options()
        ("help,h", "print this help message")
        ("sharedmemory,S", po::value<std::string>(), "set Shared Memory Name (ipc)")
        ("mountdir,D", po::value<std::string>(), "set virtual drive mount point")
        ("chunkdir,C", po::value<std::string>(), "set directory to store chunks")
        ("uniqueid,U", po::value<std::string>(), "set unique identifier")
        ("parentid,R", po::value<std::string>(), "set root parent directory identifier")
        ("drivename,N", po::value<std::string>(), "set virtual drive name")
        ("checkdata,z", "check all data in chunkstore");

    po::variables_map variables_map;
    po::store(po::command_line_parser(argc, argv).options(options_description).allow_unregistered().
                                                  run(), variables_map);
    po::notify(variables_map);

    if (variables_map.count("help")) {
      std::cout << options_description << '\n';
      return 0;
    }
#ifdef MAIDSAFE_WIN32
      if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CtrlHandler), TRUE)) {
        LOG(kError) << "Failed to set control handler.";
        return 1;
      }
#endif
    std::string mount_dir, chunk_store, product_id, unique_id, parent_id_str, drive_name;

    if (variables_map.count("sharedmemory")) {
      std::string shared_memory_name = GetStringFromProgramOption("sharedmemory", &variables_map);
      auto shared_strings = maidsafe::ipc::ReadSharedMemory(shared_memory_name, 5);

      mount_dir = shared_strings.at(0);
      chunk_store = shared_strings.at(1);
      unique_id = shared_strings.at(2);
      parent_id_str = shared_strings.at(3);
      drive_name = shared_strings.at(4);
    } else {
      // set up options for config file
      po::options_description config_file_options;
      config_file_options.add(options_description);

      // try open some config options
      std::ifstream local_config_file("maidsafe_drive.conf");
      fs::path main_config_path(fs::path(maidsafe::GetUserAppDir() / "maidsafe_drive.conf"));
      std::ifstream main_config_file(main_config_path.string().c_str());

      // try local first for testing
      if (local_config_file) {
        std::cout << "Using local config file \"maidsafe_drive.conf\"";
        po::store(parse_config_file(local_config_file, config_file_options), variables_map);
        notify(variables_map);
      } else if (main_config_file) {
        std::cout << "Using main config file " << main_config_path;
        po::store(parse_config_file(main_config_file, config_file_options), variables_map);
        notify(variables_map);
      } else {
        std::cout << "No configuration file found at " << main_config_path;
      }

      mount_dir = GetStringFromProgramOption("mountdir", &variables_map);
      chunk_store = GetStringFromProgramOption("chunkdir", &variables_map);
      unique_id = GetStringFromProgramOption("uniqueid", &variables_map);
      parent_id_str = GetStringFromProgramOption("parentid", &variables_map);
      drive_name = GetStringFromProgramOption("drivename", &variables_map);
    }

    if (mount_dir.empty() || chunk_store.empty() || unique_id.empty() || drive_name.empty()) {
      LOG(kWarning) << options_description;
      return 1;
    }

    maidsafe::Identity parent_id;
    if (!parent_id_str.empty())
      parent_id = maidsafe::Identity(parent_id_str);

    return maidsafe::drive::Mount(fs::path(mount_dir), fs::path(chunk_store),
                                  maidsafe::Identity(unique_id), parent_id, drive_name);
  }
  catch (const std::exception& e) {
    LOG(kError) << "Exception: " << e.what();
    return 1;
  }
  catch (...) {
    LOG(kError) << "Exception of unknown type!";
  }
  return 0;
}
