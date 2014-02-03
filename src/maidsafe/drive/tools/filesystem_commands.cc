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

#include "maidsafe/drive/tools/filesystem_commands.h"

#include <functional>
#include <iostream>
#include <string>

#include "boost/filesystem/operations.hpp"
#include "boost/program_options.hpp"

#include "maidsafe/common/error.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/drive/tools/commands/close_file_command.h"
#include "maidsafe/drive/tools/commands/create_file_command.h"
#include "maidsafe/drive/tools/commands/exit_tool_command.h"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace maidsafe {

namespace drive {

namespace tools {

namespace {

#ifdef _MSC_VER
// This function is needed to avoid use of po::bool_switch causing MSVC warning C4505:
// 'boost::program_options::typed_value<bool>::name' : unreferenced local function has been removed.
void UseUnreferenced() {
  auto dummy = po::typed_value<bool>(nullptr);
  static_cast<void>(dummy);
}
#endif

Environment g_environment;

std::function<void()> clean_root([] {
  boost::system::error_code error_code;
  fs::directory_iterator end;
  for (fs::directory_iterator directory_itr(g_environment.root); directory_itr != end;
       ++directory_itr) {
    fs::remove_all(*directory_itr, error_code);
  }
});

template <typename Command>
void PrintMainInfo() {
  std::string dots(40U - Command::kName.size(), '.');
  std::cout << Command::kName << " " << dots << " " << static_cast<int>(Command::kTypeId) << '\n';
}

template <typename Command>
void Run() {
  std::cout << "\n\t" << Command::kName << " chosen.\n";
  Command command(g_environment);
  command.Run();
}

}  // namespace

void PrintAvailableCommands() {
  std::cout << "============================================\nAvailable commands:\n";
  PrintMainInfo<ExitToolCommand>();
  PrintMainInfo<CreateFileCommand>();
  PrintMainInfo<CloseFileCommand>();
}

void GetAndExecuteCommand() {
  std::cout << "Enter command choice: ";
  Operation operation(Operation::kUninitialised);
  while (operation == Operation::kUninitialised) {
    try {
      std::string choice;
      std::getline(std::cin, choice);
      operation = static_cast<Operation>(std::stoi(choice));
      switch (operation) {
        case ExitToolCommand::kTypeId:
          return Run<ExitToolCommand>();
        case CreateFileCommand::kTypeId:
          return Run<CreateFileCommand>();
        case CloseFileCommand::kTypeId:
          return Run<CloseFileCommand>();
        default:
          BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
      }
    }
    catch (const Restart&) { return; }
    catch (const std::exception&) {
      std::cout << "Invalid choice.  Enter number between 0 and "
                << static_cast<int>(Operation::kUninitialised) - 1 << " inclusive: ";
      operation = Operation::kUninitialised;
    }
  }
}

}  // namespace tools

}  // namespace drive


namespace test {

int RunTool(int /*argc*/, char** /*argv*/, const fs::path& root, const fs::path& temp) {
//  std::vector<std::string> arguments(argv, argv + argc);
  drive::tools::g_environment.root = root;
  drive::tools::g_environment.temp = temp;
  on_scope_exit cleanup(drive::tools::clean_root);

  while (drive::tools::g_environment.running) {
    drive::tools::PrintAvailableCommands();
    drive::tools::GetAndExecuteCommand();
  }

  return 0;
}

}  // namespace test

}  // namespace maidsafe
