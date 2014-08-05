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
#include <iostream>  // NOLINT
#include <memory>
#include <string>
#include <fstream>  // NOLINT
#include <iterator>

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

#include "maidsafe/passport/types.h"
#include "maidsafe/passport/passport.h"

#include "maidsafe/nfs/client/maid_node_nfs.h"

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
typedef CbfsDrive<nfs_client::MaidNodeNfs> NetworkDrive;
#else
typedef FuseDrive<nfs_client::MaidNodeNfs> NetworkDrive;
#endif

std::unique_ptr<NetworkDrive> g_network_drive(nullptr);
std::shared_ptr<nfs_client::MaidNodeNfs> g_maid_node_nfs;
std::once_flag g_unmount_flag;
std::string g_error_message;
int g_return_code(0);

void Unmount() {
  std::call_once(g_unmount_flag, [&] {
    g_maid_node_nfs->Stop();
    g_network_drive->Unmount();
    g_network_drive = nullptr;
  });
}

#ifdef MAIDSAFE_WIN32

process::ProcessInfo GetParentProcessInfo(const Options& options) {
  return process::ProcessInfo(options.parent_handle);
}

BOOL CtrlHandler(DWORD control_type) {
  LOG(kInfo) << "Received console control signal " << control_type << ".  Unmounting.";
  if (!g_network_drive)
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

// void SetSignalHandler() {}

#endif

po::options_description CommandLineOptions() {
  po::options_description options("Network Drive options");
  options.add_options()
    ("help,h", "Show help message.")
    ("shared_memory", po::value<std::string>(), "Shared memory name (IPC).");
  return options;
}

template <typename Char>
po::variables_map ParseCommandLine(int argc, Char* argv[]) {
  auto command_line_options(CommandLineOptions());
  po::basic_parsed_options<Char> parsed(po::basic_command_line_parser<Char>(argc, argv)
                                        .options(command_line_options)
                                        .allow_unregistered()
                                        .run());
  po::variables_map variables_map;
  po::store(parsed, variables_map);
  po::notify(variables_map);

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

void GetOptions(const po::variables_map& variables_map, Options& options) {
  if (!variables_map.count("shared_memory"))
    BOOST_THROW_EXCEPTION(maidsafe::MakeError(maidsafe::CommonErrors::uninitialised));
  ReadAndRemoveInitialSharedMemory(variables_map.at("shared_memory").as<std::string>(), options);
}

void ValidateOptions(const Options& options) {
  std::string error_message;
  g_return_code = 0;

  if (options.mount_path.empty()) {
    error_message += "  mount_dir must be set\n";
    ++g_return_code;
  }
  if (options.drive_name.empty()) {
    error_message += "  drive_name must be set\n";
    ++g_return_code;
  }
  if (!options.unique_id.IsInitialised()) {
    error_message += "  unique_id must be set to a 64 character string\n";
    ++g_return_code;
  }
  if (!options.root_parent_id.IsInitialised()) {
    error_message += "  parent_id must be set to a 64 character string\n";
    ++g_return_code;
  }
  if (options.encrypted_maid.empty()) {
    error_message += "  encrypted_maid must be set\n";
    ++g_return_code;
  }
  if (options.symm_key.empty()) {
    error_message += "  symm_key must be set\n";
    ++g_return_code;
  }
  if (options.symm_iv.empty()) {
    error_message += "  symm_iv must be set\n";
    ++g_return_code;
  }

  if (g_return_code) {
    g_error_message = "Fatal error:\n" + error_message + "\n\n";
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
  }
}

void MonitorParentProcess(const Options& options) {
  auto parent_process_info(GetParentProcessInfo(options));
  while (g_network_drive && process::IsRunning(parent_process_info))
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  Unmount();
}

int Mount(const Options& options) {
  std::shared_ptr<passport::Maid> maid;
  boost::system::error_code error_code;

  fs::path user_app_dir(GetUserAppDir());
  if (!fs::exists(user_app_dir, error_code)) {
    LOG(kError) << "Creating " << user_app_dir;
    if (!fs::create_directories(user_app_dir, error_code)) {
      LOG(kError) << user_app_dir << " creation failed.";
      return error_code.value();
    }
  }

  crypto::AES256Key symm_key(options.symm_key);
  crypto::AES256InitialisationVector symm_iv(options.symm_iv);
  crypto::CipherText encrypted_maid(NonEmptyString(options.encrypted_maid));
  maid.reset(new passport::Maid(passport::DecryptMaid(encrypted_maid, symm_key, symm_iv)));

  g_maid_node_nfs = nfs_client::MaidNodeNfs::MakeShared(*maid);
  g_network_drive.reset(new NetworkDrive(g_maid_node_nfs, options.unique_id,
    options.root_parent_id, options.mount_path, user_app_dir, options.drive_name,
    options.mount_status_shared_object_name, options.create_store));

#ifdef MAIDSAFE_WIN32
  g_network_drive->SetGuid(BOOST_PP_STRINGIZE(PRODUCT_ID));
#endif

  if (options.monitor_parent) {
    std::thread poll_parent([&] { MonitorParentProcess(options); });
    g_network_drive->Mount();
    poll_parent.join();
  } else {
    g_network_drive->Mount();
  }
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
  try {
    auto variables_map(maidsafe::drive::ParseCommandLine(argc, argv));
    maidsafe::drive::HandleHelp(variables_map);
    maidsafe::drive::Options options;
    maidsafe::drive::GetOptions(variables_map, options);
    maidsafe::drive::ValidateOptions(options);
    return maidsafe::drive::Mount(options);
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
