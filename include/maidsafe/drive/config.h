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

#ifndef MAIDSAFE_DRIVE_CONFIG_H_
#define MAIDSAFE_DRIVE_CONFIG_H_

#include <memory>
#include <string>

#include "boost/filesystem/path.hpp"

#include "maidsafe/common/types.h"


namespace testing { class AssertionResult; }

namespace maidsafe {

namespace encrypt {
struct DataMap;
template<typename Storage> class SelfEncryptor;
}  // namespace encrypt

namespace drive {

struct MetaData;
typedef Identity DirectoryId;

namespace detail {

class DirectoryListing;

enum OpType { kCreated, kRenamed, kAdded, kRemoved, kMoved, kModified };

typedef std::shared_ptr<encrypt::DataMap> DataMapPtr;

typedef std::shared_ptr<DirectoryId> DirectoryIdPtr;
typedef std::shared_ptr<DirectoryListing> DirectoryListingPtr;

extern const boost::filesystem::path kMsHidden;
extern const boost::filesystem::path kEmptyPath;
extern const boost::filesystem::path kRoot;
extern const boost::filesystem::path kOwner;
extern const boost::filesystem::path kGroup;
extern const boost::filesystem::path kWorld;
extern const boost::filesystem::path kServices;

namespace test {
testing::AssertionResult DirectoriesMatch(DirectoryListingPtr directory1,
                                          DirectoryListingPtr directory2);
}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_CONFIG_H_
