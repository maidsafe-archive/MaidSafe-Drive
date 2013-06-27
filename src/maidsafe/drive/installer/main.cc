/* Copyright 2012 MaidSafe.net limited

This MaidSafe Software is licensed under the MaidSafe.net Commercial License, version 1.0 or later,
and The General Public License (GPL), version 3. By contributing code to this project You agree to
the terms laid out in the MaidSafe Contributor Agreement, version 1.0, found in the root directory
of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also available at:

http://www.novinet.com/license

Unless required by applicable law or agreed to in writing, software distributed under the License is
distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing permissions and limitations under the
License.
*/

#include<Windows.h>
#include <string>
#include<iostream> // NOLINT
#include "boost/filesystem.hpp"
#include "boost/filesystem/path.hpp"
#include "boost/system/error_code.hpp"
#include "CbFs.h"  // NOLINT

namespace fs = boost::filesystem;

bool ModuleDriverStatus(const fs::path &dll_path,
                        BOOL *installed,
                        DWORD *version_high,
                        DWORD *version_low) {
  bool success;
  // Get dll handle...
  HINSTANCE dll_handle = LoadLibrary(dll_path.wstring().c_str());
  if (dll_handle != NULL) {
    // Obtain a pointer to required function in the dll...
    FARPROC ProcessID = GetProcAddress(HMODULE(dll_handle), "GetModuleStatusA");
    if (ProcessID != NULL) {
      // Function prototype...
      typedef bool (__stdcall * pFunction)(LPCSTR, DWORD, BOOL*, PDWORD, PDWORD);
      pFunction GetModuleStatusA = pFunction(ProcessID);
      // Call the dll function...
      success = GetModuleStatusA("713CC6CE-B3E2-4fd9-838D-E28F558F6866",
                                 CBFS_MODULE_DRIVER,
                                 installed,
                                 version_high,
                                 version_low);
    } else {
      // Release dll handle...
      FreeLibrary(dll_handle);
      return false;
    }
  } else {
    return false;
  }
  // Release dll handle...
  FreeLibrary(dll_handle);
  return success;
}

bool ModuleDriverInstall(const fs::path &cab_path, const fs::path &dll_path, DWORD *reboot) {
  bool success;
  // Get dll handle...
  HINSTANCE dll_handle = LoadLibrary(dll_path.wstring().c_str());
  if (dll_handle != NULL) {
    // Obtain a pointer to required function in the dll...
    FARPROC ProcessID = GetProcAddress(HMODULE(dll_handle), "InstallA");
    if (ProcessID != NULL) {
      // Function prototype...
      typedef bool (__stdcall * pFunction)(LPCSTR, LPCSTR, LPCSTR, bool, DWORD, DWORD*);
      pFunction InstallA = pFunction(ProcessID);
      // Call the dll function...
      success = InstallA(cab_path.string().c_str(),
                         "713CC6CE-B3E2-4fd9-838D-E28F558F6866",
                         "",
                         true,
                         CBFS_MODULE_DRIVER |
                           CBFS_MODULE_NET_REDIRECTOR_DLL |
                           CBFS_MODULE_MOUNT_NOTIFIER_DLL,
                         reboot);
    } else {
      // Release dll handle...
      FreeLibrary(dll_handle);
      return false;
    }
  } else {
    return false;
  }
  // Release dll handle...
  FreeLibrary(dll_handle);
  return success;
}

bool ModuleDriverUninstall(const fs::path &cab_path, const fs::path &dll_path, DWORD *reboot) {
  bool success;
  // Get dll handle...
  HINSTANCE dll_handle = LoadLibrary(dll_path.wstring().c_str());
  if (dll_handle != NULL) {
    // Obtain a pointer to required function in the dll...
    FARPROC ProcessID = GetProcAddress(HMODULE(dll_handle), "UninstallA");
    if (ProcessID != NULL) {
      // Function prototype...
      typedef bool (__stdcall * pFunction)(LPCSTR, LPCSTR, LPCSTR, DWORD*);
      pFunction UninstallA = pFunction(ProcessID);
      // Call the dll function...
      success = UninstallA(cab_path.string().c_str(),
                           "713CC6CE-B3E2-4fd9-838D-E28F558F6866",
                           "",
                           reboot);
    } else {
      // Release dll handle...
      FreeLibrary(dll_handle);
      return false;
    }
  } else {
    return false;
  }
  // Release dll handle...
  FreeLibrary(dll_handle);
  return success;
}

DWORD InstallDriver(const fs::path &cab_path, const fs::path &dll_path, DWORD *reboot) {
  bool success = ModuleDriverInstall(cab_path, dll_path, reboot);
  if (!success) {
    // std::cout << "cbfs driver installation failed";
    return GetLastError();
  } else {
    return 0;
  }
}

DWORD UninstallDriver(const fs::path &cab_path, const fs::path &dll_path, DWORD *reboot) {
  BOOL installed;
  DWORD version_high, version_low;

  bool success = ModuleDriverStatus(dll_path, &installed, &version_high, &version_low);
  if (success) {
    if (installed) {
      success = ModuleDriverUninstall(cab_path, dll_path, reboot);
      if (!success) {
        // std::cout << "cbfs driver uninstallation failed";
        return GetLastError();
      } else {
        return 0;
      }
    } else {
      // std::cout << "Cbfs Driver doesn't exit.";
      return 0;
    }
  } else {
      std::cout << "Failed to get cbfs driver status";
      return GetLastError();
  }
}

fs::path GetInstallerDllPath(fs::path cab_path) {
    boost::system::error_code ec;
    fs::path dll_path(fs::path(cab_path).parent_path() / "cbfsinst.dll");
    if (!fs::exists(dll_path, ec)) {
      // std::cout << "Unable to find cbfsinst.dll at "
      //           << dll_path.string() << " : " << ec.value();
      return fs::path();
    }
    // std::cout << "Dll file found at : " << dll_path.string() << std::endl;
    return dll_path;
}

fs::path GetCabinateFilePath() {
  TCHAR file_name[MAX_PATH];
  if (!GetModuleFileName(NULL, file_name, MAX_PATH)) {
    // std::cout << "GetCabinateFilePath" << GetLastError();
    return fs::path();
  }
  try {
    boost::system::error_code ec;
    fs::path path(fs::path(file_name).parent_path());
    fs::path cab_path(path / "driver\\cbfs.cab");
    if (!fs::exists(cab_path, ec)) {
       // std::cout << "Unable to find cbfs cabinet file at path : "
       //           << cab_path.string() << " : "
       //           << ec.value();
       return fs::path();
    }
// std::cout << "Cabinet file found at : " << cab_path.string() << std::endl;
    return cab_path;
  } catch(...) {
    // std::cout << "Exception : " << GetLastError();
    return fs::path();
  }
}

int main(int argc, char *argv[]) {
  // Initialising logging
  DWORD reboot(0);
  if (argc == 2) {
    std::string argument(argv[1]);
    fs::path cab_path(GetCabinateFilePath());
    fs::path dll_path(GetInstallerDllPath(cab_path));
    if ((fs::path() == cab_path) || (fs::path() == dll_path)) {
      return 1;
    }
    if (argument == "install") {
      DWORD installed = InstallDriver(cab_path, dll_path, &reboot);
      if (installed != 0) {
        // std::cout << "Filesystem driver installation failed." << std::endl;
        return 0;
      }
      // std::cout << "Return value : " << installed << std::endl;
    } else if (argument == "uninstall") {
      DWORD uninstalled = UninstallDriver(cab_path, dll_path, &reboot);
      if (uninstalled != 0) {
        // std::cout << "Filesystem driver uninstallation failed." << std::endl;
        return 0;
      }
      // std::cout << "Return value : " << uninstalled << std::endl;
    } else if (argument == "update") {
      // DWORD updated = UpdateDriver(cab_path, dll_path);
      return 0;
    } else {
      // std::cout << "Unknown option." << std::endl;
      return 0;
    }
  } else {
    // std::cout << "Invalid number of arguments " << std::endl;
    return 0;
  }
  return reboot;
}
