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

const uint32_t kIdentitySize(64);

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
          const Identity& parent_id, const std::string& product_id,
          const std::string& drive_name) {
  fs::path storage_path(chunk_dir / "local_store");
  DiskUsage disk_usage(std::numeric_limits<uint64_t>().max());
  std::shared_ptr<maidsafe::data_store::LocalStore>
    storage(new maidsafe::data_store::LocalStore(storage_path, disk_usage));

  boost::system::error_code error_code;
  if (!fs::exists(chunk_dir, error_code))
    return error_code.value();

  typedef GetDrive<maidsafe::data_store::LocalStore>::type Drive;

  Drive drive(storage,
              unique_id,
#ifdef MAIDSAFE_WIN32
              parent_id,
#endif
              mount_dir,
              product_id,
              drive_name);

  drive.Mount();

  return 0;
}

}  // namespace drive
}  // namespace maidsafe


fs::path GetPathFromProgramOption(const std::string &option_name, po::variables_map *variables_map,
                                  bool must_exist) {
  if (variables_map->count(option_name)) {
    boost::system::error_code error_code;
    fs::path option_path(variables_map->at(option_name).as<std::string>());
    if (must_exist) {
      if (!fs::exists(option_path, error_code) || error_code) {
        LOG(kError) << "Invalid " << option_name << " option.  " << option_path
                    << " doesn't exist or can't be accessed (error message: "
                    << error_code.message() << ")";
        return fs::path();
      }
      if (!fs::is_directory(option_path, error_code) || error_code) {
        LOG(kError) << "Invalid " << option_name << " option.  " << option_path
                    << " is not a directory (error message: "
                    << error_code.message() << ")";
        return fs::path();
      }
    } else {
      if (fs::exists(option_path, error_code)) {
        LOG(kError) << "Invalid " << option_name << " option.  " << option_path
                    << " already exists (error message: "
                    << error_code.message() << ")";
        return fs::path();
      }
    }
    LOG(kInfo) << option_name << " set to " << option_path;
    return option_path;
  } else {
    LOG(kWarning) << "You must set the " << option_name << " option to a"
                 << (must_exist ? "n " : " non-") << "existing directory.";
    return fs::path();
  }
}

maidsafe::Identity GetIdentityFromProgramOption(const std::string &option_name,
                                                po::variables_map *variables_map) {
  if (variables_map->count(option_name)) {
    std::string option_string(variables_map->at(option_name).as<std::string>());
    if (option_string.size() != kIdentitySize)
      return maidsafe::Identity();
    LOG(kInfo) << option_name << " set to " << option_string;
    return maidsafe::Identity(option_string);
  } else {
    LOG(kWarning) << "You must set the " << option_name << " option to a string.";
    return maidsafe::Identity();
  }
}

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
    po::options_description options_description("Allowed options");
    options_description.add_options()
        ("help,h", "print this help message")
        ("chunkdir,C", po::value<std::string>(), "set directory to store chunks")
        ("mountdir,D", po::value<std::string>(), "set virtual drive mount point")
        ("uniqueid,U", po::value<std::string>(), "set unique directory identifier")
        ("parentid,R", po::value<std::string>(), "set root parent directory identifier")
        ("productid,P", po::value<std::string>(), "set drive product identifier (Windows only)")
        ("drivename,R", po::value<std::string>(), "set virtual drive name")
        ("checkdata", "check all data (metadata and chunks)");

    po::variables_map variables_map;
    po::store(po::command_line_parser(argc, argv).options(options_description).allow_unregistered().
                                                  run(), variables_map);
    po::notify(variables_map);

    // set up options for config file
    po::options_description config_file_options;
    config_file_options.add(options_description);

    // try open some config options
    std::ifstream local_config_file("maidsafe_drive.conf");
    fs::path main_config_path(fs::path(maidsafe::GetUserAppDir() / "maidsafe_drive.conf"));
    std::ifstream main_config_file(main_config_path.string().c_str());

    // try local first for testing
    if (local_config_file) {
      LOG(kInfo) << "Using local config file \"maidsafe_drive.conf\"";
      store(parse_config_file(local_config_file, config_file_options), variables_map);
      notify(variables_map);
    } else if (main_config_file) {
      LOG(kInfo) << "Using main config file " << main_config_path;
      store(parse_config_file(main_config_file, config_file_options), variables_map);
      notify(variables_map);
    } else {
      LOG(kWarning) << "No configuration file found at " << main_config_path;
    }

    if (variables_map.count("help")) {
      std::cout << options_description << '\n';
      return 0;
    }

    fs::path chunk_dir(GetPathFromProgramOption("chunkdir", &variables_map, true));
#ifdef WIN32
    fs::path mount_dir(GetPathFromProgramOption("mountdir", &variables_map, false));
    if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CtrlHandler), TRUE)) {
      LOG(kError) << "Failed to set control handler.";
      return 1;
    }
#else
    fs::path mount_dir(GetPathFromProgramOption("mountdir", &variables_map, true));
#endif

    maidsafe::Identity unique_id(GetIdentityFromProgramOption("uniqueid", &variables_map));
    maidsafe::Identity parent_id(GetIdentityFromProgramOption("parentid", &variables_map));

    if (chunk_dir == fs::path() || mount_dir == fs::path() || unique_id.string().size() == 0) {
      LOG(kWarning) << options_description;
      return 1;
    }

    std::string product_id(GetStringFromProgramOption("productid", &variables_map));
    std::string drive_name(GetStringFromProgramOption("drivename", &variables_map));

    int result(maidsafe::drive::Mount(mount_dir, chunk_dir, unique_id, parent_id, product_id,
                                      drive_name));
    return result;
  }
  catch(const std::exception& e) {
    LOG(kError) << "Exception: " << e.what();
    return 1;
  }
  catch(...) {
    LOG(kError) << "Exception of unknown type!";
  }
  return 0;
}
