/* Copyright 2011 MaidSafe.net limited

This MaidSafe Software is licensed under the MaidSafe.net Commercial License, version 1.0 or later,
and The General Public License (GPL), version 3. By contributing code to this project You agree to
the terms laid out in the MaidSafe Contributor Agreement, version 1.0, found in the root directory
of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also available at:

http://www.novinet.com/license

Unless required by applicable law or agreed to in writing, software distributed under the License is
distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing permissions and limitations under the
License.
*/

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
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/proto_structs.pb.h"


namespace args = std::placeholders;

namespace maidsafe {

namespace drive {

DirectoryListing::DirectoryListing(const DirectoryId& directory_id)
    : directory_id_(directory_id),
      children_(),
      children_itr_(children_.end()) {}

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
  return (std::find_if(children_.begin(), children_.end(),
                       [&name] (const MetaData& meta_data) {
                         return meta_data_ops::MetaDataHasName(meta_data, name);
                       }) != children_.end());
}

void DirectoryListing::GetChild(const fs::path& name, MetaData& meta_data) const {
  auto itr(std::find_if(children_.begin(), children_.end(),
           [&name] (const MetaData& meta_data) {
             return meta_data_ops::MetaDataHasName(meta_data, name);
           }));
  if (itr != children_.end()) {
    meta_data = *itr;
  } else {
    ThrowError(CommonErrors::invalid_parameter);
  }
  return;
}

bool DirectoryListing::GetChildAndIncrementItr(MetaData& meta_data) {
  if (children_itr_ != children_.end()) {
    while ((*children_itr_).name.extension() == kMsHidden) {
      ++children_itr_;
      if (children_itr_ == children_.end())
        return false;
    }
    meta_data = (*children_itr_);
    ++children_itr_;
    return true;
  } else {
    return false;
  }
}

void DirectoryListing::AddChild(const MetaData& child) {
  auto result(children_.insert(child));
  if (!result.second)
    ThrowError(CommonErrors::invalid_parameter);
  ResetChildrenIterator();
  return;
}

void DirectoryListing::RemoveChild(const MetaData& child) {
  auto result = children_.erase(child);
  if (result != 1U)
    ThrowError(CommonErrors::invalid_parameter);
  ResetChildrenIterator();
  return;
}

void DirectoryListing::UpdateChild(const MetaData& child, bool reset_itr) {
  // As children_ is a std::map, insert does not invalidate any iterators.
  // Only an iterator pointing to the erased element will be invalidated by
  // erase, so assert that children_itr_ != element to be erased if !reset_itr.
  assert(reset_itr || children_itr_ == children_.end() || children_itr_->name != child.name);
  auto erase_result = children_.erase(child);
  if (erase_result != 1U)
    ThrowError(CommonErrors::invalid_parameter);
  if (reset_itr)
    ResetChildrenIterator();
  auto insert_result(children_.insert(child));
  if (!insert_result.second)
    ThrowError(CommonErrors::invalid_parameter);
  if (reset_itr)
    ResetChildrenIterator();
  return;
}

int DirectoryListing::RenameChild(const MetaData& child,
                                  const fs::path& new_name,
                                  MetaData* target_if_exists) {
  MetaData child_copy(child);
  child_copy.name = new_name;
  auto insert_result(children_.insert(child_copy));
  if (!insert_result.second) {
    LOG(kWarning) << "Failed to add " << child_copy.name;
    *target_if_exists = *insert_result.first;
    return kFailedToAddChild;
  }
  ResetChildrenIterator();
  size_t erase_result(children_.erase(child));
  if (erase_result != 1U) {
    LOG(kWarning) << "Failed to remove " << child.name;
    children_.erase(child_copy);
    return kFailedToRemoveChild;
  }
  ResetChildrenIterator();
  return kSuccess;
}

bool DirectoryListing::empty() const {
  if (!children_.empty()) {
    for (auto itr(children_.begin()); itr != children_.end(); ++itr) {
      if ((*itr).name.extension() != kMsHidden)
        return false;
    }
  }
  return true;
}

void DirectoryListing::Serialise(std::string& serialised_directory_listing) const {
  serialised_directory_listing.clear();
  protobuf::DirectoryListing pb_directory;
  pb_directory.set_directory_id(directory_id_.string());

  for (auto child : children_) {
    std::string serialised_meta_data;
    child.Serialise(serialised_meta_data);
    protobuf::DirectoryListing::Child* pb_child = pb_directory.add_children();
    pb_child->set_serialised_meta_data(serialised_meta_data);
  }

  if (!pb_directory.SerializeToString(&serialised_directory_listing))
    ThrowError(CommonErrors::serialisation_error);

  return;
}

void DirectoryListing::Parse(const std::string& serialised_directory_listing) {
  protobuf::DirectoryListing pb_directory;

  if (!pb_directory.ParseFromString(serialised_directory_listing) ||
      !pb_directory.IsInitialized())
    ThrowError(CommonErrors::parsing_error);

  directory_id_ = Identity(pb_directory.directory_id());

  for (int i(0); i != pb_directory.children_size(); ++i) {
    if (pb_directory.children(i).IsInitialized()) {
      MetaData meta_data;
      meta_data.Parse(pb_directory.children(i).serialised_meta_data());
      children_.insert(meta_data);
    } else {
      ThrowError(CommonErrors::uninitialised);
    }
  }
  ResetChildrenIterator();
  return;
}

void DirectoryListing::GetHiddenChildNames(std::vector<std::string>* names) {
  std::for_each(children_.begin(),
                children_.end(),
                [&](const MetaData& child) {
                  if (child.name.extension() == kMsHidden)
                    names->push_back(child.name.string());
                });
}

bool DirectoryListing::operator<(const DirectoryListing& other) const {
  return directory_id_ < other.directory_id_;
}


namespace meta_data_ops {

bool MetaDataHasName(const MetaData& meta_data, const fs::path& name) {
  return (boost::algorithm::to_lower_copy(meta_data.name.wstring()) ==
          boost::algorithm::to_lower_copy(name.wstring()));
}

}  // namespace meta_data_ops

}  // namespace drive

}  // namespace maidsafe
