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

#ifndef MAIDSAFE_DRIVE_DIRECTORY_H_
#define MAIDSAFE_DRIVE_DIRECTORY_H_

#include <chrono>
#include <deque>
#include <memory>
#include <atomic>
#include <string>
#include <vector>

#include "boost/asio/io_service.hpp"
#include "boost/asio/steady_timer.hpp"
#include "boost/filesystem/path.hpp"
#include "boost/system/error_code.hpp"
#include "boost/thread/future.hpp"

#include "maidsafe/common/tagged_value.h"
#include "maidsafe/common/types.h"
#include "maidsafe/common/data_types/immutable_data.h"
#include "maidsafe/common/data_types/structured_data_versions.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/path.h"
#include "maidsafe/drive/file.h"

namespace maidsafe {

namespace drive {

namespace detail {

class Directory;

namespace test {

void DirectoriesMatch(const Directory&, const Directory&);
void SortAndResetChildrenCounter(Directory& lhs);

}  // namespace test

class Directory : public Path {
 public:
  class Listener {
   private:
    virtual void DirectoryPut(std::shared_ptr<Directory>) = 0;
    virtual boost::future<void> DirectoryPutChunk(const ImmutableData&) = 0;
    virtual void DirectoryIncrementChunks(const std::vector<ImmutableData::Name>&) = 0;

   public:
    virtual ~Listener() {}

    void Put(std::shared_ptr<Directory> directory) { DirectoryPut(directory); }

    boost::future<void> PutChunk(const ImmutableData& data) { return DirectoryPutChunk(data); }

    void IncrementChunks(const std::vector<ImmutableData::Name>& names) {
      DirectoryIncrementChunks(names);
    }
  };

  // This class must always be constructed using a Create() call to ensure that it will be
  // a shared_ptr. See the private constructors for the argument lists.
  template <typename... Types>
  static std::shared_ptr<Directory> Create(Types&&... args);

  std::shared_ptr<Directory> shared_from_this() {
    return std::static_pointer_cast<Directory>(Path::shared_from_this());
  }

  ~Directory();

  // This marks the start of an attempt to store the directory.  It serialises the appropriate
  // member data (critically parent_id_ must never be serialised), and sets 'store_state_' to
  // kOngoing.  It also calls 'FlushChild' on all children (see below).
  virtual std::string Serialise();
  // Stores all new chunks from 'child', increments all the other chunks, and resets child's
  // self_encryptor & buffer.
  void FlushChildAndDeleteEncryptor(File* child);

  size_t VersionsCount() const;
  std::tuple<DirectoryId, StructuredDataVersions::VersionName> InitialiseVersions(
      ImmutableData::Name version_id);
  // This marks the end of an attempt to store the directory.  It returns directory_id and most
  // recent 2 version names (including the one passed in), and sets 'store_state_' to kComplete.
  std::tuple<DirectoryId, StructuredDataVersions::VersionName, StructuredDataVersions::VersionName>
      AddNewVersion(ImmutableData::Name version_id);

  std::shared_ptr<Listener> GetListener() const;
  bool HasChild(const boost::filesystem::path& name) const;
  template <typename T = Path>
  typename std::enable_if<std::is_base_of<detail::Path, T>::value,
                          const std::shared_ptr<const T>>::type
      GetChild(const boost::filesystem::path& name) const;
  template <typename T = Path>
  typename std::enable_if<std::is_base_of<detail::Path, T>::value, std::shared_ptr<T>>::type
      GetMutableChild(const boost::filesystem::path& name);
  std::shared_ptr<const Path> GetChildAndIncrementCounter();
  void AddChild(std::shared_ptr<Path> child);
  std::shared_ptr<Path> RemoveChild(const boost::filesystem::path& name);
  void RenameChild(const boost::filesystem::path& old_name,
                   const boost::filesystem::path& new_name);
  void ResetChildrenCounter();
  bool empty() const;
  ParentId parent_id() const;
  void SetNewParent(const ParentId parent_id, const boost::filesystem::path& path);
  DirectoryId directory_id() const;
  virtual void ScheduleForStoring();
  void StoreImmediatelyIfPending();
  bool HasPending() const;

  friend void test::DirectoriesMatch(const Directory&, const Directory&);
  friend void test::SortAndResetChildrenCounter(Directory& lhs);

 private:
  Directory(const Directory&) = delete;
  Directory(Directory&& other) = delete;
  Directory& operator=(Directory) = delete;

  Directory(ParentId parent_id, DirectoryId directory_id, boost::asio::io_service& io_service,
            std::weak_ptr<Directory::Listener> listener,
            const boost::filesystem::path& path);  // NOLINT
  Directory(ParentId parent_id, const std::string& serialised_directory,
            const std::vector<StructuredDataVersions::VersionName>& versions,
            boost::asio::io_service& io_service, std::weak_ptr<Directory::Listener> listener,
            const boost::filesystem::path& path);

  void Initialise(const ParentId&, const DirectoryId&, boost::asio::io_service&,
                  std::weak_ptr<Directory::Listener>,
                  const boost::filesystem::path&);  // NOLINT
  void Initialise(const ParentId&, const std::string& serialised_directory,
                  const std::vector<StructuredDataVersions::VersionName>&,
                  boost::asio::io_service& io_service, std::weak_ptr<Directory::Listener>,
                  const boost::filesystem::path&);

  typedef std::vector<std::shared_ptr<Path>> Children;

  virtual void Serialise(protobuf::Directory&, std::vector<ImmutableData::Name>&);

  Children::iterator Find(const boost::filesystem::path& name);
  Children::const_iterator Find(const boost::filesystem::path& name) const;
  void SortAndResetChildrenCounter();
  void DoScheduleForStoring();
  void ProcessTimer(const boost::system::error_code&);

  ParentId parent_id_;
  DirectoryId directory_id_;
  boost::asio::steady_timer timer_;
  boost::filesystem::path path_;
  std::deque<StructuredDataVersions::VersionName> versions_;
  MaxVersions max_versions_;
  Children children_;
  size_t children_count_position_;
  enum class StoreState { kPending, kOngoing, kComplete } store_state_;
  struct NewParent {
    NewParent(const ParentId& parent_id, const boost::filesystem::path& path)
        : parent_id_(parent_id), path_(path) {}
    ParentId parent_id_;
    boost::filesystem::path path_;
  };
  const std::weak_ptr<Listener> listener_;
  std::unique_ptr<NewParent> newParent_;  // Use std::unique_ptr<> to fake an optional<>
  int pending_count_;

  mutable std::mutex mutex_;
};

bool operator<(const Directory& lhs, const Directory& rhs);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe


#include <thread>
#include "maidsafe/common/profiler.h"

namespace maidsafe {

namespace drive {

namespace detail {

template <typename... Types>
std::shared_ptr<Directory> Directory::Create(Types&&... args) {
  std::shared_ptr<Directory> self(new Directory{std::forward<Types>(args)...});
  self->Initialise(std::forward<Types>(args)...);
  return self;
}

template <typename T>
typename std::enable_if<std::is_base_of<detail::Path, T>::value,
                        const std::shared_ptr<const T>>::type
    Directory::GetChild(const boost::filesystem::path& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(name));
  if (itr == std::end(children_))
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::no_such_file));
  return std::dynamic_pointer_cast<T>(*itr);
}

template <typename T>
typename std::enable_if<std::is_base_of<detail::Path, T>::value, std::shared_ptr<T>>::type
    Directory::GetMutableChild(const boost::filesystem::path& name) {
  SCOPED_PROFILE
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(name));
  if (itr == std::end(children_))
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::no_such_file));
  return std::dynamic_pointer_cast<T>(*itr);
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_H_
