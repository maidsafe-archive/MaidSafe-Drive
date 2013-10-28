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
  typedef FileContext* FileContextPtr;
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
  std::string GetDataMap(const boost::filesystem::path& relative_path);
  std::string GetHiddenDataMap(const boost::filesystem::path& relative_path);
  // Insert a file at 'relative_path' derived from the serialised DataMap (e.g. if
  // receiving from another client).
  void InsertDataMap(const boost::filesystem::path& relative_path,
                     const std::string& serialised_data_map);

  // Adds a directory or file represented by 'meta_data' and 'relative_path' to the appropriate
  // parent directory listing. If the element is a directory, a new directory listing is created
  // and stored. The parent directory's ID is returned in 'parent_id' and its parent directory's ID
  // is returned in 'grandparent_id'.
  void Add(const boost::filesystem::path& relative_path, FileContext& file_context);
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
  std::string ReadHiddenFile(const boost::filesystem::path& relative_path);
  void WriteHiddenFile(const boost::filesystem::path& relative_path, const std::string& content,
                       bool overwrite);
  void DeleteHiddenFile(const boost::filesystem::path& relative_path);
  // Returns all hidden files at 'relative_path'.
  std::vector<std::string> GetHiddenFiles(const boost::filesystem::path& relative_path);

  // **************************** File Notes ******************************************************

  // Retrieve the collection of notes (serialised to strings) associated with given file/directory.
  std::vector<std::string> GetNotes(const boost::filesystem::path& relative_path);
  // Append a single serialised note to the collection of notes associated with given
  // file/directory.
  void AddNote(const boost::filesystem::path& relative_path, const std::string& note);

 protected:
  // Recovers Directory for 'relative_path'.
  Directory GetDirectory(const boost::filesystem::path& relative_path);
  // Returns FileContext associated with 'relative_path'.
  FileContext GetFileContext(const boost::filesystem::path& relative_path);
  // Updates parent directory at 'parent_path' with the values contained in the 'file_context'.
  void UpdateParent(FileContextPtr file_context, const boost::filesystem::path& parent_path);
  // Resizes the file.
  bool TruncateFile(FileContextPtr file_context, const uint64_t& size);
  // virtual void NotifyRename(const boost::filesystem::path& from_relative_path,
  //                           const boost::filesystem::path& to_relative_path) const = 0;

  DirectoryHandler directory_handler_;
  StoragePtr storage_;
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
  virtual void SetNewAttributes(FileContextPtr file_context, bool is_directory,
                                bool read_only) = 0;
  std::string ReadDataMap(const boost::filesystem::path& relative_path);

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
      storage_(storage),
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
#ifdef MAIDSAFE_APPLE
//  Sleep(boost::posix_time::seconds(1));
#endif
  return result;
}

template <typename Storage>
void Drive<Storage>::WaitUntilUnMounted() {
  std::unique_lock<std::mutex> lock(mount_mutex_);
  mount_condition_variable_.wait(lock, [this] { return drive_stage_ == kUnMounted; });
}

template <typename Storage>
void Drive<Storage>::Add(const boost::filesystem::path& relative_path,
                         FileContext& file_context) {
  directory_handler_.Add(relative_path, *file_context.meta_data,
                         file_context.grandparent_directory_id, file_context.parent_directory_id);
}

template <typename Storage>
bool Drive<Storage>::CanDelete(const boost::filesystem::path& /*relative_path*/) {
  return true;
}

template <typename Storage>
void Drive<Storage>::Delete(const boost::filesystem::path& relative_path) {
  directory_handler_.Delete(relative_path);
}

template <typename Storage>
void Drive<Storage>::Rename(const boost::filesystem::path& old_relative_path,
                            const boost::filesystem::path& new_relative_path,
                            MetaData& meta_data) {
  directory_handler_.Rename(old_relative_path, new_relative_path, meta_data);
}

// ********************** File / Folder Transfers *************************************************

template <typename Storage>
std::string Drive<Storage>::GetDataMap(const boost::filesystem::path& relative_path) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  return ReadDataMap(relative_path);
}

template <typename Storage>
std::string Drive<Storage>::GetHiddenDataMap(const boost::filesystem::path& relative_path) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  return ReadDataMap(relative_path);
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
  Add(relative_path, file_context);
}

// **************************** Hidden Files ******************************************************

// template <typename Storage>
// std::string Drive<Storage>::ReadHiddenFile(const boost::filesystem::path& relative_path) {
//   if (relative_path.empty() || (relative_path.extension() != detail::kMsHidden))
//     ThrowError(CommonErrors::invalid_parameter);
// 
//   FileContext file_context(GetFileContext(relative_path));
//   BOOST_ASSERT(!file_context.meta_data->directory_id);
// 
//   file_context.self_encryptor.reset(new SelfEncryptor(
//       file_context.meta_data->data_map, *storage_));
//   if (file_context.self_encryptor->size() > std::numeric_limits<uint32_t>::max())
//     ThrowError(CommonErrors::invalid_parameter);
// 
//   std::string content;
//   uint32_t bytes_to_read(static_cast<uint32_t>(file_context.self_encryptor->size()));
//   content->resize(bytes_to_read);
//   if (!file_context.self_encryptor->Read(const_cast<char*>(content.data()), bytes_to_read, 0))
//     ThrowError(CommonErrors::invalid_parameter);
//   return content;
// }
// 
// template <typename Storage>
// void Drive<Storage>::WriteHiddenFile(const boost::filesystem::path& relative_path,
//                                      const std::string& content,
//                                      bool overwrite) {
//   if (relative_path.empty() || (relative_path.extension() != detail::kMsHidden))
//     ThrowError(CommonErrors::invalid_parameter);
// 
//   boost::filesystem::path hidden_file_path(relative_path);
//   // Try getting FileContext to existing
//   FileContext file_context;
//   try {
//     file_context = GetFileContext(relative_path);
//     if (!overwrite)
//       ThrowError(CommonErrors::invalid_parameter);
//   }
//   catch(...) {
//     // Try adding a new entry if the hidden file doesn't already exist
//     file_context = FileContext(hidden_file_path.filename(), false);
//     Add(hidden_file_path, file_context);
//   }
// 
//   if (content.size() > std::numeric_limits<uint32_t>::max())
//     ThrowError(CommonErrors::invalid_parameter);
// 
//   // Write the data
//   file_context.self_encryptor.reset(new SelfEncryptor(
//       file_context.meta_data->data_map, *storage_));
// 
//   if (file_context.self_encryptor->size() > content.size())
//     file_context.self_encryptor->Truncate(content.size());
//   if (!file_context.self_encryptor->Write(static_cast<char*>(content.data()),
//                                           static_cast<uint32_t>(content.size()),
//                                           0))
//     ThrowError(CommonErrors::invalid_parameter);
// 
//   file_context.self_encryptor.reset();
//   SetNewAttributes(&file_context, false, false);
// }
// 
// template <typename Storage>
// void Drive<Storage>::DeleteHiddenFile(const boost::filesystem::path &relative_path) {
//   if (relative_path.empty() || (relative_path.extension() != detail::kMsHidden))
//     ThrowError(CommonErrors::invalid_parameter);
//   RemoveFile(relative_path);
// }
// 
// template <typename Storage>
// std::vector<std::string> Drive<Storage>::GetHiddenFiles(
//     const boost::filesystem::path &relative_path) {
//   auto directory(directory_handler_.Get(relative_path));
//   return directory.listing.GetHiddenChildNames();
// }
// 
// ***************************** File Notes *******************************************************

template <typename Storage>
std::vector<std::string> Drive<Storage>::GetNotes(const boost::filesystem::path& relative_path) {
  LOG(kInfo) << "GetNotes - " << relative_path;
  std::lock_guard<std::mutex> guard(api_mutex_);
  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  std::vector<std::string> notes;
  FileContext file_context(GetFileContext(relative_path));
  notes = file_context.meta_data->notes;
  return notes;
}

template <typename Storage>
void Drive<Storage>::AddNote(const boost::filesystem::path& relative_path,
                             const std::string& note) {
  LOG(kInfo) << "AddNote - " << relative_path;
  std::lock_guard<std::mutex> guard(api_mutex_);
  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  FileContext file_context(GetFileContext(relative_path));
  file_context.meta_data->notes.push_back(note);
  UpdateParent(&file_context, relative_path.parent_path());
}

// **************************** Miscellaneous *****************************************************

template <typename Storage>
typename Drive<Storage>::Directory Drive<Storage>::GetDirectory(
    const boost::filesystem::path& relative_path) {
  return directory_handler_.Get(relative_path);
}

template <typename Storage>
typename Drive<Storage>::FileContext Drive<Storage>::GetFileContext(
    const boost::filesystem::path& relative_path) {
  FileContext file_context;
  Directory parent(directory_handler_.Get(relative_path.parent_path()));
  parent.listing->GetChild(relative_path.filename(), *file_context.meta_data);
  file_context.grandparent_directory_id = parent.parent_id;
  file_context.parent_directory_id = parent.listing->directory_id();
  return file_context;
}

template <typename Storage>
void Drive<Storage>::UpdateParent(FileContextPtr file_context,
                                  const boost::filesystem::path& parent_path) {
  directory_handler_.UpdateParent(parent_path, *file_context->meta_data);
}

template <typename Storage>
bool Drive<Storage>::TruncateFile(FileContextPtr file_context, const uint64_t& size) {
  if (!file_context->self_encryptor) {
    file_context->self_encryptor.reset(
        new SelfEncryptor(file_context->meta_data->data_map, *storage_));
  }
  bool result = file_context->self_encryptor->Truncate(size);
  if (result) {
    file_context->content_changed = true;
  }
  return result;
}

template <typename Storage>
std::string Drive<Storage>::ReadDataMap(const boost::filesystem::path& relative_path) {
  std::string serialised_data_map;
  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  FileContext file_context(GetFileContext(relative_path));

  if (!file_context.meta_data->data_map)
    ThrowError(CommonErrors::invalid_parameter);

  try {
    encrypt::SerialiseDataMap(*file_context.meta_data->data_map, serialised_data_map);
  }
  catch(const std::exception& exception) {
    boost::throw_exception(exception);
  }
  return serialised_data_map;
}

}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DRIVE_H_
