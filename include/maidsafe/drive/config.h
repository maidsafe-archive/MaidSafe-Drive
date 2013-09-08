/*  Copyright 2011 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.novinet.com/license

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#ifndef MAIDSAFE_DRIVE_CONFIG_H_
#define MAIDSAFE_DRIVE_CONFIG_H_

#include <functional>
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
typedef std::function<void()> OnServiceAdded;
typedef std::function<void(const boost::filesystem::path& /*service_alias*/)> OnServiceRemoved;
typedef std::function<void(const boost::filesystem::path& /*old_service_alias*/,
                           const boost::filesystem::path& /*new_service_alias*/)> OnServiceRenamed;

namespace detail {

class DirectoryListing;

enum OpType : int32_t { kCreated, kRenamed, kAdded, kRemoved, kMoved, kModified };

typedef std::shared_ptr<encrypt::DataMap> DataMapPtr;

extern const boost::filesystem::path kMsHidden;
extern const boost::filesystem::path kRoot;

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_CONFIG_H_
