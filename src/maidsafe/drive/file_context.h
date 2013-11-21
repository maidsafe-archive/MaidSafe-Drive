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

#ifndef MAIDSAFE_DRIVE_FILE_CONTEXT_H_
#define MAIDSAFE_DRIVE_FILE_CONTEXT_H_

#include <deque>
#include <memory>

#include "boost/filesystem/path.hpp"

#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/meta_data.h"

namespace maidsafe {

namespace drive {

namespace detail {

struct FileContext {
  FileContext() : meta_data(), self_encryptor(), meta_data_changed(false) {}
  explicit FileContext(MetaData meta_data_in)
      : meta_data(std::move(meta_data_in)), self_encryptor(), meta_data_changed(false) {}
  FileContext(const boost::filesystem::path& name, bool is_directory)
      : meta_data(name, is_directory), self_encryptor(), meta_data_changed(false) {}

  MetaData meta_data;
  std::unique_ptr<encrypt::SelfEncryptor> self_encryptor;
  bool meta_data_changed;
};

inline bool operator<(const FileContext& lhs, const FileContext& rhs) {
  return lhs.meta_data.name < rhs.meta_data.name;
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_FILE_CONTEXT_H_
