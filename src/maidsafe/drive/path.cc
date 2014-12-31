/*  Copyright 2014 MaidSafe.net limited

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

#include "maidsafe/drive/path.h"

#include "maidsafe/drive/directory.h"

namespace maidsafe {

namespace drive {

namespace detail {

Path::Path(MetaData::FileType file_type) : meta_data(file_type) {}

Path::Path(std::shared_ptr<Directory> parent, MetaData::FileType file_type)
    : parent_(parent), meta_data(file_type) {}

std::shared_ptr<Directory> Path::Parent() const { return parent_.lock(); }

void Path::SetParent(std::shared_ptr<Directory> parent) { parent_ = parent; }

bool operator<(const Path& lhs, const Path& rhs) {
  return lhs.meta_data.name() < rhs.meta_data.name();
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
