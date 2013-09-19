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

#include "maidsafe/drive/directory_listing.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <utility>
#include <vector>

#include "boost/algorithm/string/case_conv.hpp"
#include "boost/bind.hpp"

#include "maidsafe/common/rsa.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/encrypt/data_map.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/proto_structs.pb.h"


namespace maidsafe {

namespace drive {

namespace detail {

namespace {

bool MetaDataHasName(const MetaData& meta_data, const fs::path& name) {
  return (boost::algorithm::to_lower_copy(meta_data.name.wstring()) ==
          boost::algorithm::to_lower_copy(name.wstring()));
}

}  // unnamed namespace

DirectoryListing::DirectoryListing(DirectoryId directory_id)
    : directory_id_(std::move(directory_id)),
      children_(),
      children_itr_position_(0) {}

DirectoryListing::DirectoryListing(const DirectoryListing& other)
    : directory_id_(other.directory_id_),
      children_(other.children_),
      children_itr_position_(other.children_itr_position_) {}

DirectoryListing::DirectoryListing(DirectoryListing&& other)
    : directory_id_(std::move(other.directory_id())),
      children_(std::move(other.children_)),
      children_itr_position_(std::move(other.children_itr_position_)) {}

DirectoryListing::DirectoryListing(const std::string& serialised_directory_listing)
    : directory_id_(),
      children_(),
      children_itr_position_(0) {
  protobuf::DirectoryListing pb_directory;
  if (!pb_directory.ParseFromString(serialised_directory_listing))
    ThrowError(CommonErrors::parsing_error);

  directory_id_ = Identity(pb_directory.directory_id());

  for (int i(0); i != pb_directory.children_size(); ++i) {
    if (pb_directory.children(i).IsInitialized()) {
      children_.emplace_back(pb_directory.children(i).serialised_meta_data());
    } else {
      ThrowError(CommonErrors::uninitialised);
    }
  }

  std::sort(std::begin(children_), std::end(children_));
}



DirectoryListing& DirectoryListing::operator=(DirectoryListing other) {
  swap(*this, other);
  return *this;
}

bool DirectoryListing::HasChild(const fs::path& name) const {
  return std::any_of(std::begin(children_), std::end(children_),
                     [&name] (const MetaData& meta_data) {
                       return MetaDataHasName(meta_data, name);
                     });
}

void DirectoryListing::GetChild(const fs::path& name, MetaData& meta_data) const {
  auto itr(std::find_if(std::begin(children_), std::end(children_),
                        [&name] (const MetaData& meta_data) {
                          return MetaDataHasName(meta_data, name);
                        }));
  if (itr != std::end(children_)) {
    meta_data = *itr;
  } else {
    ThrowError(CommonErrors::invalid_parameter);
  }
}

bool DirectoryListing::GetChildAndIncrementItr(MetaData& meta_data) {
  if (children_itr_position_ != children_.size()) {
    while (children_[children_itr_position_].name.extension() == kMsHidden) {
      ++children_itr_position_;
      if (children_itr_position_ == children_.size())
        return false;
    }
    meta_data = children_[children_itr_position_];
    ++children_itr_position_;
    return true;
  }
  return false;
}

void DirectoryListing::AddChild(const MetaData& child) {
  assert(!std::any_of(std::begin(children_),
                      std::end(children_),
                      [&child](const MetaData& entry) { return child.name == entry.name; }));
  children_.push_back(child);
  SortAndResetChildrenIterator();
}

void DirectoryListing::RemoveChild(const MetaData& child) {
  auto itr(std::find_if(std::begin(children_), std::end(children_),
                        [&child](const MetaData& entry) { return child.name == entry.name; }));
  if (itr == std::end(children_))
    ThrowError(CommonErrors::invalid_parameter);
  children_.erase(itr);
  SortAndResetChildrenIterator();
}

void DirectoryListing::UpdateChild(const MetaData& child) {
  auto itr(std::find_if(std::begin(children_), std::end(children_),
                        [&child](const MetaData& entry) { return child.name == entry.name; }));
  if (itr == std::end(children_))
    ThrowError(CommonErrors::invalid_parameter);
  *itr = child;
  SortAndResetChildrenIterator();
}

void DirectoryListing::ResetChildrenIterator() {
  children_itr_position_ = 0;
}

void DirectoryListing::SortAndResetChildrenIterator() {
  std::sort(std::begin(children_), std::end(children_));
  ResetChildrenIterator();
}

bool DirectoryListing::empty() const {
  if (children_.empty())
    return true;
  return std::any_of(std::begin(children_), std::end(children_),
                     [](const MetaData& element) { return element.name.extension() == kMsHidden; });
}

std::string DirectoryListing::Serialise() const {
  protobuf::DirectoryListing pb_directory;
  pb_directory.set_directory_id(directory_id_.string());

  for (const auto& child : children_)
    pb_directory.add_children()->set_serialised_meta_data(child.Serialise());

  return pb_directory.SerializeAsString();
}

std::vector<std::string> DirectoryListing::GetHiddenChildNames() const {
  std::vector<std::string> names;
  std::for_each(std::begin(children_), std::end(children_),
                [&](const MetaData& child) {
                  if (child.name.extension() == kMsHidden)
                    names.push_back(child.name.string());
                });
  return names;
}

bool operator<(const DirectoryListing& lhs, const DirectoryListing& rhs) {
  return lhs.directory_id() < rhs.directory_id();
}

void swap(DirectoryListing& lhs, DirectoryListing& rhs) {
  using std::swap;
  swap(lhs.directory_id_, rhs.directory_id_);
  swap(lhs.children_, rhs.children_);
  swap(lhs.children_itr_position_, rhs.children_itr_position_);
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
