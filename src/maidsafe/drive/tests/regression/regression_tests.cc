#include <functional>

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"

#include "maidsafe/common/test.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/drive/drive.h"
#include "maidsafe/drive/tools/launcher.h"
#include "maidsafe/drive/tests/test_utils.h"

namespace fs = boost::filesystem;

namespace maidsafe {
namespace test {
namespace {

fs::path g_temp_path, g_root_path, g_storage_path;

std::function<void()> MountDrive(std::unique_ptr<drive::Launcher>& launcher,
                                 const drive::DriveType& drive_type) {
  g_temp_path = fs::unique_path(fs::temp_directory_path() /
                                "MaidSafe_Test_Issues_%%%%-%%%%-%%%%");
  fs::create_directory(g_temp_path);
  LOG(kInfo) << "Created temp directory " << g_temp_path;

#ifdef MAIDSAFE_WIN32
  g_root_path = drive::GetNextAvailableDrivePath();
#else
  g_root_path = fs::unique_path(GetHomeDir() / "MaidSafe_Root_Issues_%%%%-%%%%-%%%%");
  fs::create_directory(g_root_path);
#endif
  LOG(kInfo) << "Set up root at " << g_root_path;

  g_storage_path = fs::unique_path(fs::temp_directory_path() /
                                   "MaidSafe_Test_Storage_%%%%-%%%%-%%%%");
  fs::create_directory(g_storage_path);
  LOG(kInfo) << "Created storage_path " << g_storage_path;

  drive::Options options;
  options.mount_path = g_root_path;
  options.storage_path = g_storage_path;
  options.drive_name = RandomAlphaNumericString(10);
  options.unique_id = Identity(RandomString(64));
  options.root_parent_id = Identity(RandomString(64));
  options.create_store = true;
  options.drive_type = drive_type;
  options.drive_logging_args = "--log_* V --log_no_async";

  launcher.reset(new drive::Launcher(options));
  g_root_path = launcher->kMountPath();

  return [] {  // NOLINT
    boost::system::error_code error_code;

    if (fs::remove_all(g_temp_path, error_code) == 0 || error_code)
      LOG(kWarning) << "Failed to remove temp_path " << g_temp_path << ": "
                    << error_code.message();
    else
      LOG(kInfo) << "Removed " << g_temp_path;

    if (fs::exists(g_root_path, error_code)) {
      if (fs::remove_all(g_root_path, error_code) == 0 || error_code) {
        LOG(kWarning) << "Failed to remove root directory " << g_root_path << ": "
                      << error_code.message();
      } else {
        LOG(kInfo) << "Removed " << g_root_path;
      }
    }

    if (fs::remove_all(g_storage_path, error_code) == 0 || error_code) {
      LOG(kWarning) << "Failed to remove storage_path " << g_storage_path << ": "
                    << error_code.message();
    } else {
      LOG(kInfo) << "Removed " << g_storage_path;
    }
  };
}

void UnmountDrive(std::unique_ptr<drive::Launcher>& launcher) {
  if (launcher)
    launcher->StopDriveProcess();
}

void GetFilesTotalSize(const fs::path& path, uintmax_t& size) {
  fs::directory_iterator it(path), end;
  while (it != end) {
    if (fs::is_regular_file(it->path()))
      size += fs::file_size(it->path());
    else if (fs::is_directory(it->path()))
      GetFilesTotalSize(it->path(), size);
    else
      boost::throw_exception(std::invalid_argument("Invalid Path Element"));
    ++it;
  }
}

}  // unnamed namespace


// Regression tests for fixed issues.

TEST_CASE("Issue38, buffer path not removed", "[Filesystem][behavioural]") {
  boost::system::error_code error_code;
  fs::path buffer_path(GetUserAppDir().parent_path() / "LocalDriveConsole//Buffers");

  CHECK(!fs::exists(buffer_path, error_code));

  {
    std::unique_ptr<drive::Launcher> launcher;
    auto cleanup_function(MountDrive(launcher, drive::DriveType::kLocalConsole));
    maidsafe::on_scope_exit cleanup_on_exit(cleanup_function);
    CHECK(launcher);
    CHECK(fs::exists(buffer_path, error_code));
    UnmountDrive(launcher);
  }

  CHECK(!fs::exists(buffer_path, error_code));
}

// Unresolved issues.

TEST_CASE("Storage path chunks not deleted", "[Filesystem][behavioural]") {
  // Related to SureFile Issue #50, the test should be reworked/removed when the implementation of
  // versions is complete and some form of communication is available to handle them. The test is
  // currently setup to highlight the issue and thus to fail.
  {
    boost::system::error_code error_code;
    std::unique_ptr<drive::Launcher> launcher;
    auto cleanup_function(MountDrive(launcher, drive::DriveType::kLocalConsole));
    maidsafe::on_scope_exit cleanup_on_exit(cleanup_function);
    CHECK(launcher);
    size_t file_size(1024 * 1024);
    uintmax_t initial_size(0), first_update_size(0), second_update_size(0);
    GetFilesTotalSize(g_storage_path, initial_size);
    fs::path test_file(
        maidsafe::drive::detail::test::CreateTestFileWithSize(g_root_path, file_size));
    GetFilesTotalSize(g_storage_path, first_update_size);
    fs::remove(test_file, error_code);
    GetFilesTotalSize(g_storage_path, second_update_size);
    CHECK(second_update_size < first_update_size);
    CHECK(initial_size == second_update_size);
    UnmountDrive(launcher);
  }
}

}  // namespace maidsafe
}  // namespace test

int main(int argc, char** argv) { return maidsafe::test::ExecuteMain(argc, argv); }