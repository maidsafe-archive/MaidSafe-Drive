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

#ifndef MAIDSAFE_DRIVE_DIRECTORY_HANDLER_H_
#define MAIDSAFE_DRIVE_DIRECTORY_HANDLER_H_

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "boost/algorithm/string/find.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/fstream.hpp"
#include "boost/filesystem/path.hpp"

#include "maidsafe/common/rsa.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/profiler.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/data_types/data_type_values.h"
#include "maidsafe/data_types/owner_directory.h"
#include "maidsafe/data_types/group_directory.h"
#include "maidsafe/data_types/world_directory.h"

#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/meta_data.h"


namespace maidsafe {

namespace drive {

namespace detail {

namespace test { class DirectoryHandlerTest; }

// ==================== Public functions and classes ===============================================
template<typename Storage>
class DirectoryHandler {
 public:
  DirectoryHandler(std::shared_ptr<Storage> storage,
                   Directory* parent,
                   DataTagValue directory_type,
                   bool immutable_root = false);
  DirectoryHandler(const DirectoryHandler& other);
  DirectoryHandler(DirectoryHandler&& other);
  DirectoryHandler& operator=(DirectoryHandler other);
  virtual ~DirectoryHandler() {}

  Directory GetFromPath(const boost::filesystem::path& path) const;

  void SetWorldReadWrite() { world_is_writeable_ = true; }
  void SetWorldReadOnly() { world_is_writeable_ = false; }
  Storage& storage() const { return *storage_; }
  DataTagValue directory_type() const { return directory_type_; }
  bool world_is_writeable() const { return world_is_writeable_; }
  bool immutable_root() const { return immutable_root_; }

  friend void swap(DirectoryHandler& lhs, DirectoryHandler& rhs) {
    using std::swap;
    swap(lhs.storage_, rhs.storage_);
    swap(lhs.parent_, rhs.parent_);
    swap(lhs.directory_type_, rhs.directory_type_);
    swap(lhs.world_is_writeable_, rhs.world_is_writeable_);
    swap(lhs.immutable_root_, rhs.immutable_root_);
  }
  friend class test::DirectoryHandlerTest;

 private:
  std::shared_ptr<Storage> storage_;
  Directory* parent_;
  DataTagValue directory_type_;
  bool world_is_writeable_, immutable_root_;
};

template<typename Storage>
void PutToStorage(Storage& storage, Directory& directory);

template<typename Storage>
Directory GetFromStorage(Storage& storage,
                         const DirectoryId& parent_id,
                         const DirectoryId& directory_id,
                         DataTagValue directory_type);

template<typename Storage>
void DeleteFromStorage(Storage& storage, const Directory& directory);

inline bool IsDirectory(const MetaData& meta_data) {
  return static_cast<bool>(meta_data.directory_id);
}



// ==================== Implementation details =====================================================
template<typename DirectoryType>
struct is_encrypted_dir;

template<>
struct is_encrypted_dir<OwnerDirectory> : public std::true_type {};

template<>
struct is_encrypted_dir<GroupDirectory> : public std::true_type {};

template<>
struct is_encrypted_dir<WorldDirectory> : public std::false_type {};

template<typename Storage, typename DirectoryType>
typename std::enable_if<is_encrypted_dir<DirectoryType>::value>::type
    PutToStorage(Storage& storage, Directory& directory);

template<typename Storage, typename DirectoryType>
typename std::enable_if<!is_encrypted_dir<DirectoryType>::value>::type
    PutToStorage(Storage& storage, const Directory& directory);

template<typename Storage, typename DirectoryType>
typename std::enable_if<is_encrypted_dir<DirectoryType>::value, Directory>::type
    GetDirectoryFromStorage(Storage& storage,
                            const DirectoryId& parent_id,
                            const DirectoryId& directory_id);

template<typename Storage, typename DirectoryType>
typename std::enable_if<!is_encrypted_dir<DirectoryType>::value, Directory>::type
    GetDirectoryFromStorage(Storage& storage,
                            const DirectoryId& parent_id,
                            const DirectoryId& directory_id);

template<typename Storage, typename DirectoryType>
DataMapPtr GetDataMapFromStorage(Storage& storage,
                                 const DirectoryId& parent_id,
                                 const DirectoryId& directory_id);

template<typename Storage, typename DirectoryType>
typename std::enable_if<is_encrypted_dir<DirectoryType>::value>::type
    DeleteFromStorage(Storage& storage, const Directory& directory);

template<typename Storage, typename DirectoryType>
typename std::enable_if<!is_encrypted_dir<DirectoryType>::value>::type
    DeleteFromStorage(Storage& storage, const Directory& directory);



template<typename Storage>
DirectoryHandler<Storage>::DirectoryHandler(std::shared_ptr<Storage> storage,
                                            Directory* parent,
                                            DataTagValue directory_type,
                                            bool immutable_root = false)
    : storage_(storage),
      parent_(parent),
      directory_type_(directory_type),
      world_is_writeable_(true),
      immutable_root_(immutable_root) {}

template<typename Storage>
DirectoryHandler<Storage>::DirectoryHandler(const DirectoryHandler& other)
    : storage_(other.storage_),
      parent_(other.parent_),
      directory_type_(other.directory_type_),
      world_is_writeable_(other.world_is_writeable_),
      immutable_root_(other.immutable_root_) {}

template<typename Storage>
DirectoryHandler<Storage>::DirectoryHandler(DirectoryHandler&& other)
    : storage_(std::move(other.storage_)),
      parent_(std::move(other.parent_)),
      directory_type_(std::move(other.directory_type_)),
      world_is_writeable_(std::move(other.world_is_writeable_)),
      immutable_root_(std::move(other.immutable_root_)) {}

template<typename Storage>
DirectoryHandler<Storage>& DirectoryHandler<Storage>::operator=(DirectoryHandler other) {
  swap(*this, other);
  return *this;
}

template<typename Storage>
Directory DirectoryHandler<Storage>::GetFromPath(const boost::filesystem::path& path) const {
  SCOPED_PROFILE
  auto itr(++std::begin(path));
  if (itr == std::end(path))
    return *parent_;

  Directory directory(*parent_);
  // Get successive directory listings until found.
  MetaData meta_data;
  while (itr != std::end(path)) {
    directory.listing->GetChild((*itr++), meta_data);
    if (!IsDirectory(meta_data))
      ThrowError(CommonErrors::invalid_parameter);
    directory = GetFromStorage(*storage_, directory.listing->directory_id(),
                               *meta_data.directory_id, directory_type_);
  }
  return directory;
}

template<typename Storage>
void PutToStorage(Storage& storage, Directory& directory) {
  switch (directory.type) {
    case DataTagValue::kOwnerDirectoryValue:
      return PutToStorage<Storage, OwnerDirectory>(storage, directory);
    case DataTagValue::kGroupDirectoryValue:
      return PutToStorage<Storage, GroupDirectory>(storage, directory);
    case DataTagValue::kWorldDirectoryValue:
      return PutToStorage<Storage, WorldDirectory>(storage, directory);
    default:
      ThrowError(CommonErrors::not_a_directory);
  }
}

template<typename Storage, typename DirectoryType>
typename std::enable_if<is_encrypted_dir<DirectoryType>::value>::type
    PutToStorage(Storage& storage, Directory& directory) {
  assert(directory.type == DirectoryType::Tag::kValue);
  // TODO(Fraser#5#): 2013-08-28 - Use versions
  try {
    DeleteFromStorage(storage, directory);
  }
  catch(...) {}
  auto serialised_directory_listing(directory.listing->Serialise());
  // Self-encrypt serialised directory listing.
  {
    if (!directory.data_map)
      directory.data_map.reset(new encrypt::DataMap);
    encrypt::SelfEncryptor<Storage> self_encryptor(directory.data_map, storage);
    assert(serialised_directory_listing.size() <= std::numeric_limits<uint32_t>::max());
    if (!self_encryptor.Write(serialised_directory_listing.c_str(),
                              static_cast<uint32_t>(serialised_directory_listing.size()), 0)) {
      ThrowError(CommonErrors::invalid_parameter);
    }
  }
  // Encrypt directory listing's datamap.
  auto encrypted_data_map = encrypt::EncryptDataMap(directory.parent_id,
                                                    directory.listing->directory_id(),
                                                    directory.data_map);
  // Store the encrypted datamap.
  DirectoryType dir(typename DirectoryType::Name(directory.listing->directory_id()),
                    encrypted_data_map);
  storage.Put(dir);
}

template<typename Storage, typename DirectoryType>
typename std::enable_if<!is_encrypted_dir<DirectoryType>::value>::type
    PutToStorage(Storage& storage, const Directory& directory) {
  assert(directory.type == DirectoryType::Tag::kValue);
  auto serialised_directory_listing(directory.listing->Serialise());
  try {
    DeleteFromStorage(storage, directory);
  }
  catch(...) {}
  DirectoryType dir(typename DirectoryType::Name(directory.listing->directory_id()),
                    NonEmptyString(serialised_directory_listing));
  storage.Put(dir);
}

template<typename Storage>
Directory GetFromStorage(Storage& storage,
                         const DirectoryId& parent_id,
                         const DirectoryId& directory_id,
                         DataTagValue directory_type) {
  switch (directory_type) {
    case DataTagValue::kOwnerDirectoryValue:
      return GetDirectoryFromStorage<Storage, OwnerDirectory>(storage, parent_id, directory_id);
    case DataTagValue::kGroupDirectoryValue:
      return GetDirectoryFromStorage<Storage, GroupDirectory>(storage, parent_id, directory_id);
    case DataTagValue::kWorldDirectoryValue:
      return GetDirectoryFromStorage<Storage, WorldDirectory>(storage, parent_id, directory_id);
    default:
      ThrowError(CommonErrors::not_a_directory);
  }
  return Directory();
}

template<typename Storage, typename DirectoryType>
typename std::enable_if<is_encrypted_dir<DirectoryType>::value, Directory>::type
    GetDirectoryFromStorage(Storage& storage,
                            const DirectoryId& parent_id,
                            const DirectoryId& directory_id) {
  Directory directory(parent_id, nullptr, nullptr, DirectoryType::Tag::kValue);
  // Retrieve encrypted datamap.
  directory.data_map = GetDataMapFromStorage<Storage, DirectoryType>(storage, parent_id,
                                                                     directory_id);
  // Decrypt serialised directory listing.
  encrypt::SelfEncryptor<Storage> self_encryptor(directory.data_map, storage);
  uint32_t data_map_chunks_size(static_cast<uint32_t>(directory.data_map->chunks.size()));
  uint32_t data_map_size;
  if (data_map_chunks_size != 0) {
    data_map_size = (data_map_chunks_size - 1) * directory.data_map->chunks[0].size +
                    directory.data_map->chunks.rbegin()->size;
  } else {
    data_map_size = static_cast<uint32_t>(directory.data_map->content.size());
  }
  std::vector<char> serialised_listing(data_map_size);
  if (!self_encryptor.Read(&serialised_listing[0], data_map_size, 0))
    ThrowError(CommonErrors::invalid_parameter);

  // Parse serialised directory listing.
  directory.listing = std::make_shared<DirectoryListing>(std::string(std::begin(serialised_listing),
                                                                     std::end(serialised_listing)));
  return directory;
}

template<typename Storage, typename DirectoryType>
typename std::enable_if<!is_encrypted_dir<DirectoryType>::value, Directory>::type
    GetDirectoryFromStorage(Storage& storage,
                            const DirectoryId& parent_id,
                            const DirectoryId& directory_id) {
  static_assert(DirectoryType::Tag::kValue == DataTagValue::kWorldDirectoryValue,
                "This should only be called with WorldDirectory type.");
  DirectoryType dir(storage.Get<DirectoryType>(typename DirectoryType::Name(directory_id)).get());
  return Directory(parent_id, std::make_shared<DirectoryListing>(dir.data().string()), nullptr,
                   DirectoryType::Tag::kValue);
}

template<typename Storage, typename DirectoryType>
DataMapPtr GetDataMapFromStorage(Storage& storage,
                                 const DirectoryId& parent_id,
                                 const DirectoryId& directory_id) {
  static_assert(is_encrypted_dir<DirectoryType>::value, "Must be an encrypted type of directory.");
  DirectoryType dir(storage.Get<DirectoryType>(typename DirectoryType::Name(directory_id)).get());
  auto data_map(std::make_shared<encrypt::DataMap>());
  encrypt::DecryptDataMap(parent_id, directory_id, dir.data().string(), data_map);
  return data_map;
}

template<typename Storage>
void DeleteFromStorage(Storage& storage, const Directory& directory) {
  switch (directory.type) {
    case DataTagValue::kOwnerDirectoryValue:
      return DeleteFromStorage<Storage, OwnerDirectory>(storage, directory);
    case DataTagValue::kGroupDirectoryValue:
      return DeleteFromStorage<Storage, GroupDirectory>(storage, directory);
    case DataTagValue::kWorldDirectoryValue:
      return DeleteFromStorage<Storage, WorldDirectory>(storage, directory);
    default:
      ThrowError(CommonErrors::not_a_directory);
  }
}

template<typename Storage, typename DirectoryType>
typename std::enable_if<is_encrypted_dir<DirectoryType>::value>::type
    DeleteFromStorage(Storage& storage, const Directory& directory) {
  assert(directory.type == DirectoryType::Tag::kValue);
  if (directory.data_map) {
    encrypt::SelfEncryptor<Storage> self_encryptor(directory.data_map, storage);
    self_encryptor.DeleteAllChunks();
  }
  // TODO(Fraser#5#): 2013-08-28 - Check if there's a case where the DM could be nullptr and we need
  //                  to retrieve the DM from Storage in order to call DeleteAllChunks().
  storage.Delete<DirectoryType>(typename DirectoryType::Name(directory.listing->directory_id()));
}

template<typename Storage, typename DirectoryType>
typename std::enable_if<!is_encrypted_dir<DirectoryType>::value>::type
    DeleteFromStorage(Storage& storage, const Directory& directory) {
  assert(directory.type == DirectoryType::Tag::kValue);
  storage.Delete<DirectoryType>(typename DirectoryType::Name(directory.listing->directory_id()));
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_HANDLER_H_
