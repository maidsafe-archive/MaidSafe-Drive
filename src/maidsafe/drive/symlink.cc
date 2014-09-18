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

#include "maidsafe/drive/symlink.h"

#include "boost/filesystem/operations.hpp"

#include "maidsafe/drive/directory.h"

namespace fs = boost::filesystem;

namespace maidsafe
{
namespace drive
{
namespace detail
{

Symlink::Symlink()
    : Path(fs::symlink_file) {
}

Symlink::Symlink(const fs::path& target,
                 const fs::path& source)
    : Path(fs::symlink_file),
      target(target),
      source(source) {
  meta_data.name = target;
}

bool Symlink::Valid() const {
  return true;
}

std::string Symlink::Serialise() {
  return std::string();
}

void Symlink::Serialise(protobuf::Directory& proto_directory,
                        std::vector<ImmutableData::Name>&,
                        std::unique_lock<std::mutex>&) {
  auto child = proto_directory.add_children();
  Serialise(*child);
}

void Symlink::Serialise(protobuf::Path& proto_path) {
  assert(proto_path.mutable_attributes() != nullptr);
  meta_data.ToProtobuf(*(proto_path.mutable_attributes()));
  proto_path.set_name(meta_data.name.string());
  proto_path.set_link_to(source.string());
}

void Symlink::ScheduleForStoring() {
  std::shared_ptr<Directory> parent = Parent();
  if (parent) {
      parent->ScheduleForStoring();
  }
}

fs::path Symlink::Target() const {
  return target;
}

} // namespace detail
} // namespace drive
} // namespace maidsafe
