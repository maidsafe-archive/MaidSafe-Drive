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
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/drive/cbfs_paths.h"

#include "CbFs.h"  // NOLINT

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace maidsafe {

namespace drive {

namespace installer {

bool shown_error_message(false);

void DoDisplayError(std::basic_string<TCHAR> message) {
  if (shown_error_message)
    return;
#ifdef UNICODE
  std::wcout << "Error: " << message << std::endl;
#else
  std::cout << "Error: " << message << std::endl;
#endif
  std::basic_string<TCHAR> title(TEXT("Error"));
  message += TEXT("\nTo see all available options, open a command prompt as administrator and ");
  message += TEXT("run this tool with '--help'.");
  MessageBox(nullptr, message.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
  shown_error_message = true;
}

void DisplayError(const std::string& message) {
  DoDisplayError(std::basic_string<TCHAR>(std::begin(message), std::end(message)));
}

void AppendErrorMessageThenDisplayAndThrow(const TCHAR* error_message) {
  LPTSTR formatted_message(nullptr);
  size_t size(FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                            FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(),
                            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                            reinterpret_cast<LPTSTR>(&formatted_message), 0, nullptr));
  std::basic_string<TCHAR> message(error_message);
  message.append(formatted_message, size);
  maidsafe::drive::installer::DoDisplayError(message);
  LocalFree(formatted_message);
  BOOST_THROW_EXCEPTION(MakeError(CommonErrors::unknown));
}

void GetDriverStatus(const std::string& product_guid, BOOL* installed, DWORD* version_high,
                     DWORD* version_low) {
  HINSTANCE dll_handle(LoadLibrary(InstallerDllPath()));
  if (!dll_handle)
    AppendErrorMessageThenDisplayAndThrow(TEXT("Failed to load installer DLL.\n"));
  on_scope_exit cleanup([&dll_handle] { FreeLibrary(dll_handle); });

#ifdef UNICODE
  FARPROC process_id(GetProcAddress(HMODULE(dll_handle), "GetModuleStatusW"));
#else
  FARPROC process_id(GetProcAddress(HMODULE(dll_handle), "GetModuleStatusA"));
#endif
  if (!process_id)
    AppendErrorMessageThenDisplayAndThrow(TEXT("Failed to find 'GetModuleStatus' in DLL.\n"));

  typedef bool(__stdcall* Functor)(const TCHAR*, DWORD, BOOL*, PDWORD, PDWORD);
  Functor get_module_status = Functor(process_id);
  std::basic_string<TCHAR> guid(std::begin(product_guid), std::end(product_guid));
  if (!get_module_status(guid.c_str(), CBFS_MODULE_DRIVER, installed, version_high, version_low))
    AppendErrorMessageThenDisplayAndThrow(TEXT("Failed running 'GetModuleStatus' in DLL.\n"));
}

void InstallDriver(const std::string& product_guid, DWORD* reboot) {
  HINSTANCE dll_handle(LoadLibrary(InstallerDllPath()));
  if (!dll_handle)
    AppendErrorMessageThenDisplayAndThrow(TEXT("Failed to load installer DLL.\n"));
  on_scope_exit cleanup([&dll_handle] { FreeLibrary(dll_handle); });

#ifdef UNICODE
  FARPROC process_id(GetProcAddress(HMODULE(dll_handle), "InstallW"));
#else
  FARPROC process_id(GetProcAddress(HMODULE(dll_handle), "InstallA"));
#endif
  if (!process_id)
    AppendErrorMessageThenDisplayAndThrow(TEXT("Failed to find 'Install' in DLL.\n"));

  typedef bool(__stdcall* Functor)(const TCHAR*, const TCHAR*, const TCHAR*, bool, DWORD, DWORD*);
  Functor install = Functor(process_id);
  std::basic_string<TCHAR> guid(std::begin(product_guid), std::end(product_guid));
  if (!install(CabinetFilePath(), guid.c_str(), TEXT(""), true,
               CBFS_MODULE_NET_REDIRECTOR_DLL | CBFS_MODULE_MOUNT_NOTIFIER_DLL, reboot))
    AppendErrorMessageThenDisplayAndThrow(TEXT("Failed running 'Install' in DLL.\n"));
}

void UninstallDriver(const std::string& product_guid, DWORD* reboot) {
  BOOL installed;
  DWORD version_high, version_low;
  GetDriverStatus(product_guid, &installed, &version_high, &version_low);
  if (!installed) {
    reboot = 0;
    return;
  }

  HINSTANCE dll_handle(LoadLibrary(InstallerDllPath()));
  if (!dll_handle)
    AppendErrorMessageThenDisplayAndThrow(TEXT("Failed to load installer DLL.\n"));
  on_scope_exit cleanup([&dll_handle] { FreeLibrary(dll_handle); });

#ifdef UNICODE
  FARPROC process_id(GetProcAddress(HMODULE(dll_handle), "UninstallW"));
#else
  FARPROC process_id(GetProcAddress(HMODULE(dll_handle), "UninstallA"));
#endif
  if (!process_id)
    AppendErrorMessageThenDisplayAndThrow(TEXT("Failed to find 'Uninstall' in DLL.\n"));

  typedef bool(__stdcall* Functor)(const TCHAR*, const TCHAR*, const TCHAR*, DWORD*);
  Functor uninstall = Functor(process_id);
  std::basic_string<TCHAR> guid(std::begin(product_guid), std::end(product_guid));
  if (!uninstall(CabinetFilePath(), guid.c_str(), TEXT(""), reboot))
    AppendErrorMessageThenDisplayAndThrow(TEXT("Failed running 'Uninstall' in DLL.\n"));
}

po::options_description AvailableOptions() {
  po::options_description installer_options("Installer options");
  installer_options.add_options()
      ("help,h", "Please note that driver installation may require a reboot")
      ("install,i", "Install the filesystem driver")
      ("uninstall,u", "Uninstall the filesystem driver")
      ("guid", po::value<std::string>()->default_value(BOOST_PP_STRINGIZE(PRODUCT_ID)),
       "Unique identifier associated with the current product (defaults to MaidSafe GUID)");
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
    DisplayError("Parser error:\n " + std::string(e.what()) + "\nRun with -h to see all options.");
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_argument));
  }
  return variables_map;
}

void HandleHelp(const po::variables_map& variables_map) {
  if (variables_map.count("help")) {
    std::ostringstream sstream;
    sstream << AvailableOptions();
    std::string available_options(sstream.str());
    std::cout << available_options << "\n\n";
    std::basic_string<TCHAR> message(std::begin(available_options), std::end(available_options));
    std::basic_string<TCHAR> title(TEXT("Help"));
    MessageBox(nullptr, message.c_str(), title.c_str(), MB_ICONINFORMATION | MB_OK);
    throw MakeError(CommonErrors::success);
  }
}

void ValidateOptions(const std::string& product_guid, const po::variables_map& variables_map) {
  if (variables_map.count("install") && variables_map.count("uninstall")) {
    DisplayError("Conflicting options.  Specify exactly one of '--install' or '--uninstall'.");
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_argument));
  }

  if (!variables_map.count("install") && !variables_map.count("uninstall")) {
    DisplayError("No operation chosen.  Specify exactly one of '--install' or '--uninstall'.");
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_argument));
  }

  if (product_guid.empty()) {
    DisplayError("Can't specify empty GUID.");
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_argument));
  }

  if (!fs::exists(CabinetFilePath()) || !fs::exists(InstallerDllPath())) {
    DisplayError("CbFs cab file or dll not found.");
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::no_such_element));
  }
}

}  // namespace installer

}  // namespace drive

}  // namespace maidsafe

int main(int argc, char* argv[]) {
  try {
    auto installer_options(maidsafe::drive::installer::AvailableOptions());
    auto variables_map(maidsafe::drive::installer::ParseOptions(argc, argv, installer_options));

    maidsafe::drive::installer::HandleHelp(variables_map);
    std::string product_guid(variables_map.at("guid").as<std::string>());
    maidsafe::drive::installer::ValidateOptions(product_guid, variables_map);

    DWORD reboot(0);
    if (variables_map.count("install"))
      maidsafe::drive::installer::InstallDriver(product_guid, &reboot);
    else
      maidsafe::drive::installer::UninstallDriver(product_guid, &reboot);

    return reboot;
  }
  catch (const maidsafe::maidsafe_error& error) {
    // Success is thrown when Help option is invoked.
    if (error.code() == maidsafe::make_error_code(maidsafe::CommonErrors::success))
      return -1;
    maidsafe::drive::installer::DisplayError(std::string("Exception: ") + error.what());
    return maidsafe::ErrorToInt(error);
  }
  catch (const std::exception& e) {
    maidsafe::drive::installer::DisplayError(std::string("Exception: ") + e.what());
  }
  catch (...) {
    maidsafe::drive::installer::DisplayError("Exception of unknown type.");
  }
  return maidsafe::ErrorToInt(maidsafe::MakeError(maidsafe::CommonErrors::unknown));
}
