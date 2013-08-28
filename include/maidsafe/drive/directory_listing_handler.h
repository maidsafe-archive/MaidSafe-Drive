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

#ifndef MAIDSAFE_DRIVE_DIRECTORY_LISTING_HANDLER_H_
#define MAIDSAFE_DRIVE_DIRECTORY_LISTING_HANDLER_H_

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

#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/meta_data.h"


namespace maidsafe {

namespace drive {

namespace detail {

namespace test { class DirectoryListingHandlerTest; }

struct DirectoryData {
  DirectoryData(const DirectoryId& parent_id_in,
                std::shared_ptr<DirectoryListing> listing_in,
                std::shared_ptr<encrypt::DataMap> data_map_in,
                DataTagValue type_in)
      : parent_id(parent_id_in),
        listing(listing_in),
        data_map(data_map_in),
        type(type_in),
        content_changed(false) {
    assert(type == DataTagValue::kOwnerDirectoryValue ||
           type == DataTagValue::kGroupDirectoryValue ||
           type == DataTagValue::kWorldDirectoryValue);
  }
  DirectoryData()
      : parent_id(),
        listing(),
        data_map(),
        type(DataTagValue::kOwnerDirectoryValue),
        content_changed(false) {}
  DirectoryId parent_id;
  std::shared_ptr<DirectoryListing> listing;
  std::shared_ptr<encrypt::DataMap> data_map;
  DataTagValue type;
  bool content_changed;
};

template<DataTagValue directory_tag>
struct is_encrypted_dir;

template<>
struct is_encrypted_dir<DataTagValue::kOwnerDirectoryValue> : public std::true_type {};

template<>
struct is_encrypted_dir<DataTagValue::kGroupDirectoryValue> : public std::true_type {};

template<>
struct is_encrypted_dir<DataTagValue::kWorldDirectoryValue> : public std::false_type {};

template<typename Storage>
void PutToStorage(Storage& storage, const DirectoryData& directory);

template<typename Storage>
DirectoryData GetDirectoryFromStorage(Storage& storage,
                                      const DirectoryId& parent_id,
                                      const DirectoryId& directory_id,
                                      DataTagValue directory_type);

template<typename Storage>
DirectoryData GetDirectoryFromStorage(Storage& storage,
                                      const DirectoryId& parent_id,
                                      const DirectoryId& directory_id,
                                      DataTagValue directory_type,
                                      std::true_type is_encrypted);

template<typename Storage>
DirectoryData GetDirectoryFromStorage(Storage& storage,
                                      const DirectoryId& parent_id,
                                      const DirectoryId& directory_id,
                                      DataTagValue directory_type,
                                      std::false_type is_not_encrypted);

template<typename Storage, typename DirectoryType>
DataMapPtr GetDataMapFromStorage(Storage& storage,
                                 const DirectoryId& parent_id,
                                 const DirectoryId& directory_id);

template<typename Storage>
void DeleteFromStorage(Storage& storage, const DirectoryData& directory);





// Adds a directory or file represented by meta_data and path to the appropriate parent
// directory listing.  If the element is a directory, a new directory listing is created and
// stored.  The parent directory's ID is returned in parent_id and its parent directory's ID is
// returned in grandparent_id.
template<typename Storage>
void AddElement(Storage& storage,
                const boost::filesystem::path& path,
                const MetaData& meta_data,
                DirectoryId* grandparent_id,
                DirectoryId* parent_id);

// Deletes the directory or file represented by path from the appropriate parent
// directory listing.  If the element is a directory, its directory listing is deleted.
// meta_data is filled with the element's details if found.  If the element is a file, this
// allows the caller to delete any corresponding chunks.  If save_changes is true, the parent
// directory listing is stored after the deletion.
template<typename Storage>
void DeleteElement(Storage& storage, const boost::filesystem::path& path, MetaData& meta_data);

template<typename Storage>
bool CanDelete(Storage& storage, const boost::filesystem::path& path);

template<typename Storage>
void RenameElement(Storage& storage,
                   const boost::filesystem::path& old_path,
                   const boost::filesystem::path& new_path,
                   MetaData& meta_data,
                   int64_t& reclaimed_space);

template<typename Storage>
void UpdateParentDirectoryListing(Storage& storage,
                                  const boost::filesystem::path& parent_path,
                                  MetaData meta_data);

inline bool IsDirectory(const MetaData& meta_data);

template<typename Storage>
void GetParentAndGrandparent(const boost::filesystem::path& path,
                             DirectoryData* grandparent,
                             DirectoryData* parent,
                             MetaData* parent_meta_data);

template<typename Storage>
bool RenameTargetCanBeRemoved(const boost::filesystem::path& new_path,
                              const MetaData& target_meta_data);

template<typename Storage>
DataTagValue GetDirectoryType(const boost::filesystem::path& path);

template<typename Storage>
bool CanAdd(const boost::filesystem::path& path);

template<typename Storage>
bool CanRename(const boost::filesystem::path& from_path, const boost::filesystem::path& to_path);

template<typename Storage>
void RenameSameParent(const boost::filesystem::path& old_path,
                      const boost::filesystem::path& new_path,
                      MetaData& meta_data,
                      int64_t& reclaimed_space);

template<typename Storage>
void RenameDifferentParent(const boost::filesystem::path& old_path,
                           const boost::filesystem::path& new_path,
                           MetaData& meta_data,
                           int64_t& reclaimed_space);

template<typename Storage>
void ReStoreDirectories(const boost::filesystem::path& path, DataTagValue directory_type);









template<typename Storage>
class DirectoryListingHandler {
 public:
  // Put relative_root_ to storage.
  DirectoryListingHandler(std::shared_ptr<Storage> storage,
                          const DirectoryData& relative_root,
                          bool immutable_root = false);
  // Retrieve relative_root_ from storage.
  DirectoryListingHandler(std::shared_ptr<Storage> storage,
                          const DirectoryId& drive_root_id,
                          const DirectoryId& service_root_id,
                          bool immutable_root = false);
  DirectoryListingHandler(const DirectoryListingHandler& other);
  DirectoryListingHandler(DirectoryListingHandler&& other);
  DirectoryListingHandler& operator=(DirectoryListingHandler other);
  virtual ~DirectoryListingHandler() {}

  DirectoryData GetFromPath(const boost::filesystem::path& path);

  void SetWorldReadWrite();
  void SetWorldReadOnly();
  Storage& storage() const { return *storage_; }

  friend void swap(DirectoryListingHandler& lhs, DirectoryListingHandler& rhs) {
    using std::swap;
    swap(lhs.storage_, rhs.storage_);
    swap(lhs.relative_root_, rhs.relative_root_);
    swap(lhs.world_is_writeable_, rhs.world_is_writeable_);
    swap(lhs.immutable_root_, rhs.immutable_root_);
  }
  friend class test::DirectoryListingHandlerTest;

 protected:
  //DirectoryData GetDirectoryFromStorage(const DirectoryId& parent_id,
  //                                  const DirectoryId& directory_id,
  //                                  DataTagValue directory_type) const;
  //void PutToStorage(const DirectoryData& directory);
  //void DeleteFromStorage(const DirectoryId& parent_id,
  //                  const DirectoryId& directory_id,
  //                  DataTagValue directory_type);
  //void GetDataMapFromStorage(const DirectoryId& parent_id,
  //                     const DirectoryId& directory_id,
  //                     DataTagValue directory_type,
  //                     DataMapPtr data_map) const;
  //bool IsDirectory(const MetaData& meta_data) const;
  //void GetParentAndGrandparent(const boost::filesystem::path& path,
  //                             DirectoryData* grandparent,
  //                             DirectoryData* parent,
  //                             MetaData* parent_meta_data);
  //bool RenameTargetCanBeRemoved(const boost::filesystem::path& new_path,
  //                              const MetaData& target_meta_data);
  //DataTagValue GetDirectoryType(const boost::filesystem::path& path);
  //bool CanAdd(const boost::filesystem::path& path);
  //bool CanRename(const boost::filesystem::path& from_path, const boost::filesystem::path& to_path);
  //void RenameSameParent(const boost::filesystem::path& old_path,
  //                      const boost::filesystem::path& new_path,
  //                      MetaData& meta_data,
  //                      int64_t& reclaimed_space);
  //void RenameDifferentParent(const boost::filesystem::path& old_path,
  //                           const boost::filesystem::path& new_path,
  //                           MetaData& meta_data,
  //                           int64_t& reclaimed_space);
  //void ReStoreDirectories(const boost::filesystem::path& path,
  //                        DataTagValue directory_type);

 private:
  std::shared_ptr<Storage> storage_;
  DirectoryData relative_root_;
  bool world_is_writeable_, immutable_root_;
};



// ==================== Implementation =============================================================
template<typename Storage>
DirectoryListingHandler<Storage>::DirectoryListingHandler(std::shared_ptr<Storage> storage,
                                                          const DirectoryData& relative_root,
                                                          bool immutable_root = false)
    : storage_(storage),
      relative_root_(relative_root),
      world_is_writeable_(true),
      immutable_root_(immutable_root) {
  PutToStorage(storage_, relative_root_);
}

template<typename Storage>
DirectoryListingHandler<Storage>::DirectoryListingHandler(std::shared_ptr<Storage> storage,
                                                          const DirectoryId& drive_root_id,
                                                          const DirectoryId& service_root_id,
                                                          bool immutable_root)
    : storage_(storage),
      relative_root_(),
      world_is_writeable_(true),
      immutable_root_(immutable_root) {
  if (!drive_root_id.IsInitialised() || !service_root_id.IsInitialised())
    ThrowError(CommonErrors::uninitialised);
  relative_root_.first = GetDirectoryFromStorage(storage_, drive_root_id, service_root_id,
                                             DataTagValue::kOwnerDirectoryValue);
}

template<typename Storage>
DirectoryListingHandler<Storage>::DirectoryListingHandler(const DirectoryListingHandler& other)
    : storage_(other.storage_),
      relative_root_(other.relative_root_),
      world_is_writeable_(other.world_is_writeable_),
      immutable_root_(other.immutable_root_) {}

template<typename Storage>
DirectoryListingHandler<Storage>::DirectoryListingHandler(DirectoryListingHandler&& other)
    : storage_(std::move(other.storage_)),
      relative_root_(std::move(other.relative_root_)),
      world_is_writeable_(std::move(other.world_is_writeable_)),
      immutable_root_(std::move(other.immutable_root_)) {}

template<typename Storage>
DirectoryListingHandler<Storage>& DirectoryListingHandler<Storage>::operator=(
    DirectoryListingHandler other) {
  swap(*this, other);
  return *this;
}

template<typename Storage>
DirectoryData DirectoryListingHandler<Storage>::GetFromPath(const boost::filesystem::path& path) {
  SCOPED_PROFILE
  auto itr(++std::begin(path));
  if (++itr == std::end(path))
    return relative_root_;

  auto directory_type(GetDirectoryType(path));
  // Get root directory listing.
  DirectoryData directory(relative_root_.first);
  // Get successive directory listings until found.
  MetaData meta_data;
  for (; itr != std::end(path); ++itr) {
    directory.listing->GetChild((*itr), meta_data);

    if (!meta_data.directory_id)
      ThrowError(CommonErrors::invalid_parameter);
    directory = GetDirectoryFromStorage(directory.listing->directory_id(), *meta_data.directory_id,
                                    directory_type);
  }
  return std::make_pair(directory, directory_type);
}

//template<typename Storage>
//void DirectoryListingHandler<Storage>::AddElement(const boost::filesystem::path& path,
//                                                  const MetaData& meta_data,
//                                                  DirectoryId* grandparent_id,
//                                                  DirectoryId* parent_id) {
//  SCOPED_PROFILE
//  if (!CanAdd(path))
//    ThrowError(CommonErrors::invalid_parameter);
//
//  auto directory_type(GetDirectoryType(path));
//  DirectoryData grandparent, parent;
//  MetaData parent_meta_data;
//
//  GetParentAndGrandparent(path, &grandparent, &parent, &parent_meta_data);
//  parent.first.listing->AddChild(meta_data);
//
//  if (IsDirectory(meta_data)) {
//    DirectoryData directory(parent.first.listing->directory_id(),
//                            std::make_shared<DirectoryListing>(*meta_data.directory_id));
//    try {
//      PutToStorage(std::make_pair(directory, directory_type));
//    }
//    catch(const std::exception& exception) {
//      parent.first.listing->RemoveChild(meta_data);
//      boost::throw_exception(exception);
//    }
//  }
//
//  parent_meta_data.UpdateLastModifiedTime();
//
//#ifndef MAIDSAFE_WIN32
//  parent_meta_data.attributes.st_ctime = parent_meta_data.attributes.st_mtime;
//  if (IsDirectory(meta_data))
//    ++parent_meta_data.attributes.st_nlink;
//#endif
//  grandparent.first.listing->UpdateChild(parent_meta_data);
//
//  try {
//    PutToStorage(parent);
//  }
//  catch(const std::exception& exception) {
//    parent.first.listing->RemoveChild(meta_data);
//    boost::throw_exception(exception);
//  }
//
//  PutToStorage(grandparent);
//
//  if (grandparent_id)
//    *grandparent_id = grandparent.first.listing->directory_id();
//  if (parent_id)
//    *parent_id = parent.first.listing->directory_id();
//}
//
//template<typename Storage>
//void DirectoryListingHandler<Storage>::DeleteElement(const boost::filesystem::path& path,
//                                                     MetaData& meta_data) {
//  SCOPED_PROFILE
//  DirectoryData grandparent, parent;
//  MetaData parent_meta_data;
//  GetParentAndGrandparent(path, &grandparent, &parent, &parent_meta_data);
//  parent.first.listing->GetChild(path.filename(), meta_data);
//
//  if (IsDirectory(meta_data)) {
//    DirectoryData directory(GetFromPath(path));
//    DeleteFromStorage(parent.first.listing->directory_id(),
//                 *meta_data.directory_id,
//                 directory.second);
//  }
//
//  parent.first.listing->RemoveChild(meta_data);
//  parent_meta_data.UpdateLastModifiedTime();
//
//#ifndef MAIDSAFE_WIN32
//  parent_meta_data.attributes.st_ctime = parent_meta_data.attributes.st_mtime;
//  if (IsDirectory(meta_data))
//    --parent_meta_data.attributes.st_nlink;
//#endif
//
//  try {
//    grandparent.first.listing->UpdateChild(parent_meta_data);
//  }
//  catch(...) { /*Non-critical*/ }
//
//#ifndef MAIDSAFE_WIN32
//  PutToStorage(grandparent);
//#endif
//  PutToStorage(parent);
//}
//
//template<typename Storage>
//void DirectoryListingHandler<Storage>::RenameElement(const boost::filesystem::path& old_path,
//                                                     const boost::filesystem::path& new_path,
//                                                     MetaData& meta_data,
//                                                     int64_t& reclaimed_space) {
//  SCOPED_PROFILE
//  if (old_path == new_path)
//    return;
//  if (!CanRename(old_path, new_path))
//    ThrowError(CommonErrors::invalid_parameter);
//
//  if (old_path.parent_path() == new_path.parent_path())
//    RenameSameParent(old_path, new_path, meta_data, reclaimed_space);
//  else
//    RenameDifferentParent(old_path, new_path, meta_data, reclaimed_space);
//}
//
//template<typename Storage>
//void DirectoryListingHandler<Storage>::RenameSameParent(
//        const boost::filesystem::path& old_path,
//        const boost::filesystem::path& new_path,
//        MetaData& meta_data,
//        int64_t& reclaimed_space) {
//  DirectoryData grandparent, parent;
//  MetaData parent_meta_data;
//  GetParentAndGrandparent(old_path, &grandparent, &parent, &parent_meta_data);
//
//#ifndef MAIDSAFE_WIN32
//  struct stat old;
//  old.st_ctime = meta_data.attributes.st_ctime;
//  old.st_mtime = meta_data.attributes.st_mtime;
//  time(&meta_data.attributes.st_mtime);
//  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
//#endif
//
//  if (!parent.first.listing->HasChild(new_path.filename())) {
//    parent.first.listing->RemoveChild(meta_data);
//    meta_data.name = new_path.filename();
//    parent.first.listing->AddChild(meta_data);
//  } else {
//    MetaData old_meta_data;
//    try {
//      parent.first.listing->GetChild(new_path.filename(), old_meta_data);
//    }
//    catch(const std::exception& exception) {
//#ifndef MAIDSAFE_WIN32
//      meta_data.attributes.st_ctime = old.st_ctime;
//      meta_data.attributes.st_mtime = old.st_mtime;
//#endif
//      boost::throw_exception(exception);
//    }
//    parent.first.listing->RemoveChild(old_meta_data);
//    reclaimed_space = old_meta_data.GetAllocatedSize();
//    parent.first.listing->RemoveChild(meta_data);
//    meta_data.name = new_path.filename();
//    parent.first.listing->AddChild(meta_data);
//  }
//
//#ifdef MAIDSAFE_WIN32
//  GetSystemTimeAsFileTime(&parent_meta_data.last_write_time);
//#else
//  parent_meta_data.attributes.st_ctime =
//      parent_meta_data.attributes.st_mtime =
//      meta_data.attributes.st_mtime;
////   if (!same_parent && IsDirectory(meta_data)) {
////     --parent_meta_data.attributes.st_nlink;
////     ++new_parent_meta_data.attributes.st_nlink;
////     new_parent_meta_data.attributes.st_ctime =
////         new_parent_meta_data.attributes.st_mtime =
////         parent_meta_data.attributes.st_mtime;
////   }
//#endif
//  PutToStorage(parent);
//
//#ifndef MAIDSAFE_WIN32
//  try {
//    grandparent.first.listing->UpdateChild(parent_meta_data);
//  }
//  catch(...) { /*Non-critical*/ }
//  PutToStorage(grandparent);
//#endif
//}
//
//template<typename Storage>
//void DirectoryListingHandler<Storage>::RenameDifferentParent(
//    const boost::filesystem::path& old_path,
//    const boost::filesystem::path& new_path,
//    MetaData& meta_data,
//    int64_t& reclaimed_space) {
//  DirectoryData old_grandparent, old_parent, new_grandparent, new_parent;
//  MetaData old_parent_meta_data, new_parent_meta_data;
//  GetParentAndGrandparent(old_path,
//                          &old_grandparent,
//                          &old_parent,
//                          &old_parent_meta_data);
//  GetParentAndGrandparent(new_path,
//                          &new_grandparent,
//                          &new_parent,
//                          &new_parent_meta_data);
//#ifndef MAIDSAFE_WIN32
//  struct stat old;
//  old.st_ctime = meta_data.attributes.st_ctime;
//  old.st_mtime = meta_data.attributes.st_mtime;
//  time(&meta_data.attributes.st_mtime);
//  meta_data.attributes.st_ctime = meta_data.attributes.st_mtime;
//#endif
//
//  if (IsDirectory(meta_data)) {
//    DirectoryData directory(GetFromPath(old_path));
//    if (directory.second != new_parent.second)  {
//      directory.first.listing->ResetChildrenIterator();
//      MetaData child_meta_data;
//      while (directory.first.listing->GetChildAndIncrementItr(child_meta_data)) {
//        if (IsDirectory(child_meta_data)) {
//          ReStoreDirectories(old_path / child_meta_data.name, new_parent.second);
//        }
//      }
//    }
//    DeleteFromStorage(directory.first.parent_id,
//                 directory.first.listing->directory_id(),
//                 directory.second);
//    directory.first.parent_id = new_parent.first.listing->directory_id();
//    directory.second = new_parent.second;
//    PutToStorage(directory);
//  }
//
//  old_parent.first.listing->RemoveChild(meta_data);
//
//  if (!new_parent.first.listing->HasChild(new_path.filename())) {
//    meta_data.name = new_path.filename();
//    new_parent.first.listing->AddChild(meta_data);
//  } else {
//    MetaData old_meta_data;
//    try {
//      new_parent.first.listing->GetChild(new_path.filename(), old_meta_data);
//    }
//    catch(const std::exception& exception) {
//#ifndef MAIDSAFE_WIN32
//      meta_data.attributes.st_ctime = old.st_ctime;
//      meta_data.attributes.st_mtime = old.st_mtime;
//#endif
//      boost::throw_exception(exception);
//    }
//    new_parent.first.listing->RemoveChild(old_meta_data);
//    reclaimed_space = old_meta_data.GetAllocatedSize();
//    meta_data.name = new_path.filename();
//    new_parent.first.listing->AddChild(meta_data);
//  }
//
//#ifdef MAIDSAFE_WIN32
//  GetSystemTimeAsFileTime(&old_parent_meta_data.last_write_time);
//#else
//  old_parent_meta_data.attributes.st_ctime =
//      old_parent_meta_data.attributes.st_mtime =
//      meta_data.attributes.st_mtime;
//  if (IsDirectory(meta_data)) {
//    --old_parent_meta_data.attributes.st_nlink;
//    ++new_parent_meta_data.attributes.st_nlink;
//    new_parent_meta_data.attributes.st_ctime =
//        new_parent_meta_data.attributes.st_mtime =
//        old_parent_meta_data.attributes.st_mtime;
//  }
//#endif
//  PutToStorage(old_parent);
//  PutToStorage(new_parent);
//
//#ifndef MAIDSAFE_WIN32
//  try {
//    old_grandparent.first.listing->UpdateChild(old_parent_meta_data);
//  }
//  catch(...) { /*Non-critical*/ }
//  PutToStorage(old_grandparent);
//#endif
//}
//
//template<typename Storage>
//void DirectoryListingHandler<Storage>::ReStoreDirectories(const boost::filesystem::path& path,
//                                                          DataTagValue directory_type) {
//  DirectoryData directory(GetFromPath(path));
//  directory.first.listing->ResetChildrenIterator();
//  MetaData meta_data;
//
//  while (directory.first.listing->GetChildAndIncrementItr(meta_data)) {
//    if (IsDirectory(meta_data)) {
//      ReStoreDirectories(path / meta_data.name, directory_type);
//    }
//  }
//
//  DeleteFromStorage(directory.first.parent_id,
//               directory.first.listing->directory_id(),
//               directory.second);
//  directory.second = directory_type;
//  PutToStorage(directory);
//}
//
//template<typename Storage>
//void DirectoryListingHandler<Storage>::UpdateParentDirectoryListing(
//        const boost::filesystem::path& parent_path,
//        MetaData meta_data) {
//  SCOPED_PROFILE
//  DirectoryData parent = GetFromPath(parent_path);
//  parent.first.listing->UpdateChild(meta_data);
//  PutToStorage(parent);
//}
//
//inline bool IsDirectory(const MetaData& meta_data) {
//  return static_cast<bool>(meta_data.directory_id);
//}
//
//template<typename Storage>
//void DirectoryListingHandler<Storage>::GetParentAndGrandparent(const boost::filesystem::path& path,
//                                                               DirectoryData* grandparent,
//                                                               DirectoryData* parent,
//                                                               MetaData* parent_meta_data) {
//  *grandparent = GetFromPath(path.parent_path().parent_path());
//  grandparent->first.listing->GetChild(path.parent_path().filename(), *parent_meta_data);
//  if (!(parent_meta_data->directory_id)) {
//    ThrowError(CommonErrors::invalid_parameter);
//  }
//  *parent = GetFromPath(path.parent_path());
//}

//// If the target is a file it can be deleted.  On POSIX, if it's a non-empty
//// directory, it can be deleted.
//#ifndef MAIDSAFE_WIN32
//template<typename Storage>
//bool DirectoryListingHandler<Storage>::RenameTargetCanBeRemoved(
//    const boost::filesystem::path& new_path,
//    const MetaData& target_meta_data) {
//  bool can_be_removed = !IsDirectory(target_meta_data);
//  if (!can_be_removed) {
//    auto target_directory_listing(GetFromPath(new_path).first.listing);
//    if (target_directory_listing)
//      can_be_removed = target_directory_listing->empty();
//  }
//  return can_be_removed;
//}
//#endif
//
//template<typename Storage>
//DataTagValue DirectoryListingHandler<Storage>::GetDirectoryType(
//    const boost::filesystem::path& path) {
//  get from root_handler kValues
//}
//
//template<>
//DataTagValue DirectoryListingHandler<data_store::SureFileStore>::GetDirectoryType(
//    const boost::filesystem::path& /*path*/);
//
//template<typename Storage>
//bool DirectoryListingHandler<Storage>::CanAdd(const boost::filesystem::path& path) {
//  auto directory_type(GetDirectoryType(path));
//  if (directory_type == DataTagValue::kGroupDirectoryValue || (directory_type == DataTagValue::kWorldDirectoryValue && !world_is_writeable_))
//    return false;
//  fs::path relative_parent_filename(path.parent_path().filename());
//  return !relative_parent_filename.empty() && relative_parent_filename != kRoot;
//}
//
//template<typename Storage>
//bool DirectoryListingHandler<Storage>::CanDelete(const fs::path& path) {
//  SCOPED_PROFILE
//  auto directory_type(GetDirectoryType(path));
//  if (directory_type == DataTagValue::kGroupDirectoryValue ||
//      (directory_type == DataTagValue::kWorldDirectoryValue && !world_is_writeable_)) {
//    return false;
//  }
//  boost::filesystem::path path_parent_filename(path.parent_path().filename());
//  return !path_parent_filename.empty() &&
//         path_parent_filename != kRoot &&
//         !(path_parent_filename == "World" && path.filename() == "Services");
//}
//
//template<typename Storage>
//bool DirectoryListingHandler<Storage>::CanRename(const boost::filesystem::path& from_path,
//                                                 const boost::filesystem::path& to_path) {
//  boost::filesystem::path from_filename(from_path.filename()), to_filename(to_path.filename()),
//           from_parent_filename(from_path.parent_path().filename()),
//           to_parent_filename(to_path.parent_path().filename());
//  if ((from_filename == kRoot || to_filename == kRoot)
//      || (from_parent_filename == kRoot || to_parent_filename == kRoot)
//      || (from_filename == "Owner" && from_parent_filename == kRoot)
//      || (to_filename == "Owner" && to_parent_filename == kRoot)
//      || (from_filename == "Group" && from_parent_filename == kRoot)
//      || (to_filename == "Group" && to_parent_filename == kRoot)
//      || (from_filename == "World" && from_parent_filename == kRoot)
//      || (to_filename == "World" && to_parent_filename == kRoot))
//    return false;
//  auto from_type(GetDirectoryType(from_path)), to_type(GetDirectoryType(to_path));
//  if (from_type != to_type) {
//    if (from_type == DataTagValue::kGroupDirectoryValue ||
//        to_type == DataTagValue::kGroupDirectoryValue ||
//            (from_type != DataTagValue::kWorldDirectoryValue &&
//            to_type == DataTagValue::kWorldDirectoryValue && !world_is_writeable_)) {
//      return false;
//    }
//  }
//  return from_type != DataTagValue::kWorldDirectoryValue ||
//         from_parent_filename != "World" ||
//         from_filename != "Services";
//}

template<typename Storage>
void DirectoryListingHandler<Storage>::SetWorldReadWrite() {
  world_is_writeable_ = true;
}

template<typename Storage>
void DirectoryListingHandler<Storage>::SetWorldReadOnly() {
  world_is_writeable_ = false;
}































template<typename Storage>
void PutToStorage(Storage& storage, const DirectoryData& directory) {
  auto serialised_directory_listing(directory.first.listing->Serialise());

  if (directory.type == DataTagValue::kWorldDirectoryValue) {
    try {
      DeleteFromStorage(storage, directory.first.parent_id, directory.first.listing->directory_id(),
                   DataTagValue::kWorldDirectoryValue);
    }
    catch(...) {}
    // Store serialised listing.
    WorldDirectory world_directory(WorldDirectory::Name(directory.first.listing->directory_id()),
                                   NonEmptyString(serialised_directory_listing));
    Put<Storage, WorldDirectory>()(*storage, world_directory);
    return;
  }

  if (directory.second == DataTagValue::kOwnerDirectoryValue) {
    try {
      DeleteFromStorage(directory.first.parent_id, directory.first.listing->directory_id(), DataTagValue::kOwnerDirectoryValue);
    }
    catch(...) {}
  } else {
    try {
      DeleteFromStorage(directory.first.parent_id, directory.first.listing->directory_id(), DataTagValue::kGroupDirectoryValue);
    }
    catch(...) {}
  }

  // Self-encrypt serialised directory listing.
  DataMapPtr data_map(new encrypt::DataMap);
  {
    encrypt::SelfEncryptor<Storage> self_encryptor(data_map, *storage_);
    assert(serialised_directory_listing.size() <= std::numeric_limits<uint32_t>::max());
    if (!self_encryptor.Write(serialised_directory_listing.c_str(),
                              static_cast<uint32_t>(serialised_directory_listing.size()),
                              0)) {
      ThrowError(CommonErrors::invalid_parameter);
    }
  }
  // Encrypt directory listing's datamap.
  asymm::CipherText encrypted_data_map =
                      encrypt::EncryptDataMap(directory.first.parent_id,
                                              directory.first.listing->directory_id(),
                                              data_map);
  if (directory.second == DataTagValue::kOwnerDirectoryValue) {
    // Store the encrypted datamap.
    OwnerDirectory owner_directory(OwnerDirectory::Name(directory.first.listing->directory_id()),
                                   encrypted_data_map);
    Put<Storage, OwnerDirectory>()(*storage_, owner_directory);
  } else if (directory.second == DataTagValue::kGroupDirectoryValue) {
    // Store the encrypted datamap.
    GroupDirectory group_directory(GroupDirectory::Name(directory.first.listing->directory_id()),
                                   encrypted_data_map);
    Put<Storage, GroupDirectory>()(*storage_, group_directory);
  } else {
    ThrowError(CommonErrors::not_a_directory);
  }
}

template<typename Storage>
DirectoryData GetDirectoryFromStorage(Storage& storage,
                                      const DirectoryId& parent_id,
                                      const DirectoryId& directory_id,
                                      DataTagValue directory_type) {
  return GetDirectoryFromStorage(storage, parent_id, directory_id, directory_type,
                                 is_encrypted_dir<directory_type>());
}


template<typename Storage>
DirectoryData GetDirectoryFromStorage(Storage& storage,
                                      const DirectoryId& parent_id,
                                      const DirectoryId& directory_id,
                                      DataTagValue directory_type,
                                      std::true_type /*is_encrypted*/) {
  DirectoryData directory(parent_id, nullptr, nullptr, directory_type);
  // Retrieve encrypted datamap.
  if (directory_type == DataTagValue::kOwnerDirectoryValue) {
    directory.data_map = GetDataMapFromStorage<Storage, OwnerDirectory>(storage, parent_id,
                                                                        directory_id);
  } else if (directory_type == DataTagValue::kGroupDirectoryValue) {
    directory.data_map = GetDataMapFromStorage<Storage, GroupDirectory>(storage, parent_id,
                                                                        directory_id);
  } else {
    ThrowError(CommonErrors::invalid_parameter);
  }
  // Decrypt serialised directory listing.
  encrypt::SelfEncryptor<Storage> self_encryptor(data_map, storage);
  uint32_t data_map_chunks_size(static_cast<uint32_t>(data_map->chunks.size()));
  uint32_t data_map_size;
  if (data_map_chunks_size != 0) {
    data_map_size = (data_map_chunks_size - 1) * data_map->chunks[0].size +
                    data_map->chunks.rbegin()->size;
  } else {
    data_map_size = static_cast<uint32_t>(data_map->content.size());
  }
  std::vector<char> serialised_listing(data_map_size);
  if (!self_encryptor.Read(&serialised_listing[0], data_map_size, 0))
    ThrowError(CommonErrors::invalid_parameter);

  // Parse serialised directory listing.
  directory.listing = std::make_shared<DirectoryListing>(std::string(std::begin(serialised_listing),
                                                                     std::end(serialised_listing)));
  return directory;
}

template<typename Storage>
DirectoryData GetDirectoryFromStorage(Storage& storage,
                                      const DirectoryId& parent_id,
                                      const DirectoryId& directory_id,
                                      DataTagValue directory_type,
                                      std::false_type /*is_not_encrypted*/) {
  if (directory_type != DataTagValue::kWorldDirectoryValue)
    ThrowError(CommonErrors::invalid_parameter);

  WorldDirectory::Name name(directory_id);
  WorldDirectory::serialised_type serialised_directory(
      Get<Storage, WorldDirectory>()(storage, name));
  WorldDirectory world_directory(name, serialised_directory);
  return DirectoryData(parent_id,
                       std::make_shared<DirectoryListing>(world_directory.data().string()),
                       nullptr,
                       DataTagValue::kWorldDirectoryValue);
}

template<typename Storage, typename DirectoryType>
DataMapPtr GetDataMapFromStorage(Storage& storage,
                                 const DirectoryId& parent_id,
                                 const DirectoryId& directory_id) {
  assert(is_encrypted_dir<DirectoryType::Tag::kValue>::value);
  typename DirectoryType::Name name(directory_id);
  typename DirectoryType::serialised_type serialised_data(
      Get<Storage, DirectoryType>()(storage, name));
  DirectoryType directory(name, serialised_data);

  auto data_map(std::make_shared<encrypt::DataMap>());
  encrypt::DecryptDataMap(parent_id, directory_id, directory.data().string(), data_map);
  return data_map;
}

template<typename Storage>
void DeleteFromStorage(Storage& storage, const DirectoryData& directory) {
  if (directory_type != DataTagValue::kWorldDirectoryValue) {
    auto data_map(GetDataMapFromStorage(parent_id, directory_id, directory_type));
    encrypt::SelfEncryptor<Storage> self_encryptor(data_map, storage);
    self_encryptor.DeleteAllChunks();
  }
  switch (directory_type) {
    case DataTagValue::kOwnerDirectoryValue: {
      OwnerDirectory::Name name(directory_id);
      Delete<Storage, OwnerDirectory>()(*storage_, name);
      break;
    }
    case DataTagValue::kGroupDirectoryValue: {
      GroupDirectory::Name name(directory_id);
      Delete<Storage, GroupDirectory>()(*storage_, name);
      break;
    }
    case DataTagValue::kWorldDirectoryValue: {
      WorldDirectory::Name name(directory_id);
      Delete<Storage, WorldDirectory>()(*storage_, name);
      break;
    }
    default:
      LOG(kError) << "Invalid directory type.";
  }
}


}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_LISTING_HANDLER_H_
