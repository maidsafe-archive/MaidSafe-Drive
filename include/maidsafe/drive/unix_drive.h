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

#ifndef MAIDSAFE_DRIVE_UNIX_DRIVE_H_
#define MAIDSAFE_DRIVE_UNIX_DRIVE_H_

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "boost/filesystem/path.hpp"
#include "boost/thread/future.hpp"

#ifdef MAIDSAFE_APPLE
#include "sys/statvfs.h"
#endif
#include "fuse/fuse.h"
#include "fuse/fuse_common.h"
#include "fuse/fuse_lowlevel.h"
#include "fuse/fuse_opt.h"

#include "maidsafe/common/on_scope_exit.h"

#include "maidsafe/drive/drive.h"
#include "maidsafe/drive/file_context.h"
#include "maidsafe/drive/utils.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

template <typename Storage>
class FuseDrive;

template <typename Storage>
struct Global {
  static FuseDrive<Storage>* g_fuse_drive;
};

template <typename Storage>
FuseDrive<Storage>* Global<Storage>::g_fuse_drive;

namespace detail {

inline std::string GetFileType(mode_t mode) {
  if (S_ISFIFO(mode))
    return "FIFO-special";
  if (S_ISCHR(mode))
    return "Character-special";
  if (S_ISDIR(mode))
    return "Directory";
  if (S_ISBLK(mode))
    return "Block-special";
  if (S_ISREG(mode))
    return "Regular";
  return "";
}

// template <typename Storage>
// bool ForceFlush(RootHandler<Storage>& root_handler, FileContext<Storage>* file_context) {
//   assert(file_context);
//   file_context->self_encryptor->Flush();
//
//   try {
//     root_handler.UpdateParentDirectoryListing(file_context->meta_data->name.parent_path(),
//                                               *file_context->meta_data.get());
//   }
//   catch (const std::exception&) {
//     return false;
//   }
//   return true;
// }

}  // namespace detail

template <typename Storage>
class FuseDrive : public Drive<Storage> {
 public:
  FuseDrive(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
            const Identity& root_parent_id, const boost::filesystem::path& mount_dir,
            const boost::filesystem::path& user_app_dir, const boost::filesystem::path& drive_name,
            bool create);

  virtual ~FuseDrive();
  virtual void Mount();
  virtual void Unmount();

 private:
  FuseDrive(const FuseDrive&);
  FuseDrive(FuseDrive&&);
  FuseDrive& operator=(FuseDrive);

  void Init();

  static int OpsAccess(const char* path, int mask);
  static int OpsChmod(const char* path, mode_t mode);
  static int OpsChown(const char* path, uid_t uid, gid_t gid);
  static int OpsCreate(const char* path, mode_t mode, struct fuse_file_info* file_info);
  static void OpsDestroy(void* fuse);
  static int OpsFgetattr(const char* path, struct stat* stbuf, struct fuse_file_info* file_info);
  static int OpsFlush(const char* path, struct fuse_file_info* file_info);
  static int OpsFsync(const char* path, int isdatasync, struct fuse_file_info* file_info);
  static int OpsFsyncDir(const char* path, int isdatasync, struct fuse_file_info* file_info);
  static int OpsFtruncate(const char* path, off_t size, struct fuse_file_info* file_info);
  static int OpsGetattr(const char* path, struct stat* stbuf);
  static void* OpsInit(struct fuse_conn_info* conn);
//  static int OpsLink(const char* to, const char* from);
//  static int OpsLock(const char* path, struct fuse_file_info* file_info, int cmd,
//                     struct flock* lock);
  static int OpsMkdir(const char* path, mode_t mode);
  static int OpsMknod(const char* path, mode_t mode, dev_t rdev);
  static int OpsOpen(const char* path, struct fuse_file_info* file_info);
  static int OpsOpendir(const char* path, struct fuse_file_info* file_info);
  static int OpsRead(const char* path, char* buf, size_t size, off_t offset,
                     struct fuse_file_info* file_info);
  static int OpsReaddir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
                        struct fuse_file_info* file_info);
  static int OpsReadlink(const char* path, char* buf, size_t size);
  static int OpsRelease(const char* path, struct fuse_file_info* file_info);
  static int OpsReleasedir(const char* path, struct fuse_file_info* file_info);
  static int OpsRename(const char* old_name, const char* new_name);
  static int OpsRmdir(const char* path);
  static int OpsStatfs(const char* path, struct statvfs* stbuf);
  static int OpsSymlink(const char* to, const char* from);
  static int OpsTruncate(const char* path, off_t size);
  static int OpsUnlink(const char* path);
  static int OpsUtimens(const char* path, const struct timespec ts[2]);
  static int OpsWrite(const char* path, const char* buf, size_t size, off_t offset,
                      struct fuse_file_info* file_info);

// We can set extended attribute for our own purposes, i.e. if we wanted to store extra info
// (revisions for instance) then we can do it here.
#ifdef HAVE_SETXATTR
//  static int OpsGetxattr(const char* path, const char* name, char* value, size_t size);
//  static int OpsListxattr(const char* path, char* list, size_t size);
//  static int OpsRemovexattr(const char* path, const char* name);
//  static int OpsSetxattr(const char* path, const char* name, const char* value, size_t size,
//                         int flags);
#endif  // HAVE_SETXATTR

  static int CreateNew(const fs::path& full_path, mode_t mode, dev_t rdev = 0);
  static int GetAttributes(const char* path, struct stat* stbuf);
  static int Truncate(const char* path, off_t size);

  static struct fuse_operations maidsafe_ops_;
  struct fuse* fuse_;
  fuse_chan* fuse_channel_;
  fs::path fuse_mountpoint_;
  std::string drive_name_;
  std::thread fuse_event_loop_thread_;
  boost::promise<void> mount_promise_;
};

const int kMaxPath(4096);

template <typename Storage>
using g_fuse_drive = FuseDrive<Storage>*;

template <typename Storage>
struct fuse_operations FuseDrive<Storage>::maidsafe_ops_;

template <typename Storage>
FuseDrive<Storage>::FuseDrive(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
                              const Identity& root_parent_id,
                              const boost::filesystem::path& mount_dir,
                              const boost::filesystem::path& user_app_dir,
                              const boost::filesystem::path& drive_name,
                              bool create)
    : Drive<Storage>(storage, unique_user_id, root_parent_id, mount_dir, user_app_dir, create),
      fuse_(nullptr),
      fuse_channel_(nullptr),
      fuse_mountpoint_(mount_dir),
      drive_name_(drive_name.string()),
      fuse_event_loop_thread_(),
      mount_promise_() {
  fs::create_directory(fuse_mountpoint_);
  Init();
}

template <typename Storage>
FuseDrive<Storage>::~FuseDrive() {
  Unmount();
  log::Logging::Instance().Flush();
}

template <typename Storage>
void FuseDrive<Storage>::Init() {
  Global<Storage>::g_fuse_drive = this;
//  maidsafe_ops_.access = OpsAccess;
  maidsafe_ops_.chmod = OpsChmod;
  maidsafe_ops_.chown = OpsChown;
  maidsafe_ops_.create = OpsCreate;
  maidsafe_ops_.destroy = OpsDestroy;
  maidsafe_ops_.fgetattr = OpsFgetattr;
  maidsafe_ops_.flush = OpsFlush;
//  maidsafe_ops_.fsync = OpsFsync;
//  maidsafe_ops_.fsyncdir = OpsFsyncDir;
  maidsafe_ops_.ftruncate = OpsFtruncate;
  maidsafe_ops_.getattr = OpsGetattr;
  maidsafe_ops_.init = OpsInit;
//  maidsafe_ops_.link = OpsLink;
//  maidsafe_ops_.lock = OpsLock;
  maidsafe_ops_.mkdir = OpsMkdir;
  maidsafe_ops_.mknod = OpsMknod;
  maidsafe_ops_.open = OpsOpen;
  maidsafe_ops_.opendir = OpsOpendir;
  maidsafe_ops_.read = OpsRead;
  maidsafe_ops_.readdir = OpsReaddir;
//  maidsafe_ops_.readlink = OpsReadlink;
  maidsafe_ops_.release = OpsRelease;
  maidsafe_ops_.releasedir = OpsReleasedir;
  maidsafe_ops_.rename = OpsRename;
  maidsafe_ops_.rmdir = OpsRmdir;
  maidsafe_ops_.statfs = OpsStatfs;
//  maidsafe_ops_.symlink = OpsSymlink;
  maidsafe_ops_.truncate = OpsTruncate;
  maidsafe_ops_.unlink = OpsUnlink;
  maidsafe_ops_.utimens = OpsUtimens;
  maidsafe_ops_.write = OpsWrite;

#ifdef HAVE_SETXATTR
  maidsafe_ops_.getxattr = OpsGetxattr;
  maidsafe_ops_.setxattr = OpsSetxattr;
  maidsafe_ops_.listxattr = OpsListxattr;
  maidsafe_ops_.removexattr = OpsRemovexattr;
#endif
  umask(0022);
}

template <typename Storage>
void FuseDrive<Storage>::Mount() {
  fuse_args args = FUSE_ARGS_INIT(0, nullptr);
  fuse_opt_add_arg(&args, (drive_name_.c_str()));
  fuse_opt_add_arg(&args, (fuse_mountpoint_.c_str()));
  // NB - If we remove -odefault_permissions, we must check in OpsOpen, etc. that the operation is
  //      permitted for the given flags.  We also need to implement OpsAccess.
  fuse_opt_add_arg(&args, "-odefault_permissions,kernel_cache");  // ,direct_io");
#ifndef NDEBUG
  // fuse_opt_add_arg(&args, "-d");  // print debug info
  // fuse_opt_add_arg(&args, "-f");  // run in foreground
#endif
  // TODO(Fraser#5#): 2014-01-08 - BEFORE_RELEASE Avoid running in foreground.
  fuse_opt_add_arg(&args, "-f");  // run in foreground

  fuse_opt_add_arg(&args, "-s");  // this is single threaded

  // if (read_only)
  //   fuse_opt_add_arg(&args, "-oro");
  // fuse_main(args.argc, args.argv, &maidsafe_ops_, NULL);

  int multithreaded, foreground;
  char *mountpoint(nullptr);
  if (fuse_parse_cmdline(&args, &mountpoint, &multithreaded, &foreground) == -1)
    ThrowError(DriveErrors::failed_to_mount);

  fuse_channel_ = fuse_mount(mountpoint, &args);
  if (!fuse_channel_) {
    fuse_opt_free_args(&args);
    ThrowError(DriveErrors::failed_to_mount);
  }

  fuse_ = fuse_new(fuse_channel_, &args, &maidsafe_ops_, sizeof(maidsafe_ops_), nullptr);
  fuse_opt_free_args(&args);
  on_scope_exit cleanup_on_error([&]()->void {
    fuse_unmount(mountpoint, fuse_channel_);
    if (fuse_)
      fuse_destroy(fuse_);
    free(mountpoint);
  });
  if (!fuse_)
    ThrowError(DriveErrors::failed_to_mount);

  if (fuse_daemonize(foreground) == -1)
    ThrowError(DriveErrors::failed_to_mount);

  if (fuse_set_signal_handlers(fuse_get_session(fuse_)) == -1)
    ThrowError(DriveErrors::failed_to_mount);

  if (multithreaded)
    fuse_event_loop_thread_ = std::move(std::thread(&fuse_loop_mt, fuse_));
  else
    fuse_event_loop_thread_ = std::move(std::thread(&fuse_loop, fuse_));

  cleanup_on_error.Release();
  free(mountpoint);
  auto wait_until_mounted(mount_promise_.get_future());
  wait_until_mounted.get();
}

template <typename Storage>
void FuseDrive<Storage>::Unmount() {
  try {
    std::call_once(this->unmounted_once_flag_, [&] {
      fuse_remove_signal_handlers(fuse_get_session(fuse_));
      fuse_unmount(fuse_mountpoint_.c_str(), fuse_channel_);
      fuse_destroy(fuse_);
      fuse_event_loop_thread_.join();
    });
  }
  catch (const std::exception& e) {
    LOG(kError) << "Exception in Unmount: " << e.what();
  }
  catch (...) {
    LOG(kError) << "Unknown exception in Unmount";
  }
}

// =============================== Callbacks =======================================================

// Quote from FUSE documentation:
//
// Check file access permissions.
//
// This will be called for the access() system call.  If the 'default_permissions' mount option is
// given, this method is not called.
template <typename Storage>
int FuseDrive<Storage>::OpsAccess(const char* /*path*/, int /* mask */) {
  return 0;
}

// Quote from FUSE documentation:
//
// Change the permission bits of a file.
template <typename Storage>
int FuseDrive<Storage>::OpsChmod(const char* path, mode_t mode) {
  LOG(kInfo) << "OpsChmod: " << path << ", to " << std::oct << mode;
  try {
    auto file_context(Global<Storage>::g_fuse_drive->GetMutableContext(path));
    file_context->meta_data.attributes.st_mode = mode;
    time(&file_context->meta_data.attributes.st_ctime);
    file_context->parent->ScheduleForStoring();
  }
  catch (const std::exception& e) {
    LOG(kWarning) << "Failed to chmod " << path << ": " << e.what();
    return -ENOENT;
  }
  return 0;
}

// Quote from FUSE documentation:
//
// Change the owner and group of a file.
template <typename Storage>
int FuseDrive<Storage>::OpsChown(const char* path, uid_t uid, gid_t gid) {
  LOG(kInfo) << "OpsChown: " << path;
  bool change_uid(uid != static_cast<uid_t>(-1));
  bool change_gid(gid != static_cast<gid_t>(-1));
  if (!change_uid && !change_gid)
    return 0;
  try {
    auto file_context(Global<Storage>::g_fuse_drive->GetMutableContext(path));
    if (change_uid)
      file_context->meta_data.attributes.st_uid = uid;
    if (change_gid)
      file_context->meta_data.attributes.st_gid = gid;
    time(&file_context->meta_data.attributes.st_ctime);
    file_context->parent->ScheduleForStoring();
  }
  catch (const std::exception& e) {
    LOG(kWarning) << "Failed to chown " << path << ": " << e.what();
    return -ENOENT;
  }
  return 0;
}

// Quote from FUSE documentation:
//
// Create and open a file.  If the file does not exist, first create it with the specified mode, and
// then open it.
//
// If this method is not implemented or under Linux kernel versions earlier than 2.6.15, the mknod()
// and open() methods will be called instead.
template <typename Storage>
int FuseDrive<Storage>::OpsCreate(const char* path, mode_t mode,
                                  struct fuse_file_info* /*file_info*/) {
  LOG(kInfo) << "OpsCreate: " << path << " (" << detail::GetFileType(mode) << "), mode: "
             << std::oct << mode;
  return Global<Storage>::g_fuse_drive->CreateNew(path, mode);
}

// Quote from FUSE documentation:
//
// Clean up filesystem.
//
// Called on filesystem exit.
template <typename Storage>
void FuseDrive<Storage>::OpsDestroy(void* /*fuse*/) {
  LOG(kInfo) << "OpsDestroy";
}

// Quote from FUSE documentation:
//
// Get attributes from an open file.
//
// This method is called instead of the getattr() method if the file information is available.
//
// Currently this is only called after the create() method if that is implemented (see above). Later
// it may be called for invocations of fstat() too.
template <typename Storage>
int FuseDrive<Storage>::OpsFgetattr(const char* path, struct stat* stbuf,
                                    struct fuse_file_info* /*file_info*/) {
  LOG(kInfo) << "OpsFgetattr: " << path;
  return GetAttributes(path, stbuf);
}

// Quote from FUSE documentation:
//
// Possibly flush cached data.
//
// BIG NOTE: This is not equivalent to fsync(). It's not a request to sync dirty data.
//
// Flush is called on each close() of a file descriptor. So if a filesystem wants to return write
// errors in close() and the file has cached dirty data, this is a good place to write back data and
// return any errors. Since many applications ignore close() errors this is not always useful.
//
// NOTE: The flush() method may be called more than once for each open(). This happens if more than
// one file descriptor refers to an opened file due to dup(), dup2() or fork() calls. It is not
// possible to determine if a flush is final, so each flush should be treated equally. Multiple
// write-flush sequences are relatively rare, so this shouldn't be a problem.
//
// Filesystems shouldn't assume that flush will always be called after some writes, or that if will
// be called at all.
template <typename Storage>
int FuseDrive<Storage>::OpsFlush(const char* path, struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsFlush: " << path << ", flags: " << file_info->flags;
  try {
    Global<Storage>::g_fuse_drive->Flush(path);
  }
  catch (const drive_error& error) {
    LOG(kError) << "OpsFlush: " << fs::path(path) << ": " << error.what();
    return (error.code() == make_error_code(DriveErrors::no_such_file)) ? -EINVAL : -EBADF;
  }
  catch (const std::exception& e) {
    LOG(kError) << "OpsFlush: " << fs::path(path) << ": " << e.what();
    return -EBADF;
  }
  return 0;
}

/*
// Quote from FUSE documentation:
//
// Synchronize file contents
//
// If the datasync parameter is non-zero, then only the user data should be flushed, not the meta
// data.
template <typename Storage>
int FuseDrive<Storage>::OpsFsync(const char* path, int isdatasync,
                                 struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsFsync: " << path;
  detail::FileContext<Storage>* file_context(detail::RecoverFileContext<Storage>(file_info));
  if (!file_context)
    return -EINVAL;

  // if (!detail::ForceFlush(Global<Storage>::g_fuse_drive->directory_handler_, file_context)) {
  //   //    int result(Update(Global<Storage>::g_fuse_drive->directory_handler_,
  //   //                      file_context,
  //   //                      false,
  //   //                      (isdatasync == 0)));
  //   //    if (result != kSuccess) {
  //   //      LOG(kError) << "OpsFsync: " << path << ", failed to update "
  //   //                  << "metadata.  Result: " << result;
  //   //      return -EIO;
  //   //    }
  // }
  return 0;
}
*/

/*
// Quote from FUSE documentation:
//
// Synchronize directory contents.
//
// If the datasync parameter is non-zero, then only the user data should be flushed, not the meta
// data
template <typename Storage>
int FuseDrive<Storage>::OpsFsyncDir(const char* path, int isdatasync,
                                    struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsFsyncDir: " << path;
  detail::FileContext<Storage>* file_context(detail::RecoverFileContext<Storage>(file_info));
  if (!file_context)
    return -EINVAL;

  //  int result(Update(Global<Storage>::g_fuse_drive->directory_handler_,
  //                    file_context,
  //                    false,
  //                    (isdatasync == 0)));
  //  if (result != kSuccess) {
  //    LOG(kError) << "OpsFsyncDir: " << path << ", failed to update "
  //                << "metadata.  Result: " << result;
  //    return -EIO;
  //  }
  return 0;
}
*/

// Quote from FUSE documentation:
//
// Change the size of an open file.
//
// This method is called instead of the truncate() method if the truncation was invoked from an
// ftruncate() system call.
//
// If this method is not implemented or under Linux kernel versions earlier than 2.6.15, the
// truncate() method will be called instead.
template <typename Storage>
int FuseDrive<Storage>::OpsFtruncate(const char* path, off_t size,
                                     struct fuse_file_info* /*file_info*/) {
  LOG(kInfo) << "OpsFtruncate: " << path << ", size: " << size;
  return Truncate(path, size);
}

// Quote from FUSE documentation:
//
// Get file attributes.
//
// Similar to stat(). The 'st_dev' and 'st_blksize' fields are ignored. The 'st_ino' field is
// ignored except if the 'use_ino' mount option is given.
template <typename Storage>
int FuseDrive<Storage>::OpsGetattr(const char* path, struct stat* stbuf) {
  LOG(kInfo) << "OpsGetattr: " << path;
  return GetAttributes(path, stbuf);
}

// Quote from FUSE documentation:
//
// Initialize filesystem
//
// The return value will passed in the private_data field of fuse_context to all file operations and
// as a parameter to the destroy() method.
template <typename Storage>
void* FuseDrive<Storage>::OpsInit(struct fuse_conn_info* /*conn*/) {
  Global<Storage>::g_fuse_drive->mount_promise_.set_value();
  return nullptr;
}

/*
// Quote from FUSE documentation:
//
// Create a hard link to a file.
template <typename Storage>
int FuseDrive<Storage>::OpsLink(const char* to, const char* from) {
  LOG(kInfo) << "OpsLink: " << from << " --> " << to;

  fs::path path_to(to), path_from(from);

  try {
    auto file_context_to(Global<Storage>::g_fuse_drive->GetFileContext(path_to));
    if (!S_ISDIR(file_context_to.meta_data.attributes.st_mode))
      ++file_context_to.meta_data.attributes.st_nlink;
    time(&file_context_to.meta_data.attributes.st_ctime);
    file_context_to.meta_data_changed = true;
    int result(Global<Storage>::g_fuse_drive->Update(&file_context_to, true));
    if (result != kSuccess) {
      LOG(kError) << "OpsLink: " << path_to << ", failed to update "
                  << "metadata.  Result: " << result;
      return -EIO;
    }

    MetaData meta_data_from(file_context_to.meta_data);
    meta_data_from.name = path_from.filename();

    result = Global<Storage>::g_fuse_drive->AddNewMetaData(path_from, &meta_data_from, nullptr);
    if (result != kSuccess) {
      LOG(kError) << "OpsLink: " << from << " --> " << to
                  << " failed to AddNewMetaData.  Result: " << result;
      return -EIO;
    }
    return 0;
  }
  catch(const std::exception& e) {
    LOG(kError) << "OpsLink: " << path_to << ", can't get meta data." << e.what();
    return -ENOENT;
  }
}
*/

// Quote from FUSE documentation:
//
// Create a directory.
//
// Note that the mode argument may not have the type specification bits set, i.e. S_ISDIR(mode) can
// be false. To obtain the correct directory type bits use mode|S_IFDIR
template <typename Storage>
int FuseDrive<Storage>::OpsMkdir(const char* path, mode_t mode) {
  LOG(kInfo) << "OpsMkdir: " << path << " (" << detail::GetFileType(mode) << "), mode: " << std::oct
             << mode;
  return Global<Storage>::g_fuse_drive->CreateNew(path, mode);
}

// Quote from FUSE documentation:
//
// Create a file node.
//
// This is called for creation of all non-directory, non-symlink  nodes. If the filesystem defines a
// create() method, then for regular files that will be called instead.
template <typename Storage>
int FuseDrive<Storage>::OpsMknod(const char* path, mode_t mode, dev_t rdev) {
  LOG(kInfo) << "OpsMknod: " << path << " (" << detail::GetFileType(mode) << "), mode: " << std::oct
             << mode << std::dec << ", rdev: " << rdev;
  assert(!S_ISDIR(mode) && !detail::GetFileType(mode).empty());
  return Global<Storage>::g_fuse_drive->CreateNew(path, mode, rdev);
}

// Quote from FUSE documentation:
//
// File open operation.
//
// No creation (O_CREAT, O_EXCL) and by default also no truncation (O_TRUNC) flags will be passed to
// open(). If an application specifies O_TRUNC, fuse first calls truncate() and then open(). Only if
// 'atomic_o_trunc' has been specified and kernel version is 2.6.24 or later, O_TRUNC is passed on
// to open.
//
// Unless the 'default_permissions' mount option is given, open should check if the operation is
// permitted for the given flags. Optionally open may also return an arbitrary filehandle in the
// fuse_file_info structure, which will be passed to all file operations.
template <typename Storage>
int FuseDrive<Storage>::OpsOpen(const char* path, struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsOpen: " << path << ", flags: " << file_info->flags << ", keep_cache: "
             << file_info->keep_cache << ", direct_io: " << file_info->direct_io;

  if (file_info->flags & O_NOFOLLOW) {
    LOG(kError) << "OpsOpen: " << path << " is a symlink.";
    return -ELOOP;
  }

  // TODO(Fraser#5#): 2013-11-26 - Investigate option to use direct IO for some/all files.

  assert(!(file_info->flags & O_DIRECTORY));
  try {
    Global<Storage>::g_fuse_drive->Open(path);
  }
  catch (const std::exception& e) {
    LOG(kError) << "OpsOpen: " << fs::path(path) << ": " << e.what();
    return -ENOENT;
  }

  // Safe to allow the kernel to cache the file assuming it doesn't change "spontaneously".  For us,
  // that presumably can happen on files which are part of a share, or if a user has >1 client
  // instance, each with this file open.  To handle this, we need to either avoid allowing the
  // kernel caching (set 'file_info->keep_cache' to 0), or preferrably use the low-level FUSE
  // interface and if a file changes in the background, call fuse_lowlevel_notify_inval_inode().
  // See http://fuse.996288.n3.nabble.com/fuse-file-info-keep-cache-usage-guidelines-td5130.html
  file_info->keep_cache = 1;

  return 0;
}

// Quote from FUSE documentation:
//
// Open directory.
//
// Unless the 'default_permissions' mount option is given, this method should check if opendir is
// permitted for this directory. Optionally opendir may also return an arbitrary filehandle in the
// fuse_file_info structure, which will be passed to readdir, closedir and fsyncdir.
template <typename Storage>
int FuseDrive<Storage>::OpsOpendir(const char* path, struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsOpendir: " << path << ", flags: " << file_info->flags << ", keep_cache: "
             << file_info->keep_cache << ", direct_io: " << file_info->direct_io;
  if (file_info->flags & O_NOFOLLOW) {
    LOG(kError) << "OpsOpendir: " << path << " is a symlink.";
    return -ELOOP;
  }
  try {
    Global<Storage>::g_fuse_drive->Open(path);
  }
  catch (const std::exception& e) {
    LOG(kError) << "OpsOpen: " << fs::path(path) << ": " << e.what();
    return -ENOENT;
  }
  return 0;
}

// Quote from FUSE documentation:
//
// Read data from an open file.
//
// Read should return exactly the number of bytes requested except on EOF or error, otherwise the
// rest of the data will be substituted with zeroes.  An exception to this is when the 'direct_io'
// mount option is specified, in which case the return value of the read system call will reflect
// the return value of this operation.
template <typename Storage>
int FuseDrive<Storage>::OpsRead(const char* path, char* buf, size_t size, off_t offset,
                                struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsRead: " << path << ", flags: 0x" << std::hex << file_info->flags << std::dec
             << " Size : " << size << " Offset : " << offset;
  try {
    return static_cast<int>(Global<Storage>::g_fuse_drive->Read(path, buf, size, offset));
  }
  catch (const std::exception& e) {
    LOG(kWarning) << "Failed to read " << path << ": " << e.what();
    return -EINVAL;
  }
}

// Quote from FUSE documentation:
//
// Read directory.
//
// The filesystem may choose between two modes of operation:
//
// 1) The readdir implementation ignores the offset parameter, and passes zero to the filler
// function's offset.  The filler function will not return '1' (unless an error happens), so the
// whole directory is read in a single readdir operation.
//
// 2) The readdir implementation keeps track of the offsets of the directory entries.  It uses the
// offset parameter and always passes non-zero offset to the filler function.  When the buffer is
// full (or an error happens) the filler function will return '1'.
template <typename Storage>
int FuseDrive<Storage>::OpsReaddir(const char* path, void* buf, fuse_fill_dir_t filler,
                                   off_t offset, struct fuse_file_info* /*file_info*/) {
  LOG(kInfo) << "OpsReaddir: " << path << "; offset = " << offset;

  filler(buf, ".", nullptr, 0);
  filler(buf, "..", nullptr, 0);
  detail::Directory* directory;
  try {
    directory = Global<Storage>::g_fuse_drive->directory_handler_.Get(path);
  }
  catch (const std::exception& e) {
    LOG(kError) << "OpsReaddir: " << path << ", can't get directory: " << e.what();
    return -EBADF;
  }
  assert(directory);

  // TODO(Fraser#5#): 2011-05-18 - Handle offset properly.
  if (offset == 0)
    directory->ResetChildrenCounter();

  const detail::FileContext* file_context(directory->GetChildAndIncrementCounter());
  while (file_context) {
    if (filler(buf, file_context->meta_data.name.c_str(), &file_context->meta_data.attributes, 0))
      break;
    file_context = directory->GetChildAndIncrementCounter();
  }

//  if (file_context) {
//    file_context->content_changed = true;
//    time(&file_context->meta_data->attributes.st_atime);
//  }
  return 0;
}

/*
// Quote from FUSE documentation:
//
// Read the target of a symbolic link.
//
// The buffer should be filled with a null terminated string.  The buffer size argument includes the
// space for the terminating null character.	If the linkname is too long to fit in the buffer, it
// should be truncated.	The return value should be 0 for success.
template <typename Storage>
int FuseDrive<Storage>::OpsReadlink(const char* path, char* buf, size_t size) {
  LOG(kInfo) << "OpsReadlink: " << path;
  try {
    auto file_context(Global<Storage>::g_fuse_drive->GetFileContext(path));
    if (S_ISLNK(file_context->meta_data->attributes.st_mode)) {
      snprintf(buf, file_context->meta_data->link_to.string().size() + 1, "%s",
               file_context->meta_data->link_to.string().c_str());
    } else {
      LOG(kError) << "OpsReadlink " << path << ", no link returned.";
      return -EINVAL;
    }
  }
  catch (const std::exception& e) {
    LOG(kWarning) << "OpsReadlink: " << path << ": " << e.what();
    return -ENOENT;
  }
  return 0;
}
*/

// Quote from FUSE documentation:
//
// Release an open file.
//
// Release is called when there are no more references to an open file: all file descriptors are
// closed and all memory mappings are unmapped.
//
// For every open() call there will be exactly one release() call with the same flags and file
// descriptor. It is possible to have a file opened more than once, in which case only the last
// release will mean, that no more reads/writes will happen on the file. The return value of release
// is ignored.
template <typename Storage>
int FuseDrive<Storage>::OpsRelease(const char* path, struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsRelease: " << path << ", flags: " << file_info->flags;
  try {
    Global<Storage>::g_fuse_drive->Release(path);
  }
  catch (const std::exception& e) {
    LOG(kError) << "OpsRelease: " << path << ": " << e.what();
    return -EBADF;
  }
  return 0;
}

// Quote from FUSE documentation:
//
// Release directory.
template <typename Storage>
int FuseDrive<Storage>::OpsReleasedir(const char* path, struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsReleasedir: " << path << ", flags: " << file_info->flags;
  try {
    Global<Storage>::g_fuse_drive->ReleaseDir(path);
  }
  catch (const std::exception& e) {
    LOG(kError) << "OpsReleasedir: " << path << ": " << e.what();
    return -EBADF;
  }
  return 0;
}

// Quote from FUSE documentation:
//
// Rename a file.
template <typename Storage>
int FuseDrive<Storage>::OpsRename(const char* old_name, const char* new_name) {
  LOG(kInfo) << "OpsRename: " << old_name << " to " << new_name;
  try {
    Global<Storage>::g_fuse_drive->Rename(old_name, new_name);
  }
  catch (const std::exception& e) {
    LOG(kError) << "Failed to rename " << old_name << " to " << new_name << ": " << e.what();
    //     switch (result) {
    //       case kChildAlreadyExists:
    //       case kFailedToAddChild:
    //         return -ENOTEMPTY;
    //       case kFailedToRemoveChild:
    //       case kFailedToGetDirectoryData:
    //       case kFailedToSaveParentDirectoryListing:
    //         return -ENOENT;
    //       default:
    //         return -EIO;
    //     }
    return -EIO;
  }
  return 0;
}

// Quote from FUSE documentation:
//
// Remove a directory.
template <typename Storage>
int FuseDrive<Storage>::OpsRmdir(const char* path) {
  LOG(kInfo) << "OpsRmdir: " << path;
  try {
    Global<Storage>::g_fuse_drive->Delete(path);
  }
  catch (const std::exception&) {
    return -EIO;
  }
  return 0;
}

// Quote from FUSE documentation:
//
// Get file system statistics.
//
// The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored.
template <typename Storage>
int FuseDrive<Storage>::OpsStatfs(const char* path, struct statvfs* stbuf) {
  LOG(kInfo) << "OpsStatfs: " << path;

  //   int res = statvfs(Global<Storage>::g_fuse_drive->mount_dir().parent_path().string().c_str(),
  //                     stbuf);
  //   if (res < 0) {
  //     LOG(kError) << "OpsStatfs: " << path;
  //     return -errno;
  //   }

  stbuf->f_bsize = 4096;
  stbuf->f_frsize = 4096;
//  if (Global<Storage>::g_fuse_drive->max_space_ == 0) {
// for future ref 2^45 = 35184372088832 = 32TB
#ifndef __USE_FILE_OFFSET64
//    stbuf->f_blocks = 8796093022208 / stbuf->f_frsize;
//    stbuf->f_bfree = 8796093022208/ stbuf->f_bsize;
#else
//    stbuf->f_blocks = 8796093022208 / stbuf->f_frsize;
//    stbuf->f_bfree = 8796093022208 / stbuf->f_bsize;
#endif
  //  } else {
  //    stbuf->f_blocks = 0;  // Global<Storage>::g_fuse_drive->max_space_ / stbuf->f_frsize;
  //    stbuf->f_bfree = 0;  // (Global<Storage>::g_fuse_drive->max_space_ -
  //                         // Global<Storage>::g_fuse_drive->used_space_) / stbuf->f_bsize;
  stbuf->f_blocks = 100000 / stbuf->f_frsize;  // FIXME BEFORE_RELEASE
  stbuf->f_bfree = 100000 / stbuf->f_bsize;
  //  }
  stbuf->f_bavail = stbuf->f_bfree;

  /*
  stbuf->f_files = 0;    // # inodes
  stbuf->f_ffree = 0;    // # free inodes
  stbuf->f_favail = 0;   // # free inodes for unprivileged users
  stbuf->f_fsid = 0;     // file system ID
  stbuf->f_flag = 0;     // mount flags
  stbuf->f_namemax = 0;  // maximum filename length
  */

  return 0;
}

/*
// Quote from FUSE documentation:
//
// Create a symbolic link.
template <typename Storage>
int FuseDrive<Storage>::OpsSymlink(const char* to, const char* from) {
  LOG(kInfo) << "OpsSymlink: " << from << " --> " << to;

  fs::path path_to(to), path_from(from);
  MetaData meta_data(path_from.filename(), false);
  time(&meta_data.attributes.st_atime);
  meta_data.attributes.st_mtime = meta_data.attributes.st_atime;
  meta_data.attributes.st_mode = S_IFLNK;
  meta_data.attributes.st_uid = fuse_get_context()->uid;
  meta_data.attributes.st_gid = fuse_get_context()->gid;
  meta_data.attributes.st_size = path_from.string().size();
  meta_data.link_to = path_to;

  int result(AddNewMetaData(Global<Storage>::g_fuse_drive->directory_handler_,
                            path_from, ShareData(), &meta_data, false,
                            nullptr, nullptr, nullptr));
  if (result != kSuccess) {
    LOG(kError) << "OpsSymlink: " << from << " --> " << to
                << " failed to AddNewMetaData.  Result: " << result;
    return -EIO;
  }
  return 0;
}
*/

// Quote from FUSE documentation:
//
// Change the size of a file.
template <typename Storage>
int FuseDrive<Storage>::OpsTruncate(const char* path, off_t size) {
  LOG(kInfo) << "OpsTruncate: " << path << ", size: " << size;
  return Truncate(path, size);
}

// Quote from FUSE documentation:
//
// Remove a file.
template <typename Storage>
int FuseDrive<Storage>::OpsUnlink(const char* path) {
  LOG(kInfo) << "OpsUnlink: " << path;
  try {
    Global<Storage>::g_fuse_drive->Delete(path);
  }
  catch (const std::exception&) {
    return -EIO;
  }
  return 0;
}

// Quote from FUSE documentation:
//
// Change the access and modification times of a file with nanosecond resolution
//
// This supersedes the old utime() interface.  New applications should use this.  See the
// utimensat(2) man page for details.
template <typename Storage>
int FuseDrive<Storage>::OpsUtimens(const char* path, const struct timespec ts[2]) {
  LOG(kInfo) << "OpsUtimens: " << path;
  detail::FileContext* file_context(nullptr);
  try {
    file_context = Global<Storage>::g_fuse_drive->GetMutableContext(path);
  }
  catch (const std::exception& e) {
    LOG(kWarning) << "Failed to change times for " << path << ": " << e.what();
    return -ENOENT;
  }

#if defined __USE_MISC || defined __USE_XOPEN2K8 || defined MAIDSAFE_APPLE
  time(&file_context->meta_data.attributes.st_ctime);
  if (ts) {
    file_context->meta_data.attributes.st_atime = ts[0].tv_sec;
    file_context->meta_data.attributes.st_mtime = ts[1].tv_sec;
  } else {
    file_context->meta_data.attributes.st_mtime = file_context->meta_data.attributes.st_atime =
        file_context->meta_data.attributes.st_ctime;
  }
#else
  timespec tspec;
  clock_gettime(CLOCK_MONOTONIC, &tspec);
  file_context->meta_data->attributes.st_ctime = tspec.tv_sec;
  file_context->meta_data->attributes.st_ctimensec = tspec.tv_nsec;
  if (ts) {
    file_context->meta_data->attributes.st_atime = ts[0].tv_sec;
    file_context->meta_data->attributes.st_atimensec = ts[0].tv_nsec;
    file_context->meta_data->attributes.st_mtime = ts[1].tv_sec;
    file_context->meta_data->attributes.st_mtimensec = ts[1].tv_nsec;
  } else {
    file_context->meta_data->attributes.st_atime = tspec.tv_sec;
    file_context->meta_data->attributes.st_atimensec = tspec.tv_nsec;
    file_context->meta_data->attributes.st_mtime = tspec.tv_sec;
    file_context->meta_data->attributes.st_mtimensec = tspec.tv_nsec;
  }
#endif
  file_context->parent->ScheduleForStoring();
  return 0;
}

// Quote from FUSE documentation:
//
// Write data to an open file.
//
// Write should return exactly the number of bytes requested except on error.  An exception to this
// is when the 'direct_io' mount option is specified (see read operation).
template <typename Storage>
int FuseDrive<Storage>::OpsWrite(const char* path, const char* buf, size_t size, off_t offset,
                                 struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsWrite: " << path << ", flags: 0x" << std::hex << file_info->flags << std::dec
             << " Size : " << size << " Offset : " << offset;

  try {
    return static_cast<int>(Global<Storage>::g_fuse_drive->Write(path, buf, size, offset));
  }
  catch (const std::exception& e) {
    LOG(kWarning) << "Failed to write " << path << ": " << e.what();
    return -EINVAL;
  }
}

#ifdef HAVE_SETXATTR
int FuseDrive<Storage>::OpsGetxattr(const char* path, const char* name, char* value, size_t size) {
  LOG(kInfo) << "OpsGetxattr: " << path;
  fs::path full_path(Global<Storage>::g_fuse_drive->metadata_dir_ / name);
  int res = lgetxattr(TranslatePath(name, path).c_str(), name, value, size);
  if (res == -1) {
    LOG(kError) << "OpsGetxattr: " << path;
    return -errno;
  }
  return res;
}

int FuseDrive<Storage>::OpsListxattr(const char* path, char* list, size_t size) {
  LOG(kInfo) << "OpsListxattr: " << path;
  fs::path full_path(Global<Storage>::g_fuse_drive->metadata_dir_ / list);
  int res = llistxattr(TranslatePath(full_path.c_str(), path).c_str(),
                       list,
                       size);
  if (res == -1) {
    LOG(kError) << "OpsListxattr: " << path;
    return -errno;
  }
  return res;
}

int FuseDrive<Storage>::OpsRemovexattr(const char* path, const char* name) {
  LOG(kInfo) << "OpsRemovexattr: " << path;
  fs::path full_path(Global<Storage>::g_fuse_drive->metadata_dir_ / name);
  int res = lremovexattr(TranslatePath(full_path.c_str(), path).c_str(),
                         name);
  if (res == -1) {
    LOG(kError) << "OpsRemovexattr: " << path;
    return -errno;
  }
  return 0;
}

int FuseDrive<Storage>::OpsSetxattr(const char* path, const char* name, const char* value,
                                    size_t size, int flags) {
  LOG(kInfo) << "OpsSetxattr: " << path << ", flags: " << flags;
  fs::path full_path(Global<Storage>::g_fuse_drive->metadata_dir_ / name);
  int res = lsetxattr(TranslatePath(full_path, path).c_str(),
                      name, value, size, flags);
  if (res == -1) {
    LOG(kError) << "OpsSetxattr: " << path << ", flags: " << flags;
    return -errno;
  }
  return 0;
}
#endif  // HAVE_SETXATTR

template <typename Storage>
int FuseDrive<Storage>::CreateNew(const fs::path& full_path, mode_t mode, dev_t rdev) {
  if (detail::ExcludedFilename(full_path.filename().stem().string())) {
    LOG(kError) << "Invalid name: " << full_path;
    return -EINVAL;
  }
  bool is_directory(S_ISDIR(mode));
  detail::FileContext file_context(full_path.filename(), is_directory);

  time(&file_context.meta_data.attributes.st_atime);
  file_context.meta_data.attributes.st_ctime = file_context.meta_data.attributes.st_mtime =
      file_context.meta_data.attributes.st_atime;
  file_context.meta_data.attributes.st_mode = mode;
  file_context.meta_data.attributes.st_rdev = rdev;
  file_context.meta_data.attributes.st_nlink = (is_directory ? 2 : 1);
  file_context.meta_data.attributes.st_uid = fuse_get_context()->uid;
  file_context.meta_data.attributes.st_gid = fuse_get_context()->gid;

  try {
    Global<Storage>::g_fuse_drive->Create(full_path, std::move(file_context));
  }
  catch (const std::exception& e) {
    LOG(kError) << "CreateNew: " << full_path << ": " << e.what();
    return -EIO;
  }

  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::GetAttributes(const char* path, struct stat* stbuf) {
  try {
    auto file_context(Global<Storage>::g_fuse_drive->GetContext(path));
    *stbuf = file_context->meta_data.attributes;
    LOG(kVerbose) << " meta_data info  = ";
    LOG(kVerbose) << "     name =  " << file_context->meta_data.name.c_str();
    LOG(kVerbose) << "     st_dev = " << file_context->meta_data.attributes.st_dev;
    LOG(kVerbose) << "     st_ino = " << file_context->meta_data.attributes.st_ino;
    LOG(kVerbose) << "     st_mode = " << file_context->meta_data.attributes.st_mode;
    LOG(kVerbose) << "     st_nlink = " << file_context->meta_data.attributes.st_nlink;
    LOG(kVerbose) << "     st_uid = " << file_context->meta_data.attributes.st_uid;
    LOG(kVerbose) << "     st_gid = " << file_context->meta_data.attributes.st_gid;
    LOG(kVerbose) << "     st_rdev = " << file_context->meta_data.attributes.st_rdev;
    LOG(kVerbose) << "     st_size = " << file_context->meta_data.attributes.st_size;
    LOG(kVerbose) << "     st_blksize = " << file_context->meta_data.attributes.st_blksize;
    LOG(kVerbose) << "     st_blocks = " << file_context->meta_data.attributes.st_blocks;
    LOG(kVerbose) << "     st_atim = " << file_context->meta_data.attributes.st_atime;
    LOG(kVerbose) << "     st_mtim = " << file_context->meta_data.attributes.st_mtime;
    LOG(kVerbose) << "     st_ctim = " << file_context->meta_data.attributes.st_ctime;
  }
  catch (const std::exception& e) {
//    if (full_path.filename().string().size() > 255) {
//      LOG(kError) << "OpsGetattr: " << full_path.filename() << " too long.";
//      return -ENAMETOOLONG;
//    }
    LOG(kWarning) << "OpsGetattr: " << path << " - " << e.what();
    return -ENOENT;
  }
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::Truncate(const char* path, off_t size) {
  try {
    auto file_context(Global<Storage>::g_fuse_drive->GetMutableContext(path));
    assert(file_context->self_encryptor);
    file_context->self_encryptor->Truncate(size);
    file_context->meta_data.attributes.st_size = size;
    time(&file_context->meta_data.attributes.st_mtime);
    file_context->meta_data.attributes.st_ctime = file_context->meta_data.attributes.st_atime =
        file_context->meta_data.attributes.st_mtime;
    file_context->parent->ScheduleForStoring();
  }
  catch (const std::exception& e) {
    LOG(kWarning) << "Failed to truncate " << path << ": " << e.what();
    return -ENOENT;
  }
  return 0;
}

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_UNIX_DRIVE_H_
