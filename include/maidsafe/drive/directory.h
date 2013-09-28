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

#ifndef MAIDSAFE_DRIVE_DIRECTORY_H_
#define MAIDSAFE_DRIVE_DIRECTORY_H_

#include <memory>

#include "maidsafe/data_types/data_type_values.h"

#include "maidsafe/encrypt/data_map.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory_listing.h"

namespace maidsafe {

namespace drive {

namespace detail {

struct Directory {
  Directory(DirectoryId parent_id_in, std::shared_ptr<DirectoryListing> listing_in,
            std::shared_ptr<encrypt::DataMap> data_map_in, DataTagValue type_in)
      : parent_id(std::move(parent_id_in)),
        listing(std::move(listing_in)),
        data_map(std::move(data_map_in)),
        type(type_in),
        content_changed(false) {
    assert(type == DataTagValue::kOwnerDirectoryValue ||
           type == DataTagValue::kGroupDirectoryValue ||
           type == DataTagValue::kWorldDirectoryValue);
  }

  Directory()
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

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_H_
