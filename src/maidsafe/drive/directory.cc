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

#include <algorithm>
#include <functional>
#include <iterator>
#include <vector>

#include "boost/algorithm/string/case_conv.hpp"
#include "boost/bind.hpp"

#include "maidsafe/common/rsa.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/encrypt/data_map.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory.h"
#include "maidsafe/drive/proto_structs.pb.h"


namespace fs = boost::filesystem;

namespace maidsafe {
namespace drive {
namespace detail {

namespace {

bool MetaDataHasName(const MetaData& meta_data, const fs::path& name) {
  return (boost::algorithm::to_lower_copy(meta_data.name.wstring()) ==
          boost::algorithm::to_lower_copy(name.wstring()));
}

}  // unnamed namespace


DirectoryListing::DirectoryListing(const DirectoryId& directory_id)
    : directory_id_(directory_id), children_(), children_itr_(children_.end()) {}

DirectoryListing::DirectoryListing(const std::string& serialised_directory_listing)
    : directory_id_(), children_(), children_itr_(std::end(children_)) {
  protobuf::DirectoryListing pb_directory;

  if (!pb_directory.ParseFromString(serialised_directory_listing) ||
      !pb_directory.IsInitialized())
    ThrowError(CommonErrors::parsing_error);

  directory_id_ = Identity(pb_directory.directory_id());

  for (int i(0); i != pb_directory.children_size(); ++i) {
    if (pb_directory.children(i).IsInitialized()) {
      MetaData meta_data(pb_directory.children(i).serialised_meta_data());
      children_.insert(meta_data);
    } else {
      ThrowError(CommonErrors::uninitialised);
    }
  }
}

DirectoryListing::DirectoryListing(const DirectoryListing& directory)
  : directory_id_(directory.directory_id()),
    children_(directory.children_),
    children_itr_(children_.begin()) {}

DirectoryListing& DirectoryListing::operator=(const DirectoryListing& directory) {
  directory_id_ = directory.directory_id();
  children_ = directory.children_;
  children_itr_ = children_.begin();
  return *this;
}

bool DirectoryListing::HasChild(const fs::path& name) const {
  return std::any_of(
      std::begin(children_), std::end(children_),
      [&name](const MetaData & meta_data) { return MetaDataHasName(meta_data, name); });
}

void DirectoryListing::GetChild(const fs::path& name, MetaData& meta_data) const {
  auto itr(std::find_if(
      std::begin(children_), std::end(children_),
      [&name](const MetaData & meta_data) { return MetaDataHasName(meta_data, name); }));
  if (itr != std::end(children_)) {
    meta_data = *itr;
  } else {
    ThrowError(CommonErrors::invalid_parameter);
  }
}

bool DirectoryListing::GetChildAndIncrementItr(MetaData& meta_data) {
  if (children_itr_ != children_.end()) {
    while ((*children_itr_).name.extension() == kMsHidden) {
      ++children_itr_;
      if (children_itr_ == children_.end())
        return false;
    }
    meta_data = *children_itr_;
    ++children_itr_;
    return true;
  }
  return false;
}

void DirectoryListing::AddChild(const MetaData& child) {
  auto result(children_.insert(child));
  if (!result.second)
    ThrowError(CommonErrors::invalid_parameter);
  ResetChildrenIterator();
}

void DirectoryListing::RemoveChild(const MetaData& child) {
  auto result = children_.erase(child);
  if (result != 1U)
    ThrowError(CommonErrors::invalid_parameter);
  ResetChildrenIterator();
}

void DirectoryListing::UpdateChild(const MetaData& child) {
  auto erase_result = children_.erase(child);
  if (erase_result != 1U)
    ThrowError(CommonErrors::invalid_parameter);
  auto insert_result(children_.insert(child));
  if (!insert_result.second)
    ThrowError(CommonErrors::invalid_parameter);
  ResetChildrenIterator();
}

void DirectoryListing::ResetChildrenIterator() { children_itr_ = children_.begin(); }

bool DirectoryListing::empty() const {
  if (children_.empty())
    return true;
  return std::any_of(std::begin(children_), std::end(children_), [](const MetaData& element) {
    return element.name.extension() == kMsHidden;
  });
}

DirectoryId DirectoryListing::directory_id() const { return directory_id_; }

std::string DirectoryListing::Serialise() const {
  std::string serialised_directory_listing;
  protobuf::DirectoryListing pb_directory;
  pb_directory.set_directory_id(directory_id_.string());

  for (auto child : children_) {
    std::string serialised_meta_data(child.Serialise());
    protobuf::DirectoryListing::Child* pb_child = pb_directory.add_children();
    pb_child->set_serialised_meta_data(serialised_meta_data);
  }

  if (!pb_directory.SerializeToString(&serialised_directory_listing))
    ThrowError(CommonErrors::serialisation_error);
  return serialised_directory_listing;
}

std::vector<std::string> DirectoryListing::GetHiddenChildNames() const {
  std::vector<std::string> names;
  std::for_each(std::begin(children_), std::end(children_), [&](const MetaData& child) {
    if (child.name.extension() == kMsHidden)
      names.push_back(child.name.string());
  });
  return names;
}

bool DirectoryListing::operator<(const DirectoryListing& other) const {
  return directory_id_ < other.directory_id_;
}

}  // namespace detail
}  // namespace drive
}  // namespace maidsafe
