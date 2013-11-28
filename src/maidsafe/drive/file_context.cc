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

FileContext::FileContext() : meta_data(), buffer(), self_encryptor() {}

FileContext::FileContext(FileContext&& other)
    : meta_data(std::move(other.meta_data)), buffer(std::move(other.buffer)),
      self_encryptor(std::move(other.self_encryptor)) {}

FileContext::FileContext(MetaData meta_data_in)
    : meta_data(std::move(meta_data_in)), buffer(), self_encryptor() {}

FileContext::FileContext(const boost::filesystem::path& name, bool is_directory)
    : meta_data(name, is_directory), buffer(), self_encryptor() {}

FileContext& FileContext::operator=(FileContext other) {
  swap(*this, other);
  return *this;
}

void swap(FileContext& lhs, FileContext& rhs) MAIDSAFE_NOEXCEPT {
  using std::swap;
  swap(lhs.meta_data, rhs.meta_data);
  swap(lhs.buffer, rhs.buffer);
  swap(lhs.self_encryptor, rhs.self_encryptor);
}

bool operator<(const FileContext& lhs, const FileContext& rhs) {
  return lhs.meta_data.name < rhs.meta_data.name;
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
