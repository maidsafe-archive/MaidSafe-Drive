/*  Copyright 2013 MaidSafe.net limited

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


#ifndef MAIDSAFE_WIN32
#include <csignal>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/program_options.hpp"
#include "boost/system/error_code.hpp"

#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/error.h"
#include "maidsafe/common/ipc.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/drive/drive.h"
#include "maidsafe/drive/tools/launcher.h"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace maidsafe {

namespace test {

int RunTool(int argc, char** argv, const fs::path& root, const fs::path& temp,
            const drive::Options& options, std::shared_ptr<drive::Launcher> launcher,
            int test_type);

namespace {

#ifdef _MSC_VER
// This function is needed to avoid use of po::bool_switch causing MSVC warning C4505:
// 'boost::program_options::typed_value<bool>::name' : unreferenced local function has been removed.
void UseUnreferenced() {
  auto dummy = po::typed_value<bool>(nullptr);
  static_cast<void>(dummy);
}
#endif

fs::path g_root, g_temp, g_storage;
drive::Options g_options;
std::shared_ptr<drive::Launcher> g_launcher;
std::string g_error_message;
int g_return_code(0);
enum class TestType {
  kLocal = static_cast<int>(drive::DriveType::kLocal),
  kNetwork = static_cast<int>(drive::DriveType::kNetwork),
  kLocalConsole = static_cast<int>(drive::DriveType::kLocalConsole),
  kNetworkConsole = static_cast<int>(drive::DriveType::kNetworkConsole),
  kDisk
} g_test_type;
bool g_enable_vfs_logging;
#ifdef MAIDSAFE_WIN32
const std::string kHelpInfo(
    "You must pass exactly one of '--disk', '--local', '--local_console', "
    "'--network' or '--network_console'");
#else
const std::string kHelpInfo("You must pass exactly one of '--disk', '--local' or '--network'");
#endif

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
      LOG(kWarning) << "Failed to remove root directory " << g_root << ": " << error_code.message();
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

passport::MaidAndSigner CreateAccount() {
  passport::MaidAndSigner maid_and_signer{ passport::CreateMaidAndSigner() };
  auto maid_client = nfs_client::MaidClient::MakeShared(maid_and_signer);
  maid_client->Stop();
  LOG(kSuccess) << " Account created for MAID : " << DebugId(maid_and_signer.first.name());
  return maid_and_signer;
}

po::options_description CommandLineOptions() {
  boost::system::error_code error_code;
  po::options_description command_line_options(std::string("Filesystem Tool Options:\n") +
                                               kHelpInfo);
  command_line_options.add_options()("help,h", "Show help message.")
      ("disk", "Perform all tests/benchmarks on native hard disk.")
      ("local", "Perform all tests/benchmarks on local VFS.")
      ("network", "Perform all tests/benchmarks on network VFS.");
#ifdef MAIDSAFE_WIN32
  command_line_options.add_options()
      ("local_console", "Perform all tests/benchmarks on local VFS running as a console app.")
      ("network_console", "Perform all tests/benchmarks on network VFS running as a console app.")
      ("enable_vfs_logging", po::bool_switch(&g_enable_vfs_logging), "Enable logging on the VFS "
       "(this is only useful if used with '--local_console' or '--network_console'.");
#else
  command_line_options.add_options()(
      "enable_vfs_logging", po::bool_switch(&g_enable_vfs_logging),
      "Enable logging on the VFS (this is only useful if used with '--local' or '--network'.");
#endif

  return command_line_options;
}

po::variables_map ParseAllOptions(int& argc, char* argv[],
                                  std::vector<std::string>& unused_options) {
  auto command_line_options(CommandLineOptions());
  po::parsed_options parsed(po::command_line_parser(unused_options)
                                .options(command_line_options)
                                .allow_unregistered()
                                .run());

  po::variables_map variables_map;
  po::store(parsed, variables_map);
  po::notify(variables_map);

  // Strip used command line options before passing to RunTool function.
  unused_options = po::collect_unrecognized(parsed.options, po::include_positional);
  argc = static_cast<int>(unused_options.size());
  int position(0);
  for (const auto& unused_option : unused_options)
    std::strcpy(argv[position++], unused_option.c_str());  // NOLINT

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

void GetTestType(const po::variables_map& variables_map) {
  int option_count(0);
  if (variables_map.count("disk")) {
    ++option_count;
    g_test_type = TestType::kDisk;
  }
  if (variables_map.count("local")) {
    ++option_count;
    g_test_type = TestType::kLocal;
  }
  if (variables_map.count("network")) {
    ++option_count;
    g_test_type = TestType::kNetwork;
  }
  if (variables_map.count("local_console")) {
    ++option_count;
    g_test_type = TestType::kLocalConsole;
  }
  if (variables_map.count("network_console")) {
    ++option_count;
    g_test_type = TestType::kNetworkConsole;
  }
  if (option_count != 1) {
    std::ostringstream stream;
    stream << kHelpInfo << "'.  For all options, run '--help'\n\n";
    g_error_message = stream.str();
    g_return_code = 1;
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_argument));
  }
}

std::function<void()> PrepareDisk() {
  SetUpTempDirectory();
  SetUpRootDirectory(fs::temp_directory_path());
  return [] {  // NOLINT
    RemoveTempDirectory();
    RemoveRootDirectory();
  };
}

std::function<void()> PrepareLocalVfs() {
  SetUpTempDirectory();
  drive::Options options;
  SetUpRootDirectory(GetHomeDir());
  options.mount_path = g_root;
  options.storage_path = SetUpStorageDirectory();
  options.drive_name = RandomAlphaNumericString(10);
  options.unique_id = Identity(RandomString(64));
  options.root_parent_id = Identity(RandomString(64));
  options.create_store = true;
  options.drive_type = static_cast<drive::DriveType>(g_test_type);
  if (g_enable_vfs_logging)
    options.drive_logging_args = "--log_* V --log_colour_mode 2 --log_no_async";

  g_launcher.reset(new drive::Launcher(options));
  g_root = g_launcher->kMountPath();
  g_options = options;

  return [options] {  // NOLINT
    RemoveTempDirectory();
    RemoveStorageDirectory(options.storage_path);
    RemoveRootDirectory();
  };
}

/*
std::string GetStringFromProgramOption(const std::string& option_name,
                                       const po::variables_map& variables_map) {
  if (variables_map.count(option_name)) {
    std::string option_string(variables_map.at(option_name).as<std::string>());
    LOG(kInfo) << option_name << " set to " << option_string;
    return option_string;
  } else {
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::uninitialised));
  }
}
*/

std::function<void()> PrepareNetworkVfs() {
  SetUpTempDirectory();
  SetUpRootDirectory(GetHomeDir());

  drive::Options options;
  auto maid_and_signer = CreateAccount();

  crypto::AES256Key symm_key{ RandomString(crypto::AES256_KeySize) };
  crypto::AES256InitialisationVector symm_iv{ RandomString(crypto::AES256_IVSize) };
  crypto::CipherText encrypted_maid(passport::EncryptMaid(maid_and_signer.first, symm_key,
                                                          symm_iv));
  passport::PublicMaid public_maid(maid_and_signer.first);

  options.mount_path = g_root;
  options.drive_name = RandomAlphaNumericString(10);
  options.unique_id = Identity(crypto::Hash<crypto::SHA512>(public_maid.name()->string()));
  options.root_parent_id = Identity(crypto::Hash<crypto::SHA512>(options.unique_id.string()));
  options.encrypted_maid = encrypted_maid.data.string();
  options.symm_key = symm_key.string();
  options.symm_iv = symm_iv.string();
  options.create_store = true;
  options.drive_type = static_cast<drive::DriveType>(g_test_type);
  if (g_enable_vfs_logging)
    options.drive_logging_args = "--log_* V --log_colour_mode 2 --log_no_async";

  g_launcher.reset(new drive::Launcher(options));
  g_root = g_launcher->kMountPath();
  g_options = options;

  return [options] {  // NOLINT
    RemoveTempDirectory();
    RemoveRootDirectory();
  };
}

std::function<void()> PrepareTest() {
  switch (g_test_type) {
    case TestType::kDisk:
      return PrepareDisk();
    case TestType::kLocal:
    case TestType::kLocalConsole:
      return PrepareLocalVfs();
    case TestType::kNetwork:
    case TestType::kNetworkConsole:
      return PrepareNetworkVfs();
    default:
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_argument));
  }
}

}  // unnamed namespace

}  // namespace test

}  // namespace maidsafe

int main(int argc, char** argv) {
  try {
    auto unuseds(maidsafe::log::Logging::Instance().Initialise(argc, argv));
    std::vector<std::string> unused_options;
    for (const auto& unused : unuseds)
      unused_options.emplace_back(&unused[0]);
    auto variables_map(maidsafe::test::ParseAllOptions(argc, argv, unused_options));
    maidsafe::test::HandleHelp(variables_map);
    maidsafe::test::GetTestType(variables_map);

    auto cleanup_functor(maidsafe::test::PrepareTest());
    maidsafe::on_scope_exit cleanup_on_exit(cleanup_functor);

    auto tests_result(maidsafe::test::RunTool(argc, argv, maidsafe::test::g_root,
                                              maidsafe::test::g_temp, maidsafe::test::g_options,
                                              maidsafe::test::g_launcher,
                                              static_cast<int>(maidsafe::test::g_test_type)));
    if (maidsafe::test::g_launcher)
      maidsafe::test::g_launcher->StopDriveProcess();
    return tests_result;
  }
  catch (const std::exception& e) {
    if (!maidsafe::test::g_error_message.empty()) {
      std::cout << maidsafe::test::g_error_message;
      return maidsafe::test::g_return_code;
    }
    LOG(kError) << "Exception: " << e.what();
  }
  catch (...) {
    LOG(kError) << "Exception of unknown type!";
  }
  return 64;
}
