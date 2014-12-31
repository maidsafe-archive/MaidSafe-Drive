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

#ifndef MAIDSAFE_DRIVE_TESTS_TEST_UTILS_H_
#define MAIDSAFE_DRIVE_TESTS_TEST_UTILS_H_

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

#include "maidsafe/common/rsa.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/encrypt/data_map.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/drive_api.h"
#include "maidsafe/drive/directory.h"


namespace fs = boost::filesystem;
namespace bptime = boost::posix_time;

namespace maidsafe {

namespace drive {

namespace detail {

namespace test {

enum TestOperationCode { kCopy = 0, kRead = 1, kCompare = 2 };

template <typename Storage>
struct GlobalDrive {
  static std::shared_ptr<typename VirtualDrive<Storage>::value_type> g_drive;
};

template <typename Storage>
std::shared_ptr<typename VirtualDrive<Storage>::value_type> GlobalDrive<Storage>::g_drive;

void PrintResult(const bptime::ptime& start_time, const bptime::ptime& stop_time, size_t size,
                 TestOperationCode operation_code);
fs::path CreateTestFile(fs::path const& path, int64_t& file_size);
fs::path CreateTestFileWithSize(fs::path const& path, size_t size);
fs::path CreateTestFileWithContent(fs::path const& path, const std::string& content);
fs::path CreateTestDirectory(fs::path const& path);
fs::path CreateTestDirectoriesAndFiles(fs::path const& path);
fs::path CreateNamedFile(fs::path const& path, const std::string& name, int64_t& file_size);
fs::path CreateNamedDirectory(fs::path const& path, const std::string& name);
bool ModifyFile(fs::path const& path, int64_t& file_size);
bool SameFileContents(fs::path const& path1, fs::path const& path2);

uint64_t TotalSize(const encrypt::DataMap& data_map);

void GenerateDirectoryListingEntryForFile(boost::asio::io_service& io_service,
                                          std::shared_ptr<Directory> directory,
                                          const fs::path& path, const uintmax_t& file_size);

void CheckedExists(const fs::path& path);
void CheckedNotExists(const fs::path& path);
void RequiredExists(const fs::path& path);
void RequiredNotExists(const fs::path& path);

void CheckedRemove(const fs::path& path);
void CheckedNotRemove(const fs::path& path);
void RequiredRemove(const fs::path& path);
void RequiredNotRemove(const fs::path& path);

void CheckedRename(const fs::path& old_path, const fs::path& new_path);
void CheckedNotRename(const fs::path& old_path, const fs::path& new_path);
void RequiredRename(const fs::path& old_path, const fs::path& new_path);
void RequiredNotRename(const fs::path& old_path, const fs::path& new_path);

void CheckedCreateDirectories(const fs::path& path);
void CheckedNotCreateDirectories(const fs::path& path);
void RequiredCreateDirectories(const fs::path& path);
void RequiredNotCreateDirectories(const fs::path& path);

template <typename SetType, typename Expected, typename Unexpected>
bool VerifyDistinctSets(const std::set<SetType>& expected_set,
                        const std::initializer_list<SetType>& all_possible_set,
                        const Expected& expected_predicate,
                        const Unexpected& unexpected_predicate) {
  std::vector<SetType> unexpected_set;

  for (const auto permission : all_possible_set) {
    if (expected_set.find(permission) == expected_set.end()) {
      unexpected_set.push_back(permission);
    }
  }

  const bool has_expected = std::find_if_not(expected_set.begin(), expected_set.end(),
                                             expected_predicate) == expected_set.end();

  const bool not_has_unexpected = std::find_if_not(unexpected_set.begin(), unexpected_set.end(),
                                                   unexpected_predicate) == unexpected_set.end();

  return has_expected && not_has_unexpected;
}

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_TESTS_TEST_UTILS_H_
