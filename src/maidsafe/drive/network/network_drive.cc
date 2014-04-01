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
#include "maidsafe/common/data_stores/local_store.h"

#include "maidsafe/passport/types.h"

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

fs::path g_root, g_temp, g_storage;
NetworkDrive* g_network_drive(nullptr);
std::once_flag g_unmount_flag;
const std::string kConfigFile("maidsafe_network_drive.conf");
std::string g_error_message;
int g_return_code(0);
bool g_call_once_(false);
std::vector<passport::PublicPmid> g_pmids_from_file_;
AsioService g_asio_service_(2);
std::shared_ptr<nfs_client::MaidNodeNfs> g_client_nfs_;

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
  if (g_test_type == TestType::kDisk) {
    g_root = fs::unique_path(base_dir / "MaidSafe_Root_Filesystem_%%%%-%%%%-%%%%");
    CreateDir(g_root);
  } else {
    g_root = drive::GetNextAvailableDrivePath();
  }
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


void Unmount() {
  std::call_once(g_unmount_flag, [&] {
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
  po::options_description options("NetworkDrive options");
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
  while (g_network_drive && process::IsRunning(parent_process_info))
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  Unmount();
}

void RoutingJoin(routing::Routing& routing,
                 const std::vector<boost::asio::ip::udp::endpoint>& peer_endpoints) {
  std::shared_ptr<std::promise<bool>> join_promise(std::make_shared<std::promise<bool>>());
  routing::Functors functors_;
  functors_.network_status = [join_promise](int result) {
//     std::cout << "Network health: " << result << std::endl;
    if ((result == 100) && (!g_call_once_)) {
      g_call_once_ = true;
      join_promise->set_value(true);
    }
  };
  functors_.typed_message_and_caching.group_to_group.message_received =
      [&](const routing::GroupToGroupMessage &msg) { 
        g_client_nfs_->HandleMessage(msg); };  // NOLINT
  functors_.typed_message_and_caching.group_to_single.message_received =
      [&](const routing::GroupToSingleMessage &msg) {
        g_client_nfs_->HandleMessage(msg); 
      };  // NOLINT
  functors_.typed_message_and_caching.single_to_group.message_received =
      [&](const routing::SingleToGroupMessage &msg) { 
        g_client_nfs_->HandleMessage(msg); };  // NOLINT
  functors_.typed_message_and_caching.single_to_single.message_received =
      [&](const routing::SingleToSingleMessage &msg) { 
        g_client_nfs_->HandleMessage(msg); };  // NOLINT
  functors_.request_public_key =
      [&](const NodeId & node_id, const routing::GivePublicKeyFunctor & give_key) {
        nfs::detail::PublicPmidHelper temp_helper;
        nfs::detail::DoGetPublicKey(*g_client_nfs_, node_id, give_key,
                                    g_pmids_from_file_, temp_helper);
      };
  routing.Join(functors_, peer_endpoints);
  auto future(std::move(join_promise->get_future()));
  auto status(future.wait_for(std::chrono::seconds(10)));
  if (status == std::future_status::timeout || !future.get()) {
    std::cout << "can't join routing network" << std::endl;
    BOOST_THROW_EXCEPTION(MakeError(RoutingErrors::not_connected));
  }
  std::cout << "Client node joined routing network" << std::endl;
}

int MountAndWaitForIpcNotification(const Options& options) {
std::cout << "MountAndWaitForIpcNotification 1 " << options.keys_path << std::endl;
  std::vector<passport::detail::AnmaidToPmid> all_keychains_ =
      maidsafe::passport::detail::ReadKeyChainList(options.keys_path);
std::cout << "MountAndWaitForIpcNotification 1A " << all_keychains_.size() << std::endl;
  for (auto& key_chain : all_keychains_)
    g_pmids_from_file_.push_back(passport::PublicPmid(key_chain.pmid));
  passport::detail::AnmaidToPmid key_chain(all_keychains_[10]);
std::cout << "MountAndWaitForIpcNotification 1B " << std::endl;
  routing::Routing client_routing_(key_chain.maid);
  passport::PublicPmid::Name pmid_name(Identity(key_chain.pmid.name().value));

  std::cout << "MountAndWaitForIpcNotification 1C " << std::endl;
  g_client_nfs_.reset(new nfs_client::MaidNodeNfs(g_asio_service_, client_routing_, pmid_name));
std::cout << "MountAndWaitForIpcNotification 1D" << std::endl;
  boost::asio::ip::udp::endpoint ep;
  ep.port(5483);
  ep.address(boost::asio::ip::address::from_string("192.168.0.36"));
  std::vector<boost::asio::ip::udp::endpoint> peer_endpoints;
  peer_endpoints.push_back(ep);
  RoutingJoin(client_routing_, peer_endpoints);
std::cout << "MountAndWaitForIpcNotification 2" << std::endl;
  bool account_exists(false);
  passport::PublicMaid public_maid(key_chain.maid);
  {
    passport::PublicAnmaid public_anmaid(key_chain.anmaid);
    auto future(g_client_nfs_->CreateAccount(nfs_vault::AccountCreation(public_maid,
                                                                      public_anmaid)));
    auto status(future.wait_for(boost::chrono::seconds(3)));
    if (status == boost::future_status::timeout) {
      std::cout << "can't create account" << std::endl;
      BOOST_THROW_EXCEPTION(MakeError(VaultErrors::failed_to_handle_request));
    }
    if (future.has_exception()) {
//       std::cout << "having error during create account" << std::endl;
      try {
        future.get();
      } catch (const maidsafe_error& error) {
//         std::cout << "caught a maidsafe_error : " << error.what() << std::endl;
        if (error.code() == make_error_code(VaultErrors::account_already_exists))
          account_exists = true;
      } catch (...) {
        std::cout << "caught an unknown exception" << std::endl;
      }
    }
  }
std::cout << "MountAndWaitForIpcNotification 3" << std::endl;
  bool register_pmid_for_client(true);
  if (account_exists) {
    std::cout << "Account exists for maid " << HexSubstr(public_maid.name()->string())
              << std::endl;
    register_pmid_for_client = false;
  } else {
    // waiting for syncs resolved
    boost::this_thread::sleep_for(boost::chrono::seconds(2));
    std::cout << "Account created for maid " << HexSubstr(public_maid.name()->string())
              << std::endl;
    // before register pmid, need to store pmid to network first
    g_client_nfs_->Put(passport::PublicPmid(key_chain.pmid));
    boost::this_thread::sleep_for(boost::chrono::seconds(2));
  }
std::cout << "MountAndWaitForIpcNotification 4" << std::endl;
  if (register_pmid_for_client) {
    {
      g_client_nfs_->RegisterPmid(nfs_vault::PmidRegistration(key_chain.maid, key_chain.pmid, false));
      boost::this_thread::sleep_for(boost::chrono::seconds(3));
std::cout << "MountAndWaitForIpcNotification 4 AAAAAAAAAAAAAA" << std::endl;
      auto future(g_client_nfs_->GetPmidHealth(pmid_name));
      auto status(future.wait_for(boost::chrono::seconds(3)));
std::cout << "MountAndWaitForIpcNotification 4 BBBBBBBBBBBBBB" << std::endl;
      if (status == boost::future_status::timeout) {
        std::cout << "can't fetch pmid health" << std::endl;
        BOOST_THROW_EXCEPTION(MakeError(VaultErrors::failed_to_handle_request));
      }
      std::cout << "The fetched PmidHealth for pmid_name " << HexSubstr(pmid_name.value.string())
                << " is " << future.get() << std::endl;
    }
    // waiting for the GetPmidHealth updating corresponding accounts
    boost::this_thread::sleep_for(boost::chrono::seconds(3));
    LOG(kInfo) << "Pmid Registered created for the client node to store chunks";
  }
std::cout << "MountAndWaitForIpcNotification 5" << std::endl;
//   boost::system::error_code error_code;
//   if (!fs::exists(options.storage_path, error_code)) {
//     LOG(kError) << options.storage_path << " doesn't exist.";
//     return error_code.value();
//   }
//   if (!fs::exists(GetUserAppDir(), error_code)) {
//     LOG(kError) << "Creating " << GetUserAppDir();
//     if (!fs::create_directories(GetUserAppDir(), error_code)) {
//       LOG(kError) << GetUserAppDir() << " creation failed.";
//       return error_code.value();
//     }
//   }

  NetworkDrive drive(g_client_nfs_, options.unique_id, options.root_parent_id, options.mount_path,
                     GetUserAppDir(), options.drive_name, options.mount_status_shared_object_name,
                     options.create_store);
  g_network_drive = &drive;
#ifdef MAIDSAFE_WIN32
  drive.SetGuid("MaidSafe-SureFile");
#endif
  // Start a thread to poll the parent process' continued existence *before* calling drive.Mount().
  std::thread poll_parent([&] { MonitorParentProcess(options); });

  drive.Mount();
  // Drive should already be unmounted by this point, but we need to make 'g_network_drive' null to
  // allow 'poll_parent' to join.
  Unmount();
  poll_parent.join();
  return 0;
}

int MountAndWaitForSignal(const Options& options) {
  std::vector<passport::detail::AnmaidToPmid> all_keychains_ =
      maidsafe::passport::detail::ReadKeyChainList(options.keys_path);
  for (auto& key_chain : all_keychains_)
    g_pmids_from_file_.push_back(passport::PublicPmid(key_chain.pmid));
  passport::detail::AnmaidToPmid key_chain(all_keychains_[10]);
  routing::Routing client_routing_(key_chain.maid);
  passport::PublicPmid::Name pmid_name(Identity(key_chain.pmid.name().value));

  g_client_nfs_.reset(new nfs_client::MaidNodeNfs(g_asio_service_, client_routing_, pmid_name));

  boost::asio::ip::udp::endpoint ep;
  ep.port(5483);
  ep.address(boost::asio::ip::address::from_string("192.168.0.36"));
  std::vector<boost::asio::ip::udp::endpoint> peer_endpoints;
  peer_endpoints.push_back(ep);
  RoutingJoin(client_routing_, peer_endpoints);

  bool account_exists(false);
  passport::PublicMaid public_maid(key_chain.maid);
  {
    passport::PublicAnmaid public_anmaid(key_chain.anmaid);
    auto future(g_client_nfs_->CreateAccount(nfs_vault::AccountCreation(public_maid,
                                                                        public_anmaid)));
    auto status(future.wait_for(boost::chrono::seconds(3)));
    if (status == boost::future_status::timeout) {
//       std::cout << "can't create account" << std::endl;
      BOOST_THROW_EXCEPTION(MakeError(VaultErrors::failed_to_handle_request));
    }
    if (future.has_exception()) {
//       std::cout << "having error during create account" << std::endl;
      try {
        future.get();
      } catch (const maidsafe_error& error) {
        std::cout << "caught a maidsafe_error : " << error.what() << std::endl;
        if (error.code() == make_error_code(VaultErrors::account_already_exists))
          account_exists = true;
      } catch (...) {
        std::cout << "caught an unknown exception" << std::endl;
      }
    }
  }

  bool register_pmid_for_client(true);
  if (account_exists) {
    std::cout << "Account exists for maid " << HexSubstr(public_maid.name()->string())
              << std::endl;
    register_pmid_for_client = false;
  } else {
    // waiting for syncs resolved
    boost::this_thread::sleep_for(boost::chrono::seconds(2));
    std::cout << "Account created for maid " << HexSubstr(public_maid.name()->string())
              << std::endl;
    // before register pmid, need to store pmid to network first
    g_client_nfs_->Put(passport::PublicPmid(key_chain.pmid));
    boost::this_thread::sleep_for(boost::chrono::seconds(2));
  }

  if (register_pmid_for_client) {
    g_client_nfs_->RegisterPmid(nfs_vault::PmidRegistration(key_chain.maid, key_chain.pmid, false));
    boost::this_thread::sleep_for(boost::chrono::seconds(3));
    auto future(g_client_nfs_->GetPmidHealth(pmid_name));
    auto status(future.wait_for(boost::chrono::seconds(3)));

    if (status == boost::future_status::timeout) {
      std::cout << "can't fetch pmid health" << std::endl;
      BOOST_THROW_EXCEPTION(MakeError(VaultErrors::failed_to_handle_request));
    }
    std::cout << "The fetched PmidHealth for pmid_name " << HexSubstr(pmid_name.value.string())
              << " is " << future.get() << std::endl;
    // waiting for the GetPmidHealth updating corresponding accounts
    boost::this_thread::sleep_for(boost::chrono::seconds(3));
    LOG(kInfo) << "Pmid Registered created for the client node to store chunks";
  }

//   boost::system::error_code error_code;
//   if (!fs::exists(options.storage_path, error_code)) {
//     LOG(kError) << options.storage_path << " doesn't exist.";
//     return error_code.value();
//   }
//   if (!fs::exists(GetUserAppDir(), error_code)) {
//     LOG(kError) << "Creating " << GetUserAppDir();
//     if (!fs::create_directories(GetUserAppDir(), error_code)) {
//       LOG(kError) << GetUserAppDir() << " creation failed.";
//       return error_code.value();
//     }
//   }
//   std::shared_ptr<nfs_client::MaidNodeNfs> client_nfs;
//   client_nfs.reset(std::move(g_client_nfs_));

  maidsafe::Identity unique_id(crypto::Hash<crypto::SHA512>(public_maid.name()->string()));
  maidsafe::Identity root_parent_id(crypto::Hash<crypto::SHA512>(unique_id.string()));
  std::cout << "unique_id : " << HexSubstr(unique_id.string()) << std::endl;
  std::cout << "root_parent_id : " << HexSubstr(root_parent_id.string()) << std::endl;
  bool create_store(true);
  if (account_exists)
    create_store = false;

  NetworkDrive drive(g_client_nfs_, unique_id, root_parent_id, options.mount_path,
                     GetUserAppDir(), options.drive_name, options.mount_status_shared_object_name,
                     create_store);

  g_network_drive = &drive;
#ifdef MAIDSAFE_WIN32
  drive.SetGuid("MaidSafe-SureFile");
#endif
  // Start a thread to poll the parent process' continued existence *before* calling drive.Mount().
// //   std::thread poll_parent([&] { MonitorParentProcess(options); });

  drive.Mount();
  // Drive should already be unmounted by this point, but we need to make 'g_network_drive' null to
  // allow 'poll_parent' to join.

  Unmount();
// //   poll_parent.join();
  return 0;
//   fs::path storage_path(options.storage_path / "local_store");
//   DiskUsage disk_usage(std::numeric_limits<uint64_t>().max());
//   auto storage(std::make_shared<data_stores::LocalStore>(storage_path, disk_usage));
// 
//   boost::system::error_code error_code;
//   if (!fs::exists(options.storage_path, error_code)) {
//     LOG(kError) << options.storage_path << " doesn't exist.";
//     return error_code.value();
//   }
//   if (!fs::exists(GetUserAppDir(), error_code)) {
//     LOG(kError) << "Creating " << GetUserAppDir();
//     if (!fs::create_directories(GetUserAppDir(), error_code)) {
//       LOG(kError) << GetUserAppDir() << " creation failed.";
//       return error_code.value();
//     }
//   }
// 
//   NetworkDrive drive(storage, options.unique_id, options.root_parent_id, options.mount_path,
//                      GetUserAppDir(), options.drive_name, "", options.create_store);
//   g_network_drive = &drive;
// #ifdef MAIDSAFE_WIN32
//   drive.SetGuid("MaidSafe-SureFile");
// #endif
//   drive.Mount();
//   return 0;
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

    maidsafe::drive::SetUpTempDirectory();
    maidsafe::drive::Options options;
    maidsafe::drive::SetUpRootDirectory(maidsafe::GetHomeDir());
    options.mount_path = maidsafe::drive::g_root;
    options.storage_path = maidsafe::drive::SetUpStorageDirectory();
    options.keys_path = fs::path(fs::temp_directory_path() / "key_directory.dat");
    options.drive_name = maidsafe::RandomAlphaNumericString(10);
    options.unique_id = maidsafe::Identity(maidsafe::RandomString(64));
    options.root_parent_id = maidsafe::Identity(maidsafe::RandomString(64));
    options.create_store = true;


    // Validate options and run the Drive
    maidsafe::drive::ValidateOptions(options);

      maidsafe::drive::SetSignalHandler();
      maidsafe::drive::MountAndWaitForSignal(options);

    maidsafe::drive::RemoveTempDirectory();
    maidsafe::drive::RemoveStorageDirectory(options.storage_path);
    maidsafe::drive::RemoveRootDirectory();
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


// namespace fs = boost::filesystem;
// namespace po = boost::program_options;
// 
// namespace maidsafe {
// namespace drive {
// 
// namespace {
// 
// 
// }  // unnamed namespace
// 
// template<typename Storage>
// struct GetDrive {
// #ifdef MAIDSAFE_WIN32
//   typedef CbfsDrive<Storage> type;
// #else
//   typedef FuseDrive<Storage> type;
// #endif
// };
// 
// int Mount(const fs::path &/*mount_dir*/, const fs::path &chunk_dir) {
//   fs::path storage_path(chunk_dir / "store");
//   DiskUsage disk_usage(1048576000);
//   MemoryUsage memory_usage(0);
//   std::shared_ptr<maidsafe::data_stores::LocalStore>
//       storage(new maidsafe::data_stores::LocalStore(storage_path, disk_usage));
// 
//   boost::system::error_code error_code;
//   if (!fs::exists(chunk_dir, error_code))
//     return error_code.value();
// 
//   std::string root_parent_id_str;
//   fs::path id_path(storage_path / "root_parent_id");
//   bool first_run(!fs::exists(id_path, error_code));
//   if (!first_run)
//     BOOST_VERIFY(ReadFile(id_path, &root_parent_id_str));
// 
//   // The following values are passed in and returned on unmount.
//   Identity unique_user_id(std::string(64, 'a'));
//   Identity root_parent_id(root_parent_id_str.empty() ? Identity() : Identity(root_parent_id_str));
//   std::string product_id;
// 
//   // typedef GetDrive<maidsafe::data_stores::LocalStore>::type Drive;
//   // Drive drive(storage,
//   //             unique_user_id,
//   //             root_parent_id,
//   //             mount_dir,
//   //             product_id,
//   //             "MaidSafeDrive");
//   // if (first_run)
//   //   BOOST_VERIFY(WriteFile(id_path, drive.root_parent_id().string()));
// 
//   // g_unmount_functor = [&] { drive.Unmount(); };  // NOLINT
//   // signal(SIGINT, CtrlCHandler);
//   // drive.Mount();
// 
//   return 0;
// }
// 
// }  // namespace drive
// }  // namespace maidsafe
// 
// 
// fs::path GetPathFromProgramOption(const std::string &option_name,
//                                   po::variables_map *variables_map,
//                                   bool must_exist) {
//   if (variables_map->count(option_name)) {
//     boost::system::error_code error_code;
//     fs::path option_path(variables_map->at(option_name).as<std::string>());
//     if (must_exist) {
//       if (!fs::exists(option_path, error_code) || error_code) {
//         LOG(kError) << "Invalid " << option_name << " option.  " << option_path
//                     << " doesn't exist or can't be accessed (error message: "
//                     << error_code.message() << ")";
//         return fs::path();
//       }
//       if (!fs::is_directory(option_path, error_code) || error_code) {
//         LOG(kError) << "Invalid " << option_name << " option.  " << option_path
//                     << " is not a directory (error message: "
//                     << error_code.message() << ")";
//         return fs::path();
//       }
//     } else {
//       if (fs::exists(option_path, error_code)) {
//         LOG(kError) << "Invalid " << option_name << " option.  " << option_path
//                     << " already exists (error message: "
//                     << error_code.message() << ")";
//         return fs::path();
//       }
//     }
//     LOG(kInfo) << option_name << " set to " << option_path;
//     return option_path;
//   } else {
//     LOG(kWarning) << "You must set the " << option_name << " option to a"
//                  << (must_exist ? "n " : " non-") << "existing directory.";
//     return fs::path();
//   }
// }
// 
// 
// #ifdef USES_WINMAIN
// int CALLBACK wWinMain(HINSTANCE /*handle_to_instance*/, HINSTANCE /*handle_to_previous_instance*/,
//                       PWSTR /*command_line_args_without_program_name*/, int /*command_show*/) {
//   int argc(0);
//   LPWSTR* argv(nullptr);
//   argv = CommandLineToArgvW(GetCommandLineW(), &argc);
//   typedef po::wcommand_line_parser CommandLineParser;
// #else
// int main(int argc, char* argv[]) {
//   typedef po::command_line_parser CommandLineParser;
//   maidsafe::log::Logging::Instance().Initialise(argc, argv);
// #endif
//   boost::system::error_code error_code;
// #ifdef MAIDSAFE_WIN32
//   fs::path logging_dir("C:\\ProgramData\\MaidSafeDrive\\logs");
// #else
//   fs::path logging_dir(fs::temp_directory_path(error_code) / "maidsafe_drive/logs");
//   if (error_code) {
//     LOG(kError) << error_code.message();
//     return 1;
//   }
// #endif
//   if (!fs::exists(logging_dir, error_code))
//     fs::create_directories(logging_dir, error_code);
//   if (error_code)
//     LOG(kError) << error_code.message();
//   if (!fs::exists(logging_dir, error_code))
//     LOG(kError) << "Couldn't create logging directory at " << logging_dir;
//   fs::path log_path(logging_dir / "maidsafe_drive");
//   // All command line parameters are only for this run. To allow persistance, update the config
//   // file. Command line overrides any config file settings.
//   try {
//     po::options_description options_description("Allowed options");
//     options_description.add_options()
//         ("help,h", "print this help message")
//         ("chunkdir,C", po::value<std::string>(), "set directory to store chunks")
//         ("mountdir,D", po::value<std::string>(), "set virtual drive name")
//         ("checkdata", "check all data (metadata and chunks)");
// 
//     po::variables_map variables_map;
//     po::store(CommandLineParser(argc, argv).options(options_description).allow_unregistered().
//                                             run(), variables_map);
//     po::notify(variables_map);
// 
//     // set up options for config file
//     po::options_description config_file_options;
//     config_file_options.add(options_description);
// 
//     // try open some config options
//     std::ifstream local_config_file("maidsafe_drive.conf");
// #ifdef MAIDSAFE_WIN32
//     fs::path main_config_path("C:/ProgramData/MaidSafeDrive/maidsafe_drive.conf");
// #else
//     fs::path main_config_path("/etc/maidsafe_drive.conf");
// #endif
//     std::ifstream main_config_file(main_config_path.string().c_str());
// 
//     // try local first for testing
//     if (local_config_file) {
//       LOG(kInfo) << "Using local config file \"maidsafe_drive.conf\"";
//       store(parse_config_file(local_config_file, config_file_options), variables_map);
//       notify(variables_map);
//     } else if (main_config_file) {
//       LOG(kInfo) << "Using main config file " << main_config_path;
//       store(parse_config_file(main_config_file, config_file_options), variables_map);
//       notify(variables_map);
//     } else {
//       LOG(kWarning) << "No configuration file found at " << main_config_path;
//     }
// 
//     if (variables_map.count("help")) {
//       std::cout << options_description << '\n';
//       return 1;
//     }
// 
//     fs::path chunkstore_path(GetPathFromProgramOption("chunkdir", &variables_map, true));
// #ifdef MAIDSAFE_WIN32
//     fs::path mount_path(GetPathFromProgramOption("mountdir", &variables_map, false));
// #else
//     fs::path mount_path(GetPathFromProgramOption("mountdir", &variables_map, true));
// #endif
// 
//     if (variables_map.count("stop")) {
//       LOG(kInfo) << "Trying to stop.";
//       return 0;
//     }
// 
//     if (chunkstore_path == fs::path() || mount_path == fs::path()) {
//       LOG(kWarning) << options_description;
//       return 1;
//     }
// 
//     int result(maidsafe::drive::Mount(mount_path, chunkstore_path));
//     return result;
//   }
//   catch(const std::exception& e) {
//     LOG(kError) << "Exception: " << e.what();
//     return 1;
//   }
//   catch(...) {
//     LOG(kError) << "Exception of unknown type!";
//   }
//   return 0;
// }
