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

#ifdef MAIDSAFE_APPLE
#include "sys/statvfs.h"
#endif
#include "fuse/fuse.h"
#include "fuse/fuse_common.h"
#include "fuse/fuse_opt.h"

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

inline int CreateNew(const fs::path& full_path, mode_t mode, dev_t rdev = 0) {
  if (detail::ExcludedFilename(full_path.filename().stem().string())) {
    LOG(kError) << "Invalid name: " << full_path;
    return -EINVAL;
  }
  bool is_directory(S_ISDIR(mode));
  FileContext file_context(full_path.filename(), is_directory);

  time(&file_context.meta_data->attributes.st_atime);
  file_context.meta_data->attributes.st_ctime = file_context.meta_data->attributes.st_mtime =
      file_context.meta_data->attributes.st_atime;
  file_context.meta_data->attributes.st_mode = mode;
  file_context.meta_data->attributes.st_rdev = rdev;
  file_context.meta_data->attributes.st_nlink = (is_directory ? 2 : 1);
  file_context.meta_data->attributes.st_uid = fuse_get_context()->uid;
  file_context.meta_data->attributes.st_gid = fuse_get_context()->gid;

  try {
    Global<Storage>::g_fuse_drive->Create(full_path, std::move(file_context));
  }
  catch (const std::exception& e) {
    LOG(kError) << "CreateNew: " << full_path << ": " << e.what();
    return -EIO;
  }

  return 0;
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
//   catch (...) {
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
  void Mount();

 private:
  FuseDrive(const FuseDrive&);
  FuseDrive(FuseDrive&&);
  FuseDrive& operator=(FuseDrive);

  void Init();

  static int OpsCreate(const char* path, mode_t mode, struct fuse_file_info* file_info);
  static void OpsDestroy(void* fuse);

  static int OpsFlush(const char* path, struct fuse_file_info* file_info);
  static int OpsFtruncate(const char* path, off_t size, struct fuse_file_info* file_info);
  static int OpsMkdir(const char* path, mode_t mode);
  static int OpsMknod(const char* path, mode_t mode, dev_t rdev);
  static int OpsOpen(const char* path, struct fuse_file_info* file_info);
  static int OpsOpendir(const char* path, struct fuse_file_info* file_info);
  static int OpsRead(const char* path, char* buf, size_t size, off_t offset,
                     struct fuse_file_info* file_info);
  static int OpsRelease(const char* path, struct fuse_file_info* file_info);
  static int OpsReleasedir(const char* path, struct fuse_file_info* file_info);
  static int OpsRmdir(const char* path);
  static int OpsTruncate(const char* path, off_t size);
  static int OpsUnlink(const char* path);
  static int OpsWrite(const char* path, const char* buf, size_t size, off_t offset,
                      struct fuse_file_info* file_info);

  static int OpsAccess(const char* path, int mask);
  static int OpsChmod(const char* path, mode_t mode);
  static int OpsChown(const char* path, uid_t uid, gid_t gid);
  static int OpsFgetattr(const char* path, struct stat* stbuf, struct fuse_file_info* file_info);
  static int OpsFsync(const char* path, int isdatasync, struct fuse_file_info* file_info);
  static int OpsFsyncDir(const char* path, int isdatasync, struct fuse_file_info* file_info);
  static int OpsGetattr(const char* path, struct stat* stbuf);
//  static int OpsLink(const char* to, const char* from);
//  static int OpsLock(const char* path, struct fuse_file_info* file_info, int cmd,
//                     struct flock* lock);
  static int OpsReaddir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
                        struct fuse_file_info* file_info);
  static int OpsReadlink(const char* path, char* buf, size_t size);
  static int OpsRename(const char* old_name, const char* new_name);
  static int OpsStatfs(const char* path, struct statvfs* stbuf);
  static int OpsSymlink(const char* to, const char* from);
  static int OpsUtimens(const char* path, const struct timespec ts[2]);

#ifdef HAVE_SETXATTR
//  xattr operations are optional and can safely be left unimplemented
//  static int OpsSetxattr(const char* path, const char* name, const char* value, size_t size,
//                         int flags);
//  static int OpsGetxattr(const char* path, const char* name, char* value, size_t size);
//  static int OpsListxattr(const char* path, char* list, size_t size);
//  static int OpsRemovexattr(const char* path, const char* name);
#endif  // HAVE_SETXATTR
  static int Release(const char* path, struct fuse_file_info* file_info);
  virtual void SetNewAttributes(detail::FileContext<Storage>* file_context, bool is_directory,
                                bool read_only);

  static struct fuse_operations maidsafe_ops_;
  struct fuse* fuse_;
  fs::path fuse_mountpoint_;
  std::string drive_name_;
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
      fuse_mountpoint_(mount_dir),
      drive_name_(drive_name.string()) {
  fs::create_directory(fuse_mountpoint_);
  Init();
}

template <typename Storage>
FuseDrive<Storage>::~FuseDrive() {
  log::Logging::Instance().Flush();
}

template <typename Storage>
void FuseDrive<Storage>::Init() {
  Global<Storage>::g_fuse_drive = this;
  maidsafe_ops_.create = OpsCreate;
  maidsafe_ops_.destroy = OpsDestroy;
  maidsafe_ops_.flush = OpsFlush;
  maidsafe_ops_.ftruncate = OpsFtruncate;
  maidsafe_ops_.mkdir = OpsMkdir;
  maidsafe_ops_.mknod = OpsMknod;
  maidsafe_ops_.open = OpsOpen;
  maidsafe_ops_.opendir = OpsOpendir;
  maidsafe_ops_.read = OpsRead;
  maidsafe_ops_.release = OpsRelease;
  maidsafe_ops_.releasedir = OpsReleasedir;
  maidsafe_ops_.rmdir = OpsRmdir;
  maidsafe_ops_.truncate = OpsTruncate;
  maidsafe_ops_.unlink = OpsUnlink;
  maidsafe_ops_.write = OpsWrite;
  maidsafe_ops_.access = OpsAccess;
  maidsafe_ops_.chmod = OpsChmod;
  maidsafe_ops_.chown = OpsChown;
  maidsafe_ops_.fgetattr = OpsFgetattr;
//   maidsafe_ops_.fsync = OpsFsync;
  maidsafe_ops_.fsyncdir = OpsFsyncDir;
  maidsafe_ops_.getattr = OpsGetattr;
//  maidsafe_ops_.link = OpsLink;
//  maidsafe_ops_.lock = OpsLock;
  maidsafe_ops_.readdir = OpsReaddir;
  maidsafe_ops_.readlink = OpsReadlink;
  maidsafe_ops_.rename = OpsRename;
  maidsafe_ops_.statfs = OpsStatfs;
//  maidsafe_ops_.symlink = OpsSymlink;
  maidsafe_ops_.utimens = OpsUtimens;
#ifdef HAVE_SETXATTR
  maidsafe_ops_.setxattr = OpsSetxattr;
  maidsafe_ops_.getxattr = OpsGetxattr;
  maidsafe_ops_.listxattr = OpsListxattr;
  maidsafe_ops_.removexattr = OpsRemovexattr;
#endif
  //umask(0022);
}

template <typename Storage>
void FuseDrive<Storage>::Mount() {
  // boost::system::error_code error_code;
  // if (!fs::exists(Drive<Storage>::kMountDir_, error_code) || error_code) {
  //   LOG(kError) << "Mount dir " << Drive<Storage>::kMountDir_ << " doesn't exist."
  //               //<< (error_code ? ("  " + error_code.message()) : "");
  //   ThrowError(DriveErrors::failed_to_mount);
  // }

  // if (!fs::is_empty(Drive<Storage>::kMountDir_, error_code) || error_code) {
  //   LOG(kError) << "Mount dir " << Drive<Storage>::kMountDir_ << " isn't empty."
  //               // << (error_code ? ("  " + error_code.message()) : "");
  //   ThrowError(DriveErrors::failed_to_mount);
  // }
  fuse_args args = FUSE_ARGS_INIT(0, nullptr);
  fuse_opt_add_arg(&args, (drive_name_.c_str()));
  fuse_opt_add_arg(&args, (fuse_mountpoint_.c_str()));
  // NB - If we remove -odefault_permissions, we must check in OpsOpen
  //      that the operation is permitted for the given flags.  We also need to
  //      implement OpsAccess.
  // fuse_opt_add_arg(&args, "-odefault_permissions,kernel_cache,direct_io");
#ifndef NDEBUG
  // fuse_opt_add_arg(&args, "-d");  // print debug info
  // fuse_opt_add_arg(&args, "-f");  // run in foreground
#endif
  fuse_opt_add_arg(&args, "-s");  // this is single threaded

  //   if (read_only)
  //     fuse_opt_add_arg(&args, "-oro");
  fuse_main(args.argc, args.argv, &maidsafe_ops_, NULL);
  // struct fuse_args args = FUSE_ARGS_INIT(2, opts.get());
  // fuse helper macros for options
  // fuse_opt_parse(&args, nullptr, nullptr, nullptr);

  // NB - If we remove -odefault_permissions, we must check in OpsOpen
  //      that the operation is permitted for the given flags.  We also need to
  //      implement OpsAccess.
  // fuse_opt_add_arg(&args, "-odefault_permissions,kernel_cache,direct_io");
  //   if (read_only)
  //     fuse_opt_add_arg(&args, "-oro");
  //   if (debug_info)
  //     fuse_opt_add_arg(&args, "-odebug");
  // fuse_opt_add_arg(&args, "-f");  // run in foreground
  // fuse_opt_add_arg(&args, "-s");  // this is single threaded

  // int multithreaded, foreground;
  // if (fuse_parse_cmdline(&args, &fuse_mountpoint_, &multithreaded, &foreground) == -1)
  //   ThrowError(DriveErrors::failed_to_mount);

  // fuse_channel_ = fuse_mount(fuse_mountpoint_, &args);
  // if (!fuse_channel_) {
  //   fuse_opt_free_args(&args);
  //   free(fuse_mountpoint_);
  //   ThrowError(DriveErrors::failed_to_mount);
  // }

  // fuse_ = fuse_new(fuse_channel_, &args, &maidsafe_ops_, sizeof(maidsafe_ops_), nullptr);
  // fuse_opt_free_args(&args);
  // if (fuse_ == nullptr) {
  //   fuse_unmount(fuse_mountpoint_, fuse_channel_);
  //   if (fuse_)
  //     fuse_destroy(fuse_);
  //   free(fuse_mountpoint_);
  //   ThrowError(DriveErrors::failed_to_mount);
  // }

  // if (fuse_daemonize(foreground) == -1) {
  //   fuse_unmount(fuse_mountpoint_, fuse_channel_);
  //   if (fuse_)
  //     fuse_destroy(fuse_);
  //   free(fuse_mountpoint_);
  //   ThrowError(DriveErrors::failed_to_mount);
  // }

  // if (fuse_set_signal_handlers(fuse_get_session(fuse_)) == -1) {
  //   fuse_unmount(fuse_mountpoint_, fuse_channel_);
  //   if (fuse_)
  //     fuse_destroy(fuse_);
  //   free(fuse_mountpoint_);
  //   ThrowError(DriveErrors::failed_to_mount);
  // }

  // //  int res;
  // //  if (multithreaded)
  // //  fuse_event_loop_thread_ = boost::move(boost::thread(&fuse_loop_mt, fuse_));
  // //    res = fuse_loop_mt(fuse_);
  // //  else
  // fuse_event_loop_thread_ = std::move(std::thread(&fuse_loop, fuse_));
  // //    res = fuse_loop(fuse_);

  // //  if (res != 0) {
  // //    LOG(kError) << "Fuse Loop result: " << res;
  // //    Drive<Storage>::SetMountState(false);
  // //    return kFuseFailedToMount;
  // //  }
}

// =============================== Callbacks =======================================================

template <typename Storage>
int FuseDrive<Storage>::OpsAccess(const char* /*path*/, int /* mask */) {
  return 0;
}

// Create and open a file.  If the file does not exist, first create it with the specified mode, and
// then open it.
//
// If this method is not implemented or under Linux kernel versions earlier than 2.6.15, the mknod()
// and open() methods will be called instead.
template <typename Storage>
int FuseDrive<Storage>::OpsCreate(const char* path, mode_t mode, struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsCreate: " << path << " (" << GetFileType(mode) << "), mode: " << std::oct
             << mode;
  return CreateNew(path, mode);
}

template <typename Storage>
void FuseDrive<Storage>::OpsDestroy(void* /*fuse*/) {
  LOG(kInfo) << "OpsDestroy";
}

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

template <typename Storage>
int FuseDrive<Storage>::OpsFtruncate(const char* path, off_t size,
                                     struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsFtruncate: " << path << ", size: " << size;
  detail::FileContext<Storage>* file_context(detail::RecoverFileContext<Storage>(file_info));
  if (!file_context)
    return -EINVAL;

  if (Global<Storage>::g_fuse_drive->TruncateFile(file_context, size)) {
    file_context->meta_data->attributes.st_size = size;
    time(&file_context->meta_data->attributes.st_mtime);
    file_context->meta_data->attributes.st_ctime = file_context->meta_data->attributes.st_atime =
        file_context->meta_data->attributes.st_mtime;
    //    Update(Global<Storage>::g_fuse_drive->directory_handler_, file_context, false, false);
    if (!file_context->self_encryptor->Flush()) {
      LOG(kInfo) << "OpsFtruncate: " << path << ", failed to flush";
    }
  }
  return 0;
}

// Create a directory.
//
// Note that the mode argument may not have the type specification bits set, i.e. S_ISDIR(mode) can
// be false. To obtain the correct directory type bits use mode|S_IFDIR
template <typename Storage>
int FuseDrive<Storage>::OpsMkdir(const char* path, mode_t mode) {
  LOG(kInfo) << "OpsMkdir: " << path << " (" << GetFileType(mode) << "), mode: " << std::oct
             << mode;
  return CreateNew(path, mode);
}

// Create a file node.
//
// This is called for creation of all non-directory, non-symlink  nodes. If the filesystem defines a
// create() method, then for regular files that will be called instead.
template <typename Storage>
int FuseDrive<Storage>::OpsMknod(const char* path, mode_t mode, dev_t rdev) {
  LOG(kInfo) << "OpsMknod: " << path << " (" << GetFileType(mode) << "), mode: " << std::oct
             << mode << std::dec << ", rdev: " << rdev;
  assert(!S_ISDIR(mode) && !GetFileType(mode).empty());
  return CreateNew(path, mode, rdev);
}

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

  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsOpendir(const char* path, struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsOpendir: " << path << ", flags: " << file_info->flags << ", keep_cache: "
             << file_info->keep_cache << ", direct_io: " << file_info->direct_io;

  std::unique_ptr<detail::FileContext<Storage>> file_context(
      detail::RecoverFileContext<Storage>(file_info));
  if (file_context) {
    file_context.release();
    return 0;
  }

  file_info->keep_cache = 1;
  fs::path full_path(path);
  //  assert(file_info->flags & O_DIRECTORY);
  if (file_info->flags & O_NOFOLLOW) {
    LOG(kError) << "OpsOpendir: " << path << " is a symlink.";
    return -ELOOP;
  }

  try {
    file_context.reset(
        new detail::FileContext<Storage>(Global<Storage>::g_fuse_drive->GetFileContext(full_path)));
  }
  catch (...) {
    LOG(kError) << "OpsOpendir: " << path << ", failed to GetMetaData.";
    return -ENOENT;
  }

  // Transfer ownership of the pointer to fuse's file_info.
  detail::SetFileContext(file_info, file_context.get());
  file_context.release();
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsRead(const char* path, char* buf, size_t size, off_t offset,
                                struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsRead: " << path << ", flags: 0x" << std::hex << file_info->flags << std::dec
             << " Size : " << size << " Offset : " << offset;

  detail::FileContext<Storage>* file_context(detail::RecoverFileContext<Storage>(file_info));
  if (!file_context)
    return -EINVAL;

  //   // Update in case this follows a write (to force a flush on the
  //   // encrypted stream)
  //   int result(kSuccess);
  //   if (file_context->file_content_changed) {
  //     result = Global<Storage>::g_fuse_drive->Update(file_context, false);
  //     if (result != kSuccess) {
  //       LOG(kError) << "OpsRead: " << path << ", failed to update.  Result: "
  //                   << result;
  //       return -EIO;
  //     }
  //   }

  // TODO(Fraser#5#): 2011-05-18 - Check this is right.
  if (file_context->meta_data->attributes.st_size == 0)
    return 0;

  assert(file_context->self_encryptor);
  //     if (!file_context->self_encryptor) {
  //       file_context->self_encryptor.reset(new encrypt::SE(
  //           file_context->meta_data->data_map, Global<Storage>::g_fuse_drive->chunk_store()));
  //     }

  bool msrf_result(file_context->self_encryptor->Read(buf, size, offset));
  if (!msrf_result) {
    return -EINVAL;
    // unsuccessful read -> invalid encrypted stream, error already logged
    // return (msrf_result == kInvalidSeek) ? -EINVAL : -EBADF;
  }
  size_t bytes_read(0);

  if (static_cast<uint64_t>(size + offset) > file_context->self_encryptor->size()) {
    if (static_cast<uint64_t>(offset) > file_context->self_encryptor->size())
      bytes_read = 0;
    else
      bytes_read = file_context->self_encryptor->size() - offset;
  } else {
    bytes_read = size;
  }

  LOG(kInfo) << "OpsRead: " << path << ", bytes read: " << bytes_read
             << " from the file with size of: " << file_context->self_encryptor->size();
  time(&file_context->meta_data->attributes.st_atime);
  file_context->content_changed = true;

  //    result = Global<Storage>::g_fuse_drive->Update(file_context, false);
  //    if (result != kSuccess) {
  //      LOG(kError) << "OpsRead: " << path << ", failed to update.  Result: "
  //                  << result;
  //      // Non-critical error - don't return here.
  //    }
  return bytes_read;
}

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
  return Release(path, file_info);
}

// Release directory.
template <typename Storage>
int FuseDrive<Storage>::OpsReleasedir(const char* path, struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsReleasedir: " << path << ", flags: " << file_info->flags;
  return Release(path, file_info);
}

template <typename Storage>
int FuseDrive<Storage>::OpsRmdir(const char* path) {
  LOG(kInfo) << "OpsRmdir: " << path;

  // try {
  //  fs::path full_path(path);
  //  Global<Storage>::g_fuse_drive->GetFileContext(full_path);
  // } catch(...) {
  //  LOG(kError) << "OpsRmdir " << full_path << ", failed to get data for the item.";
  //  return -ENOENT;
  // }

  try {
    Global<Storage>::g_fuse_drive->Delete(path);
  }
  catch (...) {
    LOG(kError) << "OpsRmdir: " << path << ", failed MaidSafeDelete.  ";
    //     return -ENOTEMPTY;
    return -EIO;
  }
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsTruncate(const char* path, off_t size) {
  LOG(kInfo) << "OpsTruncate: " << path << ", size: " << size;
  try {
    fs::path full_path(path);
    auto file_context(Global<Storage>::g_fuse_drive->GetFileContext(full_path));
    if (Global<Storage>::g_fuse_drive->TruncateFile(&file_context, size)) {
      file_context.meta_data->attributes.st_size = size;
      time(&file_context.meta_data->attributes.st_mtime);
      file_context.meta_data->attributes.st_ctime = file_context.meta_data->attributes.st_atime =
          file_context.meta_data->attributes.st_mtime;
      Global<Storage>::g_fuse_drive->UpdateParent(&file_context, full_path.parent_path());
    } else {
      ThrowError(CommonErrors::invalid_parameter);
    }
  }
  catch (...) {
    LOG(kWarning) << "OpsTruncate: " << path << ", failed to locate file.";
    return -ENOENT;
  }

  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsUnlink(const char* path) {
  LOG(kInfo) << "OpsUnlink: " << path;
  // try {
  //  fs::path full_path(path);
  //  Global<Storage>::g_fuse_drive->GetFileContext(full_path);
  // } catch(...) {
  //  LOG(kError) << "OpsUnlink " << full_path << ", failed to get parent data for the item.";
  //  return -ENOENT;
  // }

  try {
    Global<Storage>::g_fuse_drive->Delete(path);
  }
  catch (...) {
    LOG(kError) << "OpsUnlink: " << path << ", failed MaidSafeDelete.  ";
    return -EIO;
  }

  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsWrite(const char* path, const char* buf, size_t size, off_t offset,
                                 struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsWrite: " << path << ", flags: 0x" << std::hex << file_info->flags << std::dec
             << " Size : " << size << " Offset : " << offset;

  detail::FileContext<Storage>* file_context(detail::RecoverFileContext<Storage>(file_info));
  if (!file_context)
    return -EINVAL;

  assert(file_context->self_encryptor);
  if (!file_context->self_encryptor) {
    LOG(kInfo) << "Resetting the encryption stream";
    file_context->self_encryptor.reset(
          new encrypt::SelfEncryptor<Storage>(file_context->meta_data->data_map,
              *Global<Storage>::g_fuse_drive->storage_));
  }

  // TODO(JLB): 2011-06-02 - look into SSIZE_MAX?

  bool mswf_result(file_context->self_encryptor->Write(buf, size, offset));
  if (!mswf_result) {
    // unsuccessful write -> invalid enc. stream, error already logged
    LOG(kError) << "Error writing file: " << mswf_result;
    return -EBADF;
  }

  int64_t max_size(
      std::max(static_cast<off_t>(offset + size), file_context->meta_data->attributes.st_size));
  file_context->meta_data->attributes.st_size = max_size;

  file_context->meta_data->attributes.st_blocks = file_context->meta_data->attributes.st_size / 512;
  LOG(kInfo) << "OpsWrite: " << path << ", bytes written: " << size
             << ", file size: " << file_context->meta_data->attributes.st_size
             << ", mswf_result: " << mswf_result;

  time(&file_context->meta_data->attributes.st_mtime);
  file_context->meta_data->attributes.st_ctime = file_context->meta_data->attributes.st_mtime;
  file_context->content_changed = true;

  //    int result(Global<Storage>::g_fuse_drive->Update(file_context, false));
  //    if (result != kSuccess) {
  //      LOG(kError) << "OpsWrite: " << path << ", failed to update parent "
  //                  << "dir.  Result: " << result;
  //      // Non-critical error - don't return here.
  //    }
  return size;
}

template <typename Storage>
int FuseDrive<Storage>::OpsChmod(const char* path, mode_t mode) {
  LOG(kInfo) << "OpsChmod: " << path << ", to " << std::oct << mode;
  detail::FileContext<Storage> file_context;
  try {
    file_context =
        detail::FileContext<Storage>(Global<Storage>::g_fuse_drive->GetFileContext(path));
  }
  catch (...) {
    LOG(kError) << "OpsChmod: " << path << ", can't get meta data.";
    return -ENOENT;
  }

  file_context.meta_data->attributes.st_mode = mode;
  time(&file_context.meta_data->attributes.st_ctime);
  file_context.content_changed = true;
  //  int result(Update(Global<Storage>::g_fuse_drive->directory_handler_, &file_context,
  //                    false, true));
  //  if (result != kSuccess) {
  //    LOG(kError) << "OpsChmod: " << path << ", fail to update parent "
  //                << "dir.  Result: " << result;
  //    return -EBADF;
  //  }
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsChown(const char* path, uid_t uid, gid_t gid) {
  LOG(kInfo) << "OpsChown: " << path;
  detail::FileContext<Storage> file_context;
  try {
    file_context = Global<Storage>::g_fuse_drive->GetFileContext(path);
  }
  catch (...) {
    LOG(kError) << "OpsChown: " << path << ", can't get meta data.";
    return -ENOENT;
  }

  bool changed(false);
  if (uid != static_cast<uid_t>(-1)) {
    file_context.meta_data->attributes.st_uid = uid;
    changed = true;
  }
  if (gid != static_cast<gid_t>(-1)) {
    file_context.meta_data->attributes.st_gid = gid;
    changed = true;
  }
  if (changed) {
    time(&file_context.meta_data->attributes.st_ctime);
    file_context.content_changed = true;
    //    int result(Update(Global<Storage>::g_fuse_drive->directory_handler_, &file_context,
    //                      false, true));
    //    if (result != kSuccess) {
    //      LOG(kError) << "OpsChown: " << path << ", failed to update parent " << "dir: " <<
    // result;
    //      return -EIO;
    //    }
  }
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsFgetattr(const char* path, struct stat* stbuf,
                                    struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsFgetattr: " << path;
  detail::FileContext<Storage>* file_context(detail::RecoverFileContext<Storage>(file_info));
  if (!file_context)
    return -ENOENT;

  *stbuf = file_context->meta_data->attributes;

  // TODO(Dan): Verify that the following code is not needed. I think that the
  //            fuse_file_info should contain already the correct info since it
  //            should have been populated correctly in Getattr. That's what I
  //            think.
  //  if (ReadOnlyShare(file_context->parent_share_data))
  //    stbuf->st_mode = (0444 | S_IFREG);

  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsFsync(const char* path, int /*isdatasync*/,
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

template <typename Storage>
int FuseDrive<Storage>::OpsFsyncDir(const char* path, int /*isdatasync*/,
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

template <typename Storage>
int FuseDrive<Storage>::OpsGetattr(const char* path, struct stat* stbuf) {
  LOG(kInfo) << "OpsGetattr: " << path;
  int result(0);
  fs::path full_path(path);
  try {
    auto file_context(Global<Storage>::g_fuse_drive->GetFileContext(full_path));
    *stbuf = file_context.meta_data->attributes;
    //    LOG(kInfo) << " meta_data info  = ";
    //    LOG(kInfo) << "     name =  " << file_context.meta_data->name.c_str();
    //    LOG(kInfo) << "     st_dev = " << file_context.meta_data->attributes.st_dev;
    //    LOG(kInfo) << "     st_ino = " << file_context.meta_data->attributes.st_ino;
    LOG(kInfo) << "     st_mode = " << file_context.meta_data->attributes.st_mode;
//    LOG(kInfo) << "     st_nlink = " << file_context.meta_data->attributes.st_nlink;
//    LOG(kInfo) << "     st_uid = " << file_context.meta_data->attributes.st_uid;
//    LOG(kInfo) << "     st_gid = " << file_context.meta_data->attributes.st_gid;
//    LOG(kInfo) << "     st_rdev = " << file_context.meta_data->attributes.st_rdev;
//    LOG(kInfo) << "     st_size = " << file_context.meta_data->attributes.st_size;
//    LOG(kInfo) << "     st_blksize = " << file_context.meta_data->attributes.st_blksize;
//    LOG(kInfo) << "     st_blocks = " << file_context.meta_data->attributes.st_blocks;
//    LOG(kInfo) << "     st_atim = " << file_context.meta_data->attributes.st_atime;
//    LOG(kInfo) << "     st_mtim = " << file_context.meta_data->attributes.st_mtime;
//    LOG(kInfo) << "     st_ctim = " << file_context.meta_data->attributes.st_ctime;
  }
  catch (...) {
    if (full_path.filename().string().size() > 255) {
      LOG(kError) << "OpsGetattr: " << full_path.filename() << " too long.";
      return -ENAMETOOLONG;
    }
    LOG(kWarning) << "OpsGetattr: " << full_path << ", can't get meta data.";
    return -ENOENT;
  }
  return result;
}

/*
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

template <typename Storage>
int FuseDrive<Storage>::OpsReaddir(const char* path, void* buf, fuse_fill_dir_t filler,
                                   off_t offset, struct fuse_file_info* file_info) {
  LOG(kInfo) << "OpsReaddir: " << path << "; offset = " << offset;

  filler(buf, ".", nullptr, 0);
  filler(buf, "..", nullptr, 0);
  std::shared_ptr<detail::DirectoryListing> dir_listing;
  try {
    detail::Directory directory(Global<Storage>::g_fuse_drive->GetDirectory(fs::path(path)));
    dir_listing = directory.listing;
  }
  catch (...) {
  }

  if (!dir_listing) {
    LOG(kError) << "OpsReaddir: " << path << ", can't get dir listing.";
    return -EBADF;
  }

  MetaData meta_data;
  // TODO(Fraser#5#): 2011-05-18 - Handle offset properly.
  if (offset == 0)
    dir_listing->ResetChildrenIterator();

  while (dir_listing->GetChildAndIncrementItr(meta_data)) {
    if (filler(buf, meta_data.name.c_str(), &meta_data.attributes, 0))
      break;
  }

  detail::FileContext<Storage>* file_context(detail::RecoverFileContext<Storage>(file_info));
  if (file_context) {
    file_context->content_changed = true;
    time(&file_context->meta_data->attributes.st_atime);
  }
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsReadlink(const char* path, char* buf, size_t /*size*/) {
  LOG(kInfo) << "OpsReadlink: " << path;
  try {
    auto file_context(Global<Storage>::g_fuse_drive->GetFileContext(path));
    if (S_ISLNK(file_context.meta_data->attributes.st_mode)) {
      snprintf(buf, file_context.meta_data->link_to.string().size() + 1, "%s",
               file_context.meta_data->link_to.string().c_str());
    } else {
      LOG(kError) << "OpsReadlink " << path << ", no link returned.";
      return -EINVAL;
    }
  }
  catch (...) {
    LOG(kWarning) << "OpsReadlink: " << path << ", can't get meta data.";
    return -ENOENT;
  }
  return 0;
}

template <typename Storage>
int FuseDrive<Storage>::OpsRename(const char* old_name, const char* new_name) {
  LOG(kInfo) << "OpsRename: " << old_name << " --> " << new_name;

  fs::path old_path(old_name), new_path(new_name);
  detail::FileContext<Storage> old_file_context;
  try {
    old_file_context = Global<Storage>::g_fuse_drive->GetFileContext(old_path);
  }
  catch (...) {
    LOG(kError) << "OpsRename " << old_path << " --> " << new_path << ", failed to get meta data.";
    return -ENOENT;
  }

  // TODO(JLB): 2011-06-01 - look into value of LINK_MAX for potentially
  // exceeding, requiring a return of EMLINK

  try {
    Global<Storage>::g_fuse_drive->Rename(old_path, new_path, *old_file_context.meta_data);
  }
  catch (...) {
    LOG(kError) << "OpsRename " << old_path << " --> " << new_path
                << ", failed to rename meta data.";
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

template <typename Storage>
int FuseDrive<Storage>::OpsUtimens(const char* path, const struct timespec ts[2]) {
  LOG(kInfo) << "OpsUtimens: " << path;
  detail::FileContext<Storage> file_context;
  try {
    file_context = Global<Storage>::g_fuse_drive->GetFileContext(path);
  }
  catch (...) {
    LOG(kError) << "OpsUtimens: " << path << ", can't get meta data.";
    return -ENOENT;
  }

#if defined __USE_MISC || defined __USE_XOPEN2K8 || defined MAIDSAFE_APPLE
  time(&file_context.meta_data->attributes.st_ctime);
  if (ts) {
    file_context.meta_data->attributes.st_atime = ts[0].tv_sec;
    file_context.meta_data->attributes.st_mtime = ts[1].tv_sec;
  } else {
    file_context.meta_data->attributes.st_mtime = file_context.meta_data->attributes.st_atime =
        file_context.meta_data->attributes.st_ctime;
  }
#else
  timespec tspec;
  clock_gettime(CLOCK_MONOTONIC, &tspec);
  file_context.meta_data->attributes.st_ctime = tspec.tv_sec;
  file_context.meta_data->attributes.st_ctimensec = tspec.tv_nsec;
  if (ts) {
    file_context.meta_data->attributes.st_atime = ts[0].tv_sec;
    file_context.meta_data->attributes.st_atimensec = ts[0].tv_nsec;
    file_context.meta_data->attributes.st_mtime = ts[1].tv_sec;
    file_context.meta_data->attributes.st_mtimensec = ts[1].tv_nsec;
  } else {
    file_context.meta_data->attributes.st_atime = tspec.tv_sec;
    file_context.meta_data->attributes.st_atimensec = tspec.tv_nsec;
    file_context.meta_data->attributes.st_mtime = tspec.tv_sec;
    file_context.meta_data->attributes.st_mtimensec = tspec.tv_nsec;
  }
#endif
  file_context.content_changed = true;
  //  int result(Update(Global<Storage>::g_fuse_drive->directory_handler_, &file_context,
  //                    false, true));
  //  if (result != kSuccess) {
  //    LOG(kError) << "OpsUtimens: " << path << ", failed to update.  " << "Result: " << result;
  //    return -EBADF;
  //  }
  return 0;
}



/***************************************************************
 * NOT IMPLEMENTED BELOW THIS LINE (Not required!)
 * **********************************************************


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

// We can set extended attribute for our own purposes
// i.e. if we wanted to store extra info (revisions for instance)
// then we can do it here easy enough.
#ifdef HAVE_SETXATTR
// xattr operations are optional and can safely be left unimplemented
int FuseDrive<Storage>::OpsSetxattr(const char* path, const char* name, const char* value,
                                    size_t size, int flags) {
  LOG(kInfo) << "OpsSetxattr: " << path << ", flags: " << flags;
  fs::path full_path(Global<Storage>::g_fuse_drive->metadata_dir_ / name);
  int res = lsetxattr(TranslatePath(full_path, path).c_str(),
                      name, value, size, flags);
  if (res == -1){
    LOG(kError) << "OpsSetxattr: " << path << ", flags: " << flags;
    return -errno;
  }
  return 0;
}

int FuseDrive<Storage>::OpsGetxattr(const char* path, const char* name, char* value, size_t size) {
  LOG(kInfo) << "OpsGetxattr: " << path;
  fs::path full_path(Global<Storage>::g_fuse_drive->metadata_dir_ / name);
  int res = lgetxattr(TranslatePath(name, path).c_str(), name, value, size);
  if (res == -1){
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
  if (res == -1){
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
  if (res == -1){
    LOG(kError) << "OpsRemovexattr: " << path;
    return -errno;
  }
  return 0;
}
#endif  // HAVE_SETXATTR
*/
// End of not implemented

template <typename Storage>
int FuseDrive<Storage>::Release(const char* path, struct fuse_file_info* file_info) {
  LOG(kInfo) << "Release - " << path;
  try {
    Global<Storage>::g_fuse_drive->Release(path);
  }
  catch (const std::exception& e) {
    LOG(kError) << "Release: " << path << ": " << e.what();
    return -EBADF;
  }
  return 0;
}

template <typename Storage>
void FuseDrive<Storage>::SetNewAttributes(detail::FileContext<Storage>* file_context,
                                          bool is_directory, bool read_only) {
  LOG(kError) << "SetNewAttributes - name: " << file_context->meta_data->name
              << ", read_only: " << std::boolalpha << read_only;
  time(&file_context->meta_data->attributes.st_atime);
  file_context->meta_data->attributes.st_ctime = file_context->meta_data->attributes.st_mtime =
      file_context->meta_data->attributes.st_atime;
  file_context->meta_data->attributes.st_uid = fuse_get_context()->uid;
  file_context->meta_data->attributes.st_gid = fuse_get_context()->gid;

  // TODO(Fraser#5#): 2011-12-04 - Check these modes are OK
  if (is_directory) {
    if (read_only)
      file_context->meta_data->attributes.st_mode = (0555 | S_IFDIR);
    else
      file_context->meta_data->attributes.st_mode = (0755 | S_IFDIR);
    file_context->meta_data->attributes.st_nlink = 2;
  } else {
    if (read_only)
      file_context->meta_data->attributes.st_mode = (0444 | S_IFREG);
    else
      file_context->meta_data->attributes.st_mode = (0644 | S_IFREG);
    file_context->meta_data->attributes.st_nlink = 1;
    assert(file_context->self_encryptor);
    file_context->meta_data->attributes.st_size = file_context->self_encryptor->size();
    file_context->meta_data->attributes.st_blocks =
        file_context->meta_data->attributes.st_size / 512;
  }
}

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_UNIX_DRIVE_H_
