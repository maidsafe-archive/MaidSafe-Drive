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

#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/utils.h"
#include "maidsafe/drive/proto_structs.pb.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace detail {

namespace {

bool FileContextHasName(const FileContext* file_context, const fs::path& name) {
  return GetLowerCase(file_context->meta_data.name.string()) == GetLowerCase(name.string());
}

}  // unnamed namespace

Directory::Directory(ParentId parent_id, DirectoryId directory_id,
                     boost::asio::io_service& io_service,
                     std::function<void(const boost::system::error_code&)> store_functor)
    : mutex_(), parent_id_(std::move(parent_id)), directory_id_(std::move(directory_id)),
      timer_(io_service), store_functor_(store_functor), versions_(), max_versions_(kMaxVersions),
      children_(), children_itr_position_(0), store_pending_(false) {
  DoScheduleForStoring();
}

Directory::Directory(ParentId parent_id, const std::string& serialised_directory,
                     const std::vector<StructuredDataVersions::VersionName>& versions,
                     boost::asio::io_service& io_service,
                     std::function<void(const boost::system::error_code&)> store_functor)
    : versions_(std::begin(versions), std::end(versions)), parent_id_(std::move(parent_id)),
      timer_(io_service), store_functor_(store_functor), directory_id_(),
      max_versions_(kMaxVersions), children_(), children_itr_position_(0), store_pending_(false) {
  protobuf::Directory proto_directory;
  if (!proto_directory.ParseFromString(serialised_directory))
    ThrowError(CommonErrors::parsing_error);

  directory_id_ = Identity(proto_directory.directory_id());
  max_versions_ = MaxVersions(proto_directory.max_versions());

  for (int i(0); i != proto_directory.children_size(); ++i)
    children_.emplace_back(new FileContext(MetaData(proto_directory.children(i)), this));
  std::sort(std::begin(children_), std::end(children_));
}

std::string Directory::Serialise() {
  protobuf::Directory proto_directory;
  std::lock_guard<std::mutex> lock(mutex_);
  proto_directory.set_directory_id(directory_id_.string());
  proto_directory.set_max_versions(max_versions_.data);

  for (const auto& child : children_)
    child->meta_data.ToProtobuf(proto_directory.add_children());

  store_pending_ = false;
  return proto_directory.SerializeAsString();
}

Directory::Children::iterator Directory::Find(const fs::path& name) {
  return std::find_if(std::begin(children_), std::end(children_),
                      [&name](const Children::value_type& file_context) {
                           return FileContextHasName(file_context.get(), name); });
}

Directory::Children::const_iterator Directory::Find(const fs::path& name) const {
  return std::find_if(std::begin(children_), std::end(children_),
                      [&name](const Children::value_type& file_context) {
                           return FileContextHasName(file_context.get(), name); });
}

void Directory::SortAndResetChildrenIterator() {
  std::sort(std::begin(children_), std::end(children_));
  children_itr_position_ = 0;
}

void Directory::DoScheduleForStoring(bool use_delay) {
  if (use_delay) {
    auto cancelled_count(timer_.expires_from_now(kInactivityDelay));
#ifndef NDEBUF
    if (cancelled_count > 0 && store_pending_) {
      LOG(kSuccess) << "Successfully cancelled " << cancelled_count << " store functor.";
      assert(cancelled_count == 1);
    } else if (store_pending_) {
      LOG(kWarning) << "Failed to cancel store functor.";
    }
#endif
    static_cast<void>(cancelled_count);
    timer_.async_wait(store_functor_);
    store_pending_ = true;
  } else if (store_pending_) {
    auto cancelled_count(timer_.cancel());
    if (cancelled_count > 0) {
      LOG(kSuccess) << "Successfully cancelled " << cancelled_count << " store functor.";
      assert(cancelled_count == 1);
      timer_.get_io_service().post([this] { store_functor_(boost::system::error_code()); });
    } else {
      LOG(kWarning) << "Failed to cancel store functor.";
    }
    store_pending_ = true;
#ifndef NDEBUF
  } else {
    LOG(kSuccess) << "No store functor pending.";
#endif
  }
}

bool Directory::HasChild(const fs::path& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::any_of(std::begin(children_), std::end(children_),
      [&name](const Children::value_type& file_context) {
          return FileContextHasName(file_context.get(), name); });
}

const FileContext* Directory::GetChild(const fs::path& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(name));
  if (itr == std::end(children_))
    ThrowError(DriveErrors::no_such_file);
  return itr->get();
}

FileContext* Directory::GetMutableChild(const fs::path& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(name));
  if (itr == std::end(children_))
    ThrowError(DriveErrors::no_such_file);
  return itr->get();
}

const FileContext* Directory::GetChildAndIncrementItr() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (children_itr_position_ != children_.size()) {
    const FileContext* file_context(children_[children_itr_position_].get());
    ++children_itr_position_;
    return file_context;
  }
  return nullptr;
}

void Directory::AddChild(FileContext&& child) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(child.meta_data.name));
  if (itr != std::end(children_))
    ThrowError(DriveErrors::file_exists);
  child.parent = this;
  children_.emplace_back(new FileContext(std::move(child)));
  SortAndResetChildrenIterator();
  DoScheduleForStoring();
}

FileContext Directory::RemoveChild(const fs::path& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(name));
  if (itr == std::end(children_))
    ThrowError(DriveErrors::no_such_file);
  std::unique_ptr<FileContext> file_context(std::move(*itr));
  children_.erase(itr);
  SortAndResetChildrenIterator();
  DoScheduleForStoring();
  return std::move(*file_context);
}

void Directory::RenameChild(const fs::path& old_name, const fs::path& new_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto itr(Find(new_name));
  if (itr != std::end(children_))
    ThrowError(DriveErrors::file_exists);
  itr = Find(old_name);
  if (itr == std::end(children_))
    ThrowError(DriveErrors::no_such_file);
  (*itr)->meta_data.name = new_name;
  SortAndResetChildrenIterator();
  DoScheduleForStoring();
}

void Directory::ResetChildrenIterator() {
  std::lock_guard<std::mutex> lock(mutex_);
  children_itr_position_ = 0;
}

bool Directory::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return children_.empty();
}

ParentId Directory::parent_id() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return parent_id_;
}

void Directory::SetParentId(const ParentId parent_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  parent_id_ = parent_id;
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

std::tuple<DirectoryId, StructuredDataVersions::VersionName, StructuredDataVersions::VersionName>
    Directory::AddNewVersion(ImmutableData::Name version_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (versions_.empty()) {
    versions_.emplace_back(0, version_id);
    return std::make_tuple(directory_id_, StructuredDataVersions::VersionName(), versions_[0]);
  }
  versions_.emplace_front(versions_.front().index + 1, version_id);
  auto itr(std::begin(versions_));
  return std::make_tuple(directory_id_, *(itr + 1), *itr);
}

bool operator<(const Directory& lhs, const Directory& rhs) {
  return lhs.directory_id() < rhs.directory_id();
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
