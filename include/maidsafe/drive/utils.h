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

#ifndef MAIDSAFE_DRIVE_UTILS_H_
#define MAIDSAFE_DRIVE_UTILS_H_

#include <memory>
#include <string>
#include <vector>

#include "boost/filesystem/path.hpp"

#include "maidsafe/data_store/sure_file_store.h"
#include "maidsafe/encrypt/self_encryptor.h"
#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"

namespace maidsafe {

namespace drive {

namespace detail {

static const uint32_t kDirectorySize = 4096;

template<typename Storage>
class DirectoryHandler;

template<typename Storage>
struct FileContext {
  FileContext();
  FileContext(const boost::filesystem::path& name, bool is_directory);
  FileContext(const FileContext& other);
  FileContext(FileContext&& other);
  FileContext& operator=(FileContext other);

  std::shared_ptr<MetaData> meta_data;
  std::shared_ptr<encrypt::SelfEncryptor<Storage>> self_encryptor;
  bool content_changed;
  DirectoryId grandparent_directory_id, parent_directory_id;
};

template<typename Storage>
void swap(FileContext<Storage>& lhs, FileContext<Storage>& rhs);

template<typename Storage>
FileContext<Storage>::FileContext()
    : meta_data(new MetaData),
      self_encryptor(),
      content_changed(false),
      grandparent_directory_id(),
      parent_directory_id() {}

template<typename Storage>
FileContext<Storage>::FileContext(const boost::filesystem::path& name, bool is_directory)
    : meta_data(new MetaData(name, is_directory)),
      self_encryptor(),
      content_changed(false),
      grandparent_directory_id(),
      parent_directory_id() {}

template<typename Storage>
FileContext<Storage>::FileContext(const FileContext& other)
    : meta_data(other.meta_data),
      self_encryptor(other.self_encryptor),
      content_changed(other.content_changed),
      grandparent_directory_id(other.grandparent_directory_id),
      parent_directory_id(other.parent_directory_id) {}

template<typename Storage>
FileContext<Storage>::FileContext(FileContext&& other)
    : meta_data(std::move(other.meta_data)),
      self_encryptor(std::move(other.self_encryptor)),
      content_changed(std::move(other.content_changed)),
      grandparent_directory_id(std::move(other.grandparent_directory_id)),
      parent_directory_id(std::move(other.parent_directory_id)) {}

template<typename Storage>
FileContext<Storage>& FileContext<Storage>::operator=(FileContext other) {
  swap(*this, other);
  return *this;
}

template<typename Storage>
void swap(FileContext<Storage>& lhs, FileContext<Storage>& rhs) {
  using std::swap;
  swap(lhs.meta_data, rhs.meta_data);
  swap(lhs.self_encryptor, rhs.self_encryptor);
  swap(lhs.content_changed, rhs.content_changed);
  swap(lhs.grandparent_directory_id, rhs.grandparent_directory_id);
  swap(lhs.parent_directory_id, rhs.parent_directory_id);
}

// TODO(David) Delete
//#ifndef MAIDSAFE_WIN32
//// Not called by Windows...
//template<typename Storage>
//bool ForceFlush(const RootHandler<Storage>& root_handler,
//                FileContext<Storage>* file_context) {
//  assert(file_context);
//  file_context->self_encryptor->Flush();

//  try {
//    root_handler->UpdateParentDirectoryListing(
//        file_context->meta_data->name.parent_path(), *file_context->meta_data.get());
//  } catch(...) {
//      return false;
//  }
//  return true;
//}
//#endif

bool ExcludedFilename(const std::string& file_name);

bool MatchesMask(std::wstring mask, const boost::filesystem::path& file_name);
bool SearchesMask(std::wstring mask, const boost::filesystem::path& file_name);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_UTILS_H_
