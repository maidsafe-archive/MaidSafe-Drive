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
    : Path(parent_in, meta_data_in.file_type),
      buffer(),
      timer(),
      flushed(false) {
  meta_data = std::move(meta_data_in);
}

File::File(const boost::filesystem::path& name, bool is_directory)
    : Path(is_directory ? fs::directory_file : fs::regular_file),
      buffer(),
      timer(),
      flushed(false) {
  meta_data = MetaData(name, is_directory ? fs::directory_file : fs::regular_file);
}

File::~File() {
  if (timer) {
    timer->cancel();
    Flush();
  }
}

bool File::Valid() const {
  // The open_count must be >=0.  If > 0 and the context doesn't represent a directory, the buffer
  // and encryptor should be non-null.
  return ((open_count == 0) ||
          ((open_count > 0) && (meta_data.directory_id || (buffer && self_encryptor && timer))));
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
    FlushEncryptor([this, &lock](const ImmutableData& data) {
        std::shared_ptr<Directory::Listener> listener = listener_.lock();
        if (listener) {
          listener->PutChunk(data, lock);
        }
      },
      chunks);
    flushed = false;
  } else if (meta_data.data_map) {
    if (flushed) {  // File has already been flushed
      flushed = false;
    } else {  // File has not been opened
      for (const auto& chunk : meta_data.data_map->chunks)
        chunks.emplace_back(
	    Identity(std::string(std::begin(chunk.hash), std::end(chunk.hash))));
    }
  }
}

void File::Serialise(protobuf::Path& proto_path) {
  assert(proto_path.mutable_attributes() != nullptr);
  meta_data.ToProtobuf(*(proto_path.mutable_attributes()));
  proto_path.set_name(meta_data.name.string());
  switch (meta_data.file_type) {
    case fs::directory_file:
      proto_path.set_directory_id(meta_data.directory_id->string());
      break;
    case fs::regular_file: {
      std::string serialised_data_map;
      encrypt::SerialiseDataMap(*meta_data.data_map, serialised_data_map);
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

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
