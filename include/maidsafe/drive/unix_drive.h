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
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <functional>

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
#include "maidsafe/drive/file.h"
#include "maidsafe/drive/symlink.h"
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

inline common::Clock::time_point ToTimePoint(const struct timespec& ts) {
  using namespace std::chrono;
  return common::Clock::time_point(seconds(ts.tv_sec) + nanoseconds(ts.tv_nsec));
}

// template <typename Storage>
// bool ForceFlush(RootHandler<Storage>& root_handler, File<Storage>* file_context) {
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

inline MetaData::FileType ToFileType(mode_t mode) {
  if (S_ISDIR(mode)) {
    return fs::directory_file;
  } else if (S_ISREG(mode)) {
    return fs::regular_file;
  } else if (S_ISLNK(mode)) {
    return fs::symlink_file;
  } else {
    return fs::status_error;
  }
}

inline mode_t ToFileMode(MetaData::FileType file_type, mode_t mode) {
  auto permission = mode & ~S_IFMT; // Clear file type fields
  switch (file_type) {
    case fs::directory_file:
      return permission | S_IFDIR;
    case fs::regular_file:
      return permission | S_IFREG;
    case fs::symlink_file:
      return permission | S_IFLNK;
    default:
      assert(false); // Not supported yet
      return mode;
    }
}

inline bool IsSupported(mode_t mode) {
  return S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode);
}

inline struct stat ToStat(const MetaData& meta) {
  struct stat result;
  std::memset(&result, 0, sizeof(result));
  result.st_ino = std::hash<std::string>()(meta.name.native());
  result.st_mode = detail::ToFileMode(meta.file_type, result.st_mode);
  result.st_uid = fuse_get_context()->uid;
  result.st_gid = fuse_get_context()->gid;
  result.st_nlink = (meta.file_type == fs::directory_file) ? 2 : 1;
  result.st_size = meta.size;
  result.st_blksize = detail::kFileBlockSize;
  result.st_blocks = result.st_size / result.st_blksize;
  result.st_atime = common::Clock::to_time_t(meta.last_access_time);
  result.st_mtime = common::Clock::to_time_t(meta.last_write_time);
  result.st_ctime = common::Clock::to_time_t(meta.last_status_time);
  return result;
}

}  // namespace detail

template <typename Storage>
class FuseDrive : public Drive<Storage> {
 public:
  FuseDrive(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
            const Identity& root_parent_id, const boost::filesystem::path& mount_dir,
            const boost::filesystem::path& user_app_dir, const boost::filesystem::path& drive_name,
            const std::string& mount_status_shared_object_name, bool create);

  virtual ~FuseDrive();
  virtual void Mount();
  virtual void Unmount();

 private:
  FuseDrive(const FuseDrive&);
  FuseDrive(FuseDrive&&);
  FuseDrive& operator=(FuseDrive);

  void Init();
  void SetMounted();

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

  static int CreateFile(const fs::path& target, mode_t);
  static int CreateDirectory(const fs::path& target, mode_t);
  static int CreateSymlink(const fs::path& target, const fs::path& source);
  static int GetAttributes(const char* path, struct stat* stbuf);
  static int Truncate(const char* path, off_t size);

  static struct fuse_operations maidsafe_ops_;
  struct fuse* fuse_;
  fuse_chan* fuse_channel_;
  fs::path fuse_mountpoint_;
  std::string drive_name_;
  std::once_flag mounted_once_flag_;
  std::thread unmount_ipc_waiter_;
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
                              const std::string& mount_status_shared_object_name, bool create)
    : Drive<Storage>(storage, unique_user_id, root_parent_id, mount_dir, user_app_dir,
                     mount_status_shared_object_name, create),
      fuse_(nullptr),
      fuse_channel_(nullptr),
      fuse_mountpoint_(mount_dir),
      drive_name_(drive_name.string()),
      mounted_once_flag_(),
      unmount_ipc_waiter_() {
  fs::create_directory(fuse_mountpoint_);
  Init();
}

template <typename Storage>
FuseDrive<Storage>::~FuseDrive() {
  Unmount();
  if (unmount_ipc_waiter_.joinable())
    unmount_ipc_waiter_.join();
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
  maidsafe_ops_.readlink = OpsReadlink;
  maidsafe_ops_.release = OpsRelease;
  maidsafe_ops_.releasedir = OpsReleasedir;
  maidsafe_ops_.rename = OpsRename;
  maidsafe_ops_.rmdir = OpsRmdir;
  maidsafe_ops_.statfs = OpsStatfs;
  maidsafe_ops_.symlink = OpsSymlink;
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
  // umask(0022);
}

template <typename Storage>
void FuseDrive<Storage>::SetMounted() {
  std::call_once(mounted_once_flag_, [&] {
    if (!this->kMountStatusSharedObjectName_.empty()) {
      LOG(kVerbose) << "FuseDrive<Storage>::SetMounted() kMountStatusSharedObjectName_ : "
                    << this->kMountStatusSharedObjectName_;
      unmount_ipc_waiter_ = std::thread([&] {
        NotifyMountedAndWaitForUnmountRequest(this->kMountStatusSharedObjectName_);
        Unmount();
      });
    }
    this->mount_promise_.set_value();
  });
}

template <typename Storage>
void FuseDrive<Storage>::Mount() {
  fuse_args args = FUSE_ARGS_INIT(0, nullptr);
  fuse_opt_add_arg(&args, (drive_name_.c_str()));
  fuse_opt_add_arg(&args, (fuse_mountpoint_.c_str()));
  std::string fsname_arg("-ofsname=" + drive_name_);
  fuse_opt_add_arg(&args, (fsname_arg.c_str()));
#ifdef MAIDSAFE_APPLE
  std::string volname_arg("-ovolname=" + drive_name_);
  fuse_opt_add_arg(&args, (volname_arg.c_str()));
#endif
  // NB - If we remove -odefault_permissions, we must check in OpsOpen, etc. that the operation is
  //      permitted for the given flags.  We also need to implement OpsAccess.
  fuse_opt_add_arg(&args, "-odefault_permissions,kernel_cache");
#ifndef NDEBUG
  // fuse_opt_add_arg(&args, "-d");  // print debug info
  // fuse_opt_add_arg(&args, "-f");  // run in foreground
#endif
  // TODO(Fraser#5#): 2014-01-08 - BEFORE_RELEASE Avoid running in foreground.
  fuse_opt_add_arg(&args, "-f");  // run in foreground
  fuse_opt_add_arg(&args, "-s");  // run single threaded

  // tag the volume as "local" to make it appear on the Desktop and in Finder's sidebar.
  // fuse_opt_add_arg(&args, "-olocal");

  // fuse_main(args.argc, args.argv, &maidsafe_ops_, NULL);

  int multithreaded, foreground;
  char *mountpoint(nullptr);
  if (fuse_parse_cmdline(&args, &mountpoint, &multithreaded, &foreground) == -1)
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::failed_to_mount));

  fuse_channel_ = fuse_mount(mountpoint, &args);
  if (!fuse_channel_) {
    fuse_opt_free_args(&args);
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::failed_to_mount));
  }

  fuse_ = fuse_new(fuse_channel_, &args, &maidsafe_ops_, sizeof(maidsafe_ops_), nullptr);
  fuse_opt_free_args(&args);
  on_scope_exit cleanup_on_error([&]()->void {
    if (fuse_) {
      fuse_unmount(mountpoint, fuse_channel_);
      fuse_destroy(fuse_);
      free(mountpoint);
    }
    this->mount_promise_.set_value();
  });
  if (!fuse_)
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::failed_to_mount));

  if (fuse_daemonize(foreground) == -1)
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::failed_to_mount));

  if (fuse_set_signal_handlers(fuse_get_session(fuse_)) == -1)
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::failed_to_mount));

  if (multithreaded) {
    if (fuse_loop_mt(fuse_) == -1)
      BOOST_THROW_EXCEPTION(MakeError(DriveErrors::failed_to_mount));
  } else {
    if (fuse_loop(fuse_) == -1)
      BOOST_THROW_EXCEPTION(MakeError(DriveErrors::failed_to_mount));
  }

  cleanup_on_error.Release();
  free(mountpoint);
}

template <typename Storage>
void FuseDrive<Storage>::Unmount() {
  try {
    std::call_once(this->unmounted_once_flag_, [&] {
      fuse_remove_signal_handlers(fuse_get_session(fuse_));
      fuse_unmount(fuse_mountpoint_.c_str(), fuse_channel_);
      fuse_destroy(fuse_);
    });
  }
  catch (const std::exception& e) {
    LOG(kError) << "Exception in Unmount: " << e.what();
  }
  catch (...) {
    LOG(kError) << "Unknown exception in Unmount";
  }
  if (!this->kMountStatusSharedObjectName_.empty())
    NotifyUnmounted(this->kMountStatusSharedObjectName_);
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
  // Permissions cannot be changed at the moment
  return -EPERM;
}

// Quote from FUSE documentation:
//
// Change the owner and group of a file.
template <typename Storage>
int FuseDrive<Storage>::OpsChown(const char* path, uid_t, gid_t) {
  LOG(kInfo) << "OpsChown: " << path;
  return -EPERM;
}

// Quote from FUSE documentation:
//
// Create and open a file.  If the file does not exist, first create it with the specified mode, and
// then open it.
//
// If this method is not implemented or under Linux kernel versions earlier than 2.6.15, the mknod()
// and open() methods will be called instead.
template <typename Storage>
int FuseDrive<Storage>::OpsCreate(const char* path,
                                  mode_t mode,
                                  struct fuse_file_info* /*file_info*/) {
  LOG(kInfo) << "OpsCreate: " << path << " (" << detail::GetFileType(mode) << "), mode: "
             << std::oct << mode;
  switch (detail::ToFileType(mode)) {
    case fs::symlink_file:
      // FIXME: Permissions (mode) are ignored
      return Global<Storage>::g_fuse_drive->CreateSymlink(path, fs::path());
    case fs::directory_file:
      return Global<Storage>::g_fuse_drive->CreateDirectory(path, mode);
    case fs::regular_file:
      return Global<Storage>::g_fuse_drive->CreateFile(path, mode);
    default:
      return -EPERM;
  }
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
  detail::File<Storage>* file_context(detail::RecoverFile<Storage>(file_info));
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
  detail::File<Storage>* file_context(detail::RecoverFile<Storage>(file_info));
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
  Global<Storage>::g_fuse_drive->SetMounted();
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
    auto file_context_to(Global<Storage>::g_fuse_drive->GetFile(path_to));
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
  mode = (mode & ~S_IFMT) | S_IFDIR;
  LOG(kInfo) << "OpsMkdir: " << path << " (" << detail::GetFileType(mode) << "), mode: " << std::oct
             << mode;
  return Global<Storage>::g_fuse_drive->CreateDirectory(path, mode);
}

// Quote from FUSE documentation:
//
// Create a file node.
//
// This is called for creation of all non-directory, non-symlink  nodes. If the filesystem defines a
// create() method, then for regular files that will be called instead.
template <typename Storage>
int FuseDrive<Storage>::OpsMknod(const char* path, mode_t mode, dev_t) {
  // Ignores the dev_t parameter because it is only used for character and block devices,
  // which we do not support.
  LOG(kInfo) << "OpsMknod: " << path << " (" << detail::GetFileType(mode) << "), mode: " << std::oct
             << mode;
  switch (detail::ToFileType(mode)) {
    case fs::regular_file:
      return Global<Storage>::g_fuse_drive->CreateFile(path, mode);
    default:
      return -EPERM;
  }
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
  std::shared_ptr<detail::Directory> directory;
  try {
    directory =
        Global<Storage>::g_fuse_drive->directory_handler_->template Get<detail::Directory>(path);
  }
  catch (const std::exception& e) {
    LOG(kError) << "OpsReaddir: " << path << ", can't get directory: " << e.what();
    return -EBADF;
  }
  assert(directory);

  // TODO(Fraser#5#): 2011-05-18 - Handle offset properly.
  if (offset == 0)
    directory->ResetChildrenCounter();

  auto file(directory->GetChildAndIncrementCounter());
  while (file) {
    struct stat attributes = ToStat(file->meta_data);
    if (filler(buf, file->meta_data.name.c_str(), &attributes, 0))
      break;
    file = directory->GetChildAndIncrementCounter();
  }

//  if (file_context) {
//    file_context->content_changed = true;
//    time(&file_context->meta_data->attributes.st_atime);
//  }
  return 0;
}

// Quote from FUSE documentation:
//
// Read the target of a symbolic link.
//
// The buffer should be filled with a null terminated string.  The buffer size argument includes the
// space for the terminating null character.  If the linkname is too long to fit in the buffer, it
// should be truncated.  The return value should be 0 for success.
template <typename Storage>
int FuseDrive<Storage>::OpsReadlink(const char* path, char* buf, size_t size) {
  LOG(kInfo) << "OpsReadlink: " << path;
  try {
    auto symlink(Global<Storage>::g_fuse_drive->template GetContext<detail::Symlink>(path));
    if (symlink) {
      std::string link_path(symlink->Target().string());
      size_t link_path_size(link_path.size());
      if (size != 0) {
        if (link_path_size >= size) {
          std::string link_path_substr(link_path.substr(0, size - 1));
          snprintf(buf, size, "%s", link_path_substr.c_str());
        } else {
          snprintf(buf, link_path_size + 1, "%s", link_path.c_str());
        }
      }
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

  // Although POSIX states that there is no correspondence between st_blksize and
  // f_bsize, we set them the the same value for convenience.
  stbuf->f_bsize = detail::kFileBlockSize;
  stbuf->f_frsize = detail::kFileBlockSize;
  stbuf->f_blocks = (std::numeric_limits<int64_t>::max() - 10000) / stbuf->f_frsize;
  stbuf->f_bfree = (std::numeric_limits<int64_t>::max() - 10000) / stbuf->f_bsize;
  stbuf->f_bavail = stbuf->f_bfree;
  /*
  stbuf->f_files = 0;    // # inodes
  stbuf->f_ffree = 0;    // # free inodes
  stbuf->f_namemax = 0;  // maximum filename length
  */
  return 0;
}

// Quote from FUSE documentation:
//
// Create a symbolic link.
template <typename Storage>
int FuseDrive<Storage>::OpsSymlink(const char* to, const char* from) {
  LOG(kInfo) << "OpsSymlink: " << from << " --> " << to;

  try {
    return CreateSymlink(fs::path(from), fs::path(to));
  }
  catch (const std::exception&) {
    return -EIO;
  }
}

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
  std::shared_ptr<detail::Path> file;
  try {
    file = Global<Storage>::g_fuse_drive->GetMutableContext(path);
    file->meta_data.last_access_time = detail::ToTimePoint(ts[0]);
    file->meta_data.last_write_time = detail::ToTimePoint(ts[1]);
    file->meta_data.last_status_time = common::Clock::now();
  }
  catch (const std::exception& e) {
    LOG(kWarning) << "Failed to change times for " << path << ": " << e.what();
    return -ENOENT;
  }

  file->ScheduleForStoring();
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
int FuseDrive<Storage>::CreateSymlink(const fs::path& target,
                                      const fs::path& source) {
  if (detail::ExcludedFilename(target.filename().stem().string())) {
    LOG(kError) << "Invalid name: " << target;
    return -EINVAL;
  }
  try {
    auto symlink = detail::Symlink::Create(target.filename(),
                                           source.filename());
    symlink->meta_data.creation_time
        = symlink->meta_data.last_status_time
        = symlink->meta_data.last_write_time
        = symlink->meta_data.last_access_time
        = common::Clock::now();
    Global<Storage>::g_fuse_drive->Create(target, symlink);
  } catch (const std::exception& e) {
    LOG(kError) << "CreateSymlink: " << source << " -> " << target << ": " << e.what();
    return -EIO;
  }
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::CreateDirectory(const fs::path& target,
                                        mode_t mode) {
  if (detail::ToFileType(mode) != fs::directory_file) {
    return -EINVAL;
  }
  try {
    // FIXME: Replace with detail::Directory::Create
    auto directory = detail::File::Create(target.filename(), true);
    directory->meta_data.creation_time
        = directory->meta_data.last_status_time
        = directory->meta_data.last_write_time
        = directory->meta_data.last_access_time
        = common::Clock::now();
    Global<Storage>::g_fuse_drive->Create(target, directory);
  } catch (const std::exception& e) {
    LOG(kError) << "CreateDirectory: " << target << ": " << e.what();
    return -EIO;
  }
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::CreateFile(const fs::path& target, mode_t mode) {
  if (detail::ExcludedFilename(target.filename().stem().string())) {
    LOG(kError) << "Invalid name: " << target;
    return -EINVAL;
  }
  if (detail::ToFileType(mode) != fs::regular_file) {
    return -EINVAL;
  }
  try {
    auto file = detail::File::Create(target.filename(), false);
    file->meta_data.creation_time
        = file->meta_data.last_status_time
        = file->meta_data.last_write_time
        = file->meta_data.last_access_time
        = common::Clock::now();
    Global<Storage>::g_fuse_drive->Create(target, file);
  } catch (const std::exception& e) {
    LOG(kError) << "CreateFile: " << target << ": " << e.what();
    return -EIO;
  }
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::GetAttributes(const char* path, struct stat* stbuf) {
  try {
    using namespace std::chrono;

    auto file(Global<Storage>::g_fuse_drive->GetContext(path));
    *stbuf = ToStat(file->meta_data);
    LOG(kVerbose) << " meta_data info  = ";
    LOG(kVerbose) << "     name =  " << file->meta_data.name.c_str();
    LOG(kVerbose) << "     st_dev = " << stbuf->st_dev;
    LOG(kVerbose) << "     st_ino = " << stbuf->st_ino;
    LOG(kVerbose) << "     st_mode = " << stbuf->st_mode;
    LOG(kVerbose) << "     st_nlink = " << stbuf->st_nlink;
    LOG(kVerbose) << "     st_uid = " << stbuf->st_uid;
    LOG(kVerbose) << "     st_gid = " << stbuf->st_gid;
    LOG(kVerbose) << "     st_rdev = " << stbuf->st_rdev;
    LOG(kVerbose) << "     st_size = " << stbuf->st_size;
    LOG(kVerbose) << "     st_blksize = " << stbuf->st_blksize;
    LOG(kVerbose) << "     st_blocks = " << stbuf->st_blocks;
    LOG(kVerbose) << "     st_atim = " << stbuf->st_atime;
    LOG(kVerbose) << "     st_mtim = " << stbuf->st_mtime;
    LOG(kVerbose) << "     st_ctim = " << stbuf->st_ctime;
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
    auto file(Global<Storage>::g_fuse_drive->GetMutableContext(path));
    assert(file->self_encryptor);
    file->self_encryptor->Truncate(size);
    file->meta_data.size = size;
    file->meta_data.creation_time
        = file->meta_data.last_status_time
        = file->meta_data.last_write_time
        = file->meta_data.last_access_time
        = common::Clock::now();
    file->ScheduleForStoring();
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
