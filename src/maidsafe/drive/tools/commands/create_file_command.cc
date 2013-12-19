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

#include "maidsafe/drive/tools/commands/create_file_command.h"

#include <iostream>

#include "maidsafe/drive/tools/commands/command_utils.h"

namespace maidsafe {

namespace drive {

namespace tools {

const std::string CreateFileCommand::kName("Create file");

void CreateFileCommand::Run() {
  auto path(GetRelativePath(environment_));
  auto virtual_file(CreateFile((environment_.root / path).wstring().c_str(), GENERIC_ALL, 0, NULL,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
  auto real_file(CreateFile((environment_.root / path).wstring().c_str(), GENERIC_ALL, 0, NULL,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
  std::cout << (virtual_file == nullptr ? "\tFailed to create" : "\tCreated") << " virtual file "
            << environment_.root / path << '\n';
  std::cout << (real_file == nullptr ? "\tFailed to create" : "\tCreated") << " real file "
            << environment_.temp / path << '\n';
  if (virtual_file && real_file)
    environment_.files.emplace(path, std::make_pair(virtual_file, real_file));
}

}  // namespace tools

}  // namespace drive

}  // namespace maidsafe
