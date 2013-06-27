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

#ifndef MAIDSAFE_DRIVE_UNIX_DRIVE_H_
#define MAIDSAFE_DRIVE_UNIX_DRIVE_H_

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif

#include <cstdio>
#include <map>
#include <memory>
#include <string>

#include "boost/filesystem/path.hpp"
#include "boost/thread/thread.hpp"

#ifdef MAIDSAFE_APPLE
#  include "maidsafe/drive/mac_fuse.h"
#  include "osxfuse/fuse/fuse.h"
#  include "sys/statvfs.h"
#else
#  include "fuse/fuse.h"
#  include "fuse/fuse_common.h"
#  include "fuse/fuse_opt.h"
#endif

#include "maidsafe/drive/drive_api.h"


namespace fs = boost::filesystem;

namespace maidsafe {

namespace priv {
namespace chunk_store { class RemoteChunkStore; }
}  // namespace priv

namespace drive {

class FuseDriveInUserSpace : public DriveInUserSpace {
 public:
  FuseDriveInUserSpace(ClientNfs& client_nfs,
                       DataStore& data_store,
                       const Maid& maid,
                       const Identity& unique_user_id,
                       const std::string& root_parent_id,
                       const fs::path &mount_dir,
                       const fs::path &drive_name,
                       const int64_t &max_space,
                       const int64_t &used_space);
  virtual ~FuseDriveInUserSpace();

  // Assigns customised functions to file sysyem operations
  virtual int Init();
  // Mounts the drive. Successful return means the drive is ready for IO operations
  virtual int Mount();
  // Unmount drive
  virtual int Unmount(int64_t &max_space, int64_t &used_space);
  // Return drive's used space
  int64_t UsedSpace() const;
  // Notifies filesystem of name change
  virtual void NotifyRename(const fs::path& from_relative_path,
                            const fs::path& to_relative_path) const;

 private:
  FuseDriveInUserSpace(const FuseDriveInUserSpace&);
  FuseDriveInUserSpace& operator=(const FuseDriveInUserSpace&);

  static int OpsCreate(const char *path, mode_t mode, struct fuse_file_info *file_info);
  static void OpsDestroy(void *fuse);
  static int OpsFlush(const char *path, struct fuse_file_info *file_info);
  static int OpsFtruncate(const char *path, off_t size, struct fuse_file_info *file_info);
  static int OpsMkdir(const char *path, mode_t mode);
  static int OpsMknod(const char *path, mode_t mode, dev_t rdev);
  static int OpsOpen(const char *path, struct fuse_file_info *file_info);
  static int OpsOpendir(const char *path, struct fuse_file_info *file_info);
  static int OpsRead(const char *path,
                     char *buf,
                     size_t size,
                     off_t offset,
                     struct fuse_file_info *file_info);
  static int OpsRelease(const char *path, struct fuse_file_info *file_info);
  static int OpsReleasedir(const char *path, struct fuse_file_info *file_info);
  static int OpsRmdir(const char *path);
  static int OpsTruncate(const char *path, off_t size);
  static int OpsUnlink(const char *path);
  static int OpsWrite(const char *path,
                      const char *buf,
                      size_t size,
                      off_t offset,
                      struct fuse_file_info *file_info);

//  static int OpsAccess(const char *path, int mask);
  static int OpsChmod(const char *path, mode_t mode);
  static int OpsChown(const char *path, uid_t uid, gid_t gid);
  static int OpsFgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *file_info);
  static int OpsFsync(const char *path, int isdatasync, struct fuse_file_info *file_info);
  static int OpsFsyncDir(const char *path, int isdatasync, struct fuse_file_info *file_info);
  static int OpsGetattr(const char *path, struct stat *stbuf);
//  static int OpsLink(const char *to, const char *from);
//  static int OpsLock(const char *path,
//                     struct fuse_file_info *file_info,
//                     int cmd,
//                     struct flock *lock);
  static int OpsReaddir(const char *path,
                        void *buf,
                        fuse_fill_dir_t filler,
                        off_t offset,
                        struct fuse_file_info *file_info);
  static int OpsReadlink(const char *path, char *buf, size_t size);
  static int OpsRename(const char *old_name, const char *new_name);
  static int OpsStatfs(const char *path, struct statvfs *stbuf);
  static int OpsSymlink(const char *to, const char *from);
  static int OpsUtimens(const char *path, const struct timespec ts[2]);

#ifdef HAVE_SETXATTR
//  xattr operations are optional and can safely be left unimplemented
//  static int OpsSetxattr(const char *path,
//                         const char *name,
//                         const char *value,
//                         size_t size, int flags);
//  static int OpsGetxattr(const char *path,
//                         const char *name,
//                         char *value,
//                         size_t size);
//  static int OpsListxattr(const char *path, char *list, size_t size);
//  static int OpsRemovexattr(const char *path, const char *name);
#endif  // HAVE_SETXATTR

  static int Release(const char *path, struct fuse_file_info *file_info);
  static void RenameOpenContexts(const std::string &old_path, const std::string &new_path);
  virtual void SetNewAttributes(FileContext *file_context, bool is_directory, bool read_only);

  static struct fuse_operations maidsafe_ops_;
  struct fuse *fuse_;
  struct fuse_chan *fuse_channel_;
  char *fuse_mountpoint_;
  std::string drive_name_;
  boost::thread fuse_event_loop_thread_;
  std::multimap<fs::path, std::shared_ptr<FileContext>> open_files_;
  static const bool kAllowMsHidden_;
};

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_UNIX_DRIVE_H_
