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
template<typename Storage>
struct Drive {
  typedef CbfsDriveInUserSpace<Storage> TestDriveInUserSpace;
};
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

template<typename Storage>
class DerivedDriveInUserSpace : public Drive<Storage>::TestDriveInUserSpace {
 public:
  DerivedDriveInUserSpace(Storage& storage,
                          const passport::Maid& default_maid,
                          const Identity& unique_user_id,
                          const std::string& root_parent_id,
                          const boost::filesystem::path &mount_dir,
                          const boost::filesystem::path &drive_name,
                          const int64_t& max_space,
                          const int64_t& used_space)
      : Drive<Storage>::TestDriveInUserSpace(storage,
                                             default_maid,
                                             unique_user_id,
                                             root_parent_id,
                                             mount_dir,
                                             drive_name,
                                             max_space,
                                             used_space) {}

  std::shared_ptr<DirectoryListingHandler<Storage>> directory_listing_handler() const {
    return directory_listing_handler_;
  }
};

template<typename Storage>
std::shared_ptr<DerivedDriveInUserSpace<Storage>> MakeAndMountDrive(
    const Identity& unique_user_id,
    const std::string& root_parent_id,
    routing::Routing& routing,
    const passport::Maid& maid,
    const maidsafe::test::TestPath& main_test_dir,
    const int64_t &max_space,
    const int64_t &used_space,
    std::shared_ptr<Storage>& storage,
    fs::path& mount_directory) {
  // won't work for objects with different signaturess...
  storage.reset(new Storage(*main_test_dir / "local", DiskUsage(1073741824)));
  std::shared_ptr<DerivedDriveInUserSpace<Storage>> drive(
      std::make_shared<DerivedDriveInUserSpace<Storage>>(*data_store,
                                                         maid,
                                                         unique_user_id,
                                                         root_parent_id,
                                                         "S:",
                                                         "MaidSafeDrive",
                                                         max_space,
                                                         used_space));

#ifdef WIN32
  fs::path mount_dir("S:");
#else
  fs::path mount_dir(*main_test_dir / "MaidSafeDrive");
#endif

  boost::system::error_code error_code;
#ifndef WIN32
  fs::create_directories(mount_dir, error_code);
  if (error_code) {
    LOG(kError) << "Failed creating mount directory";
//     asio_service.Stop();
    return std::shared_ptr<DerivedDriveInUserSpace<Storage>>();
  }
#endif

#ifdef WIN32
  mount_dir /= "\\Owner";
#else
  // TODO(Team): Find out why, if the mount is put on the asio service,
  //             unmount hangs
  boost::thread th(std::bind(&DerivedDriveInUserSpace<Storage>::Mount, drive));
  if (!drive->WaitUntilMounted()) {
    LOG(kError) << "Drive failed to mount";
//     asio_service.Stop();
    return std::shared_ptr<DerivedDriveInUserSpace<Storage>>();
  }
#endif

  mount_directory = mount_dir;

  return drive;
}

template<typename Storage>
void UnmountDrive(std::shared_ptr<DerivedDriveInUserSpace<Storage>> drive,
                  AsioService& asio_service) {
  int64_t max_space(0), used_space(0);
#ifdef WIN32
  EXPECT_TRUE(drive->Unmount(max_space, used_space));
#else
  drive->Unmount(max_space, used_space);
  drive->WaitUntilUnMounted();
#endif
  asio_service.Stop();
}

template<typename Storage>
struct GlobalDrive {
  static std::shared_ptr<DerivedDriveInUserSpace<Storage>> g_drive;
};

template<typename Storage>
std::shared_ptr<DerivedDriveInUserSpace<Storage>> GlobalDrive<Storage>::g_drive;

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
