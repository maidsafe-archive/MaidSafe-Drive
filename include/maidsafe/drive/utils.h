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

#include "maidsafe/encrypt/self_encryptor.h"
#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"


namespace maidsafe {
namespace drive {
namespace detail {

template <typename Storage>
struct FileContext {
  typedef std::shared_ptr<MetaData> MetaDataPtr;
  typedef std::shared_ptr<encrypt::SelfEncryptor<Storage>> SelfEncryptorPtr;

  FileContext();
  FileContext(const boost::filesystem::path& name, bool is_directory);
  explicit FileContext(MetaDataPtr meta_data_in);

  MetaDataPtr meta_data;
  SelfEncryptorPtr self_encryptor;
  bool content_changed;
  DirectoryId grandparent_directory_id, parent_directory_id;
};

template <typename Storage>
FileContext<Storage>::FileContext()
    : meta_data(new MetaData),
      self_encryptor(),
      content_changed(false),
      grandparent_directory_id(),
      parent_directory_id() {}

template <typename Storage>
FileContext<Storage>::FileContext(const boost::filesystem::path& name, bool is_directory)
    : meta_data(new MetaData(name, is_directory)),
      self_encryptor(),
      content_changed(!is_directory),
      grandparent_directory_id(),
      parent_directory_id() {}

template <typename Storage>
FileContext<Storage>::FileContext(MetaDataPtr meta_data_in)
    : meta_data(meta_data_in),
      self_encryptor(),
      content_changed(false),
      grandparent_directory_id(),
      parent_directory_id() {}


bool ExcludedFilename(const boost::filesystem::path& path);
bool MatchesMask(std::wstring mask, const boost::filesystem::path& file_name);
bool SearchesMask(std::wstring mask, const boost::filesystem::path& file_name);

}  // namespace detail
}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_UTILS_H_
