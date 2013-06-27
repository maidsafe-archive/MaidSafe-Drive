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

#ifndef MAIDSAFE_DRIVE_UTILS_H_
#define MAIDSAFE_DRIVE_UTILS_H_


#include <memory>
#include <string>
#include <vector>

#include "boost/filesystem/path.hpp"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory_listing.h"

#include "maidsafe/encrypt/self_encryptor.h"

namespace fs = boost::filesystem;


namespace maidsafe {

namespace drive {

static const uint32_t kDirectorySize = 4096;

struct FileContext {
  FileContext();
  FileContext(const fs::path& name, bool is_directory);
  explicit FileContext(std::shared_ptr<MetaData> meta_data_in);

  std::shared_ptr<MetaData> meta_data;
  SelfEncryptorPtr self_encryptor;
  bool content_changed;
  DirectoryId grandparent_directory_id, parent_directory_id;
};

int ForceFlush(DirectoryListingHandlerPtr directory_listing_handler,
               FileContext* file_context);

bool ExcludedFilename(const fs::path& path);

bool MatchesMask(std::wstring mask, const fs::path& file_name);
bool SearchesMask(std::wstring mask, const fs::path& file_name);

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_UTILS_H_
