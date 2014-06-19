/*  Copyright 2014 MaidSafe.net limited

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

#ifndef MAIDSAFE_DRIVE_TOOLS_LAUNCHER_H_
#define MAIDSAFE_DRIVE_TOOLS_LAUNCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "maidsafe/common/utils.h"

#include "boost/filesystem/path.hpp"
#include "boost/interprocess/mapped_region.hpp"
#include "boost/interprocess/shared_memory_object.hpp"
#include "boost/interprocess/sync/interprocess_mutex.hpp"
#include "boost/interprocess/sync/interprocess_condition.hpp"
#include "boost/process/child.hpp"

#include "maidsafe/common/types.h"

#include "maidsafe/nfs/client/maid_node_nfs.h"

namespace maidsafe {

namespace drive {

struct MountStatus;
struct Options;

#ifdef MAIDSAFE_WIN32
boost::filesystem::path GetNextAvailableDrivePath();
#endif

boost::asio::ip::udp::endpoint GetBootstrapEndpoint(const std::string& peer);

// This derives a name for the shared memory object which will be used to store the MountStatus from
// the name of the initial shared memory passed to Drive on the command line.
std::string GetMountStatusSharedMemoryName(const std::string& initial_shared_memory_name);

void ReadAndRemoveInitialSharedMemory(const std::string& initial_shared_memory_name,
                                      Options& options);

void NotifyMountedAndWaitForUnmountRequest(const std::string& mount_status_shared_object_name);

void NotifyUnmounted(const std::string& mount_status_shared_object_name);

enum class DriveType { kLocal, kLocalConsole, kNetwork, kNetworkConsole };

struct MountStatus {
  MountStatus() : mutex(), condition(), mounted(false), unmount(false) {}

  mutable boost::interprocess::interprocess_mutex mutex;
  boost::interprocess::interprocess_condition condition;
  bool mounted, unmount;
};

struct Options {
  Options() : mount_path(), storage_path(), keys_path(), drive_name(), key_index(-1),
              unique_id(), root_parent_id(),
              create_store(false), check_data(false), monitor_parent(true),
              drive_type(DriveType::kNetwork),
              drive_logging_args(), mount_status_shared_object_name(), peer_endpoint(),
              encrypted_maid(), symm_key(), symm_iv(), parent_handle(nullptr) {}
  boost::filesystem::path mount_path, storage_path, keys_path, drive_name;
  int key_index;
  Identity unique_id, root_parent_id;
  bool create_store, check_data, monitor_parent;
  DriveType drive_type;
  std::string drive_logging_args, mount_status_shared_object_name, peer_endpoint,
              encrypted_maid, symm_key, symm_iv;
  void* parent_handle;
};

class Launcher {
 public:
  explicit Launcher(const Options& options);
  Launcher(Options& options, const passport::Anmaid& anmaid);
  ~Launcher();
  // If the attempt to stop the child process via IPC notification fails, then the child can be
  // terminated if required.  This should not be needed if the child is monitoring the parent
  // process (i.e. this process) and will exit if the parent process stops.
  void StopDriveProcess(bool terminate_on_ipc_failure = false);
  boost::filesystem::path kMountPath() const { return kMountPath_; }

 private:
  Launcher(const Launcher&);
  Launcher(Launcher&&);
  Launcher& operator=(Launcher);

  void LogIn(Options& options, const passport::Anmaid& anmaid);
  void CreateInitialSharedMemory(const Options& options);
  void CreateMountStatusSharedMemory();
  void StartDriveProcess(const Options& options);
  boost::filesystem::path GetDriveExecutablePath(DriveType drive_type);
  void WaitForDriveToMount();
  void Cleanup();

  std::string initial_shared_memory_name_;
  const boost::filesystem::path kMountPath_;
  boost::interprocess::shared_memory_object mount_status_shared_object_;
  boost::interprocess::mapped_region mount_status_mapped_region_;
  MountStatus* mount_status_;
  void* this_process_handle_;
  std::unique_ptr<boost::process::child> drive_process_;
};

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_TOOLS_LAUNCHER_H_
