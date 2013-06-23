/*******************************************************************************
 *  Copyright 2011 maidsafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 *******************************************************************************
 */

#ifndef MAIDSAFE_DRIVE_DRIVE_API_H_
#define MAIDSAFE_DRIVE_DRIVE_API_H_

#include <tuple>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>
#include <mutex>

#include "boost/filesystem/path.hpp"
#include "boost/signals2/connection.hpp"
#include "boost/signals2/signal.hpp"
#include "boost/thread/condition_variable.hpp"
#include "boost/thread/mutex.hpp"

#include "maidsafe/common/rsa.h"
// #include "maidsafe/data_store/data_store.h"
#include "maidsafe/data_store/permanent_store.h"
#include "maidsafe/nfs/nfs.h"
#include "maidsafe/drive/config.h"

namespace fs = boost::filesystem;
namespace bs2 = boost::signals2;

namespace maidsafe {

namespace drive {

fs::path RelativePath(const fs::path& mount_dir, const fs::path& absolute_path);

struct FileContext;
struct MetaData;
class DirectoryListingHandler;

// For use in all cases of Create, Delete and Rename.  Signature is absolute
// path, new absolute path for Rename only, and operation type.
typedef bs2::signal<void(fs::path, fs::path, OpType)> DriveChangedSignal;
typedef std::shared_ptr<std::function<DriveChangedSignal::signature_type> > DriveChangedSlotPtr;
typedef std::function<void(const std::string&, const std::string&)> ShareRenamedFunction;
typedef bs2::signal<void(const std::string&, const std::string&)> ShareRenamedSignal;

// For use within a Share for Create, Delete, Move and Rename and ModifyContent.
// Signature is share_name, target path relative to the Share's root,
// num_of_entries (normally 1, only greater in case of Add and Delete children)
// old path relative to the Share's root (only for Rename and Move),
// new path relative to the Share's root (only for Rename and Move),
// and operation type.
typedef std::function<void(const std::string&,
                           const fs::path&,
                           const uint32_t&,
                           const fs::path&,
                           const fs::path&,
                           const OpType&)>
        ShareChangedFunction;
typedef bs2::signal<void(const std::string&,
                         const fs::path&,
                         const uint32_t&,
                         const fs::path&,
                         const fs::path&,
                         const OpType&)>
        ShareChangedSignal;

class DriveInUserSpace {
  typedef bs2::signal<void(const fs::path&, OpType op)> NotifyDirectoryChangeSignal;

 public:
  typedef nfs::ClientMaidNfs ClientNfs;
  // typedef data_store::DataStore<data_store::DataBuffer> DataStore;
  typedef data_store::PermanentStore DataStore;
  typedef passport::Maid Maid;

  // client_nfs: enables network file operations
  // data_store: an alternative to client_nfs for testing purpose
  // maid: client performing io operations
  // unique_user_id: a random id representing drive, it is created during user creation
  // root_parent_id: a random string representing parent of unique_user_id, requierd when performing
  //   operations in drive
  // mount_dir: identifies the path to which the drive mounts
  // max_space: drive maximum space
  // used_spance: drive used space
  DriveInUserSpace(ClientNfs& client_nfs,
                   DataStore& data_store,
                   const Maid& maid,
                   const Identity& unique_user_id,
                   const std::string& root_parent_id,
                   const fs::path& mount_dir,
                   const int64_t& max_space,
                   const int64_t& used_space);
  virtual ~DriveInUserSpace();
  virtual int Unmount(int64_t &max_space, int64_t &used_space) = 0;
#ifdef MAIDSAFE_APPLE
  fs::path GetMountDir() { return mount_dir_; }
#endif
  // returns drive's id
  std::string unique_user_id() const;
  // returns drive's parent id
  std::string root_parent_id() const;
  // returns drive used space
  int64_t GetUsedSpace() const;
  // sets the mount state of drive
  void SetMountState(bool mounted);
  // blocks until the state of drive becomes mounted. Times out if state does not change in
  // expected period
  bool WaitUntilMounted();
  // blocks until the state of drive becomes unmounted
  void WaitUntilUnMounted();

  // ********************* File / Folder Transfers *****************************

  // Retrieve the serialised DataMap of the file at relative_path (e.g. to send
  // to another client)
  void GetDataMap(const fs::path& relative_path, std::string* serialised_data_map);
  // Retrieve the serialised DataMap of hidden file at relative_path
  void GetDataMapHidden(const fs::path& relative_path, std::string* serialised_data_map);
  // Insert a file at relative_path derived from the serialised DataMap (e.g. if
  // receiving from another client)
  void InsertDataMap(const fs::path& relative_path, const std::string& serialised_data_map);

  // Moves existing directory 'from' to existing parent of 'to' outwith normal
  // filesystem calls then informs system of the changes, before any call 'to'
  // should not exist.
  int MoveDirectory(const fs::path& from, const fs::path& to);

  void GetMetaData(const fs::path& relative_path,
                   MetaData& meta_data,
                   DirectoryId* grandparent_directory_id,
                   DirectoryId* parent_directory_id);
  void UpdateParent(FileContext* file_context, const fs::path& parent_path);
  void AddFile(const fs::path& relative_path,
               const MetaData& meta_data,
               DirectoryId* grandparent_directory_id,
               DirectoryId* parent_directory_id);
  bool CanRemove(const fs::path& relative_path);
  void RemoveFile(const fs::path& relative_path);
  void RenameFile(const fs::path& old_relative_path,
                  const fs::path& new_relative_path,
                  MetaData& meta_data,
                  int64_t& reclaimed_space);
  bool TruncateFile(FileContext* file_context, const uint64_t& size);

  // *************************** Hidden Files **********************************

  // All hidden files in this sense have extension ".ms_hidden" and are not
  // accessible through the normal filesystem methods.
  void ReadHiddenFile(const fs::path& relative_path, std::string* content);
  void WriteHiddenFile(const fs::path& relative_path,
                      const std::string& content,
                      bool overwrite_existing);
  void DeleteHiddenFile(const fs::path& relative_path);
  void SearchHiddenFiles(const fs::path& relative_path, std::vector<std::string>* results);

  // **************************** File Notes ***********************************

  // Retrieve the collection of notes (serialised to strings) associated with
  // the given file/directory.
  void GetNotes(const fs::path& relative_path, std::vector<std::string>* notes);
  // Append a single serialised note to the collection of notes associated with
  // the given file/directory.
  void AddNote(const fs::path& relative_path, const std::string& note);

  // ************************* Signals Handling ********************************

  bs2::connection ConnectToDriveChanged(DriveChangedSlotPtr slot);
  bs2::connection ConnectToShareChanged(const ShareChangedFunction& function);
  bs2::connection ConnectToShareRenamed(const ShareRenamedFunction& function);

 protected:
  virtual void NotifyRename(const fs::path& from_relative_path,
                            const fs::path& to_relative_path) const = 0;

  enum DriveStage { kUnInitialised, kInitialised, kMounted, kUnMounted, kCleaned } drive_stage_;
  ClientNfs& client_nfs_;
  DataStore& data_store_;
  const Maid maid_;
  std::shared_ptr<DirectoryListingHandler> directory_listing_handler_;
  fs::path mount_dir_;
  int64_t max_space_, used_space_;
  DriveChangedSignal drive_changed_signal_;
  ShareRenamedSignal share_renamed_signal_;
  boost::mutex unmount_mutex_;
#ifdef MAIDSAFE_WIN32
  NotifyDirectoryChangeSignal notify_directory_change_;
#endif
  mutable std::mutex api_mutex_;

 private:
  virtual void SetNewAttributes(FileContext* file_context,
                                bool is_directory,
                                bool read_only) = 0;
  void ReadDataMap(const fs::path& relative_path, std::string* serialised_data_map);

  boost::condition_variable unmount_condition_variable_;
  boost::mutex mount_mutex_;
  boost::condition_variable mount_condition_variable_;
};

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DRIVE_API_H_
