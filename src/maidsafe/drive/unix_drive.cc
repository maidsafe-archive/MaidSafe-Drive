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

#include "maidsafe/drive/unix_drive.h"

#include <dirent.h>
#ifdef HAVE_SETXATTR
#  include <sys/xattr.h>
#endif

#include <sys/stat.h>
#include <sys/time.h>
#include <algorithm>
#include <cerrno>
#include <memory>
#include <string>
#include <utility>

#include "boost/filesystem.hpp"
#include "boost/shared_array.hpp"
#include "boost/algorithm/string/find.hpp"
#include "maidsafe/common/log.h"

#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/directory_listing_handler.h"
#include "maidsafe/drive/return_codes.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/utils.h"

namespace fs = boost::filesystem;


namespace maidsafe {

namespace drive {

fs::path RelativePath(const fs::path &mount_dir, const fs::path &absolute_path) {
  if (absolute_path.string().substr(0, mount_dir.string().size()) != mount_dir.string())
    return fs::path();
  return fs::path(absolute_path.string().substr(mount_dir.string().size()));
}

const int kMaxPath(4096);

namespace {

FuseDriveInUserSpace *g_fuse_drive;

static inline struct FileContext *GetFileContext(
  struct fuse_file_info *file_info) {
  if (file_info->fh == 0) {
    LOG(kWarning) << "Bad pointer.";
    return nullptr;
  }
  return reinterpret_cast<FileContext*>(file_info->fh);
}

static inline void SetFileContext(
    struct fuse_file_info *file_info,
    struct FileContext *file_context) {
  file_info->fh = reinterpret_cast<uint64_t>(file_context);
}

}  // unnamed namespace


struct fuse_operations FuseDriveInUserSpace::maidsafe_ops_;
const bool FuseDriveInUserSpace::kAllowMsHidden_(false);

FuseDriveInUserSpace::FuseDriveInUserSpace(ClientNfs& client_nfs,
                                           DataStore& data_store,
                                           const Maid& maid,
                                           const Identity& unique_user_id,
                                           const std::string& root_parent_id,
                                           const fs::path &mount_dir,
                                           const fs::path &drive_name,
                                           const int64_t &max_space,
                                           const int64_t &used_space)
        : DriveInUserSpace(client_nfs,
                           data_store,
                           maid,
                           unique_user_id,
                           root_parent_id,
                           mount_dir,
                           max_space,
                           used_space),
          fuse_(nullptr),
          fuse_channel_(nullptr),
          fuse_mountpoint_(nullptr),
          drive_name_(drive_name.string()),
          fuse_event_loop_thread_(),
          open_files_() {
  g_fuse_drive = this;
  int result = Init();
  if (result != kSuccess) {
    LOG(kError) << "Constructor Failed to initialise drive.  Result: " << result;
    ThrowError(LifeStuffErrors::kCreateStorageError);
  }
}

FuseDriveInUserSpace::~FuseDriveInUserSpace() {
  Unmount(max_space_, used_space_);
}

int FuseDriveInUserSpace::Init() {
  maidsafe_ops_.create       = OpsCreate;
  maidsafe_ops_.destroy      = OpsDestroy;
#ifdef MAIDSAFE_APPLE
  maidsafe_ops_.flush        = OpsFlush;
#endif
  maidsafe_ops_.ftruncate    = OpsFtruncate;
  maidsafe_ops_.mkdir        = OpsMkdir;
  maidsafe_ops_.mknod        = OpsMknod;
  maidsafe_ops_.open         = OpsOpen;
  maidsafe_ops_.opendir      = OpsOpendir;
  maidsafe_ops_.read         = OpsRead;
  maidsafe_ops_.release      = OpsRelease;
  maidsafe_ops_.releasedir   = OpsReleasedir;
  maidsafe_ops_.rmdir        = OpsRmdir;
  maidsafe_ops_.truncate     = OpsTruncate;
  maidsafe_ops_.unlink       = OpsUnlink;
  maidsafe_ops_.write        = OpsWrite;
//  maidsafe_ops_.access       = OpsAccess;
  maidsafe_ops_.chmod        = OpsChmod;
  maidsafe_ops_.chown        = OpsChown;
  maidsafe_ops_.fgetattr     = OpsFgetattr;
//   maidsafe_ops_.fsync        = OpsFsync;
  maidsafe_ops_.fsyncdir     = OpsFsyncDir;
  maidsafe_ops_.getattr      = OpsGetattr;
//  maidsafe_ops_.link         = OpsLink;
//  maidsafe_ops_.lock         = OpsLock;
  maidsafe_ops_.readdir      = OpsReaddir;
  maidsafe_ops_.readlink     = OpsReadlink;
  maidsafe_ops_.rename       = OpsRename;
  maidsafe_ops_.statfs       = OpsStatfs;
//  maidsafe_ops_.symlink      = OpsSymlink;
  maidsafe_ops_.utimens      = OpsUtimens;
#ifdef HAVE_SETXATTR
  maidsafe_ops_.setxattr    = OpsSetxattr;
  maidsafe_ops_.getxattr    = OpsGetxattr;
  maidsafe_ops_.listxattr   = OpsListxattr;
  maidsafe_ops_.removexattr = OpsRemovexattr;
#endif

  umask(0022);

  drive_stage_ = kInitialised;
  return kSuccess;
}

int FuseDriveInUserSpace::Mount() {
  boost::system::error_code error_code;
  if (!fs::exists(mount_dir_, error_code) || error_code) {
    LOG(kError) << "Mount dir " << mount_dir_ << " doesn't exist."
                << (error_code ? ("  " + error_code.message()) : "");
    return kMountError;
  }


  if (!fs::is_empty(mount_dir_, error_code) || error_code) {
    LOG(kError) << "Mount dir " << mount_dir_ << " isn't empty."
                << (error_code ? ("  " + error_code.message()) : "");
    return kMountError;
  }

  boost::shared_array<char*> opts(new char*[2]);
  opts[0] = const_cast<char*>(drive_name_.c_str());
  opts[1] = const_cast<char*>(mount_dir_.c_str());

  struct fuse_args args = FUSE_ARGS_INIT(2, opts.get());
  // fuse helper macros for options
  fuse_opt_parse(&args, nullptr, nullptr, nullptr);

  // NB - If we remove -odefault_permissions, we must check in OpsOpen
  //      that the operation is permitted for the given flags.  We also need to
  //      implement OpsAccess.
  fuse_opt_add_arg(&args, "-odefault_permissions,kernel_cache,direct_io");
//   if (read_only)
//     fuse_opt_add_arg(&args, "-oro");
//   if (debug_info)
//     fuse_opt_add_arg(&args, "-odebug");
  fuse_opt_add_arg(&args, "-f");  // run in foreground
  fuse_opt_add_arg(&args, "-s");  // this is single threaded

  int multithreaded, foreground;
  if (fuse_parse_cmdline(&args, &fuse_mountpoint_, &multithreaded, &foreground) == -1) {
    return kFuseFailedToParseCommandLine;
  }

  fuse_channel_ = fuse_mount(fuse_mountpoint_, &args);
  if (!fuse_channel_) {
    fuse_opt_free_args(&args);
    free(fuse_mountpoint_);
    return kFuseFailedToMount;
  }

  fuse_ = fuse_new(fuse_channel_, &args, &maidsafe_ops_, sizeof(maidsafe_ops_), nullptr);
  fuse_opt_free_args(&args);
  if (fuse_ == nullptr) {
    fuse_unmount(fuse_mountpoint_, fuse_channel_);
    if (fuse_)
      fuse_destroy(fuse_);
    free(fuse_mountpoint_);
    return kFuseNewFailed;
  }

  if (fuse_daemonize(foreground) == -1) {
    fuse_unmount(fuse_mountpoint_, fuse_channel_);
    if (fuse_)
      fuse_destroy(fuse_);
    free(fuse_mountpoint_);
    return kFuseFailedToDaemonise;
  }

  if (fuse_set_signal_handlers(fuse_get_session(fuse_)) == -1) {
    fuse_unmount(fuse_mountpoint_, fuse_channel_);
    if (fuse_)
      fuse_destroy(fuse_);
    free(fuse_mountpoint_);
    return kFuseFailedToSetSignalHandlers;
  }

  SetMountState(true);


  int res;
  if (multithreaded)
//  fuse_event_loop_thread_ = boost::move(boost::thread(&fuse_loop_mt, fuse_));
    res = fuse_loop_mt(fuse_);
  else
  //  fuse_event_loop_thread_ = boost::move(boost::thread(&fuse_loop, fuse_));
    res = fuse_loop(fuse_);

  if (res != 0) {
    LOG(kError) << "Fuse Loop result: " << res;
    SetMountState(false);
    return kFuseFailedToMount;
  }

  return kSuccess;
}

bool FuseDriveInUserSpace::Unmount(int64_t &max_space, int64_t &used_space) {
  if (drive_stage_ != kMounted) {
//    LOG(kInfo) << "Not mounted at all;";
    return false;  // kUnmountError
  }
#ifdef MAIDSAFE_APPLE
  std::string command(g_fuse_drive->GetMountDir().string());
  boost::mutex::scoped_lock lock(g_fuse_drive->unmount_mutex_);
#endif
  max_space = max_space_;
  used_space = used_space_;
  fuse_exit(fuse_);
#ifdef MAIDSAFE_APPLE
  fuse_unmount(fuse_mountpoint_, fuse_channel_);
#else
  fuse_teardown(fuse_, fuse_mountpoint_);
#endif
  SetMountState(false);
#ifdef MAIDSAFE_APPLE
  command = "diskutil unmount " + command;
  system(command.c_str());
#endif
  return true;  // kSuccess
}

int64_t FuseDriveInUserSpace::UsedSpace() const {
  return used_space_;
}

/********************************* content ************************************/

int FuseDriveInUserSpace::OpsCreate(const char *path,
                                    mode_t mode,
                                    struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsCreate: " << path << ", mode: " << std::oct << mode << ", "
             << std::boolalpha << S_ISDIR(mode) << ", open_file_count="
             << g_fuse_drive->open_files_.size();

  fs::path full_path(path);
  if (ExcludedFilename(full_path)) {
    LOG(kError) << "OpsCreate: invalid name " << full_path.filename();
    return -EINVAL;
  }
  bool is_directory(S_ISDIR(mode));
  file_info->fh = 0;

  std::shared_ptr<FileContext> file_context(
      new FileContext(full_path.filename(), is_directory));

  time(&file_context->meta_data->attributes.st_atime);
  file_context->meta_data->attributes.st_ctime =
      file_context->meta_data->attributes.st_mtime =
      file_context->meta_data->attributes.st_atime;
  file_context->meta_data->attributes.st_mode = mode;
  file_context->meta_data->attributes.st_nlink = (is_directory ? 2 : 1);
  file_context->meta_data->attributes.st_uid = fuse_get_context()->uid;
  file_context->meta_data->attributes.st_gid = fuse_get_context()->gid;

  try {
    g_fuse_drive->directory_listing_handler_->AddElement(full_path,
                                                         *file_context->meta_data.get(),
                                                         &file_context->grandparent_directory_id,
                                                         &file_context->parent_directory_id);
  } catch(...) {
    LOG(kError) << "OpsCreate: " << path << ", failed to AddNewMetaData.  ";
    return -EIO;
  }

  if (!is_directory) {
    // create a copy of the datamap to avoid updates being made to the original
    encrypt::DataMapPtr data_map(new encrypt::DataMap());
    *data_map = *file_context->meta_data->data_map;
    file_context->meta_data->data_map = data_map;
    file_context->self_encryptor.reset(new encrypt::SelfEncryptor(file_context->meta_data->data_map,
                                                                  g_fuse_drive->client_nfs_,
                                                                  g_fuse_drive->data_store_));
  }

  file_info->keep_cache = 1;
  SetFileContext(file_info, file_context.get());
//   UniqueLock lock(g_fuse_drive->shared_mutex_);
  g_fuse_drive->open_files_.insert(std::make_pair(full_path, file_context));
#ifdef DEBUG
  for (auto i = g_fuse_drive->open_files_.begin();
       i != g_fuse_drive->open_files_.end(); ++i)
    LOG(kInfo) << "\t\t" << (*i).first;
#endif
  g_fuse_drive->drive_changed_signal_(g_fuse_drive->mount_dir_ / full_path, fs::path(), kCreated);

  return 0;
}

void FuseDriveInUserSpace::OpsDestroy(void */*fuse*/) {
  LOG(kInfo) << "OpsDestroy";
//   g_fuse_drive->SetMountState(false);
}

int FuseDriveInUserSpace::OpsFlush(const char *path, struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsFlush: " << path << ", flags: " << file_info->flags;
/*  FileContext *file_context(GetFileContext(file_info));
  if (!file_context) {
    LOG(kError) << "OpsFlush: " << path << ", failed find filecontext for " << path;
    return -EINVAL;
  }

  int result(ForceFlush(g_fuse_drive->directory_listing_handler_, file_context));
  if (result != kSuccess) {
    LOG(kError) << "OpsFlush: " << path << ", failed to update. Result: " << result;
    return -EBADF;
  }
*/
  return 0;
}

int FuseDriveInUserSpace::OpsFtruncate(const char *path,
                                       off_t size,
                                       struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsFtruncate: " << path << ", size: " << size;
  FileContext *file_context(GetFileContext(file_info));
  if (!file_context)
    return -EINVAL;

  if (g_fuse_drive->TruncateFile(file_context, size)) {
    if (file_context->meta_data->attributes.st_size < size) {
      int64_t additional_size(size - file_context->meta_data->attributes.st_size);
      if (additional_size + g_fuse_drive->used_space_ > g_fuse_drive->max_space_) {
        LOG(kError) << "OpsTruncate: " << path << ", not enough memory.";
        return -ENOSPC;
      } else {
        g_fuse_drive->used_space_ += additional_size;
      }
    } else if (file_context->meta_data->attributes.st_size > size) {
      int64_t reduced_size(file_context->meta_data->attributes.st_size - size);
      if (g_fuse_drive->used_space_ < reduced_size) {
        g_fuse_drive->used_space_ = 0;
      } else {
        g_fuse_drive->used_space_ -= reduced_size;
      }
    }
    file_context->meta_data->attributes.st_size = size;
    time(&file_context->meta_data->attributes.st_mtime);
    file_context->meta_data->attributes.st_ctime =
        file_context->meta_data->attributes.st_atime =
        file_context->meta_data->attributes.st_mtime;
//    Update(g_fuse_drive->directory_listing_handler_, file_context, false, false);
    if (!file_context->self_encryptor->Flush()) {
        LOG(kInfo) << "OpsFtruncate: " << path << ", failed to flush";
    }
  }
  return 0;
}

int FuseDriveInUserSpace::OpsMkdir(const char *path, mode_t mode) {
  LOG(kInfo) << "OpsMkdir: " << path << ", mode: " << std::oct
             << mode << ", " << std::boolalpha << S_ISDIR(mode);

  fs::path full_path(path);
  if (ExcludedFilename(full_path)) {
    LOG(kError) << "OpsMkdir: invalid name " << full_path.filename();
    return -EINVAL;
  }
  MetaData meta_data(full_path.filename(), true);
  meta_data.attributes.st_nlink = 2;
  meta_data.attributes.st_uid = fuse_get_context()->uid;
  meta_data.attributes.st_gid = fuse_get_context()->gid;

  try {
    g_fuse_drive->directory_listing_handler_->AddElement(
        full_path, meta_data, nullptr, nullptr);
  } catch(...) {
    LOG(kError) << "OpsMkdir: " << path << ", failed to AddNewMetaData.  ";
    return -EIO;
  }

  g_fuse_drive->used_space_ += kDirectorySize;
  g_fuse_drive->drive_changed_signal_(g_fuse_drive->mount_dir_ / full_path, fs::path(), kCreated);
  return 0;
}

int FuseDriveInUserSpace::OpsMknod(const char *path, mode_t mode, dev_t rdev) {
#ifdef DEBUG
  std::string file_type;
  if (S_ISFIFO(mode))
    file_type = "FIFO-special";
  if (S_ISCHR(mode))
    file_type = "Character-special";
  if (S_ISDIR(mode))
    file_type = "Directory";
  if (S_ISBLK(mode))
    file_type = "Block-special";
  if (S_ISREG(mode))
    file_type = "Regular";
  LOG(kInfo) << "OpsMknod: " << path << "(" << file_type << "), mode: "
             << std::oct << mode << std::dec << ", rdev: " << rdev;
  BOOST_ASSERT(!S_ISDIR(mode) && !file_type.empty());
#endif

  fs::path full_path(path);
// TODO(Fraser#5#): 2011-05-18 - Cater for FIFO (and other?) modes in MetaData.
  MetaData meta_data(full_path.filename(), false);
  meta_data.attributes.st_mode = mode;
  meta_data.attributes.st_rdev = rdev;
  meta_data.attributes.st_uid = fuse_get_context()->uid;
  meta_data.attributes.st_gid = fuse_get_context()->gid;

  try {
    g_fuse_drive->directory_listing_handler_->AddElement(
        full_path, meta_data, nullptr, nullptr);
  } catch(...) {
    LOG(kError) << "OpsMknod: " << path << ", failed to AddNewMetaData.  ";
    return -EIO;
  }

  meta_data.attributes.st_size = kDirectorySize;
  g_fuse_drive->used_space_ += kDirectorySize;

  g_fuse_drive->drive_changed_signal_(g_fuse_drive->mount_dir_ / full_path, fs::path(), kCreated);

  return 0;
}

int FuseDriveInUserSpace::OpsOpen(const char *path, struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsOpen: " << path << ", flags: " << file_info->flags
             << ", keep_cache: " << file_info->keep_cache << ", direct_io: "
             << file_info->direct_io;
// TODO(Fraser#5#): 2011-06-01 - Consider handling O_NONBLOCK.
  FileContext *file_context;
  file_info->keep_cache = 1;
  fs::path full_path(path);
  bool is_directory(file_info->flags & O_DIRECTORY);
  std::shared_ptr<FileContext> file_context_ptr(
      new FileContext(full_path.filename(), is_directory));
  file_context = file_context_ptr.get();

  auto itr(g_fuse_drive->open_files_.find(full_path));

  if (itr != g_fuse_drive->open_files_.end()) {
    file_context->meta_data = (*itr).second->meta_data;
    file_context->parent_directory_id = (*itr).second->parent_directory_id;
    file_context->self_encryptor = (*itr).second->self_encryptor;
  } else {
    file_context->meta_data->name = full_path.filename();
    try {
      g_fuse_drive->GetMetaData(full_path,
                                *file_context->meta_data.get(),
                                &file_context->grandparent_directory_id,
                                &file_context->parent_directory_id);
    } catch(...) {
      LOG(kError) << "OpsOpen: " << path << ", failed to GetMetaData.";
      return -ENOENT;
    }
    if (!is_directory) {
      // create a copy of the datamap to avoid updates being made to the original
      encrypt::DataMapPtr data_map(new encrypt::DataMap());
      *data_map = *file_context->meta_data->data_map;
      file_context->meta_data->data_map = data_map;
    }

    if ((file_info->flags & O_NOFOLLOW) &&
        (file_context->meta_data->link_to != fs::path())) {
      LOG(kError) << "OpsOpen: " << path << " is a symlink.";
      return -ELOOP;
    }
  }

  if (file_context->meta_data->data_map) {
    if (is_directory) {
      LOG(kError) << "OpsOpen: " << path << " is a directory.";
      return -EISDIR;
    }

    if (!file_context->self_encryptor) {
      file_context->self_encryptor.reset(
          new encrypt::SelfEncryptor(file_context->meta_data->data_map,
                                     g_fuse_drive->client_nfs_,
                                     g_fuse_drive->data_store_));
    }
  }
  SetFileContext(file_info, file_context);
//   UpgradeToUniqueLock unique_lock(upgrade_lock);
  g_fuse_drive->open_files_.insert(std::make_pair(full_path, file_context_ptr));
  return 0;
}

int FuseDriveInUserSpace::OpsOpendir(const char *path, struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsOpendir: " << path << ", flags: " << file_info->flags
             << ", keep_cache: " << file_info->keep_cache << ", direct_io: "
             << file_info->direct_io;

  FileContext *file_context;
  file_info->keep_cache = 1;
  fs::path full_path(path);
  std::shared_ptr<FileContext> file_context_ptr(new FileContext(full_path.filename(), true));
  file_context = file_context_ptr.get();

  auto itr(g_fuse_drive->open_files_.find(full_path));
  if (itr != g_fuse_drive->open_files_.end()) {
    file_context->meta_data = (*itr).second->meta_data;
    file_context->grandparent_directory_id = (*itr).second->grandparent_directory_id;
    file_context->parent_directory_id = (*itr).second->parent_directory_id;
  } else {
    file_context->meta_data->name = full_path.filename();
    try {
      g_fuse_drive->GetMetaData(full_path,
                                *file_context->meta_data.get(),
                                &file_context->grandparent_directory_id,
                                &file_context->parent_directory_id);
    } catch(...) {
      LOG(kError) << "OpsOpendir: " << path << ", failed to GetMetaData.";
      return -ENOENT;
    }
  }
  SetFileContext(file_info, file_context);

  g_fuse_drive->open_files_.insert(std::make_pair(full_path, file_context_ptr));
  return 0;
}

int FuseDriveInUserSpace::OpsRead(const char *path,
                                  char *buf,
                                  size_t size,
                                  off_t offset,
                                  struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsRead: " << path << ", flags: 0x" << std::hex << file_info->flags << std::dec
             << " Size : " <<  size << " Offset : " << offset;

  FileContext *file_context(GetFileContext(file_info));
  if (!file_context)
    return -EINVAL;

//   // Update in case this follows a write (to force a flush on the
//   // encrypted stream)
//   int result(kSuccess);
//   if (file_context->file_content_changed) {
//     result = g_fuse_drive->Update(file_context, false);
//     if (result != kSuccess) {
//       LOG(kError) << "OpsRead: " << path << ", failed to update.  Result: "
//                   << result;
//       return -EIO;
//     }
//   }

  // TODO(Fraser#5#): 2011-05-18 - Check this is right.
  if (file_context->meta_data->attributes.st_size == 0)
    return 0;

  BOOST_ASSERT(file_context->self_encryptor);
//     if (!file_context->self_encryptor) {
//       file_context->self_encryptor.reset(new encrypt::SE(
//           file_context->meta_data->data_map, g_fuse_drive->chunk_store()));
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

//    result = g_fuse_drive->Update(file_context, false);
//    if (result != kSuccess) {
//      LOG(kError) << "OpsRead: " << path << ", failed to update.  Result: "
//                  << result;
//      // Non-critical error - don't return here.
//    }
  return bytes_read;
}

int FuseDriveInUserSpace::OpsRelease(const char *path, struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsRelease: " << path << ", flags: " << file_info->flags;
  return Release(path, file_info);
}

int FuseDriveInUserSpace::OpsReleasedir(const char *path, struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsReleasedir: " << path << ", flags: " << file_info->flags;
  return Release(path, file_info);
}

int FuseDriveInUserSpace::OpsRmdir(const char *path) {
  LOG(kInfo) << "OpsRmdir: " << path;

  fs::path full_path(path);
  MetaData meta_data;
  try {
    g_fuse_drive->GetMetaData(full_path, meta_data, nullptr, nullptr);
  } catch(...) {
    LOG(kError) << "OpsRmdir " << full_path << ", failed to get data for the item.";
    return -ENOENT;
  }

  g_fuse_drive->used_space_ -= meta_data.attributes.st_size;

  try {
    g_fuse_drive->RemoveFile(path);
  } catch(...) {
    LOG(kError) << "OpsRmdir: " << path << ", failed MaidSafeDelete.  ";
//     return -ENOTEMPTY;
    return -EIO;
  }

  g_fuse_drive->drive_changed_signal_(g_fuse_drive->mount_dir_ / path, fs::path(), kRemoved);

  return 0;
}

int FuseDriveInUserSpace::OpsTruncate(const char *path, off_t size) {
  LOG(kInfo) << "OpsTruncate: " << path << ", size: " << size;

  // Check in the open file streams to truncate them
  bool update_metadata(true);
  {
//     UniqueLock unique_lock(g_fuse_drive->shared_mutex_);
    typedef std::multimap<fs::path, std::shared_ptr<FileContext>> multi;

    std::pair<multi::iterator, multi::iterator> p = g_fuse_drive->open_files_.equal_range(path);
    while (p.first != p.second) {
      FileContext *file_context = (*p.first).second.get();
      if (g_fuse_drive->TruncateFile(file_context, size)) {
        if (file_context->meta_data->attributes.st_size < size) {
          int64_t additional_size(size - file_context->meta_data->attributes.st_size);
          if (additional_size + g_fuse_drive->used_space_ > g_fuse_drive->max_space_) {
            LOG(kError) << "OpsTruncate: " << path << ", not enough memory.";
            return -ENOSPC;
          } else {
            g_fuse_drive->used_space_ += additional_size;
          }
        } else if (file_context->meta_data->attributes.st_size > size) {
          int64_t reduced_size(file_context->meta_data->attributes.st_size - size);
          if (g_fuse_drive->used_space_ < reduced_size) {
            g_fuse_drive->used_space_ = 0;
          } else {
            g_fuse_drive->used_space_ -= reduced_size;
          }
        }
        file_context->meta_data->attributes.st_size = size;
        time(&file_context->meta_data->attributes.st_mtime);
        file_context->meta_data->attributes.st_ctime =
            file_context->meta_data->attributes.st_atime =
            file_context->meta_data->attributes.st_mtime;
//        Update(g_fuse_drive->directory_listing_handler_, file_context, false, false);
      }
      update_metadata = false;
      ++p.first;
    }
  }

  if (update_metadata) {
    // Truncate the directory-listing-held stream
    FileContext file_context;

    try {
      g_fuse_drive->GetMetaData(path,
                                *file_context.meta_data.get(),
                                &file_context.grandparent_directory_id,
                                &file_context.parent_directory_id);
    } catch(...) {
      LOG(kWarning) << "OpsTruncate: " << path << ", failed to locate file.";
      return -ENOENT;
    }

    if (g_fuse_drive->TruncateFile(&file_context, size)) {
      if (file_context.meta_data->attributes.st_size < size) {
        int64_t additional_size(size - file_context.meta_data->attributes.st_size);
        if (additional_size + g_fuse_drive->used_space_ > g_fuse_drive->max_space_) {
          LOG(kError) << "OpsTruncate: " << path << ", not enough memory.";
          return -ENOSPC;
        } else {
          g_fuse_drive->used_space_ += additional_size;
        }
      } else if (file_context.meta_data->attributes.st_size > size) {
        int64_t reduced_size(file_context.meta_data->attributes.st_size - size);
        if (g_fuse_drive->used_space_ < reduced_size) {
          g_fuse_drive->used_space_ = 0;
        } else {
          g_fuse_drive->used_space_ -= reduced_size;
        }
      }
      file_context.meta_data->attributes.st_size = size;
      time(&file_context.meta_data->attributes.st_mtime);
      file_context.meta_data->attributes.st_ctime =
          file_context.meta_data->attributes.st_atime =
          file_context.meta_data->attributes.st_mtime;
//      Update(g_fuse_drive->directory_listing_handler_, &file_context, false, false);
      if (!file_context.self_encryptor->Flush()) {
        LOG(kError) << "OpsTruncate: " << path << ", failed to flush";
      }
    }
  }

  return 0;
}

int FuseDriveInUserSpace::OpsUnlink(const char *path) {
  LOG(kInfo) << "OpsUnlink: " << path;

  fs::path full_path(path);
  MetaData temp_meta;
  try {
    g_fuse_drive->GetMetaData(full_path, temp_meta, nullptr, nullptr);
  } catch(...) {
    LOG(kError) << "OpsUnlink " << full_path << ", failed to get parent data for the item.";
    return -ENOENT;
  }

  try {
    g_fuse_drive->RemoveFile(path);
  } catch(...) {
    LOG(kError) << "OpsUnlink: " << path << ", failed MaidSafeDelete.  ";
    return -EIO;
  }

  g_fuse_drive->used_space_ -= temp_meta.attributes.st_size;
  g_fuse_drive->drive_changed_signal_(g_fuse_drive->mount_dir_ / path, fs::path(), kRemoved);

  return 0;
}

int FuseDriveInUserSpace::OpsWrite(const char *path,
                                   const char *buf,
                                   size_t size,
                                   off_t offset,
                                   struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsWrite: " << path << ", flags: 0x" << std::hex << file_info->flags << std::dec
             << " Size : " <<  size << " Offset : " << offset;

  FileContext *file_context(GetFileContext(file_info));
  if (!file_context)
    return -EINVAL;

  if (!file_context->self_encryptor) {
    LOG(kInfo) << "Resetting the encryption stream";
    file_context->self_encryptor.reset(new encrypt::SelfEncryptor(file_context->meta_data->data_map,
                                                                  g_fuse_drive->client_nfs_,
                                                                  g_fuse_drive->data_store_));
  }

  // check for sufficient space before writing
//   if (((g_fuse_drive->max_space_ != 0) &&
//           static_cast<int64_t>(size + g_fuse_drive->used_space_) > g_fuse_drive->max_space_) ||
//       (g_fuse_drive->client_nfs_.Capacity() != 0 &&
//           (size + g_fuse_drive->client_nfs_.Size()) >
//               g_fuse_drive->client_nfs_.Capacity())) {
//     LOG(kError) << "OpsWrite: " << path << ", insufficient space available.";
//     return -ENOSPC;
//   }

  // TODO(JLB): 2011-06-02 - look into SSIZE_MAX?

  bool mswf_result(file_context->self_encryptor->Write(buf, size, offset));
  if (!mswf_result) {
    // unsuccessful write -> invalid enc. stream, error already logged
    LOG(kError) << "Error writing file: " << mswf_result;
    return -EBADF;
  }

  int64_t max_size(std::max(static_cast<off_t>(offset + size),
                            file_context->meta_data->attributes.st_size));
  if (file_context->meta_data->attributes.st_size != max_size) {
    int64_t additional_size(max_size - file_context->meta_data->attributes.st_size);
    if (additional_size + g_fuse_drive->used_space_ > g_fuse_drive->max_space_) {
      LOG(kError) << "OpsWrite: " << path << ", not enough memory.";
      return -ENOSPC;
    } else {
      g_fuse_drive->used_space_ += additional_size;
    }
    file_context->meta_data->attributes.st_size = max_size;
  }

  file_context->meta_data->attributes.st_blocks = file_context->meta_data->attributes.st_size / 512;
  LOG(kInfo) << "OpsWrite: " << path << ", bytes written: " << size
             << ", file size: " << file_context->meta_data->attributes.st_size
             << ", mswf_result: " << mswf_result;

  time(&file_context->meta_data->attributes.st_mtime);
  file_context->meta_data->attributes.st_ctime = file_context->meta_data->attributes.st_mtime;
  file_context->content_changed = true;

//    int result(g_fuse_drive->Update(file_context, false));
//    if (result != kSuccess) {
//      LOG(kError) << "OpsWrite: " << path << ", failed to update parent "
//                  << "dir.  Result: " << result;
//      // Non-critical error - don't return here.
//    }
  return size;
}


/********************************** metadata **********************************/

int FuseDriveInUserSpace::OpsChmod(const char *path, mode_t mode) {
  LOG(kInfo) << "OpsChmod: " << path << ", to " << std::oct << mode;
  FileContext file_context;
  try {
    g_fuse_drive->GetMetaData(path,
                              *file_context.meta_data.get(),
                              &file_context.grandparent_directory_id,
                              &file_context.parent_directory_id);
  } catch(...) {
    LOG(kError) << "OpsChmod: " << path << ", can't get meta data.";
    return -ENOENT;
  }

  file_context.meta_data->attributes.st_mode = mode;
  time(&file_context.meta_data->attributes.st_ctime);
  file_context.content_changed = true;
//  int result(Update(g_fuse_drive->directory_listing_handler_, &file_context, false, true));
//  if (result != kSuccess) {
//    LOG(kError) << "OpsChmod: " << path << ", fail to update parent "
//                << "dir.  Result: " << result;
//    return -EBADF;
//  }
  return 0;
}

int FuseDriveInUserSpace::OpsChown(const char *path, uid_t uid, gid_t gid) {
  LOG(kInfo) << "OpsChown: " << path;
  FileContext file_context;
  try {
    g_fuse_drive->GetMetaData(path,
                              *file_context.meta_data.get(),
                              &file_context.grandparent_directory_id,
                              &file_context.parent_directory_id);
  } catch(...) {
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
//    int result(Update(g_fuse_drive->directory_listing_handler_, &file_context, false, true));
//    if (result != kSuccess) {
//      LOG(kError) << "OpsChown: " << path << ", failed to update parent " << "dir: " << result;
//      return -EIO;
//    }
  }
  return 0;
}

int FuseDriveInUserSpace::OpsFgetattr(const char *path,
                                      struct stat *stbuf,
                                      struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsFgetattr: " << path;
  FileContext *file_context(GetFileContext(file_info));
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

int FuseDriveInUserSpace::OpsFsync(const char *path,
                                   int /*isdatasync*/,
                                   struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsFsync: " << path;
  FileContext *file_context(GetFileContext(file_info));
  if (!file_context)
    return -EINVAL;

  int result(ForceFlush(g_fuse_drive->directory_listing_handler_, file_context));
  if (result != kSuccess) {
//    int result(Update(g_fuse_drive->directory_listing_handler_,
//                      file_context,
//                      false,
//                      (isdatasync == 0)));
//    if (result != kSuccess) {
//      LOG(kError) << "OpsFsync: " << path << ", failed to update "
//                  << "metadata.  Result: " << result;
//      return -EIO;
//    }
  }
  return 0;
}

int FuseDriveInUserSpace::OpsFsyncDir(const char *path,
                                      int /*isdatasync*/,
                                      struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsFsyncDir: " << path;
  FileContext *file_context(GetFileContext(file_info));
  if (!file_context)
    return -EINVAL;

//  int result(Update(g_fuse_drive->directory_listing_handler_,
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

int FuseDriveInUserSpace::OpsGetattr(const char *path, struct stat *stbuf) {
  LOG(kInfo) << "OpsGetattr: " << path;
#ifdef MAIDSAFE_APPLE
  if (!g_fuse_drive->unmount_mutex_.try_lock()) {
    LOG(kInfo) << "try lock unmount_mutex_ failed";
    return -EIO;
  }
#endif
  int result(0);
  fs::path full_path(path);
  MetaData meta_data;
  try {
    g_fuse_drive->GetMetaData(full_path, meta_data, nullptr, nullptr);
  } catch(...) {
    if (full_path.filename().string().size() > 255) {
      LOG(kError) << "OpsGetattr: " << full_path.filename() << " too long.";
      return -ENAMETOOLONG;
    }
    LOG(kWarning) << "OpsGetattr: " << full_path << ", can't get meta data.";
    return -ENOENT;
  }

  *stbuf = meta_data.attributes;
//    LOG(kInfo) << " meta_data info  = ";
//    LOG(kInfo) << "     name =  " << meta_data.name.c_str();
//    LOG(kInfo) << "     st_dev = " << meta_data.attributes.st_dev;
//    LOG(kInfo) << "     st_ino = " << meta_data.attributes.st_ino;
  LOG(kInfo) << "     st_mode = " << meta_data.attributes.st_mode;
//    LOG(kInfo) << "     st_nlink = " << meta_data.attributes.st_nlink;
//    LOG(kInfo) << "     st_uid = " << meta_data.attributes.st_uid;
//    LOG(kInfo) << "     st_gid = " << meta_data.attributes.st_gid;
//    LOG(kInfo) << "     st_rdev = " << meta_data.attributes.st_rdev;
//    LOG(kInfo) << "     st_size = " << meta_data.attributes.st_size;
//    LOG(kInfo) << "     st_blksize = " << meta_data.attributes.st_blksize;
//    LOG(kInfo) << "     st_blocks = " << meta_data.attributes.st_blocks;
//    LOG(kInfo) << "     st_atim = " << meta_data.attributes.st_atime;
//    LOG(kInfo) << "     st_mtim = " << meta_data.attributes.st_mtime;
//    LOG(kInfo) << "     st_ctim = " << meta_data.attributes.st_ctime;

#ifdef MAIDSAFE_APPLE
  g_fuse_drive->unmount_mutex_.unlock();
#endif
  return result;
}

/*
int FuseDriveInUserSpace::OpsLink(const char *to, const char *from) {
  LOG(kInfo) << "OpsLink: " << from << " --> " << to;

  fs::path path_to(to), path_from(from);

  FileContext file_context_to;
  if (g_fuse_drive->GetMetaData(path_to, &file_context_to.meta_data,
                                &file_context.parent_directory_id)) {
    if (!S_ISDIR(file_context_to.meta_data.attributes.st_mode))
      ++file_context_to.meta_data.attributes.st_nlink;
    time(&file_context_to.meta_data.attributes.st_ctime);
    file_context_to.meta_data_changed = true;
    int result(g_fuse_drive->Update(&file_context_to, true));
    if (result != kSuccess) {
      LOG(kError) << "OpsLink: " << path_to << ", failed to update "
                  << "metadata.  Result: " << result;
      return -EIO;
    }

    MetaData meta_data_from(file_context_to.meta_data);
    meta_data_from.name = path_from.filename();

    result = g_fuse_drive->AddNewMetaData(path_from, &meta_data_from, nullptr);
    if (result != kSuccess) {
      LOG(kError) << "OpsLink: " << from << " --> " << to
                  << " failed to AddNewMetaData.  Result: " << result;
      return -EIO;
    }
    return 0;
  } else {
    LOG(kError) << "OpsLink: " << path_to << ", can't get meta data.";
    return -ENOENT;
  }
}
*/

int FuseDriveInUserSpace::OpsReaddir(const char *path,
                                     void *buf,
                                     fuse_fill_dir_t filler,
                                     off_t offset,
                                     struct fuse_file_info *file_info) {
  LOG(kInfo) << "OpsReaddir: " << path << "; offset = " << offset;

  filler(buf, ".", nullptr, 0);
  filler(buf, "..", nullptr, 0);
  DirectoryListingPtr dir_listing;
  try {
    DirectoryListingHandler::DirectoryType directory(
        g_fuse_drive->directory_listing_handler_->GetFromPath(fs::path(path)));
    dir_listing = directory.first.listing;
  } catch(...) {}

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

  FileContext *file_context(GetFileContext(file_info));
  if (file_context) {
    file_context->content_changed = true;
    time(&file_context->meta_data->attributes.st_atime);
  }
  return 0;
}

int FuseDriveInUserSpace::OpsReadlink(const char *path,
                                      char *buf,
                                      size_t /*size*/) {
  LOG(kInfo) << "OpsReadlink: " << path;
  MetaData meta_data;
  try {
    g_fuse_drive->GetMetaData(path, meta_data, nullptr, nullptr);
  } catch(...) {
    LOG(kWarning) << "OpsReadlink: " << path << ", can't get meta data.";
    return -ENOENT;
  }

  if (S_ISLNK(meta_data.attributes.st_mode)) {
    snprintf(buf, meta_data.link_to.string().size() + 1, "%s", meta_data.link_to.string().c_str());
  } else {
    LOG(kError) << "OpsReadlink " << path << ", no link returned.";
    return -EINVAL;
  }
  return 0;
}

int FuseDriveInUserSpace::OpsRename(const char *old_name, const char *new_name) {
  LOG(kInfo) << "OpsRename: " << old_name << " --> " << new_name;

  fs::path old_path(old_name), new_path(new_name);
  if (ExcludedFilename(new_path)) {
    LOG(kError) << "OpsRename: invalid new name " << new_path.filename();
    return -EINVAL;
  }
  // To improve performance, there is no update for each write operation
  // So, if an opened file_context needs to be renamed,
  // an Update must be called to ensure the meta_data is upto date
  std::string old_name_string(old_path.filename().string());
  for (auto itr = g_fuse_drive->open_files_.begin();
        itr != g_fuse_drive->open_files_.end(); ++itr) {
    std::string checking_name(((*itr).second.get())->meta_data->name.string());
    if (checking_name == old_name_string) {
        FileContext file_context = *(itr->second.get());
        if (file_context.self_encryptor->Flush()) {
          try {
            g_fuse_drive->directory_listing_handler_->UpdateParentDirectoryListing(
                (*itr).first.parent_path(), *file_context.meta_data.get());
          } catch(...) {
            LOG(kInfo) << "OpsRename: " << old_name << " --> " << new_name
                       << " , failed udpating open file";
          }
        } else {
          LOG(kError) << "OpsRename: " << old_name << " --> " << new_name
                      << " failed to flush.";
          return -EBADF;
        }
      break;
    }
  }

  MetaData meta_data;
  try {
    g_fuse_drive->GetMetaData(old_path, meta_data, nullptr, nullptr);
  } catch(...) {
    LOG(kError) << "OpsRename " << old_path << " --> " << new_path << ", failed to get meta data.";
    return -ENOENT;
  }

  // TODO(JLB): 2011-06-01 - look into value of LINK_MAX for potentially
  // exceeding, requiring a return of EMLINK

  int64_t reclaimed_space(0);
  try {
    g_fuse_drive->RenameFile(old_path, new_path, meta_data, reclaimed_space);
  } catch(...) {
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
  g_fuse_drive->used_space_ -= reclaimed_space;
  RenameOpenContexts(old_path.string(), new_path.string());

  g_fuse_drive->drive_changed_signal_(g_fuse_drive->mount_dir_ / old_path,
                                      g_fuse_drive->mount_dir_ / new_path,
                                      kRenamed);
  return 0;
}

int FuseDriveInUserSpace::OpsStatfs(const char *path, struct statvfs *stbuf) {
  LOG(kInfo) << "OpsStatfs: " << path;

//   int res = statvfs(g_fuse_drive->mount_dir().parent_path().string().c_str(),
//                     stbuf);
//   if (res < 0) {
//     LOG(kError) << "OpsStatfs: " << path;
//     return -errno;
//   }

  stbuf->f_bsize = 4096;
  stbuf->f_frsize = 4096;
  if (g_fuse_drive->max_space_ == 0) {
  // for future ref 2^45 = 35184372088832 = 32TB
#ifndef __USE_FILE_OFFSET64
    stbuf->f_blocks = 8796093022208 / stbuf->f_frsize;
    stbuf->f_bfree = 8796093022208/ stbuf->f_bsize;
#else
    stbuf->f_blocks = 8796093022208 / stbuf->f_frsize;
    stbuf->f_bfree = 8796093022208 / stbuf->f_bsize;
#endif
  } else {
    stbuf->f_blocks = g_fuse_drive->max_space_ / stbuf->f_frsize;
    stbuf->f_bfree = (g_fuse_drive->max_space_ - g_fuse_drive->used_space_) / stbuf->f_bsize;
  }
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

int FuseDriveInUserSpace::OpsUtimens(const char *path, const struct timespec ts[2]) {
  LOG(kInfo) << "OpsUtimens: " << path;
  FileContext file_context;
  try {
    g_fuse_drive->GetMetaData(path,
                              *file_context.meta_data.get(),
                              &file_context.grandparent_directory_id,
                              &file_context.parent_directory_id);
  } catch(...) {
    LOG(kError) << "OpsUtimens: " << path << ", can't get meta data.";
    return -ENOENT;
  }

#if defined __USE_MISC || defined __USE_XOPEN2K8 || defined MAIDSAFE_APPLE
  time(&file_context.meta_data->attributes.st_ctime);
  if (ts) {
    file_context.meta_data->attributes.st_atime = ts[0].tv_sec;
    file_context.meta_data->attributes.st_mtime = ts[1].tv_sec;
  } else {
    file_context.meta_data->attributes.st_mtime =
        file_context.meta_data->attributes.st_atime =
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
//  int result(Update(g_fuse_drive->directory_listing_handler_, &file_context, false, true));
//  if (result != kSuccess) {
//    LOG(kError) << "OpsUtimens: " << path << ", failed to update.  " << "Result: " << result;
//    return -EBADF;
//  }
  return 0;
}



/***************************************************************
 * NOT IMPLEMENTED BELOW THIS LINE (Not required!)
 * **********************************************************


int FuseDriveInUserSpace::OpsSymlink(const char *to, const char *from) {
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

  int result(AddNewMetaData(g_fuse_drive->directory_listing_handler_,
                            path_from, ShareData(), &meta_data, false,
                            nullptr, nullptr, nullptr));
  if (result != kSuccess) {
    LOG(kError) << "OpsSymlink: " << from << " --> " << to
                << " failed to AddNewMetaData.  Result: " << result;
    return -EIO;
  }

  g_fuse_drive->drive_changed_signal_(g_fuse_drive->mount_dir_ / full_path,
                                      fs::path(), kCreated);

  return 0;
}

// We can set extended attribute for our own purposes
// i.e. if we wanted to store extra info (revisions for instance)
// then we can do it here easy enough.
#ifdef HAVE_SETXATTR
// xattr operations are optional and can safely be left unimplemented
int FuseDriveInUserSpace::OpsSetxattr(const char *path,
                                      const char *name,
                                      const char *value,
                                      size_t size,
                                      int flags) {
  LOG(kInfo) << "OpsSetxattr: " << path << ", flags: " << flags;
  fs::path full_path(g_fuse_drive->metadata_dir_ / name);
  int res = lsetxattr(TranslatePath(full_path, path).c_str(),
                      name, value, size, flags);
  if (res == -1){
    LOG(kError) << "OpsSetxattr: " << path << ", flags: " << flags;
    return -errno;
  }
  return 0;
}

int FuseDriveInUserSpace::OpsGetxattr(const char *path,
                                      const char *name,
                                      char *value,
                                      size_t size) {
  LOG(kInfo) << "OpsGetxattr: " << path;
  fs::path full_path(g_fuse_drive->metadata_dir_ / name);
  int res = lgetxattr(TranslatePath(name, path).c_str(), name, value, size);
  if (res == -1){
    LOG(kError) << "OpsGetxattr: " << path;
    return -errno;
  }
  return res;
}

int FuseDriveInUserSpace::OpsListxattr(const char *path,
                                       char *list,
                                       size_t size) {
  LOG(kInfo) << "OpsListxattr: " << path;
  fs::path full_path(g_fuse_drive->metadata_dir_ / list);
  int res = llistxattr(TranslatePath(full_path.c_str(), path).c_str(),
                       list,
                       size);
  if (res == -1){
    LOG(kError) << "OpsListxattr: " << path;
    return -errno;
  }
  return res;
}

int FuseDriveInUserSpace::OpsRemovexattr(const char *path, const char *name) {
  LOG(kInfo) << "OpsRemovexattr: " << path;
  fs::path full_path(g_fuse_drive->metadata_dir_ / name);
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

int FuseDriveInUserSpace::Release(const char *path, struct fuse_file_info *file_info) {
  LOG(kInfo) << "Release - " << path;
  FileContext *file_context(GetFileContext(file_info));
  if (!file_context)
    return -EINVAL;

  fs::path full_path(path);
  int result(0);

  if (file_context->self_encryptor) {
    if (file_context->self_encryptor->Flush()) {
      if (file_context->content_changed) {
        try {
          g_fuse_drive->UpdateParent(file_context, full_path.parent_path());
        } catch(...) {
          LOG(kError) << "Release: " << path << ", failed to update.  Result: " << result;
          return -EBADF;
        }
      }
    }
  }

  file_info->fh = 0;
//   UpgradeLock upgrade_lock(g_fuse_drive->shared_mutex_);
#ifdef DEBUG
  LOG(kInfo) << "Release: " << path << ", erasing file ctxt: " << file_context;
  for (auto i = g_fuse_drive->open_files_.begin(); i != g_fuse_drive->open_files_.end(); ++i)
    LOG(kInfo) << "\t\t\t" << (*i).first;
#endif
//   auto found_range(g_fuse_drive->open_files_.equal_range(full_path));
//   while (found_range.first != found_range.second) {
//     if ((*found_range.first).second.get() == file_context) {
// //       UpgradeToUniqueLock unique_lock(upgrade_lock);
//       g_fuse_drive->open_files_.erase(found_range.first);
//       break;
//     }
//     ++found_range.first;
//   }
  for (auto itr(g_fuse_drive->open_files_.begin()); itr != g_fuse_drive->open_files_.end(); ++itr) {
    if ((*itr).second.get() == file_context) {
      g_fuse_drive->open_files_.erase(itr);
      break;
    }
  }
  LOG(kInfo) << "Release: " << path << ".  size after: " << g_fuse_drive->open_files_.size();
  return result;
}

void FuseDriveInUserSpace::RenameOpenContexts(const std::string &old_path,
                                              const std::string &new_path) {
  LOG(kInfo) << "RenameOpenContexts - " << old_path << " - " << new_path;
//   UpgradeLock upgrade_lock(g_fuse_drive->shared_mutex_);
  auto itr(g_fuse_drive->open_files_.begin());
  bool found(false);
  std::multimap<fs::path, std::shared_ptr<FileContext>> replacements;

  while (itr != g_fuse_drive->open_files_.end()) {
    if ((*itr).first.string().substr(0, old_path.size()) == old_path) {
      found = true;
      std::string modified_name(new_path + (*itr).first.string().substr(old_path.size()));
      std::pair<fs::path, std::shared_ptr<FileContext>> new_element(modified_name, (*itr).second);
//       UpgradeToUniqueLock unique_lock(upgrade_lock);
      g_fuse_drive->open_files_.erase(itr++);
      replacements.insert(new_element);
    } else if (found) {  // Now past last
      break;
    } else {
      ++itr;
    }
  }
  if (!replacements.empty()) {
//     UpgradeToUniqueLock unique_lock(upgrade_lock);
    g_fuse_drive->open_files_.insert(replacements.begin(), replacements.end());
  }
}

void FuseDriveInUserSpace::SetNewAttributes(FileContext *file_context,
                                            bool is_directory,
                                            bool read_only) {
  LOG(kError) << "SetNewAttributes - name: " << file_context->meta_data->name
              << ", read_only: " << std::boolalpha << read_only;
  time(&file_context->meta_data->attributes.st_atime);
  file_context->meta_data->attributes.st_ctime =
      file_context->meta_data->attributes.st_mtime =
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
    file_context->self_encryptor.reset(new encrypt::SelfEncryptor(file_context->meta_data->data_map,
                                                                  client_nfs_,
                                                                  data_store_));
    file_context->meta_data->attributes.st_size = file_context->self_encryptor->size();
    file_context->meta_data->attributes.st_blocks =
        file_context->meta_data->attributes.st_size / 512;
  }
}

void FuseDriveInUserSpace::NotifyRename(
    fs::path const& /*from_relative_path*/,fs::path const& /*to_relative_path*/) const {}


}  // namespace drive

}  // namespace maidsafe





