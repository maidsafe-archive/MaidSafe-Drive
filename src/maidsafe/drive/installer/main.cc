/*  Copyright 2012 MaidSafe.net limited

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

#include <Windows.h>
#include <iostream>
#include <string>

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/program_options.hpp"
#include "boost/preprocessor/stringize.hpp"
#include "boost/system/error_code.hpp"

#include "maidsafe/common/error.h"

#include "CbFs.h"  // NOLINT

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace maidsafe {
namespace drive {
namespace {

struct DriverOptions {
  std::string operation;
  std::string product_id;
  std::string cbfs_root;
};

bool DriverStatus(const fs::path& dll_path, const std::string& product_id, PBOOL installed,
                  PDWORD version_high, PDWORD version_low) {
  bool success;
  HINSTANCE dll_handle = LoadLibrary(dll_path.wstring().c_str());
  if (dll_handle != NULL) {
    FARPROC ProcessID = GetProcAddress(HMODULE(dll_handle), "GetModuleStatusA");
    if (ProcessID != NULL) {
      typedef bool(__stdcall * pFunction)(LPCSTR, DWORD, BOOL*, PDWORD, PDWORD);
      pFunction GetModuleStatusA = pFunction(ProcessID);
      success = GetModuleStatusA(product_id.data(), CBFS_MODULE_DRIVER, installed, version_high,
                                 version_low);
    } else {
      FreeLibrary(dll_handle);
      return false;
    }
  } else {
    return false;
  }
  FreeLibrary(dll_handle);
  return success;
}

bool DriverInstall(const fs::path& cab_path, const fs::path& dll_path,
                   const std::string& product_id, PDWORD reboot) {
  bool success;
  HINSTANCE dll_handle = LoadLibrary(dll_path.wstring().c_str());
  if (dll_handle != NULL) {
    FARPROC ProcessID = GetProcAddress(HMODULE(dll_handle), "InstallA");
    if (ProcessID != NULL) {
      typedef bool(__stdcall * pFunction)(LPCSTR, LPCSTR, LPCSTR, bool, DWORD, PDWORD);
      pFunction InstallA = pFunction(ProcessID);
      success = InstallA(
          cab_path.string().c_str(), product_id.data(), "", true,
          CBFS_MODULE_DRIVER | CBFS_MODULE_NET_REDIRECTOR_DLL | CBFS_MODULE_MOUNT_NOTIFIER_DLL,
          reboot);
    } else {
      FreeLibrary(dll_handle);
      return false;
    }
  } else {
    return false;
  }
  FreeLibrary(dll_handle);
  return success;
}

bool DriverUninstall(const fs::path& cab_path, const fs::path& dll_path,
                     const std::string& product_id, PDWORD reboot) {
  bool success;
  HINSTANCE dll_handle = LoadLibrary(dll_path.wstring().c_str());
  if (dll_handle != NULL) {
    FARPROC ProcessID = GetProcAddress(HMODULE(dll_handle), "UninstallA");
    if (ProcessID != NULL) {
      typedef bool(__stdcall * pFunction)(LPCSTR, LPCSTR, LPCSTR, PDWORD);
      pFunction UninstallA = pFunction(ProcessID);
      success = UninstallA(cab_path.string().c_str(), product_id.data(), "", reboot);
    } else {
      FreeLibrary(dll_handle);
      return false;
    }
  } else {
    return false;
  }
  FreeLibrary(dll_handle);
  return success;
}

DWORD InstallDriver(const fs::path& cab_path, const fs::path& dll_path,
                    const std::string& product_id, PDWORD reboot) {
  bool success = DriverInstall(cab_path, dll_path, product_id, reboot);
  if (!success)
    return GetLastError();
  else
    return 0;
}

DWORD UninstallDriver(const fs::path& cab_path, const fs::path& dll_path,
                      const std::string& product_id, PDWORD reboot) {
  BOOL installed;
  DWORD version_high, version_low;

  bool success = DriverStatus(dll_path, product_id, &installed, &version_high, &version_low);
  if (success) {
    if (installed) {
      success = DriverUninstall(cab_path, dll_path, product_id, reboot);
      if (!success) {
        return GetLastError();
      } else {
        return 0;
      }
    } else {
      return 0;
    }
  } else {
    return GetLastError();
  }
}

fs::path InstallerDllPath(const fs::path& cab_path) {
  boost::system::error_code error_code;
  if (!fs::exists(cab_path, error_code))
    return fs::path();
  fs::path dll_path(cab_path.parent_path() / "cbfsinst.dll");
  if (!fs::exists(dll_path, error_code)) {
    fs::path cbfs_path(cab_path.parent_path().parent_path());
    std::string architecture(BOOST_PP_STRINGIZE(TARGET_ARCHITECTURE));
    if (architecture == "x86_64")
      dll_path = cbfs_path / "HelperDLLs\\Installer\\64bit\\x64\\cbfsinst.dll";
    else
      dll_path = cbfs_path / "HelperDLLs\\Installer\\32bit\\cbfsinst.dll";
    if (!fs::exists(dll_path, error_code))
      return fs::path();
  }
  return dll_path;
}

fs::path CabinetFilePath() {
  TCHAR file_name[MAX_PATH];
  if (!GetModuleFileName(NULL, file_name, MAX_PATH))
    return fs::path();

  boost::system::error_code error_code;
  fs::path path(fs::path(file_name).parent_path());
  fs::path cab_path(path / "driver\\cbfs.cab");
  if (!fs::exists(cab_path, error_code)) {
    cab_path = BOOST_PP_STRINGIZE(CBFS_ROOT_DIR);
    std::string cbfs("Callback File System");
    while (fs::exists(cab_path, error_code) && cab_path.filename().string() != cbfs)
      cab_path = cab_path.parent_path();
    if (fs::exists(cab_path, error_code) && cab_path.filename().string() == cbfs)
      return cab_path /= "Drivers\\cbfs.cab";
    return fs::path();
  }
  return cab_path;
}

po::options_description AvailableOptions() {
  po::options_description installer_options("Installer options");
  installer_options.add_options()("help,h", "Print this help message")
      ("op,O", po::value<std::string>(), "Either 'install' or 'uninstall' the filesystem driver")
      ("id,I", po::value<std::string>(), "Unique product identifier associated with the un/installation")
      ("cbfs_root,P", po::value<std::string>(),
          "Path to root of CBFS installation folder, e.g., C:\\Program Files\\Eldos\\Callback File System");
  return installer_options;
}

po::variables_map ParseOptions(int argc, char* argv[],
                               const po::options_description& installer_options) {
  po::variables_map variables_map;
  try {
    po::store(po::command_line_parser(argc, argv).options(installer_options).run(),
              variables_map);
    po::notify(variables_map);
  }
  catch (const std::exception& e) {
    std::cout << "Parser error:\n " + std::string(e.what()) +
                 "\nRun with -h to see all options.\n\n";
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
  }
  return variables_map;
}

void HandleHelp() {
  std::ostringstream stream;
  stream << AvailableOptions() << "\n\n";
  std::cout << stream.str();
}

std::string GetStringFromProgramOption(const std::string& option_name,
                                       const po::variables_map& variables_map) {
  if (variables_map.count(option_name)) {
    std::string option_string(variables_map.at(option_name).as<std::string>());
    return option_string;
  } else {
    return "";
  }
}

DriverOptions GetOptions(const po::variables_map& variables_map) {
  DriverOptions options;
  if (variables_map.count("help")) {
    HandleHelp();
    return options;
  }
  options.product_id = GetStringFromProgramOption("id", variables_map);
  if (options.product_id.empty())
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
  options.operation = GetStringFromProgramOption("op", variables_map);
  if (options.operation != "install" && options.operation != "uninstall")
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
  options.cbfs_root = GetStringFromProgramOption("cbfs_root", variables_map);
  return options;
}

}  // unnamed namespace
}  // namespace drive
}  // namespace maidsafe

int main(int argc, char* argv[]) {
  try {
    auto installer_options(maidsafe::drive::AvailableOptions());
    auto variables_map(maidsafe::drive::ParseOptions(argc, argv, installer_options));
    maidsafe::drive::DriverOptions options(maidsafe::drive::GetOptions(variables_map));

    if (options.operation.empty() || options.product_id.empty())
      return 0;

    fs::path cab_path;
    if (options.cbfs_root.empty())
      cab_path = maidsafe::drive::CabinetFilePath();
    else
      cab_path = fs::path(options.cbfs_root) / "Drivers\\cbfs.cab";
    fs::path dll_path(maidsafe::drive::InstallerDllPath(cab_path));

    if ((fs::path() == cab_path) || (fs::path() == dll_path)) {
      std::cout << "CbFs cab file or dll not found.";
      return 0;
    }

    DWORD reboot(0);
    if (options.operation == "install") {
      DWORD installed = maidsafe::drive::InstallDriver(cab_path, dll_path, options.product_id,
                                                       &reboot);
      if (installed != 0)
        return 0;
    } else {
      DWORD uninstalled = maidsafe::drive::UninstallDriver(cab_path, dll_path, options.product_id,
                                                           &reboot);
      if (uninstalled != 0)
        return 0;
    }
    return reboot;
  }
  catch (const std::exception& e) {
    std::cout << "Exception: " + std::string(e.what()) << std::endl;
  }
  catch (...) {
    std::cout << "Exception of unknown type!" << std::endl;
  }
  return 0;
}
