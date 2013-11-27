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

#include "maidsafe/drive/file_context.h"

#include <utility>

namespace maidsafe {

namespace drive {

namespace detail {

FileContext::FileContext()
    : meta_data(), data_buffer(), self_encryptor(), meta_data_changed(false) {}

FileContext::FileContext(FileContext&& other)
    : meta_data(std::move(other.meta_data)), data_buffer(std::move(other.data_buffer)),
      self_encryptor(std::move(other.self_encryptor)),
      meta_data_changed(std::move(other.meta_data_changed)) {}

FileContext::FileContext(MetaData meta_data_in)
    : meta_data(std::move(meta_data_in)), data_buffer(), self_encryptor(),
      meta_data_changed(false) {}

FileContext::FileContext(const boost::filesystem::path& name, bool is_directory)
    : meta_data(name, is_directory), data_buffer(), self_encryptor(), meta_data_changed(false) {}

FileContext& FileContext::operator=(FileContext other) {
  swap(*this, other);
  return *this;
}

void swap(FileContext& lhs, FileContext& rhs) MAIDSAFE_NOEXCEPT {
  using std::swap;
  swap(lhs.meta_data, rhs.meta_data);
  swap(lhs.data_buffer, rhs.data_buffer);
  swap(lhs.self_encryptor, rhs.self_encryptor);
  swap(lhs.meta_data_changed, rhs.meta_data_changed);
}

bool operator<(const FileContext& lhs, const FileContext& rhs) {
  return lhs.meta_data.name < rhs.meta_data.name;
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
