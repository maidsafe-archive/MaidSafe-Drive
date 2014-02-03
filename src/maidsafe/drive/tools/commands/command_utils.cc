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

#include "maidsafe/drive/tools/commands/command_utils.h"

#include <iostream>
#include <iterator>

#include "maidsafe/common/error.h"
#include "maidsafe/drive/utils.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace tools {

const std::string kRestart("restart");

std::string GetLine() {
  std::cout << " (enter \"Restart\" to go back to main menu): ";
  std::string choice;
  std::getline(std::cin, choice);
  if (detail::GetLowerCase(choice) == kRestart)
    throw Restart();
  return choice;
}

boost::filesystem::path GetRelativePath(const Environment& environment) {
  boost::filesystem::path path;
  std::cout << "\tEnter relative path";
  while (path.empty()) {
    try {
      path = fs::path(GetLine());
      if (path.empty())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
    }
    catch (const std::exception&) {
      std::cout << "\tInvalid choice.  Enter path relative to " << environment.root;
    }
  }
  return path;
}

boost::filesystem::path ChooseRelativePath(const Environment& environment) {
  if (environment.files.empty()) {
    std::cout << "\tInvalid selection; no open files.  Going back to main menu.\n";
    throw Restart();
  }

  std::cout << "\tCurrently-open files:\n";
  size_t index(0);
  for (const auto& file : environment.files)
    std::cout << "\t    " << index++ << '\t' << file.first << '\n';

  boost::filesystem::path path;
  std::cout << "\tChoose open file.  Enter index number, not file name";
  while (path.empty()) {
    try {
      index = static_cast<size_t>(std::stoull(GetLine()));
      if (index >= environment.files.size())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
      auto itr(std::begin(environment.files));
      std::advance(itr, index);
      path = itr->first;
    }
    catch (const std::exception&) {
      std::cout << "\tInvalid choice.  Enter index number of chosen file";
    }
  }
  return path;
}

}  // namespace tools

}  // namespace drive

}  // namespace maidsafe
