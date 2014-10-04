Nfs Drive API
=========

Design document 0.1 - discussion stage

Introduction
============

Drive is represented as a cross patform cirtual filesystem at the moment. This requires the drive library interface with FUSE, OSXFUSE and CBFS for Linux, OSX and Windows repspectively. It also provides the back end network storage and retrieval (handled vis Nfs and Encrypt). This design requires this duality to be split into seperate libraries. 

Motivation
==========

The motivation for this proposal is twofold, to clear up the responsibility of libraries into a more defined structure and provide users of the Nfs library to create appplications without the need to install any kernel level drivers. This implementaion will also relax the requirements of the system that a drive object requires a complete routing node attached. This should reduce the required machine resources. 

Perhaps the greatest motivation though is to allow direct access to data and content from an application. As private shared drives are required the locking mechanism will be one of 'lock free'. Instead the system will provide eventual consistency, this is best suited for such netwworks and will require applicaitons are built with this in mind. Providing a virtual filesystem does not lend itself to this limitation/opportunity very well. 


Overview
=========

PLEASE DISCUSS - Best outcome is this interface can be used as directly as possible with fuse/osxfuse/cbfs as well as implementing a fielsystem that moves towards posix complience (forgiving the eventual consistency of course)


The API to be made available to Nfs (public interface) is below.
```c++
template <typename IdType>
struct NfsDrive {
  NfsDrive(IdType id_type); // retrieve root dir from network) ?? template or pass the root dir ??
  static int Chmod(const char* path, mode_t mode);
  static int Chown(const char* path, uid_t uid, gid_t gid);
  static int Create(const char* path, mode_t mode, struct file_info* file_info);
  static int Fgetattr(const char* path, struct stat* stbuf, struct file_info* file_info);
  static int Flush(const char* path, struct file_info* file_info);
  static int Fsync(const char* path, int isdatasync, struct file_info* file_info);
  static int FsyncDir(const char* path, int isdatasync, struct file_info* file_info);
  static int Ftruncate(const char* path, off_t size, struct file_info* file_info);
  static int Getattr(const char* path, struct stat* stbuf);
  static int Link(const char* to, const char* from);
  static int Lock(const char* path, struct fuse_file_info* file_info, int cmd, struct flock* lock);
  static int Mkdir(const char* path, mode_t mode);
  static int Mknod(const char* path, mode_t mode, dev_t rdev);
  static int Open(const char* path, struct fuse_file_info* file_info);
  static int Opendir(const char* path, struct fuse_file_info* file_info);
  static int Read(const char* path, char* buf, size_t size, off_t offset, struct file_info* file_info);
  static int Readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct file_info* file_info);
  static int Readlink(const char* path, char* buf, size_t size);
  static int Release(const char* path, struct file_info* file_info);
  static int Releasedir(const char* path, struct file_info* file_info);
  static int Rename(const char* old_name, const char* new_name);
  static int Rmdir(const char* path);
  static int Statfs(const char* path, struct statvfs* stbuf);
  static int Symlink(const char* to, const char* from);
  static int Truncate(const char* path, off_t size);
  static int Unlink(const char* path);
  static int Utimens(const char* path, const struct timespec ts[2]);
  static int Write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* file_info);
  static int CeateNew(const fs::path& full_path, mode_t mode, dev_t rdev = 0);
  static int GetAttributes(const char* path, struct stat* stbuf);
  static int Truncate(const char* path, off_t size);
};
```
The file_info struct is as anticipated to be a very simple struct in implementation 1.0. This essentially holds the pointer / handle to an open file.

```c++
struct file_info {
   /** File handle.  May be filled in by filesystem in open().                                       
       Available in all other file operations */                                                     
  uint64_t fh; 
};

```

