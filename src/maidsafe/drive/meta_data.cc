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

#include "maidsafe/drive/meta_data.h"

#include "boost/algorithm/string/predicate.hpp"

#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/drive/proto_structs.pb.h"
#include "maidsafe/drive/utils.h"


namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace detail {

#ifdef MAIDSAFE_WIN32
namespace {

const uint32_t kAttributesDir = 0x4000;
const uint32_t kAttributesFormat = 0x0FFF;
const uint32_t kAttributesRegular = 0x8000;

}  // unnamed namespace
#endif

MetaData::MetaData(FileType file_type)
    : data_map_(),
      directory_id_(),
      name_(),
      file_type_(file_type),
      creation_time_(common::Clock::now()),
      last_status_time_(creation_time_),
      last_write_time_(creation_time_),
      last_access_time_(creation_time_),
      size_(0),
      allocation_size_(0)
#ifdef MAIDSAFE_WIN32
      , attributes_(0xFFFFFFFF)
#endif
        {}

MetaData::MetaData(const fs::path& name, FileType file_type)
    : data_map_((file_type == FileType::directory_file)
                ? nullptr
                : new encrypt::DataMap()),
      directory_id_((file_type == FileType::directory_file)
                    ? new DirectoryId(RandomString(64))
                    : nullptr),
      name_(name),
      file_type_(file_type),
      creation_time_(common::Clock::now()),
      last_status_time_(creation_time_),
      last_write_time_(creation_time_),
      last_access_time_(creation_time_),
      size_(0),
      allocation_size_(0)
#ifdef MAIDSAFE_WIN32
      , attributes_(file_type == FileType::directory_file ? FILE_ATTRIBUTE_DIRECTORY : 0xFFFFFFFF)
#endif
        {
#ifndef MAIDSAFE_WIN32
  if (file_type == FileType::directory_file) {
    size_ = 4096;  // #BEFORE_RELEASE detail::kDirectorySize;
  }
#endif
}

MetaData::MetaData(const protobuf::Path& entry)
    : data_map_(),
      directory_id_(nullptr),
      name_(entry.name()),
      file_type_(FileType::status_error),
      creation_time_(),
      last_status_time_(),
      last_write_time_(),
      last_access_time_(),
      size_(entry.attributes().st_size()),
      allocation_size_(entry.attributes().st_size())
#ifdef MAIDSAFE_WIN32
      , attributes_(0xFFFFFFFF)
#endif
       {
  if ((name_ == "\\") || (name_ == "/"))
    name_ = kRoot;

  const protobuf::Attributes& attributes = entry.attributes();

  switch (attributes.file_type()) {
    case protobuf::Attributes::DIRECTORY_TYPE:
      file_type_ = FileType::directory_file;
      if (!entry.has_directory_id())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      if (entry.has_serialised_data_map())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      directory_id_.reset(new DirectoryId(entry.directory_id()));
      break;

    case protobuf::Attributes::REGULAR_FILE_TYPE:
      file_type_ = FileType::regular_file;
      if (entry.has_directory_id())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      if (!entry.has_serialised_data_map())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      data_map_.reset(new encrypt::DataMap());
      encrypt::ParseDataMap(entry.serialised_data_map(), *data_map());
      break;

    case protobuf::Attributes::SYMLINK_FILE_TYPE:
      file_type_ = FileType::symlink_file;
      if (entry.has_directory_id())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      if (entry.has_serialised_data_map())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      break;
  }
  {
    using namespace std::chrono;
    creation_time_ = TimePoint(nanoseconds(attributes.creation_time()));
    last_status_time_ = TimePoint(nanoseconds(attributes.last_status_time()));
    last_write_time_ = TimePoint(nanoseconds(attributes.last_write_time()));
    last_access_time_ = TimePoint(nanoseconds(attributes.last_access_time()));
  }

#ifdef MAIDSAFE_WIN32
  if (file_type() == FileType::directory_file) {
	  attributes_ |= FILE_ATTRIBUTE_DIRECTORY;
    size_ = 0;
  }

  if (attributes.has_win_attributes())
	  attributes_ = static_cast<DWORD>(attributes.win_attributes());
#else
  if (file_type() == FileType::directory_file) {
    size_ = 4096;
  }
#endif
}

MetaData::MetaData(MetaData&& other)
    : data_map_(),
      directory_id_(),
      name_(),
      file_type_(),
      creation_time_(),
      last_status_time_(),
      last_write_time_(),
      last_access_time_(),
      size_(0),
      allocation_size_(0)
#ifdef MAIDSAFE_WIN32
      , attributes_(0xFFFFFFFF)
#endif 
        {
  swap(other);
}

MetaData& MetaData::operator=(MetaData other) {
  swap(other);
  return *this;
}

void MetaData::ToProtobuf(protobuf::Attributes& proto_attributes) const {
  switch (file_type()) {
    case FileType::directory_file:
      proto_attributes.set_file_type(protobuf::Attributes::DIRECTORY_TYPE);
      break;
    case FileType::regular_file:
      proto_attributes.set_file_type(protobuf::Attributes::REGULAR_FILE_TYPE);
      break;
    case FileType::symlink_file:
      proto_attributes.set_file_type(protobuf::Attributes::SYMLINK_FILE_TYPE);
      break;
    default:
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
      break;
  }
  proto_attributes.set_creation_time(creation_time().time_since_epoch().count());
  proto_attributes.set_last_status_time(last_status_time().time_since_epoch().count());
  proto_attributes.set_last_write_time(last_write_time().time_since_epoch().count());
  proto_attributes.set_last_access_time(last_access_time().time_since_epoch().count());

  proto_attributes.set_st_size(size());

#ifdef MAIDSAFE_WIN32
  proto_attributes.set_win_attributes(attributes());
#else
  uint32_t win_attributes(0x10);  // FILE_ATTRIBUTE_DIRECTORY
  if (file_type() == FileType::regular_file)
    win_attributes = 0x80;  // FILE_ATTRIBUTE_NORMAL
  if (name().string()[0]  == '.')
    win_attributes |= 0x2;  // FILE_ATTRIBUTE_HIDDEN
  proto_attributes.set_win_attributes(win_attributes);
#endif
}

bool MetaData::operator<(const MetaData& other) const {
  return boost::ilexicographical_compare(
      name().wstring(), other.name().wstring());
}

MetaData::Permissions MetaData::GetPermissions(
    MetaData::Permissions base_permissions) const {
  if (file_type() != MetaData::FileType::directory_file)
  {
    return base_permissions;
  }

  if (HasPermission(base_permissions, MetaData::Permissions::owner_read))
  {
    base_permissions |= MetaData::Permissions::owner_exe;
  }

  if (HasPermission(base_permissions, MetaData::Permissions::group_read))
  {
    base_permissions |= MetaData::Permissions::group_exe;
  }

  if (HasPermission(base_permissions, MetaData::Permissions::others_read))
  {
    base_permissions |= MetaData::Permissions::others_exe;
  }

  return base_permissions;
}

void MetaData::UpdateLastStatusTime() {
  last_status_time_ = common::Clock::now();
  last_access_time_ = last_status_time();
}

void MetaData::UpdateLastModifiedTime() {
  last_write_time_ = common::Clock::now();
  last_access_time_ = last_write_time();
  last_status_time_ = last_write_time();
}

void MetaData::UpdateLastAccessTime() {
  last_access_time_ = common::Clock::now();
}

void MetaData::UpdateSize(const std::uint64_t new_size) {
  size_ = new_size;
  allocation_size_ = size_;

  last_write_time_ = common::Clock::now();
  last_access_time_ = last_write_time();
  last_status_time_ = last_write_time();
}

void MetaData::UpdateAllocationSize(const std::uint64_t new_size) {
  allocation_size_ = new_size;
  last_write_time_ = common::Clock::now();
  last_access_time_ = last_write_time();
  last_status_time_ = last_write_time();
}

void MetaData::swap(MetaData& rhs) MAIDSAFE_NOEXCEPT {
  using std::swap;
  swap(data_map_, rhs.data_map_);
  swap(directory_id_, rhs.directory_id_);
  swap(name_, rhs.name_);
  swap(file_type_, rhs.file_type_);
  swap(creation_time_, rhs.creation_time_);
  swap(last_status_time_, rhs.last_status_time_);
  swap(last_write_time_, rhs.last_write_time_);
  swap(last_access_time_, rhs.last_access_time_);
  swap(size_, rhs.size_);
  swap(allocation_size_, rhs.allocation_size_);
#ifdef MAIDSAFE_WIN32
  swap(attributes_, rhs.attributes_);
#endif
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
