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
#include <string>
#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/system/error_code.hpp"
#include "CbFs.h"  // NOLINT

namespace fs = boost::filesystem;

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

int main(int argc, char* argv[]) {
  DWORD reboot(0);
  if (argc == 3) {
    std::string argument(argv[1]);
    std::string product_id(argv[2]);
    fs::path cab_path(CabinetFilePath());
    fs::path dll_path(InstallerDllPath(cab_path));

    if ((fs::path() == cab_path) || (fs::path() == dll_path) || product_id.empty())
      return 0;

    if (argument == "install") {
      DWORD installed = InstallDriver(cab_path, dll_path, product_id, &reboot);
      if (installed != 0)
        return 0;
    } else if (argument == "uninstall") {
      DWORD uninstalled = UninstallDriver(cab_path, dll_path, product_id, &reboot);
      if (uninstalled != 0)
        return 0;
    } else {
      return 0;
    }
  }
  return reboot;
}
