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

#include "maidsafe/drive/tools/commands/close_file_command.h"

#include <iostream>

#include "maidsafe/drive/tools/commands/command_utils.h"

namespace maidsafe {

namespace drive {

namespace tools {

const std::string CloseFileCommand::kName("Close file");

void CloseFileCommand::Run() {
  auto path(ChooseRelativePath(environment_));
  auto itr(environment_.files.find(path));
  assert(itr != std::end(environment_.files));
  assert(itr->second.first && itr->second.second);
#ifdef MAIDSAFE_WIN32
  auto virtual_result(CloseHandle(itr->second.first));
  auto real_result(CloseHandle(itr->second.second));
#else
  auto virtual_result(close(itr->second.first));
  auto real_result(close(itr->second.second));
#endif
  std::cout << (virtual_result == 0 ? "\tFailed to close" : "\tClosed") << " virtual file "
            << environment_.root / path << '\n';
  std::cout << (real_result == 0 ? "\tFailed to close" : "\tClosed") << " real file "
            << environment_.temp / path << '\n';
  environment_.files.erase(itr);
}

}  // namespace tools

}  // namespace drive

}  // namespace maidsafe
