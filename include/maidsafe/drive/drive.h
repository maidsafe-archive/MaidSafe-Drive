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
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"

#include "maidsafe/common/application_support_directories.h"
#include "maidsafe/common/rsa.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/utils.h"

namespace maidsafe {

namespace drive {

template <typename Storage>
class Drive {
 public:
  //typedef detail::FileContext<Storage> FileContext;
  //typedef FileContext* FileContextPtr;
  //typedef detail::DirectoryHandler<Storage> DirectoryHandler;
  //typedef detail::Directory Directory;

  Drive(std::shared_ptr<Storage> storage, const Identity& unique_user_id, const Identity& root_parent_id,
        const boost::filesystem::path& mount_dir, bool create = false);

  virtual ~Drive() {}
  Identity root_parent_id() const;

  // ********************* File / Folder Transfers ************************************************
/*
  // Retrieve the serialised DataMap of the file at 'relative_path' (e.g. to send to another client)
  std::string GetDataMap(const boost::filesystem::path& relative_path);
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
              const boost::filesystem::path& new_relative_path, MetaData& meta_data);  */

 protected:
  void Create(const boost::filesystem::path& relative_path, detail::FileContext&& file_context);
  //detail::Directory& GetDirectory(const boost::filesystem::path& relative_path);
  detail::FileContext& GetFileContext(const boost::filesystem::path& relative_path);
  void UpdateParent(FileContextPtr file_context, const boost::filesystem::path& parent_path);
  bool TruncateFile(FileContextPtr file_context, const uint64_t& size);

  DirectoryHandler directory_handler_;
  std::shared_ptr<Storage> storage_;
  const boost::filesystem::path kMountDir_;

 private:
  typedef detail::FileContext::Buffer Buffer;
  virtual void SetNewAttributes(FileContextPtr file_context, bool is_directory,
                                bool read_only) = 0;
  std::string ReadDataMap(const boost::filesystem::path& relative_path);

  std::function<NonEmptyString(const std::string&)> get_chunk_from_store_;
  MemoryUsage default_max_buffer_memory_;
  DiskUsage default_max_buffer_disk_;
};

#ifdef MAIDSAFE_WIN32
inline boost::filesystem::path GetNextAvailableDrivePath() {
  uint32_t drive_letters(GetLogicalDrives()), mask(0x4);
  std::string path("C:");
  while (drive_letters & mask) {
    mask <<= 1;
    ++path[0];
  }
  if (path[0] > 'Z')
    ThrowError(DriveErrors::no_drive_letter_available);
  return boost::filesystem::path(path);
}
#endif

// ==================== Implementation =============================================================

template <typename Storage>
Drive<Storage>::Drive(std::shared_ptr<Storage> storage,
                      const Identity& unique_user_id,
                      const Identity& root_parent_id,
                      const boost::filesystem::path& mount_dir,
                      bool create)
    : directory_handler_(storage, unique_user_id, root_parent_id, create),
      storage_(storage),
      kMountDir_(mount_dir),
      get_chunk_from_store_(),
      // TODO(Fraser#5#): 2013-11-27 - BEFORE_RELEASE - confirm following 2 variables.
      default_max_buffer_memory_(Concurrency() * 1024 * 1024),  // cores * default chunk size
      default_max_buffer_disk_(static_cast<uint64_t>(
          boost::filesystem::space(GetUserAppDir()).available / 10)) {
  // TODO(Fraser#5#): 2013-11-27 - BEFORE_RELEASE If default_max_buffer_disk_ < some limit, throw?
  get_chunk_from_store_ = [this](const std::string& name)->NonEmptyString {
    auto chunk(storage_->Get(ImmutableData::Name(Identity(name))).get());
    return chunk.data();
  };
}

template <typename Storage>
Identity Drive<Storage>::root_parent_id() const {
  return directory_handler_.root_parent_id();
}

template <typename Storage>
void Drive<Storage>::Create(const boost::filesystem::path& relative_path,
                            detail::FileContext&& file_context) {
  // If this is a file and not a directory, initialise the self-encryptor.
  if (!file_context.meta_data.directory_id) {
    auto buffer_pop_functor([this, relative_path](const std::string& name,
                                                  const NonEmptyString& content) {
      directory_handler_.HandleDataPoppedFromBuffer(relative_path, name, content);
    });
    auto disk_buffer_path(boost::filesystem::unique_path(
        GetUserAppDir() / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%");
    file_context->data_buffer.reset(new detail::FileContext::Buffer(default_max_buffer_memory_,
        default_max_buffer_disk_, buffer_pop_functor, disk_buffer_path));
    file_context->self_encryptor.reset(new encrypt::SelfEncryptor(file_context->meta_data.data_map,
        *file_context->data_buffer, get_chunk_from_store_));
  }
  directory_handler_.Add(relative_path, std::move(file_context));
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
  return ReadDataMap(relative_path);
}

template <typename Storage>
void Drive<Storage>::InsertDataMap(const boost::filesystem::path& relative_path,
                                   const std::string& serialised_data_map) {
  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  FileContext file_context(relative_path.filename(), false);
  encrypt::ParseDataMap(serialised_data_map, *file_context.meta_data->data_map);
  SetNewAttributes(&file_context, false, false);
  Add(relative_path, file_context);
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
