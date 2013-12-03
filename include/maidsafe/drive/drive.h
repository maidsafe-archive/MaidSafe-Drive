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

//#include "maidsafe/common/on_scope_exit.h"
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
  Drive(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
        const Identity& root_parent_id, const boost::filesystem::path& mount_dir,
        const boost::filesystem::path& user_app_dir, bool create);

  virtual ~Drive() {}
  Identity root_parent_id() const;

 protected:
  const detail::FileContext* GetContext(const boost::filesystem::path& relative_path);
  detail::FileContext* GetMutableContext(const boost::filesystem::path& relative_path);
  void Create(const boost::filesystem::path& relative_path, detail::FileContext&& file_context);
  void Open(const boost::filesystem::path& relative_path);
  void Flush(const boost::filesystem::path& relative_path);
  void Release(const boost::filesystem::path& relative_path);
  void Delete(const boost::filesystem::path& relative_path);
  void Rename(const boost::filesystem::path& old_relative_path,
              const boost::filesystem::path& new_relative_path);
  uint32_t Read(const boost::filesystem::path& relative_path, char* data, uint32_t size,
                uint64_t offset);
  uint32_t Write(const boost::filesystem::path& relative_path, const char* data, uint32_t size,
                 uint64_t offset);

  detail::DirectoryHandler<Storage> directory_handler_;
  std::shared_ptr<Storage> storage_;
  const boost::filesystem::path kMountDir_;
  const boost::filesystem::path kUserAppDir_;

 private:
  typedef detail::FileContext::Buffer Buffer;
  void InitialiseEncryptor(const boost::filesystem::path& relative_path,
                           detail::FileContext& file_context) const;
  bool FlushEncryptor(detail::FileContext* file_context);

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
Drive<Storage>::Drive(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
                      const Identity& root_parent_id, const boost::filesystem::path& mount_dir,
                      const boost::filesystem::path& user_app_dir, bool create)
    : directory_handler_(storage, unique_user_id, root_parent_id,
                         boost::filesystem::unique_path(user_app_dir / "Buffers" /
                                                        "%%%%%-%%%%%-%%%%%-%%%%%"), create),
      storage_(storage),
      kMountDir_(mount_dir),
      kUserAppDir_(user_app_dir),
      get_chunk_from_store_(),
      // TODO(Fraser#5#): 2013-11-27 - BEFORE_RELEASE - confirm the following 2 variables.
      default_max_buffer_memory_(Concurrency() * 1024 * 1024),  // cores * default chunk size
      default_max_buffer_disk_(static_cast<uint64_t>(
          boost::filesystem::space(kUserAppDir_).available / 10)) {
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
void Drive<Storage>::InitialiseEncryptor(const boost::filesystem::path& relative_path,
                                         detail::FileContext& file_context) const {
  auto buffer_pop_functor([this, relative_path](const std::string& name,
                                                const NonEmptyString& content) {
    directory_handler_.HandleDataPoppedFromBuffer(relative_path, name, content);
  });
  auto disk_buffer_path(
      boost::filesystem::unique_path(kUserAppDir_ / "Buffers" / "%%%%%-%%%%%-%%%%%-%%%%%"));
  file_context.buffer.reset(new detail::FileContext::Buffer(default_max_buffer_memory_,
      default_max_buffer_disk_, buffer_pop_functor, disk_buffer_path));
  file_context.self_encryptor.reset(new encrypt::SelfEncryptor(*file_context.meta_data.data_map,
      *file_context.buffer, get_chunk_from_store_));
}

template <typename Storage>
bool Drive<Storage>::FlushEncryptor(detail::FileContext* file_context) {
  assert(file_context->self_encryptor);
  assert(file_context->meta_data.data_map);
  //on_scope_exit cleanup([file_context] {
  //  if (file_context->open_count == 0) {
  //    file_context->self_encryptor.reset();
  //    file_context->buffer.reset();
  //  }
  //});
  if (!file_context->self_encryptor->Flush())
    return false;
  for (const auto& chunk : file_context->meta_data.data_map->chunks) {
    auto content(file_context->buffer->Get(chunk.hash));
    storage_->Put(ImmutableData(content));
  }
  return true;
}

template <typename Storage>
const detail::FileContext* Drive<Storage>::GetContext(
    const boost::filesystem::path& relative_path) {
  detail::Directory* parent(directory_handler_.Get(relative_path.parent_path()));
  auto file_context(parent->GetChild(relative_path.filename()));
  // The open_count must be >=0.  If > 0 and the context doesn't represent a directory, the buffer
  // and encryptor should be non-null.
  assert(file_context->open_count == 0 || (file_context->open_count > 0 &&
            (file_context->meta_data.directory_id ||
                (file_context->buffer && file_context->self_encryptor))));
  return file_context;
}

template <typename Storage>
detail::FileContext* Drive<Storage>::GetMutableContext(
    const boost::filesystem::path& relative_path) {
  detail::Directory* parent(directory_handler_.Get(relative_path.parent_path()));
  auto file_context(parent->GetMutableChild(relative_path.filename()));
  // The open_count must be >=0.  If > 0 and the context doesn't represent a directory, the buffer
  // and encryptor should be non-null.
  assert(file_context->open_count == 0 || (file_context->open_count > 0 &&
              (file_context->meta_data.directory_id ||
                   (file_context->buffer && file_context->self_encryptor))));
  return file_context;
}

template <typename Storage>
void Drive<Storage>::Create(const boost::filesystem::path& relative_path,
                            detail::FileContext&& file_context) {
  if (!file_context.meta_data.directory_id) {
    InitialiseEncryptor(relative_path, file_context);
    file_context.open_count = 1;
  }
  directory_handler_.Add(relative_path, std::move(file_context));
}

template <typename Storage>
void Drive<Storage>::Open(const boost::filesystem::path& relative_path) {
  auto file_context(GetMutableContext(relative_path));
  if (!file_context->meta_data.directory_id) {
    ++file_context->open_count;
    if (!file_context->self_encryptor)
      InitialiseEncryptor(relative_path, *file_context);
  }
}

template <typename Storage>
void Drive<Storage>::Flush(const boost::filesystem::path& relative_path) {
  auto file_context(GetMutableContext(relative_path));
  if (file_context->self_encryptor && !FlushEncryptor(file_context)) {
    LOG(kError) << "Failed to flush " << relative_path;
    ThrowError(CommonErrors::unknown);
  }
  directory_handler_.PutVersion(relative_path.parent_path());
}

template <typename Storage>
void Drive<Storage>::Release(const boost::filesystem::path& relative_path) {
  auto file_context(GetMutableContext(relative_path));
  if (!file_context->meta_data.directory_id)
    --file_context->open_count;
  if (file_context->self_encryptor && !FlushEncryptor(file_context))
    LOG(kError) << "Failed to flush " << relative_path << " during Release";
  directory_handler_.PutVersion(relative_path.parent_path());
}

template <typename Storage>
void Drive<Storage>::Delete(const boost::filesystem::path& relative_path) {
  directory_handler_.Delete(relative_path);
}

template <typename Storage>
void Drive<Storage>::Rename(const boost::filesystem::path& old_relative_path,
                            const boost::filesystem::path& new_relative_path) {
  directory_handler_.Rename(old_relative_path, new_relative_path);
}

template <typename Storage>
uint32_t Drive<Storage>::Read(const boost::filesystem::path& relative_path, char* data,
                              uint32_t size, uint64_t offset) {
  auto file_context(GetContext(relative_path));
  assert(file_context->self_encryptor);
  LOG(kInfo) << "For "  << relative_path << ", reading " << size << " of "
             << file_context->self_encryptor->size() << " bytes at offset " << offset;
  if (!file_context->self_encryptor->Read(data, size, offset))
    ThrowError(CommonErrors::unknown);
  // TODO(Fraser#5#): 2013-12-02 - Update last access time?
  if (offset + size > file_context->self_encryptor->size()) {
    return offset > file_context->self_encryptor->size() ? 0 :
           static_cast<uint32_t>(file_context->self_encryptor->size() - offset);
  } else {
    return size;
  }
}

template <typename Storage>
uint32_t Drive<Storage>::Write(const boost::filesystem::path& relative_path, const char* data,
                               uint32_t size, uint64_t offset) {
  auto file_context(GetContext(relative_path));
  assert(file_context->self_encryptor);
  LOG(kInfo) << "For "  << relative_path << ", writing " << size << " bytes at offset " << offset;
  if (!file_context->self_encryptor->Write(data, size, offset))
    ThrowError(CommonErrors::unknown);
  // TODO(Fraser#5#): 2013-12-02 - Update last write time?
#ifndef MAIDSAFE_WIN32
  int64_t max_size(
      std::max(static_cast<off_t>(offset + size), file_context->meta_data.attributes.st_size));
  file_context->meta_data.attributes.st_size = max_size;
  file_context->meta_data.attributes.st_blocks = file_context->meta_data.attributes.st_size / 512;
#endif
  return size;
}

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DRIVE_H_
