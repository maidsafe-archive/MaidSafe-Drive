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

#include "maidsafe/common/tagged_value.h"
#include "maidsafe/common/types.h"
#include "maidsafe/common/data_types/immutable_data.h"
#include "maidsafe/common/data_types/structured_data_versions.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/file_context.h"

namespace maidsafe {

namespace drive {

namespace detail {

class Directory;

namespace test {

void DirectoriesMatch(const Directory&, const Directory&);
void SortAndResetChildrenCounter(Directory& lhs);

}  // namespace test

template <typename Lock>
class ScopedUnlocker {
 public:
  explicit ScopedUnlocker(Lock& lock) : lock(lock) {
    lock.unlock();
  }
  ~ScopedUnlocker() {
    lock.lock();
  }
  Lock &lock;
};

class Directory : public std::enable_shared_from_this<Directory> {
 public:
  class Listener {
  public:
    virtual ~Listener() {}
    virtual void DirectoryPut(std::shared_ptr<Directory>) = 0;
    virtual void DirectoryPutChunk(const ImmutableData&) = 0;
    virtual void DirectoryIncrementChunks(const std::vector<Identity>&) = 0;

  private:
    friend class Directory;
    template <typename Lock>
    void Put(std::shared_ptr<Directory> directory, Lock& lock) {
      ScopedUnlocker<Lock> unlocker(lock);
      DirectoryPut(directory);
    }
    template <typename Lock>
    void PutChunk(const ImmutableData& data, Lock& lock) {
      ScopedUnlocker<Lock> unlocker(lock);
      DirectoryPutChunk(data);
    }
    template <typename Lock>
    void IncrementChunks(const std::vector<Identity>& names, Lock& lock) {
      ScopedUnlocker<Lock> unlocker(lock);
      DirectoryIncrementChunks(names);
    }
  };

  // This class must always be constructed using a Create() call to ensure that it will be
  // a shared_ptr. See the private constructors for the argument lists.
  template <typename... Types>
  static std::shared_ptr<Directory> Create(Types&&... args) {
    std::shared_ptr<Directory> self(new Directory{std::forward<Types>(args)...});
    self->Initialise(std::forward<Types>(args)...);
    return self;
  }

  ~Directory();

  // This marks the start of an attempt to store the directory.  It serialises the appropriate
  // member data (critically parent_id_ must never be serialised), and sets 'store_state_' to
  // kOngoing.  It also calls 'FlushChild' on all children (see below).
  std::string Serialise();
  // Stores all new chunks from 'child', increments all the other chunks, and resets child's
  // self_encryptor & buffer.
  void FlushChildAndDeleteEncryptor(FileContext* child);

  size_t VersionsCount() const;
  std::tuple<DirectoryId, StructuredDataVersions::VersionName>
      InitialiseVersions(Identity version_id);
  // This marks the end of an attempt to store the directory.  It returns directory_id and most
  // recent 2 version names (including the one passed in), and sets 'store_state_' to kComplete.
  std::tuple<DirectoryId, StructuredDataVersions::VersionName, StructuredDataVersions::VersionName>
      AddNewVersion(Identity version_id);

  bool HasChild(const boost::filesystem::path& name) const;
  const FileContext* GetChild(const boost::filesystem::path& name) const;
  FileContext* GetMutableChild(const boost::filesystem::path& name);
  const FileContext* GetChildAndIncrementCounter();
  void AddChild(FileContext&& child);
  FileContext RemoveChild(const boost::filesystem::path& name);
  void RenameChild(const boost::filesystem::path& old_name,
                   const boost::filesystem::path& new_name);
  void ResetChildrenCounter();
  bool empty() const;
  ParentId parent_id() const;
  void SetNewParent(const ParentId parent_id, const boost::filesystem::path& path);
  DirectoryId directory_id() const;
  void ScheduleForStoring();
  void StoreImmediatelyIfPending();
  bool HasPending() const;

  friend void test::DirectoriesMatch(const Directory&, const Directory&);
  friend void test::SortAndResetChildrenCounter(Directory& lhs);

  // TODO(Fraser#5#): 2014-01-30 - BEFORE_RELEASE - Make mutex_ private.
  mutable std::mutex mutex_;

 private:
  Directory(const Directory& other) = delete;
  Directory(Directory&& other) = delete;
  Directory& operator=(Directory other) = delete;

  Directory(ParentId parent_id,
            DirectoryId directory_id,
            boost::asio::io_service& io_service,
            std::weak_ptr<Directory::Listener> listener,
            const boost::filesystem::path& path);  // NOLINT
  Directory(ParentId parent_id,
            const std::string& serialised_directory,
            const std::vector<StructuredDataVersions::VersionName>& versions,
            boost::asio::io_service& io_service,
            std::weak_ptr<Directory::Listener> listener,
            const boost::filesystem::path& path);

  void Initialise(ParentId parent_id,
                  DirectoryId directory_id,
                  boost::asio::io_service& io_service,
                  std::weak_ptr<Directory::Listener> listener,
                  const boost::filesystem::path& path);  // NOLINT
  void Initialise(ParentId parent_id,
                  const std::string& serialised_directory,
                  const std::vector<StructuredDataVersions::VersionName>& versions,
                  boost::asio::io_service& io_service,
                  std::weak_ptr<Directory::Listener> listener,
                  const boost::filesystem::path& path);

  typedef std::vector<std::unique_ptr<FileContext>> Children;

  Children::iterator Find(const boost::filesystem::path& name);
  Children::const_iterator Find(const boost::filesystem::path& name) const;
  void SortAndResetChildrenCounter();
  void DoScheduleForStoring(bool use_delay = true);
  void ProcessTimer(const boost::system::error_code&);

  ParentId parent_id_;
  DirectoryId directory_id_;
  boost::asio::steady_timer timer_;
  boost::filesystem::path path_;
  std::weak_ptr<Directory::Listener> weakListener;
  std::vector<Identity> chunks_to_be_incremented_;
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
  std::unique_ptr<NewParent> newParent_;  // Use std::unique_ptr<> to fake an optional<>
  int pending_count_;
};

bool operator<(const Directory& lhs, const Directory& rhs);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DIRECTORY_H_
