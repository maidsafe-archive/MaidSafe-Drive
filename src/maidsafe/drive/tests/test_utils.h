/* Copyright 2011 MaidSafe.net limited

This MaidSafe Software is licensed under the MaidSafe.net Commercial License, version 1.0 or later,
and The General Public License (GPL), version 3. By contributing code to this project You agree to
the terms laid out in the MaidSafe Contributor Agreement, version 1.0, found in the root directory
of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also available at:

http://www.novinet.com/license

Unless required by applicable law or agreed to in writing, software distributed under the License is
distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing permissions and limitations under the
License.
*/

#ifndef MAIDSAFE_DRIVE_TESTS_TEST_UTILS_H_
#define MAIDSAFE_DRIVE_TESTS_TEST_UTILS_H_

#include <string>

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

#include "maidsafe/common/rsa.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"

// #include "maidsafe/data_store/data_store.h"
#include "maidsafe/data_store/permanent_store.h"
#include "maidsafe/nfs/nfs.h"

#include "maidsafe/encrypt/data_map.h"
#include "maidsafe/encrypt/self_encryptor.h"

#ifdef WIN32
#  ifdef HAVE_CBFS
#    include "maidsafe/drive/win_drive.h"
#  else
#    include "maidsafe/drive/dummy_win_drive.h"
#  endif
#else
#  include "maidsafe/drive/unix_drive.h"
#endif

namespace fs = boost::filesystem;
namespace bptime = boost::posix_time;

namespace maidsafe {

namespace drive {

#ifdef WIN32
#  ifdef HAVE_CBFS
typedef CbfsDriveInUserSpace TestDriveInUserSpace;
#  else
typedef DummyWinDriveInUserSpace TestDriveInUserSpace;
#  endif
#else
typedef FuseDriveInUserSpace TestDriveInUserSpace;
#endif

namespace test {

enum TestOperationCode {
  kCopy = 0,
  kRead = 1,
  kCompare = 2
};

class DerivedDriveInUserSpace : public TestDriveInUserSpace {
 public:
  // typedef data_store::DataStore<data_store::DataBuffer> DataStore;
  typedef data_store::PermanentStore DataStore;

  DerivedDriveInUserSpace(nfs::ClientMaidNfs& client_nfs,
                          DataStore& data_store,
                          const passport::Maid& default_maid,
                          const Identity& unique_user_id,
                          const std::string& root_parent_id,
                          const boost::filesystem::path &mount_dir,
                          const boost::filesystem::path &drive_name,
                          const int64_t& max_space,
                          const int64_t& used_space)
      : TestDriveInUserSpace(client_nfs,
                             data_store,
                             default_maid,
                             unique_user_id,
                             root_parent_id,
                             mount_dir,
                             drive_name,
                             max_space,
                             used_space) {}

  std::shared_ptr<DirectoryListingHandler> directory_listing_handler() const {
    return directory_listing_handler_;
  }
};

std::shared_ptr<DerivedDriveInUserSpace> MakeAndMountDrive(
    const std::string &unique_user_id,
    const std::string &root_parent_id,
    routing::Routing& routing,
    const passport::Maid& maid,
    const maidsafe::test::TestPath& main_test_dir,
    const int64_t &max_space,
    const int64_t &used_space,
    std::shared_ptr<nfs::ClientMaidNfs>& client_nfs,
    /*std::shared_ptr<data_store::DataStore<data_store::DataBuffer>>& data_store,*/
    std::shared_ptr<data_store::PermanentStore>& data_store,
    fs::path& mount_directory);

void UnmountDrive(std::shared_ptr<DerivedDriveInUserSpace> drive,
                  AsioService& asio_service);

void PrintResult(const bptime::ptime &start_time,
                 const bptime::ptime &stop_time,
                 size_t size,
                 TestOperationCode operation_code);
fs::path CreateTestFile(fs::path const& path, int64_t &file_size);
fs::path CreateTestFileWithSize(fs::path const& path, size_t size);
fs::path CreateTestFileWithContent(fs::path const& path, const std::string &content);
fs::path CreateTestDirectory(fs::path const& path);
fs::path CreateTestDirectoriesAndFiles(fs::path const& path);
fs::path CreateNamedFile(fs::path const& path, const std::string &name, int64_t &file_size);
fs::path CreateNamedDirectory(fs::path const& path, const std::string &name);
bool ModifyFile(fs::path const& path, int64_t &file_size);
bool SameFileContents(fs::path const& path1, fs::path const& path2);
int64_t CalculateUsedSpace(fs::path const& path);

uint64_t TotalSize(encrypt::DataMapPtr data_map);

}  // namespace test

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_TESTS_TEST_UTILS_H_
