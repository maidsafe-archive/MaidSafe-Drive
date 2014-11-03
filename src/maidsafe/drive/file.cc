/*  Copyright 2013 MaidSafe.net limited

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

#include "maidsafe/drive/file.h"

#include <utility>

#include "maidsafe/common/make_unique.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/drive/directory.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace detail {

namespace {
std::shared_ptr<Directory::Listener> GetDirectoryListener(const std::shared_ptr<Directory>& directory) {
  if (directory != nullptr) {
    return directory->GetListener();
  }
  return nullptr;
}
}

File::File(
    boost::asio::io_service& asio_service,
    MetaData meta_data_in,
    std::shared_ptr<Directory> parent_in)
  : Path(parent_in, meta_data_in.file_type()),
    file_data_(),
    close_timer_(asio_service),
    data_mutex_(),
    skip_chunk_incrementing_(false) {
  meta_data = std::move(meta_data_in);
}

File::File(
    boost::asio::io_service& asio_service,
    const boost::filesystem::path& name,
    bool is_directory)
  : Path(is_directory ?
         MetaData::FileType::directory_file : MetaData::FileType::regular_file),
    file_data_(),
    close_timer_(asio_service),
    data_mutex_(),
    skip_chunk_incrementing_(false) {
  meta_data = MetaData(
      name,
      is_directory ? MetaData::FileType::directory_file : MetaData::FileType::regular_file);
}

File::~File() {
  /* There is no use in flushing the file in the destructor currently. The
     directory needs to know the latest data map for serialising the directory.
     The latest data map is contained within this file object. Therefore, if
     there are new chunks that are flushed here, they will not be referenced by
     the data map in the directory, and will go missing. Since a directory has
     a shared_ptr to a file object, it should handle the last
     flushing/serialisation before destructing the files. However, a file can be
     created, closed, and deleted before cleanup timers execute. */
  try {
    close_timer_.cancel();
    if (HasBuffer()) {
      assert(!file_data_->IsOpen());
      file_data_->self_encryptor_.Close();
      file_data_.reset();
    }
  }
  catch (...) {
  }
}

std::string File::Serialise() {
  return std::string();
}

void File::Serialise(protobuf::Directory& proto_directory,
                     std::vector<ImmutableData::Name>& chunks) {
  const std::lock_guard<std::mutex> lock(data_mutex_);

  if (HasBuffer()) {
    FlushEncryptor(chunks);
  }
  else if (meta_data.data_map()) { // still have directories being created as file objects
    if (!skip_chunk_incrementing_) {
      chunks.reserve(chunks.size() + meta_data.data_map()->chunks.size());
      for (const auto& chunk : meta_data.data_map()->chunks) {
        chunks.emplace_back(
            Identity(std::string(std::begin(chunk.hash), std::end(chunk.hash))));
      }
    }
  }

  skip_chunk_incrementing_ = false;

  // Flushing encryptor updates data map, so serialise after flush
  auto child = proto_directory.add_children();
  Serialise(*child);
}

void File::Serialise(protobuf::Path& proto_path) {
  assert(proto_path.mutable_attributes() != nullptr);
  meta_data.ToProtobuf(*(proto_path.mutable_attributes()));
  proto_path.set_name(meta_data.name().string());
  switch (meta_data.file_type()) {
    case fs::directory_file:
      proto_path.set_directory_id(meta_data.directory_id()->string());
      break;
    case fs::regular_file: {
      std::string serialised_data_map;
      encrypt::SerialiseDataMap(*meta_data.data_map(), serialised_data_map);
      proto_path.set_serialised_data_map(serialised_data_map);
      break;
    }
    case fs::symlink_file:
    default:
      assert(false); // Serialised by the Symlink or directory class
      break;
  }
}

void File::Open(
    const std::function<NonEmptyString(const std::string&)>& get_chunk_from_store,
    const MemoryUsage max_memory_usage,
    const DiskUsage max_disk_usage,
    const boost::filesystem::path& disk_buffer_location) {
  const std::lock_guard<std::mutex> lock(data_mutex_);

  if (meta_data.file_type() == MetaData::FileType::regular_file) {
    assert(meta_data.data_map() != nullptr);
    if (!HasBuffer()) {
      file_data_ = maidsafe::make_unique<Data>(
        meta_data.name(),
        max_memory_usage,
        max_disk_usage,
        disk_buffer_location,
        *meta_data.data_map(),
        get_chunk_from_store);
    }

    LOG(kInfo) << "Opened " << meta_data.name() << " with open count " << file_data_->open_count_;

    assert(HasBuffer());
    close_timer_.cancel();
    ++(file_data_->open_count_);
    assert(file_data_->IsOpen());
  }
}

std::uint32_t File::Read(char* data, std::uint32_t length, std::uint64_t offset) {
  const std::lock_guard<std::mutex> lock(data_mutex_);
  VerifyHasBuffer();

  LOG(kInfo) << "For "  << meta_data.name() << ", reading " << length << " of "
             << file_data_->self_encryptor_.size() << " bytes at offset " << offset;

  if (offset > file_data_->self_encryptor_.size()) {
    return 0;
  }

  length = std::uint32_t(
      std::min<std::uint64_t>(length, file_data_->self_encryptor_.size() - offset));

  if (length > 0 && !file_data_->self_encryptor_.Read(data, length, offset)) {
    BOOST_THROW_EXCEPTION(MakeError(EncryptErrors::failed_to_read));
  }

  meta_data.UpdateLastAccessTime();
  return length;
}

std::uint32_t File::Write(const char* data, std::uint32_t length, std::uint64_t offset) {
  {
    const std::lock_guard<std::mutex> lock(data_mutex_);
    VerifyHasBuffer();

    LOG(kInfo) << "For " << meta_data.name() << ", writing " << length << " bytes at offset " << offset;

    if (!file_data_->self_encryptor_.Write(data, length, offset)) {
      BOOST_THROW_EXCEPTION(MakeError(EncryptErrors::failed_to_write));
    }

    meta_data.UpdateSize(file_data_->self_encryptor_.size());
  }
  ScheduleForStoring();
  return length;
}

void File::Truncate(std::uint64_t offset) {
  {
    const std::lock_guard<std::mutex> lock(data_mutex_);
    VerifyHasBuffer();

    LOG(kInfo) << "Truncating file " << meta_data.name() << " from " << meta_data.size() << " to " << offset;
    if (!file_data_->self_encryptor_.Truncate(offset)) {
      BOOST_THROW_EXCEPTION(MakeError(EncryptErrors::failed_to_write));
    }

    meta_data.UpdateSize(file_data_->self_encryptor_.size());
  }
  ScheduleForStoring();
}

void File::Close() {
  const std::lock_guard<std::mutex> lock(data_mutex_);
  if (meta_data.file_type() == MetaData::FileType::regular_file) {
    VerifyHasBuffer();

    LOG(kInfo) << "Closing " << meta_data.name() << " with open count " << file_data_->open_count_;

    assert(file_data_->IsOpen());
    if (file_data_->IsOpen()) {
      --(file_data_->open_count_);
    }

    if (!file_data_->IsOpen()) {
      LOG(kInfo) << "Setting close timer for " << meta_data.name();
      close_timer_.expires_from_now(detail::kFileInactivityDelay);

      const std::weak_ptr<File> this_weak(
          std::static_pointer_cast<File>(shared_from_this()));
      close_timer_.async_wait(
          [this_weak](const boost::system::error_code& error) {
            const std::shared_ptr<File> this_shared(this_weak.lock());
            if (this_shared != nullptr && error != boost::asio::error::operation_aborted) {
              std::vector<ImmutableData::Name> chunks_to_be_incremented;
              {
                const std::lock_guard<std::mutex> lock(this_shared->data_mutex_);
                if (this_shared->HasBuffer() && !this_shared->file_data_->IsOpen()) {
                  this_shared->FlushEncryptor(chunks_to_be_incremented);
                  LOG(kInfo) << "Deleting encryptor and buffer for " << this_shared->meta_data.name();
                }
              }

              if (!chunks_to_be_incremented.empty()) {
                const std::shared_ptr<Directory::Listener> listener(GetDirectoryListener(this_shared->Parent()));
                if (listener) {
                  listener->IncrementChunks(chunks_to_be_incremented);
                }
              }
            }
          });
    }
  }
}

void File::ScheduleForStoring() {
  std::shared_ptr<Directory> parent = Parent();
  if (parent) {
    parent->ScheduleForStoring();
  }
}

bool File::HasBuffer() const {
  return file_data_ != nullptr;
}

void File::VerifyHasBuffer() const {
  assert(HasBuffer());
  if (!HasBuffer()) {
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::null_pointer));
  }
}

void File::FlushEncryptor(std::vector<ImmutableData::Name>& chunks_to_be_incremented) {
  assert(HasBuffer());

  const std::shared_ptr<Directory::Listener> listener = GetDirectoryListener(Parent());
  file_data_->self_encryptor_.Flush();

  if (file_data_->self_encryptor_.original_data_map().chunks.empty()) {
    // If the original data map didn't contain any chunks, just store the new ones.
    for (const auto& chunk : file_data_->self_encryptor_.data_map().chunks) {
      auto content(file_data_->buffer_.Get(
                       std::string(std::begin(chunk.hash), std::end(chunk.hash))));
      if (listener) {
	listener->PutChunk(ImmutableData(content));
      }
    }
  }
  else {
    // Check each new chunk against the original data map's chunks.  Store the new ones and
    // increment the reference count on the existing chunks.
    for (const auto& chunk : file_data_->self_encryptor_.data_map().chunks) {
      if (std::any_of(std::begin(file_data_->self_encryptor_.original_data_map().chunks),
                      std::end(file_data_->self_encryptor_.original_data_map().chunks),
                      [&chunk](const encrypt::ChunkDetails& original_chunk) {
                            return chunk.hash == original_chunk.hash;
                      })) {
        chunks_to_be_incremented.emplace_back(
            Identity(std::string(std::begin(chunk.hash), std::end(chunk.hash))));
      } else {
        auto content(file_data_->buffer_.Get(
                         std::string(std::begin(chunk.hash), std::end(chunk.hash))));
	if (listener) {
	  listener->PutChunk(ImmutableData(content));
	}
      }
    }
  }

  if (!file_data_->IsOpen()) {
    file_data_->self_encryptor_.Close();
    file_data_.reset();
  }
  skip_chunk_incrementing_ = true;
}

File::Data::Data(
    const boost::filesystem::path& name,
    const MemoryUsage max_memory_usage,
    const DiskUsage max_disk_usage,
    const boost::filesystem::path& disk_buffer_location,
    encrypt::DataMap& data_map,
    const std::function<NonEmptyString(const std::string&)>& get_chunk_from_store)
  : buffer_(
      max_memory_usage,
      max_disk_usage,
      [name](const std::string&, const NonEmptyString&) {
        LOG(kWarning) << name << "is too large for storage";
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::file_too_large));
      },
      boost::filesystem::unique_path(disk_buffer_location / "%%%%%-%%%%%-%%%%%-%%%%%")),
    self_encryptor_(data_map, buffer_, get_chunk_from_store),
    open_count_(0) {
}


}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
