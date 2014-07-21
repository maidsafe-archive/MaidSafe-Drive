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

#include "maidsafe/drive/file.h"

#include <utility>

#include "maidsafe/drive/directory.h"

namespace maidsafe {

namespace drive {

namespace detail {

File::File()
    : Path(),
      buffer(), self_encryptor(), timer(), open_count(new std::atomic<int>(0)),
      flushed(false) {}

File::File(MetaData meta_data_in, std::shared_ptr<Directory> parent_in)
    : Path(parent_in),
      buffer(), self_encryptor(), timer(),
      open_count(new std::atomic<int>(0)), flushed(false) {
  meta_data = std::move(meta_data_in);
}

File::File(const boost::filesystem::path& name, bool is_directory)
    : Path(),
      buffer(), self_encryptor(), timer(),
      open_count(new std::atomic<int>(0)), flushed(false) {
  meta_data = MetaData(name, is_directory);
}

File::~File() {
  if (timer) {
    timer->cancel();
    Flush();
  }
}

void File::Flush() {
  std::shared_ptr<Directory> p = Parent();
  if (p) {
      p->FlushChildAndDeleteEncryptor(this);
  }
}

void File::ScheduleForStoring() {
  std::shared_ptr<Directory> p = Parent();
  if (p) {
      p->ScheduleForStoring();
  }
}

bool operator<(const File& lhs, const File& rhs) {
  return lhs.meta_data.name < rhs.meta_data.name;
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
