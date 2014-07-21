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

#ifndef MAIDSAFE_DRIVE_FILE_H_
#define MAIDSAFE_DRIVE_FILE_H_

#include <memory>
#include <string>

#include "boost/asio/steady_timer.hpp"
#include "boost/filesystem/path.hpp"

#include "maidsafe/common/config.h"
#include "maidsafe/common/data_buffer.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/path.h"

namespace maidsafe {

namespace drive {

namespace detail {

class Directory;

class File : public Path {
 public:
  typedef DataBuffer<std::string> Buffer;

  // This class must always be constructed using a Create() call to ensure that it will be
  // a shared_ptr. See the private constructors for the argument lists.
  template <typename... Types>
  static std::shared_ptr<File> Create(Types&&... args) {
    std::shared_ptr<File> self(new File{std::forward<Types>(args)...});
    return self;
  }

  ~File();

  void Flush();
  void ScheduleForStoring();

  std::unique_ptr<Buffer> buffer;
  std::unique_ptr<encrypt::SelfEncryptor> self_encryptor;
  std::unique_ptr<boost::asio::steady_timer> timer;
  std::unique_ptr<std::atomic<int>> open_count;
  bool flushed;

 private:
  File();
  File(MetaData meta_data_in, std::shared_ptr<Directory> parent_in);
  File(const boost::filesystem::path& name, bool is_directory);
  File(File&&) = delete;
  File& operator=(File) = delete;
};

// void swap(File& lhs, File& rhs) MAIDSAFE_NOEXCEPT;

bool operator<(const File& lhs, const File& rhs);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_FILE_H_
