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

Directory::Directory(ParentId parent_id, DirectoryId directory_id)
    : versions_(), parent_id_(std::move(parent_id)), last_changed_(),
      directory_id_(std::move(directory_id)), max_versions_(kMaxVersions), children_(),
      children_itr_position_(0) {}

Directory::Directory(ParentId parent_id, const std::string& serialised_directory,
                     std::vector<StructuredDataVersions::VersionName> versions)
    : versions_(std::move(versions)), parent_id_(std::move(parent_id)), last_changed_(),
      directory_id_(), max_versions_(kMaxVersions), children_(), children_itr_position_(0) {
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
  proto_directory.set_directory_id(directory_id_.string());
  proto_directory.set_max_versions(max_versions_.data);

  for (const auto& child : children_)
    child->meta_data.ToProtobuf(proto_directory.add_children());

  last_changed_.reset();
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

bool Directory::HasChild(const fs::path& name) const {
  return std::any_of(std::begin(children_), std::end(children_),
      [&name](const Children::value_type& file_context) {
          return FileContextHasName(file_context.get(), name); });
}

const FileContext* Directory::GetChild(const fs::path& name) const {
  auto itr(Find(name));
  if (itr == std::end(children_))
    ThrowError(DriveErrors::no_such_file);
  return itr->get();
}

FileContext* Directory::GetMutableChild(const fs::path& name) {
  auto itr(Find(name));
  if (itr == std::end(children_))
    ThrowError(DriveErrors::no_such_file);
  return itr->get();
}

const FileContext* Directory::GetChildAndIncrementItr() {
  if (children_itr_position_ != children_.size()) {
    const FileContext* file_context(children_[children_itr_position_].get());
    ++children_itr_position_;
    return file_context;
  }
  return nullptr;
}

void Directory::AddChild(FileContext&& child) {
  auto itr(Find(child.meta_data.name));
  if (itr != std::end(children_))
    ThrowError(DriveErrors::file_exists);
  child.parent = this;
  children_.emplace_back(new FileContext(std::move(child)));
  SortAndResetChildrenIterator();
  MarkAsChanged();
}

FileContext Directory::RemoveChild(const fs::path& name) {
  auto itr(Find(name));
  if (itr == std::end(children_))
    ThrowError(DriveErrors::no_such_file);
  std::unique_ptr<FileContext> file_context(std::move(*itr));
  children_.erase(itr);
  SortAndResetChildrenIterator();
  MarkAsChanged();
  return std::move(*file_context);
}

void Directory::RenameChild(const fs::path& old_name, const fs::path& new_name) {
  auto itr(Find(new_name));
  if (itr != std::end(children_))
    ThrowError(DriveErrors::file_exists);
  itr = Find(old_name);
  if (itr == std::end(children_))
    ThrowError(DriveErrors::no_such_file);
  (*itr)->meta_data.name = new_name;
  SortAndResetChildrenIterator();
  MarkAsChanged();
}

bool Directory::empty() const {
  return children_.empty();
}

void Directory::SortAndResetChildrenIterator() {
  std::sort(std::begin(children_), std::end(children_));
  ResetChildrenIterator();
}

void Directory::MarkAsChanged() {
  last_changed_.reset(new std::chrono::steady_clock::time_point(std::chrono::steady_clock::now()));
}

bool Directory::NeedsToBeSaved(bool ignore_delay) const {
  if (last_changed_) {
    return (*last_changed_ + kDirectoryInactivityDelay > std::chrono::steady_clock::now()) ||
           ignore_delay;
  }
  return false;
}

bool operator<(const Directory& lhs, const Directory& rhs) {
  return lhs.directory_id() < rhs.directory_id();
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
