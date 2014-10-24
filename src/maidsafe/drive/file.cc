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

#include "maidsafe/drive/directory.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace detail {

File::File()
    : Path(fs::regular_file),
      buffer(),
      timer(),
      flushed(false) {}

File::File(MetaData meta_data_in, std::shared_ptr<Directory> parent_in)
    : Path(parent_in, meta_data_in.file_type()),
      buffer(),
      timer(),
      flushed(false) {
  meta_data = std::move(meta_data_in);
}

File::File(const boost::filesystem::path& name, bool is_directory)
    : Path(is_directory ?
           MetaData::FileType::directory_file : MetaData::FileType::regular_file),
      buffer(),
      timer(),
      flushed(false) {
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
     flushing/serialisation before destructing the files. */
  assert(self_encryptor == nullptr);
}

bool File::Valid() const {
  // The open_count must be >=0.  If > 0 and the context doesn't represent a directory, the buffer
  // and encryptor should be non-null.
  return ((open_count == 0) ||
          ((open_count > 0) && (meta_data.directory_id() || (buffer && self_encryptor && timer))));
}

std::string File::Serialise() {
  return std::string();
}

void File::Serialise(protobuf::Directory& proto_directory,
                     std::vector<ImmutableData::Name>& chunks,
                     std::unique_lock<std::mutex>& lock) {
  auto child = proto_directory.add_children();
  Serialise(*child);
  if (self_encryptor) {  // File has been opened
    timer->cancel();
    FlushEncryptor(lock, chunks);
    flushed = false;
  } else if (meta_data.data_map()) {
    if (flushed) {  // File has already been flushed
      flushed = false;
    } else {  // File has not been opened
      for (const auto& chunk : meta_data.data_map()->chunks)
        chunks.emplace_back(
            Identity(std::string(std::begin(chunk.hash), std::end(chunk.hash))));
    }
  }
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
      assert(false); // Serialised by the Symlink class
      break;
    default:
      break;
  }
}

void File::Flush() {
  std::shared_ptr<Directory> parent = Parent();
  if (parent) {
    parent->FlushChildAndDeleteEncryptor(this);
  }
}

void File::ScheduleForStoring() {
  std::shared_ptr<Directory> parent = Parent();
  if (parent) {
      parent->ScheduleForStoring();
  }
}

void File::FlushEncryptor(std::unique_lock<std::mutex>& lock,
                          std::vector<ImmutableData::Name>& chunks_to_be_incremented) {

  const std::shared_ptr<Directory::Listener> listener = GetListener();
  self_encryptor->Flush();

  if (self_encryptor->original_data_map().chunks.empty()) {
    // If the original data map didn't contain any chunks, just store the new ones.
    for (const auto& chunk : self_encryptor->data_map().chunks) {
      auto content(buffer->Get(
                       std::string(std::begin(chunk.hash), std::end(chunk.hash))));
      if (listener) {
	listener->PutChunk(ImmutableData(content), lock);
      }
    }
  } else {
    // Check each new chunk against the original data map's chunks.  Store the new ones and
    // increment the reference count on the existing chunks.
    for (const auto& chunk : self_encryptor->data_map().chunks) {
      if (std::any_of(std::begin(self_encryptor->original_data_map().chunks),
                      std::end(self_encryptor->original_data_map().chunks),
                      [&chunk](const encrypt::ChunkDetails& original_chunk) {
                            return chunk.hash == original_chunk.hash;
                      })) {
        chunks_to_be_incremented.emplace_back(
            Identity(std::string(std::begin(chunk.hash), std::end(chunk.hash))));
      } else {
        auto content(buffer->Get(
                         std::string(std::begin(chunk.hash), std::end(chunk.hash))));
	if (listener) {
	  listener->PutChunk(ImmutableData(content), lock);
	}
      }
    }
  }
  if (open_count == 0) {
    self_encryptor->Close();
    self_encryptor.reset();
    buffer.reset();
  }
  flushed = true;
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
