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

#ifndef MAIDSAFE_DRIVE_DRIVE_H_
#define MAIDSAFE_DRIVE_DRIVE_H_

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include "boost/thread/future.hpp"

#include "maidsafe/common/asio_service.h"
#include "maidsafe/common/rsa.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/tools/launcher.h"

namespace maidsafe {

namespace drive {

template <typename Storage>
class Drive {
 public:
  Identity root_parent_id() const;
  boost::future<void> GetMountFuture();

  virtual ~Drive();

  void Unmount();
  void Mount();

 protected:
  Drive(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
        const Identity& root_parent_id, const boost::filesystem::path& mount_dir,
        const boost::filesystem::path& user_app_dir,
        std::string mount_status_shared_object_name, bool create);

  template <typename T = detail::Path>
  typename std::enable_if<std::is_base_of<detail::Path, T>::value, const std::shared_ptr<const T>>::type
  GetContext(const boost::filesystem::path& relative_path) const;
  template <typename T = detail::Path>
  typename std::enable_if<std::is_base_of<detail::Path, T>::value, std::shared_ptr<T>>::type
  GetMutableContext(const boost::filesystem::path& relative_path);
  void Create(const boost::filesystem::path& relative_path,
              std::shared_ptr<detail::Path> path);
  void Open(detail::File& file);
  void ReleaseDir(const boost::filesystem::path& relative_path);
  void Delete(const boost::filesystem::path& relative_path);
  void Rename(const boost::filesystem::path& old_relative_path,
              const boost::filesystem::path& new_relative_path);

  detail::MetaData::Permissions get_base_file_permissions() const;

  const boost::filesystem::path kMountDir_;
  const boost::filesystem::path kUserAppDir_;
  const std::unique_ptr<const boost::filesystem::path,
                        std::function<void(const boost::filesystem::path* const)>> kBufferRoot_;
  const std::string kMountStatusSharedObjectName_;
  boost::promise<void> mount_promise_;
  std::once_flag unmounted_once_flag_;

 private:
  virtual void DoMount() = 0;
  virtual void DoUnmount() = 0;

 private:
  typedef detail::File::Buffer Buffer;

  std::function<NonEmptyString(const std::string&)> get_chunk_from_store_;
  MemoryUsage default_max_buffer_memory_;
  DiskUsage default_max_buffer_disk_;

  const detail::MetaData::Permissions base_file_permissions_;

 protected:
  AsioService asio_service_;
  const std::shared_ptr<detail::DirectoryHandler<Storage>> directory_handler_;
};

// ==================== Implementation =============================================================
template <typename Storage>
Drive<Storage>::Drive(std::shared_ptr<Storage> storage, const Identity& unique_user_id,
                      const Identity& root_parent_id, const boost::filesystem::path& mount_dir,
                      const boost::filesystem::path& user_app_dir,
                      std::string mount_status_shared_object_name, bool create)
    : kMountDir_(mount_dir),
      kUserAppDir_(user_app_dir),
      kBufferRoot_(new boost::filesystem::path(user_app_dir / "Buffers"),
                   [](const boost::filesystem::path* const delete_path) {
                     if (!delete_path->empty()) {
                       boost::system::error_code ec;
                       if (boost::filesystem::remove_all(*delete_path, ec) == 0)
                         LOG(kWarning) << "Failed to remove " << *delete_path;
                       if (ec.value() != 0)
                         LOG(kWarning) << "Error removing " << *delete_path << "  " << ec.message();
                     }
                     delete delete_path;
                   }),
      kMountStatusSharedObjectName_(std::move(mount_status_shared_object_name)),
      mount_promise_(),
      unmounted_once_flag_(),
      get_chunk_from_store_(),
      // TODO(Fraser#5#): 2013-11-27 - BEFORE_RELEASE - confirm the following 2 variables.
      default_max_buffer_memory_(Concurrency() * 1024 * 1024),  // cores * default chunk size
      default_max_buffer_disk_(static_cast<uint64_t>(
          boost::filesystem::space(kUserAppDir_).available / 10)),
      base_file_permissions_(
          detail::MetaData::Permissions::owner_read |
          detail::MetaData::Permissions::owner_write),
      asio_service_(2),
      directory_handler_(
          detail::DirectoryHandler<Storage>::Create(
              storage, unique_user_id, root_parent_id,
              boost::filesystem::unique_path(*kBufferRoot_ / "%%%%%-%%%%%-%%%%%-%%%%%"),
              create, asio_service_.service())) {
  assert(storage != nullptr);
  get_chunk_from_store_ = [storage](const std::string& name) {
    try {
      auto chunk(storage->Get(ImmutableData::Name(Identity(name))).get());
      return chunk.data();
    }
    catch (const std::exception& e) {
      LOG(kError) << "Failed to get chunk from storage: " << e.what();
      throw;
    }
  };
}

template <typename Storage>
Drive<Storage>::~Drive() {
  try {
    asio_service_.Stop();
    assert(directory_handler_ != nullptr);
    directory_handler_->StoreAll();
  }
  catch (...) {
  }
}

template<typename Storage>
void Drive<Storage>::Unmount() {
  asio_service_.Stop();
  DoUnmount();
}

template<typename Storage>
void Drive<Storage>::Mount() {
  DoMount();
}

template <typename Storage>
Identity Drive<Storage>::root_parent_id() const {
  return directory_handler_->root_parent_id();
}

template <typename Storage>
boost::future<void> Drive<Storage>::GetMountFuture() {
  return mount_promise_.get_future();
}

template <typename Storage>
template <typename T>
typename std::enable_if<std::is_base_of<detail::Path, T>::value, const std::shared_ptr<const T>>::type
Drive<Storage>::GetContext(const boost::filesystem::path& relative_path) const {
  const auto parent(directory_handler_->template Get<detail::Directory>(relative_path.parent_path()));
  assert(parent != nullptr);
  return parent->template GetChild<T>(relative_path.filename());
}

template <typename Storage>
template <typename T>
typename std::enable_if<std::is_base_of<detail::Path, T>::value, std::shared_ptr<T>>::type
Drive<Storage>::GetMutableContext(const boost::filesystem::path& relative_path) {
  SCOPED_PROFILE
  const auto parent(directory_handler_->template Get<detail::Directory>(relative_path.parent_path()));
  assert(parent != nullptr);
  return parent->template GetMutableChild<T>(relative_path.filename());
}

template <typename Storage>
void Drive<Storage>::Create(const boost::filesystem::path& relative_path,
                            std::shared_ptr<detail::Path> path) {
  if (path->meta_data.file_type() == detail::MetaData::FileType::regular_file) {
    auto file = std::dynamic_pointer_cast<detail::File>(path);
    assert(file != nullptr);
    Open(*file);
  }
  directory_handler_->Add(relative_path, path);
}

template <typename Storage>
void Drive<Storage>::Open(detail::File& file) {
  assert(kBufferRoot_ != nullptr);
  file.Open(
      get_chunk_from_store_, default_max_buffer_memory_, default_max_buffer_disk_, *kBufferRoot_);
}

template <typename Storage>
void Drive<Storage>::ReleaseDir(const boost::filesystem::path& relative_path) {
  SCOPED_PROFILE
  auto directory(directory_handler_->template Get<detail::Directory>(relative_path));
  directory->ResetChildrenCounter();
}

template <typename Storage>
void Drive<Storage>::Delete(const boost::filesystem::path& relative_path) {
  directory_handler_->Delete(relative_path);
}

template <typename Storage>
void Drive<Storage>::Rename(const boost::filesystem::path& old_relative_path,
                            const boost::filesystem::path& new_relative_path) {
  directory_handler_->Rename(old_relative_path, new_relative_path);
}

template <typename Storage>
detail::MetaData::Permissions Drive<Storage>::get_base_file_permissions() const {
  return base_file_permissions_;
}

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DRIVE_H_
