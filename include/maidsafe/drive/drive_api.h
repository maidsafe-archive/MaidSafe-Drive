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

  // populates the meta_data with information in relative_path. Moreover, the parent and the
  // grand_parent of the meta_data are also returned.
  void GetMetaData(const fs::path& relative_path,
                   MetaData& meta_data,
                   DirectoryId* grandparent_directory_id,
                   DirectoryId* parent_directory_id);
  // updates parent directory at parent_path using the meta_data member of file_context
  void UpdateParent(FileContext* file_context, const fs::path& parent_path);
  // Adds a directory or file represented by meta_data and relative_path to the appropriate parent
  // directory listing.  If the element is a directory, a new directory listing is created and
  // stored.  The parent directory's ID is returned in parent_id and its parent directory's ID is
  // returned in grandparent_id.
  void AddFile(const fs::path& relative_path,
               const MetaData& meta_data,
               DirectoryId* grandparent_directory_id,
               DirectoryId* parent_directory_id);
  // evaluates whether removing the contents at path are allowed
  bool CanRemove(const fs::path& relative_path);
  // Deletes file represented by relative_path from the appropriate parent directory listing
  void RemoveFile(const fs::path& relative_path);
  // renames or relocates file corresponding to meta_data from old_relative_path to
  // new new_relative_path. The space released in old_relative_path directory is
  void RenameFile(const fs::path& old_relative_path,
                  const fs::path& new_relative_path,
                  MetaData& meta_data,
                  int64_t& reclaimed_space);
  // resizes the file.
  bool TruncateFile(FileContext* file_context, const uint64_t& size);

  // *************************** Hidden Files **********************************

  // All hidden files in this sense have extension ".ms_hidden" and are not
  // accessible through the normal filesystem methods.

  // reads the hidden file at relative_path
  void ReadHiddenFile(const fs::path& relative_path, std::string* content);
  // writes the hidden file at elative_path
  void WriteHiddenFile(const fs::path& relative_path,
                      const std::string& content,
                      bool overwrite_existing);
  // delets a hidden file at relative_path
  void DeleteHiddenFile(const fs::path& relative_path);
  // returns the hidden files at relative_pathrelative_path
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
