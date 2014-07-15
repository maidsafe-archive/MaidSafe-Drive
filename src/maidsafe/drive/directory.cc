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

#include "maidsafe/drive/directory.h"

#include <algorithm>
#include <iterator>

#include "boost/asio/placeholders.hpp"

#include "maidsafe/common/profiler.h"

#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/proto_structs.pb.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace detail {

namespace {

template <typename PutChunkClosure>
void FlushEncryptor(FileContext* file_context,
                    PutChunkClosure put_chunk_closure,
                    std::vector<ImmutableData::Name>& chunks_to_be_incremented) {
  file_context->self_encryptor->Flush();
  if (file_context->self_encryptor->original_data_map().chunks.empty()) {
    // If the original data map didn't contain any chunks, just store the new ones.
    for (const auto& chunk : file_context->self_encryptor->data_map().chunks) {
      auto content(file_context->buffer->Get(chunk.hash));
      put_chunk_closure(ImmutableData(content));
    }
  } else {
    // Check each new chunk against the original data map's chunks.  Store the new ones and
    // increment the reference count on the existing chunks.
    for (const auto& chunk : file_context->self_encryptor->data_map().chunks) {
      if (std::any_of(std::begin(file_context->self_encryptor->original_data_map().chunks),
                      std::end(file_context->self_encryptor->original_data_map().chunks),
                      [&chunk](const encrypt::ChunkDetails& original_chunk) {
                            return chunk.hash == original_chunk.hash;
                      })) {
        chunks_to_be_incremented.emplace_back(Identity(chunk.hash));
      } else {
        auto content(file_context->buffer->Get(chunk.hash));
        put_chunk_closure(ImmutableData(content));
      }
    }
  }
  if (*file_context->open_count == 0) {
    file_context->self_encryptor.reset();
    file_context->buffer.reset();
  }
  file_context->flushed = true;
}

}  // unnamed namespace

Directory::Directory(ParentId parent_id,
                     DirectoryId directory_id,
                     boost::asio::io_service& io_service,
                     std::weak_ptr<Directory::Listener> listener,
                     const boost::filesystem::path& path)
  : mutex_(),
    parent_id_(std::move(parent_id)),
    directory_id_(std::move(directory_id)),
    timer_(io_service),
    path_(path),
    weakListener(listener),
    chunks_to_be_incremented_(),
    versions_(),
    max_versions_(kMaxVersions),
    children_(),
    children_count_position_(0),
    store_state_(StoreState::kComplete),
    pending_count_(0) {
}

Directory::Directory(ParentId parent_id,
                     const std::string&,
                     const std::vector<StructuredDataVersions::VersionName>& versions,
                     boost::asio::io_service& io_service,
                     std::weak_ptr<Directory::Listener> listener,
                     const boost::filesystem::path& path)
  : mutex_(),
    parent_id_(std::move(parent_id)),
    directory_id_(),
    timer_(io_service), path_(path),
    weakListener(listener),
    chunks_to_be_incremented_(),
    versions_(std::begin(versions), std::end(versions)),
    max_versions_(kMaxVersions),
    children_(),
    children_count_position_(0),
    store_state_(StoreState::kComplete),
    pending_count_(0) {
}

Directory::~Directory() {
  std::unique_lock<std::mutex> lock(mutex_);
  DoScheduleForStoring(false);
}

void Directory::Initialise(ParentId,
                           DirectoryId,
                           boost::asio::io_service&,
                           std::weak_ptr<Directory::Listener>,
                           const boost::filesystem::path&) {
  std::lock_guard<std::mutex> lock(mutex_);
  DoScheduleForStoring();
}

void Directory::Initialise(ParentId,
                           const std::string& serialised_directory,
                           const std::vector<StructuredDataVersions::VersionName>&,
                           boost::asio::io_service&,
                           std::weak_ptr<Directory::Listener>,
                           const boost::filesystem::path&) {
    std::lock_guard<std::mutex> lock(mutex_);
    protobuf::Directory proto_directory;
    if (!proto_directory.ParseFromString(serialised_directory))
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));

    directory_id_ = Identity(proto_directory.directory_id());
    max_versions_ = MaxVersions(proto_directory.max_versions());

    for (int i(0); i != proto_directory.children_size(); ++i)
        children_.emplace_back(new FileContext(MetaData(proto_directory.children(i)),
                                               shared_from_this()));
    SortAndResetChildrenCounter();
}

std::string Directory::Serialise() {
  protobuf::Directory proto_directory;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    proto_directory.set_directory_id(directory_id_.string());
    proto_directory.set_max_versions(max_versions_.data);

    for (const auto& child : children_) {
      child->meta_data.ToProtobuf(proto_directory.add_children());
      if (child->self_encryptor) {  // Child is a file which has been opened
        child->timer->cancel();
        FlushEncryptor(child.get(),
                       [this, &lock](const ImmutableData& data) {
                         std::shared_ptr<Directory::Listener> listener = weakListener.lock();
                         listener->PutChunk(data, lock);
                       },
                       chunks_to_be_incremented_);
        child->flushed = false;
      } else if (child->meta_data.data_map) {
        if (child->flushed) {  // Child is a file which has already been flushed
          child->flushed = false;
        } else {  // Child is a file which has not been opened
          for (const auto& chunk : child->meta_data.data_map->chunks)
            chunks_to_be_incremented_.emplace_back(Identity(chunk.hash));
        }
      }
    }
    std::shared_ptr<Directory::Listener> listener = weakListener.lock();
    listener->DirectoryIncrementChunks(chunks_to_be_incremented_);
    chunks_to_be_incremented_.clear();

    store_state_ = StoreState::kOngoing;
  }
  return proto_directory.SerializeAsString();
}

void Directory::FlushChildAndDeleteEncryptor(FileContext* child) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (child->self_encryptor)  // Child could already have been flushed via 'Directory::Serialise'
    FlushEncryptor(child,
                   [this, &lock](const ImmutableData& data) {
                     std::shared_ptr<Directory::Listener> listener = weakListener.lock();
                     listener->PutChunk(data, lock);
                   },
                   chunks_to_be_incremented_);
}

size_t Directory::VersionsCount() const {
  return versions_.size();
}

std::tuple<DirectoryId, StructuredDataVersions::VersionName>
    Directory::InitialiseVersions(ImmutableData::Name version_id) {
  std::tuple<DirectoryId, StructuredDataVersions::VersionName> result;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    store_state_ = StoreState::kComplete;
    if (versions_.empty()) {
      versions_.emplace_back(0, version_id);
      result = std::make_tuple(directory_id_, versions_[0]);
    } else {
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::uninitialised));
    }
  }
  return result;
}

std::tuple<DirectoryId, StructuredDataVersions::VersionName, StructuredDataVersions::VersionName>
    Directory::AddNewVersion(ImmutableData::Name version_id) {
  std::tuple<DirectoryId, StructuredDataVersions::VersionName,
             StructuredDataVersions::VersionName> result;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    store_state_ = StoreState::kComplete;
    if (versions_.empty()) {
      versions_.emplace_back(0, version_id);
      result = std::make_tuple(directory_id_, StructuredDataVersions::VersionName(), versions_[0]);
    } else {
      versions_.emplace_front(versions_.front().index + 1, version_id);
      auto itr(std::begin(versions_));
      result = std::make_tuple(directory_id_, *(itr + 1), *itr);
      if (versions_.size() > max_versions_)
        versions_.pop_back();
    }
  }
  return result;
}

Directory::Children::iterator Directory::Find(const fs::path& name) {
  return std::find_if(std::begin(children_), std::end(children_),
                      [&name](const Children::value_type& file_context) {
                           return file_context->meta_data.name == name; });
}

Directory::Children::const_iterator Directory::Find(const fs::path& name) const {
  return std::find_if(std::begin(children_), std::end(children_),
                      [&name](const Children::value_type& file_context) {
                           return file_context->meta_data.name == name; });
}

void Directory::SortAndResetChildrenCounter() {
  std::sort(std::begin(children_), std::end(children_),
            [](const std::unique_ptr<FileContext>& lhs, const std::unique_ptr<FileContext>& rhs) {
              return *lhs < *rhs;
            });
  children_count_position_ = 0;
}

void Directory::DoScheduleForStoring(bool use_delay) {
  if (use_delay) {
    auto cancelled_count(timer_.expires_from_now(kDirectoryInactivityDelay));
#ifndef NDEBUG
    if (cancelled_count > 0 && store_state_ != StoreState::kComplete) {
      LOG(kInfo) << "Successfully cancelled " << cancelled_count << " store functor.";
      assert(cancelled_count == 1);
    } else if (store_state_ != StoreState::kComplete) {
      LOG(kWarning) << "Failed to cancel store functor.";
    }
#endif
    static_cast<void>(cancelled_count);
    timer_.async_wait(std::bind(&Directory::ProcessTimer,
                                shared_from_this(),
                                std::placeholders::_1));
    ++pending_count_;
    store_state_ = StoreState::kPending;
  } else if (store_state_ == StoreState::kPending) {
    // If 'use_delay' is false, the implication is that we should only store if there's already
    // a pending store waiting - i.e. we're just bringing forward the deadline of any outstanding
    // store.
    auto cancelled_count(timer_.cancel());
    if (cancelled_count > 0) {
      LOG(kInfo) << "Successfully brought forward schedule for " << cancelled_count
                 << " store functor.";
      assert(cancelled_count == 1);
      timer_.get_io_service().post(std::bind(&Directory::ProcessTimer,
                                             shared_from_this(),
                                             boost::system::error_code()));
      ++pending_count_;
    } else {
      LOG(kWarning) << "Failed to cancel store functor.";
    }
    store_state_ = StoreState::kPending;
#ifndef NDEBUG
  } else {
    LOG(kInfo) << "No store functor pending.";
#endif
  }
}

void Directory::ProcessTimer(const boost::system::error_code& ec) {
  std::unique_lock<std::mutex> lock(mutex_);
  switch (ec.value()) {
    case 0: {
      LOG(kInfo) << "Storing " << path_ << ", " << ec;
      std::shared_ptr<Directory::Listener> listener = weakListener.lock();
      listener->Put(shared_from_this(), lock);
      break;
    }
    case boost::asio::error::operation_aborted:
      LOG(kInfo) << "Timer was cancelled - not storing " << path_;
      break;
    default:
      LOG(kWarning) << "Timer aborted with error code " << ec;
      break;
  }
  // Update pending parent change
  if (newParent_) {
    parent_id_ = newParent_->parent_id_;
    path_ = newParent_->path_;
    newParent_ = nullptr;
  }
  --pending_count_;
}

bool Directory::HasChild(const fs::path& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::any_of(std::begin(children_), std::end(children_),
      [&name](const Children::value_type& file_context) {
          return file_context->meta_data.name == name; });
}

const FileContext* Directory::GetChild(const fs::path& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(name));
  if (itr == std::end(children_))
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::no_such_file));
  // The open_count must be >=0.  If > 0 and the context doesn't represent a directory, the buffer
  // and encryptor should be non-null.
  assert(*(*itr)->open_count == 0 || (*(*itr)->open_count > 0 &&
      ((*itr)->meta_data.directory_id ||
          ((*itr)->buffer && (*itr)->self_encryptor && (*itr)->timer))));
  return itr->get();
}

FileContext* Directory::GetMutableChild(const fs::path& name) {
  SCOPED_PROFILE
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(name));
  if (itr == std::end(children_))
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::no_such_file));
  // The open_count must be >=0.  If > 0 and the context doesn't represent a directory, the buffer
  // and encryptor should be non-null.
  assert(*(*itr)->open_count == 0 || (*(*itr)->open_count > 0 &&
      ((*itr)->meta_data.directory_id ||
          ((*itr)->buffer && (*itr)->self_encryptor && (*itr)->timer))));
  return itr->get();
}

const FileContext* Directory::GetChildAndIncrementCounter() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (children_count_position_ < children_.size()) {
    const FileContext* file_context(children_[children_count_position_].get());
    ++children_count_position_;
    return file_context;
  }
  return nullptr;
}

void Directory::AddChild(FileContext&& child) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(child.meta_data.name));
  if (itr != std::end(children_))
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::file_exists));
  child.parent = shared_from_this();
  children_.emplace_back(new FileContext(std::move(child)));
  SortAndResetChildrenCounter();
  DoScheduleForStoring();
}

FileContext Directory::RemoveChild(const fs::path& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(name));
  if (itr == std::end(children_))
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::no_such_file));
  FileContext file_context(std::move(*(*itr)));
  children_.erase(itr);
  SortAndResetChildrenCounter();
  DoScheduleForStoring();
  return std::move(file_context);
}

void Directory::RenameChild(const fs::path& old_name, const fs::path& new_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  assert(Find(new_name) == std::end(children_));
  auto itr(Find(old_name));
  if (itr == std::end(children_))
    BOOST_THROW_EXCEPTION(MakeError(DriveErrors::no_such_file));
  (*itr)->meta_data.name = new_name;
  SortAndResetChildrenCounter();
  DoScheduleForStoring();
}

void Directory::ResetChildrenCounter() {
  std::lock_guard<std::mutex> lock(mutex_);
  children_count_position_ = 0;
}

bool Directory::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return children_.empty();
}

ParentId Directory::parent_id() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return parent_id_;
}

void Directory::SetNewParent(const ParentId parent_id,
                             const boost::filesystem::path& path) {
  std::unique_lock<std::mutex> lock(mutex_);
  newParent_.reset(new NewParent(parent_id, path));
}

DirectoryId Directory::directory_id() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return directory_id_;
}

void Directory::ScheduleForStoring() {
  std::lock_guard<std::mutex> lock(mutex_);
  DoScheduleForStoring();
}

void Directory::StoreImmediatelyIfPending() {
  std::lock_guard<std::mutex> lock(mutex_);
  DoScheduleForStoring(false);
}

bool Directory::HasPending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return (pending_count_ != 0);
}

bool operator<(const Directory& lhs, const Directory& rhs) {
  return lhs.directory_id() < rhs.directory_id();
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
