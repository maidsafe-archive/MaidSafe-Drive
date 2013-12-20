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

#ifndef MAIDSAFE_DRIVE_TOOLS_COMMANDS_COMMAND_UTILS_H_
#define MAIDSAFE_DRIVE_TOOLS_COMMANDS_COMMAND_UTILS_H_

#ifdef MAIDSAFE_WIN32
#include <Windows.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include <map>
#include <string>

#include "boost/filesystem/path.hpp"

namespace maidsafe {

namespace drive {

namespace tools {

struct Environment {
  Environment() : running(true), root(), temp() {}

  bool running;
  boost::filesystem::path root, temp;
#ifdef MAIDSAFE_WIN32
  std::map<boost::filesystem::path, std::pair<HANDLE, HANDLE>> files;
#else
  std::map<boost::filesystem::path, std::pair<int, int>> files;
#endif

 private:
  Environment(const Environment&);
  Environment(Environment&&);
  Environment& operator=(Environment);
};

struct Restart {};

extern const std::string kRestart;

std::string GetLine();
boost::filesystem::path GetRelativePath(const Environment& environment);
boost::filesystem::path ChooseRelativePath(const Environment& environment);

}  // namespace tools

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_TOOLS_COMMANDS_COMMAND_UTILS_H_
