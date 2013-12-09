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

#ifndef MAIDSAFE_DRIVE_CONFIG_H_
#define MAIDSAFE_DRIVE_CONFIG_H_

#include <cstdint>
#include <chrono>

#include "boost/filesystem/path.hpp"

#include "maidsafe/common/types.h"

namespace maidsafe {

namespace drive {

typedef Identity DirectoryId;

namespace detail {

struct MaxVersionsTag;
struct ParentIdTag;

}  // namespace detail

typedef TaggedValue<uint32_t, detail::MaxVersionsTag> MaxVersions;
typedef TaggedValue<Identity, detail::ParentIdTag> ParentId;

namespace detail {

extern const boost::filesystem::path kRoot;
extern const MaxVersions kMaxVersions;
// The delay between the last update to a directory and the creation of the corresponding version.
extern const std::chrono::steady_clock::duration kInactivityDelay;

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_CONFIG_H_
