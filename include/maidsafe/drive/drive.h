/*  Copyright 2011 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.novinet.com/license

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#ifndef MAIDSAFE_DRIVE_DRIVE_H_
#define MAIDSAFE_DRIVE_DRIVE_H_

#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include "boost/filesystem/path.hpp"

#include "maidsafe/common/rsa.h"
#include "maidsafe/data_store/sure_file_store.h"
#include "maidsafe/nfs/client/maid_node_nfs.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/root_handler.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/utils.h"


namespace maidsafe {

namespace drive {

boost::filesystem::path RelativePath(const boost::filesystem::path& mount_dir,
                                     const boost::filesystem::path& absolute_path);

template<typename Storage>
class DriveInUserSpace {
 public:
  DriveInUserSpace(std::shared_ptr<nfs_client::MaidNodeNfs> maid_node_nfs,
                   const Identity& unique_user_id,
                   const Identity& drive_root_id,
                   const boost::filesystem::path& mount_dir,
                   OnServiceAdded on_service_added);

  DriveInUserSpace(const Identity& drive_root_id,
                   const boost::filesystem::path& mount_dir,
                   OnServiceAdded on_service_added,
                   OnServiceRemoved on_service_removed,
                   OnServiceRenamed on_service_renamed);

  virtual ~DriveInUserSpace() {}

  virtual bool Unmount() = 0;

#ifdef MAIDSAFE_APPLE
  boost::filesystem::path GetMountDir() const { return kMountDir_; }
#endif

  Identity drive_root_id() const;

  void SetMountState(bool mounted);

  // Times out after 10 seconds.
  bool WaitUntilMounted();
  // Doesn't time out.
  void WaitUntilUnMounted();

  // ********************* LifeStuff share functions ***********************************************

  template<typename ShareStorage>
  void AddService(const boost::filesystem::path& service_alias,
                  std::shared_ptr<ShareStorage> storage);

  // ********************* SureFile functions ******************************************************

  void AddService(const boost::filesystem::path& service_alias,
                  const boost::filesystem::path& store_path,
                  const Identity& service_root_id);
  void RemoveService(const boost::filesystem::path& service_alias);

  // ********************* File / Folder Transfers *************************************************

  // Retrieve the serialised DataMap of the file at 'relative_path' (e.g. to send to another client)
  void GetDataMap(const boost::filesystem::path& relative_path, std::string* serialised_data_map);
  void GetDataMapHidden(const boost::filesystem::path& relative_path,
                        std::string* serialised_data_map);
  // Insert a file at 'relative_path' derived from the serialised DataMap (e.g. if
  // receiving from another client).
  void InsertDataMap(const boost::filesystem::path& relative_path,
                     const std::string& serialised_data_map);
  void GetMetaData(const boost::filesystem::path& relative_path,
                   MetaData& meta_data,
                   DirectoryId& grandparent_directory_id,
                   DirectoryId& parent_directory_id);
  // Adds a directory or file represented by 'meta_data' and 'relative_path' to the appropriate
  // parent directory listing. If the element is a directory, a new directory listing is created
  // and stored. The parent directory's ID is returned in 'parent_id' and its parent directory's ID
  // is returned in 'grandparent_id'.
  void AddFile(const boost::filesystem::path& relative_path,
               const MetaData& meta_data,
               DirectoryId& grandparent_directory_id,
               DirectoryId& parent_directory_id);
  bool CanRemove(const boost::filesystem::path& relative_path);
  // Deletes the file at 'relative_path' from the appropriate parent directory listing as well as
  // the listing associated with that path if it represents a directory.
  void RemoveFile(const boost::filesystem::path& relative_path);
  // Renames/moves the file located at 'old_relative_path' to that at 'new_relative_path', setting
  // 'reclaimed_space' to a non-zero value if the paths are identical and the file sizes differ.
  void RenameFile(const boost::filesystem::path& old_relative_path,
                  const boost::filesystem::path& new_relative_path,
                  MetaData& meta_data);

  // *************************** Hidden Files ******************************************************

  // All hidden files in this sense have extension ".ms_hidden" and are not accessible through the
  // normal filesystem methods.
  void ReadHiddenFile(const boost::filesystem::path& relative_path, std::string* content);
  void WriteHiddenFile(const boost::filesystem::path& relative_path,
                       const std::string& content,
                       bool overwrite_existing);
  void DeleteHiddenFile(const boost::filesystem::path& relative_path);
  // Returns all hidden files at 'relative_path'.
  std::vector<std::string> GetHiddenFiles(const boost::filesystem::path& relative_path);

  // **************************** File Notes *******************************************************

  // Retrieve the collection of notes (serialised to strings) associated with given file/directory.
  void GetNotes(const boost::filesystem::path& relative_path, std::vector<std::string>* notes);
  // Append a single serialised note to the collection of notes associated with given file/directory
  void AddNote(const boost::filesystem::path& relative_path, const std::string& note);

 protected:
  detail::Directory GetDirectory(const boost::filesystem::path& relative_path);
  Storage* GetStorage(const boost::filesystem::path& path) const;
  detail::FileContext<Storage> GetFileContext(const boost::filesystem::path& path) const;
  // Updates parent directory at 'parent_path' with the values contained in the 'file_context'.
  void UpdateParent(detail::FileContext<Storage>* file_context,
                    const boost::filesystem::path& parent_path);
  // Resizes the file.
  bool TruncateFile(const boost::filesystem::path& relative_path,
                    detail::FileContext<Storage>* file_context,
                    const uint64_t& size);
  virtual void NotifyDirectoryChange(const boost::filesystem::path& relative_path,
                                     detail::OpType op) const = 0;
  void NotifyRename(const boost::filesystem::path& from_relative_path,
                    const boost::filesystem::path& to_relative_path) const;

  enum DriveStage { kUnInitialised, kInitialised, kMounted, kUnMounted, kCleaned } drive_stage_;
  detail::RootHandler<Storage> root_handler_;
  const boost::filesystem::path kMountDir_;
  std::mutex unmount_mutex_;
  mutable std::mutex api_mutex_;

 private:
  DriveInUserSpace(const DriveInUserSpace&);
  DriveInUserSpace(DriveInUserSpace&&);
  DriveInUserSpace& operator=(const DriveInUserSpace&);

  std::unique_ptr<detail::DirectoryHandler<Storage>> GetHandler(
      const boost::filesystem::path& relative_path);
  virtual void SetNewAttributes(detail::FileContext<Storage>* file_context,
                                bool is_directory,
                                bool read_only) = 0;
  void ReadDataMap(const boost::filesystem::path& relative_path, std::string* serialised_data_map);

  std::condition_variable unmount_condition_variable_;
  std::mutex mount_mutex_;
  std::condition_variable mount_condition_variable_;
};

#ifdef MAIDSAFE_WIN32
boost::filesystem::path GetNextAvailableDrivePath();
#endif



// ==================== Implementation =============================================================
template<typename Storage>
DriveInUserSpace<Storage>::DriveInUserSpace(std::shared_ptr<nfs_client::MaidNodeNfs> maid_node_nfs,
                                            const Identity& unique_user_id,
                                            const Identity& drive_root_id,
                                            const boost::filesystem::path& mount_dir,
                                            OnServiceAdded on_service_added)
    : drive_stage_(kUnInitialised),
      root_handler_(maid_node_nfs, unique_user_id, drive_root_id, on_service_added),
      kMountDir_(mount_dir),
      unmount_mutex_(),
      api_mutex_(),
      unmount_condition_variable_(),
      mount_mutex_(),
      mount_condition_variable_() {
  static_assert(std::is_same<Storage, nfs_client::MaidNodeNfs>::value,
                "Cannot use without Lifestuff");
}

template<typename Storage>
DriveInUserSpace<Storage>::DriveInUserSpace(const Identity& drive_root_id,
                                            const boost::filesystem::path& mount_dir,
                                            OnServiceAdded on_service_added,
                                            OnServiceRemoved on_service_removed,
                                            OnServiceRenamed on_service_renamed)
    : drive_stage_(kUnInitialised),
      root_handler_(drive_root_id, on_service_added, on_service_removed, on_service_renamed),
      kMountDir_(mount_dir),
      unmount_mutex_(),
      api_mutex_(),
      unmount_condition_variable_(),
      mount_mutex_(),
      mount_condition_variable_() {
  static_assert(std::is_same<Storage, data_store::SureFileStore>::value,
                "Cannot use without Surefile");
}

template<typename Storage>
Identity DriveInUserSpace<Storage>::drive_root_id() const {
  std::lock_guard<std::mutex> guard(api_mutex_);
  return root_handler_.drive_root_id();
}

template<typename Storage>
void DriveInUserSpace<Storage>::SetMountState(bool mounted) {
  {
    std::lock_guard<std::mutex> lock(mount_mutex_);
    drive_stage_ = (mounted ? kMounted : kUnMounted);
  }
  mount_condition_variable_.notify_one();
}

template<typename Storage>
bool DriveInUserSpace<Storage>::WaitUntilMounted() {
  std::unique_lock<std::mutex> lock(mount_mutex_);
  bool result(mount_condition_variable_.wait_for(lock, std::chrono::seconds(10),
                                                 [this] { return drive_stage_ == kMounted; }));
// #ifdef MAIDSAFE_APPLE
//   Sleep(std::chrono::seconds(1));
// #endif
  return result;
}

template<typename Storage>
void DriveInUserSpace<Storage>::WaitUntilUnMounted() {
  std::unique_lock<std::mutex> lock(mount_mutex_);
  mount_condition_variable_.wait(lock, [this] { return drive_stage_ == kUnMounted; });
}

template<typename Storage>
void DriveInUserSpace<Storage>::AddService(const boost::filesystem::path& service_alias,
                                           const boost::filesystem::path& store_path,
                                           const Identity& service_root_id) {
  root_handler_.AddService(service_alias, store_path, service_root_id);
#ifdef MAIDSAFE_WIN32
  NotifyDirectoryChange(detail::kRoot / service_alias, detail::OpType::kAdded);
#endif
}

template<typename Storage>
void DriveInUserSpace<Storage>::RemoveService(const boost::filesystem::path& service_alias) {
  root_handler_.RemoveService(service_alias);
}

template<typename Storage>
void DriveInUserSpace<Storage>::GetMetaData(const boost::filesystem::path& relative_path,
                                            MetaData& meta_data,
                                            DirectoryId& grandparent_directory_id,
                                            DirectoryId& parent_directory_id) {
  auto file_context(root_handler_.GetFileContext(relative_path));
  meta_data = *file_context.meta_data;
  grandparent_directory_id = file_context.grandparent_directory_id;
  parent_directory_id = file_context.parent_directory_id;
}

template<typename Storage>
std::unique_ptr<detail::DirectoryHandler<Storage>> DriveInUserSpace<Storage>::GetHandler(
    const boost::filesystem::path& relative_path) {
  return std::move(root_handler_.GetHandler(relative_path));
}

template<typename Storage>
detail::Directory DriveInUserSpace<Storage>::GetDirectory(
    const boost::filesystem::path& relative_path) {
  return root_handler_.GetFromPath(relative_path);
}

template<typename Storage>
Storage* DriveInUserSpace<Storage>::GetStorage(const boost::filesystem::path& path) const {
  return root_handler_.GetStorage(path);
}

template<typename Storage>
detail::FileContext<Storage> DriveInUserSpace<Storage>::GetFileContext(
    const boost::filesystem::path& path) const {
  return root_handler_.GetFileContext(path);
}

template<typename Storage>
void DriveInUserSpace<Storage>::UpdateParent(detail::FileContext<Storage>* file_context,
                                             const boost::filesystem::path& parent_path) {
  root_handler_.UpdateParentDirectoryListing(parent_path, *file_context->meta_data);
}

template<typename Storage>
void DriveInUserSpace<Storage>::AddFile(const boost::filesystem::path& relative_path,
                                        const MetaData& meta_data,
                                        DirectoryId& grandparent_directory_id,
                                        DirectoryId& parent_directory_id) {
  root_handler_.AddElement(relative_path, meta_data, grandparent_directory_id, parent_directory_id);
}

template<typename Storage>
bool DriveInUserSpace<Storage>::CanRemove(const boost::filesystem::path& relative_path) {
  return root_handler_.CanDelete(relative_path);
}

template<typename Storage>
void DriveInUserSpace<Storage>::RemoveFile(const boost::filesystem::path& relative_path) {
  MetaData meta_data;
  root_handler_.DeleteElement(relative_path, meta_data);
}

template<typename Storage>
void DriveInUserSpace<Storage>::RenameFile(const boost::filesystem::path& old_relative_path,
                                           const boost::filesystem::path& new_relative_path,
                                           MetaData& meta_data) {
  root_handler_.RenameElement(old_relative_path, new_relative_path, meta_data);
}

template<typename Storage>
bool DriveInUserSpace<Storage>::TruncateFile(const boost::filesystem::path& relative_path,
                                             detail::FileContext<Storage>* file_context,
                                             const uint64_t& size) {
  if (!file_context->self_encryptor) {
    auto directory_handler(GetHandler(relative_path));
    if (!directory_handler)
      return false;

    file_context->self_encryptor.reset(
        new encrypt::SelfEncryptor<Storage>(file_context->meta_data->data_map,
                                            *directory_handler->storage()));
  }

  bool result = file_context->self_encryptor->Truncate(size);
  if (result)
    file_context->content_changed = true;
  return result;
}

template<typename Storage>
void DriveInUserSpace<Storage>::NotifyRename(
    const boost::filesystem::path& from_relative_path,
    const boost::filesystem::path& to_relative_path) const {
  NotifyDirectoryChange(from_relative_path, detail::OpType::kRemoved);
  NotifyDirectoryChange(to_relative_path, detail::OpType::kRemoved);
}


// ********************** File / Folder Transfers ******************************

template<typename Storage>
void DriveInUserSpace<Storage>::GetDataMap(const boost::filesystem::path& relative_path,
                                           std::string* serialised_data_map) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  ReadDataMap(relative_path, serialised_data_map);
}

template<typename Storage>
void DriveInUserSpace<Storage>::GetDataMapHidden(const boost::filesystem::path& relative_path,
                                                 std::string* serialised_data_map) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  ReadDataMap(relative_path, serialised_data_map);
}

template<typename Storage>
void DriveInUserSpace<Storage>::ReadDataMap(const boost::filesystem::path& relative_path,
                                            std::string* serialised_data_map) {
  if (relative_path.empty() || !serialised_data_map)
    ThrowError(CommonErrors::invalid_parameter);

  serialised_data_map->clear();
  auto file_context(GetFileContext(relative_path));
  if (!file_context.meta_data->data_map)
    ThrowError(CommonErrors::invalid_parameter);

  try {
    encrypt::SerialiseDataMap(*file_context.meta_data->data_map, *serialised_data_map);
  }
  catch(const std::exception& exception) {
    serialised_data_map->clear();
    boost::throw_exception(exception);
  }
}

template<typename Storage>
void DriveInUserSpace<Storage>::InsertDataMap(const boost::filesystem::path& relative_path,
                                              const std::string& serialised_data_map) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  LOG(kInfo) << "InsertDataMap - " << relative_path;

  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  detail::FileContext<Storage> file_context(relative_path.filename(), false);
  encrypt::ParseDataMap(serialised_data_map, *file_context.meta_data->data_map);

  SetNewAttributes(&file_context, false, false);

  AddFile(relative_path,
          *file_context.meta_data.get(),
          file_context.grandparent_directory_id,
          file_context.parent_directory_id);
}

// **************************** Hidden Files ***********************************

template<typename Storage>
void DriveInUserSpace<Storage>::ReadHiddenFile(const boost::filesystem::path& relative_path,
                                               std::string* content) {
  if (relative_path.empty() || (relative_path.extension() != detail::kMsHidden) || !content)
    ThrowError(CommonErrors::invalid_parameter);

  auto file_context(GetFileContext(relative_path));
  assert(!file_context.meta_data->directory_id);

  file_context.self_encryptor.reset(new encrypt::SelfEncryptor<Storage>(
      file_context.meta_data->data_map, *root_handler_.GetStorage(relative_path)));
  if (file_context.self_encryptor->size() > std::numeric_limits<uint32_t>::max())
    ThrowError(CommonErrors::invalid_parameter);

  uint32_t bytes_to_read(static_cast<uint32_t>(file_context.self_encryptor->size()));
  content->resize(bytes_to_read);
  if (!file_context.self_encryptor->Read(const_cast<char*>(content->data()), bytes_to_read, 0))
    ThrowError(CommonErrors::invalid_parameter);
}

template<typename Storage>
void DriveInUserSpace<Storage>::WriteHiddenFile(const boost::filesystem::path &relative_path,
                                                const std::string &content,
                                                bool overwrite_existing) {
  if (relative_path.empty() || (relative_path.extension() != detail::kMsHidden))
    ThrowError(CommonErrors::invalid_parameter);

  boost::filesystem::path hidden_file_path(relative_path);
  // Try getting FileContext to existing
  detail::FileContext<Storage> file_context;
  try {
    file_context = GetFileContext(relative_path);
    if (!overwrite_existing)
      ThrowError(CommonErrors::invalid_parameter);
  }
  catch(...) {
    // Try adding a new entry if the hidden file doesn't already exist
    *file_context.meta_data = MetaData(hidden_file_path.filename(), false);
    AddFile(hidden_file_path,
            *file_context.meta_data.get(),
            file_context.grandparent_directory_id,
            file_context.parent_directory_id);
  }

  if (content.size() > std::numeric_limits<uint32_t>::max())
    ThrowError(CommonErrors::invalid_parameter);

  // Write the data
  file_context.self_encryptor.reset(new encrypt::SelfEncryptor<Storage>(
      file_context.meta_data->data_map, *root_handler_.GetStorage(relative_path)));

  if (file_context.self_encryptor->size() > content.size())
    file_context.self_encryptor->Truncate(content.size());
  if (!file_context.self_encryptor->Write(content.c_str(),
                                          static_cast<uint32_t>(content.size()),
                                          0U))
    ThrowError(CommonErrors::invalid_parameter);

  file_context.self_encryptor.reset();
  SetNewAttributes(&file_context, false, false);
}

template<typename Storage>
void DriveInUserSpace<Storage>::DeleteHiddenFile(const boost::filesystem::path &relative_path) {
  if (relative_path.empty() || (relative_path.extension() != detail::kMsHidden))
    ThrowError(CommonErrors::invalid_parameter);
  RemoveFile(relative_path);
}

template<typename Storage>
std::vector<std::string> DriveInUserSpace<Storage>::GetHiddenFiles(
    const boost::filesystem::path &relative_path) {
  auto directory_handler(GetHandler(relative_path));
  if (!directory_handler)
    return std::vector<std::string>();
  auto directory(directory_handler->GetFromPath(relative_path));
  return directory.first.listing.GetHiddenChildNames();
}

// ***************************** File Notes ************************************

template<typename Storage>
void DriveInUserSpace<Storage>::GetNotes(const boost::filesystem::path& relative_path,
                                         std::vector<std::string>* notes) {
  LOG(kInfo) << "GetNotes - " << relative_path;
  std::lock_guard<std::mutex> guard(api_mutex_);
  if (relative_path.empty() || !notes)
    ThrowError(CommonErrors::invalid_parameter);

  notes->clear();
  auto file_context(GetFileContext(relative_path));
  *notes = file_context.meta_data->notes;
}

template<typename Storage>
void DriveInUserSpace<Storage>::AddNote(const boost::filesystem::path& relative_path,
                                        const std::string& note) {
  LOG(kInfo) << "AddNote - " << relative_path;
  std::lock_guard<std::mutex> guard(api_mutex_);
  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  auto file_context(GetFileContext(relative_path));
  file_context.meta_data->notes.push_back(note);
  UpdateParent(&file_context, relative_path.parent_path());
}

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DRIVE_H_
