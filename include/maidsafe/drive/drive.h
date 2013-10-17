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

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/utils.h"

namespace maidsafe {
namespace drive {

boost::filesystem::path RelativePath(const boost::filesystem::path& mount_dir,
                                     const boost::filesystem::path& absolute_path);

template <typename Storage>
class Drive {
 public:
  typedef std::shared_ptr<Storage> StoragePtr;
  typedef encrypt::SelfEncryptor<Storage> SelfEncryptor;
  typedef detail::FileContext<Storage> FileContext;
  typedef detail::DirectoryHandler<Storage> DirectoryHandler;
  typedef detail::Directory Directory;
  typedef detail::OpType OpType;

  Drive(StoragePtr storage, const Identity& unique_user_id, const Identity& root_parent_id,
        const boost::filesystem::path& mount_dir);

  virtual ~Drive() {}

  virtual bool Unmount() = 0;

#ifdef MAIDSAFE_APPLE
  boost::filesystem::path GetMountDir() const { return kMountDir_; }
#endif

  Identity root_parent_id() const;

  void SetMountState(bool mounted);

  // Times out after 10 seconds.
  bool WaitUntilMounted();
  // Doesn't time out.
  void WaitUntilUnMounted();

  // ********************* File / Folder Transfers ************************************************

  // Retrieve the serialised DataMap of the file at 'relative_path' (e.g. to send to another client)
  void GetDataMap(const boost::filesystem::path& relative_path, std::string* serialised_data_map);
  void GetDataMapHidden(const boost::filesystem::path& relative_path,
                        std::string* serialised_data_map);
  // Insert a file at 'relative_path' derived from the serialised DataMap (e.g. if
  // receiving from another client).
  void InsertDataMap(const boost::filesystem::path& relative_path,
                     const std::string& serialised_data_map);
  void GetMetaData(const boost::filesystem::path& relative_path, MetaData& meta_data,
                   DirectoryId& grandparent_directory_id, DirectoryId& parent_directory_id);
  // Adds a directory or file represented by 'meta_data' and 'relative_path' to the appropriate
  // parent directory listing. If the element is a directory, a new directory listing is created
  // and stored. The parent directory's ID is returned in 'parent_id' and its parent directory's ID
  // is returned in 'grandparent_id'.
  void Add(const boost::filesystem::path& relative_path, const MetaData& meta_data,
           DirectoryId& grandparent_directory_id, DirectoryId& parent_directory_id);
  // Deletes the file at 'relative_path' from the appropriate parent directory listing as well as
  // the listing associated with that path if it represents a directory.
  void Delete(const boost::filesystem::path& relative_path);
  bool CanDelete(const boost::filesystem::path& relative_path);
  // Rename/move the file associated with 'meta_data' located at 'old_relative_path' to that at
  // 'new_relative_path'.
  void Rename(const boost::filesystem::path& old_relative_path,
              const boost::filesystem::path& new_relative_path, MetaData& meta_data);

  // *************************** Hidden Files *****************************************************

  // All hidden files in this sense have extension ".ms_hidden" and are not accessible through the
  // normal filesystem methods.
  void ReadHiddenFile(const boost::filesystem::path& relative_path, std::string* content);
  void WriteHiddenFile(const boost::filesystem::path& relative_path, const std::string& content,
                       bool overwrite_existing);
  void DeleteHiddenFile(const boost::filesystem::path& relative_path);
  // Returns all hidden files at 'relative_path'.
  std::vector<std::string> GetHiddenFiles(const boost::filesystem::path& relative_path);

  // **************************** File Notes ******************************************************

  // Retrieve the collection of notes (serialised to strings) associated with given file/directory.
  void GetNotes(const boost::filesystem::path& relative_path, std::vector<std::string>* notes);
  // Append a single serialised note to the collection of notes associated with given
  // .file/directory
  void AddNote(const boost::filesystem::path& relative_path, const std::string& note);

 protected:
  Directory GetDirectory(const boost::filesystem::path& relative_path);
  StoragePtr GetStorage() const;
  FileContext GetFileContext(const boost::filesystem::path& relative_path) const;
  // Updates parent directory at 'parent_path' with the values contained in the 'file_context'.
  void UpdateParent(FileContext* file_context, const boost::filesystem::path& parent_path);
  // Resizes the file.
  bool TruncateFile(FileContext* file_context, uint64_t size);
  virtual void NotifyDirectoryChange(const boost::filesystem::path& relative_path,
                                     OpType op) const = 0;
  void NotifyRename(const boost::filesystem::path& from_relative_path,
                    const boost::filesystem::path& to_relative_path) const;

  DirectoryHandler directory_handler_;
  const boost::filesystem::path kMountDir_;
  std::mutex unmount_mutex_;
  mutable std::mutex api_mutex_;
  enum DriveStage {
    kUnInitialised,
    kInitialised,
    kMounted,
    kUnMounted,
    kCleaned
  } drive_stage_;

 private:
  Drive(const Drive&);
  Drive(Drive&&);
  Drive& operator=(const Drive&);

  virtual void SetNewAttributes(FileContext* file_context, bool is_directory, bool read_only) = 0;
  void ReadDataMap(const boost::filesystem::path& relative_path, std::string* serialised_data_map);

  std::condition_variable unmount_condition_variable_;
  std::mutex mount_mutex_;
  std::condition_variable mount_condition_variable_;
};

#ifdef MAIDSAFE_WIN32
boost::filesystem::path GetNextAvailableDrivePath();
#endif

// ==================== Implementation ============================================================

template <typename Storage>
Drive<Storage>::Drive(StoragePtr storage, const Identity& unique_user_id,
                      const Identity& root_parent_id, const boost::filesystem::path& mount_dir)
    : directory_handler_(storage, unique_user_id, root_parent_id),
      kMountDir_(mount_dir),
      unmount_mutex_(),
      api_mutex_(),
      unmount_condition_variable_(),
      mount_mutex_(),
      mount_condition_variable_(),
      drive_stage_(kUnInitialised) {}

template <typename Storage>
Identity Drive<Storage>::root_parent_id() const {
  std::lock_guard<std::mutex> guard(api_mutex_);
  return directory_handler_.root_parent_id();
}

template <typename Storage>
void Drive<Storage>::SetMountState(bool mounted) {
  {
    std::lock_guard<std::mutex> lock(mount_mutex_);
    drive_stage_ = (mounted ? kMounted : kUnMounted);
  }
  mount_condition_variable_.notify_one();
}

template <typename Storage>
bool Drive<Storage>::WaitUntilMounted() {
  std::unique_lock<std::mutex> lock(mount_mutex_);
  bool result(mount_condition_variable_.wait_for(lock, std::chrono::seconds(10),
                                                 [this] { return drive_stage_ == kMounted; }));
  return result;
}

template <typename Storage>
void Drive<Storage>::WaitUntilUnMounted() {
  std::unique_lock<std::mutex> lock(mount_mutex_);
  mount_condition_variable_.wait(lock, [this] { return drive_stage_ == kUnMounted; });
}

template <typename Storage>
void Drive<Storage>::GetMetaData(const boost::filesystem::path& relative_path, MetaData& meta_data,
                                 DirectoryId& grandparent_directory_id,
                                 DirectoryId& parent_directory_id) {
  auto parent(directory_listing_handler_->Get(relative_path.parent_path()));
  parent.listing->GetChild(relative_path.filename(), meta_data);
  grandparent_directory_id = parent.parent_id;
  parent_directory_id = parent.listing->directory_id();
}

template <typename Storage>
typename Drive<Storage>::Directory Drive<Storage>::GetDirectory(
    const boost::filesystem::path& relative_path) {
  return directory_handler_.Get(relative_path);
}

template <typename Storage>
typename Drive<Storage>::StoragePtr Drive<Storage>::GetStorage() const {
  return directory_handler_.storage();
}

template <typename Storage>
typename Drive<Storage>::FileContext Drive<Storage>::GetFileContext(
    const boost::filesystem::path& relative_path) const {
  FileContext file_context;
  Directory parent(directory_handler_.Get(relative_path.parent_path()));
  parent.listing->GetChild(relative_path.filename(), *file_context.meta_data);
  file_context.grandparent_directory_id = parent.parent_id;
  file_context.parent_directory_id = parent.listing->directory_id();
  return file_context;
}

template <typename Storage>
void Drive<Storage>::UpdateParent(detail::FileContext<Storage>* file_context,
                                  const boost::filesystem::path& parent_path) {
  directory_handler_.UpdateParent(parent_path, *file_context->meta_data);
}

template <typename Storage>
void Drive<Storage>::Add(const boost::filesystem::path& relative_path, const MetaData& meta_data,
                         DirectoryId& grandparent_directory_id, DirectoryId& parent_directory_id) {
  directory_handler_.Add(relative_path, meta_data, grandparent_directory_id, parent_directory_id);
}

template <typename Storage>
void Drive<Storage>::Delete(const boost::filesystem::path& relative_path) {
  directory_handler_.Delete(relative_path);
}

template <typename Storage>
bool Drive<Storage>::CanDelete(const boost::filesystem::path& /*relative_path*/) {
  // TODO() Determine criteria that decides this.
  return true;
  // return directory_handler_.CanDelete(relative_path);
}

template <typename Storage>
void Drive<Storage>::Rename(const boost::filesystem::path& old_relative_path,
                            const boost::filesystem::path& new_relative_path,
                            MetaData& meta_data) {
  directory_handler_.Rename(old_relative_path, new_relative_path, meta_data);
}

template <typename Storage>
bool Drive<Storage>::TruncateFile(FileContext* file_context, uint64_t size) {
  if (!file_context->self_encryptor)
    file_context->self_encryptor.reset(new SelfEncryptor(
        file_context->meta_data->data_map, *directory_handler_.storage()));

  bool result = file_context->self_encryptor->Truncate(size);
  if (result)
    file_context->content_changed = true;
  return result;
}

template <typename Storage>
void Drive<Storage>::NotifyRename(
    const boost::filesystem::path& from_relative_path,
    const boost::filesystem::path& to_relative_path) const {
  NotifyDirectoryChange(from_relative_path, OpType::kRemoved);
  NotifyDirectoryChange(to_relative_path, OpType::kAdded);
}

// ********************** File / Folder Transfers ******************************

template <typename Storage>
void Drive<Storage>::GetDataMap(const boost::filesystem::path& relative_path,
                                std::string* serialised_data_map) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  ReadDataMap(relative_path, serialised_data_map);
}

template <typename Storage>
void Drive<Storage>::GetDataMapHidden(const boost::filesystem::path& relative_path,
                                                 std::string* serialised_data_map) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  ReadDataMap(relative_path, serialised_data_map);
}

template <typename Storage>
void Drive<Storage>::ReadDataMap(const boost::filesystem::path& relative_path,
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
  catch (const std::exception& exception) {
    serialised_data_map->clear();
    boost::throw_exception(exception);
  }
}

template <typename Storage>
void Drive<Storage>::InsertDataMap(const boost::filesystem::path& relative_path,
                                   const std::string& serialised_data_map) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  LOG(kInfo) << "InsertDataMap - " << relative_path;

  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  FileContext file_context(relative_path.filename(), false);
  encrypt::ParseDataMap(serialised_data_map, *file_context.meta_data->data_map);

  SetNewAttributes(&file_context, false, false);

  Add(relative_path, *file_context.meta_data.get(), file_context.grandparent_directory_id,
      file_context.parent_directory_id);
}

// **************************** Hidden Files ***********************************

template <typename Storage>
void Drive<Storage>::ReadHiddenFile(const boost::filesystem::path& relative_path,
                                    std::string* content) {
  if (relative_path.empty() || (relative_path.extension() != detail::kMsHidden) || !content)
    ThrowError(CommonErrors::invalid_parameter);

  auto file_context(GetFileContext(relative_path));
  assert(!file_context.meta_data->directory_id);

  file_context.self_encryptor.reset(new SelfEncryptor(
      file_context.meta_data->data_map, *directory_handler_.GetStorage(relative_path)));
  if (file_context.self_encryptor->size() > std::numeric_limits<uint32_t>::max())
    ThrowError(CommonErrors::invalid_parameter);

  uint32_t bytes_to_read(static_cast<uint32_t>(file_context.self_encryptor->size()));
  content->resize(bytes_to_read);
  if (!file_context.self_encryptor->Read(const_cast<char*>(content->data()), bytes_to_read, 0))
    ThrowError(CommonErrors::invalid_parameter);
}

template <typename Storage>
void Drive<Storage>::WriteHiddenFile(const boost::filesystem::path& relative_path,
                                     const std::string& content,
                                     bool overwrite_existing) {
  if (relative_path.empty() || (relative_path.extension() != detail::kMsHidden))
    ThrowError(CommonErrors::invalid_parameter);

  boost::filesystem::path hidden_file_path(relative_path);
  // Try getting FileContext to existing
  FileContext file_context;
  try {
    file_context = GetFileContext(relative_path);
    if (!overwrite_existing)
      ThrowError(CommonErrors::invalid_parameter);
  }
  catch (...) {
    // Try adding a new entry if the hidden file doesn't already exist
    *file_context.meta_data = MetaData(hidden_file_path.filename(), false);
    Add(hidden_file_path, *file_context.meta_data.get(), file_context.grandparent_directory_id,
        file_context.parent_directory_id);
  }

  if (content.size() > std::numeric_limits<uint32_t>::max())
    ThrowError(CommonErrors::invalid_parameter);

  // Write the data
  file_context.self_encryptor.reset(new SelfEncryptor(
      file_context.meta_data->data_map, *directory_handler_.GetStorage(relative_path)));

  if (file_context.self_encryptor->size() > content.size())
    file_context.self_encryptor->Truncate(content.size());
  if (!file_context.self_encryptor->Write(content.c_str(), static_cast<uint32_t>(content.size()),
                                          0U))
    ThrowError(CommonErrors::invalid_parameter);

  file_context.self_encryptor.reset();
  SetNewAttributes(&file_context, false, false);
}

template <typename Storage>
void Drive<Storage>::DeleteHiddenFile(const boost::filesystem::path& relative_path) {
  if (relative_path.empty() || (relative_path.extension() != detail::kMsHidden))
    ThrowError(CommonErrors::invalid_parameter);
  Delete(relative_path);
}

template <typename Storage>
std::vector<std::string> Drive<Storage>::GetHiddenFiles(
    const boost::filesystem::path& relative_path) {
  auto directory(directory_handler_->GetFromPath(relative_path));
  return directory.first.listing.GetHiddenChildNames();
}

// ***************************** File Notes ************************************

template <typename Storage>
void Drive<Storage>::GetNotes(const boost::filesystem::path& relative_path,
                              std::vector<std::string>* notes) {
  LOG(kInfo) << "GetNotes - " << relative_path;
  std::lock_guard<std::mutex> guard(api_mutex_);
  if (relative_path.empty() || !notes)
    ThrowError(CommonErrors::invalid_parameter);

  notes->clear();
  auto file_context(GetFileContext(relative_path));
  *notes = file_context.meta_data->notes;
}

template <typename Storage>
void Drive<Storage>::AddNote(const boost::filesystem::path& relative_path,
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
