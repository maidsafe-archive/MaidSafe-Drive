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

namespace {

const uint32_t kAttributesDir = 0x4000;

#ifdef MAIDSAFE_WIN32
const uint32_t kAttributesFormat = 0x0FFF;
const uint32_t kAttributesRegular = 0x8000;
#endif

}  // unnamed namespace


MetaData::MetaData(FileType file_type)
    : name(),
      file_type(file_type),
      size(0),
#ifdef MAIDSAFE_WIN32
      allocation_size(0),
      attributes(0xFFFFFFFF),
      data_map(),
      directory_id() {}
#else
      data_map(),
      directory_id() {
}
#endif

MetaData::MetaData(const fs::path& name, FileType file_type)
    : name(name),
      file_type(file_type),
      creation_time(common::Clock::now()),
      last_status_time(creation_time),
      last_write_time(creation_time),
      last_access_time(creation_time),
      size(0),
#ifdef MAIDSAFE_WIN32
      allocation_size(0),
      attributes(file_type == FileType::directory_file ? FILE_ATTRIBUTE_DIRECTORY : 0xFFFFFFFF),
#endif
      data_map((file_type == fs::directory_file)
               ? nullptr
               : new encrypt::DataMap()),
      directory_id((file_type == fs::directory_file)
                   ? new DirectoryId(RandomString(64))
                   : nullptr) {
#ifdef MAIDSAFE_WIN32
#else
  if (file_type == fs::directory_file) {
    size = 4096;  // #BEFORE_RELEASE detail::kDirectorySize;
  }
#endif
}

MetaData::MetaData(const protobuf::Path& entry)
    : name(entry.name()),
      size(entry.attributes().st_size()),
#ifdef MAIDSAFE_WIN32
      allocation_size(entry.attributes().st_size()),
      attributes(0xFFFFFFFF),
#endif
      data_map(),
      directory_id(nullptr) {
  if ((name == "\\") || (name == "/"))
    name = kRoot;

  const protobuf::Attributes& attributes = entry.attributes();

  switch (attributes.file_type()) {
    case protobuf::Attributes::DIRECTORY_TYPE:
      file_type = fs::directory_file;
      if (!entry.has_directory_id())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      if (entry.has_serialised_data_map())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      directory_id.reset(new DirectoryId(entry.directory_id()));
      break;

    case protobuf::Attributes::REGULAR_FILE_TYPE:
      file_type = fs::regular_file;
      if (entry.has_directory_id())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      if (!entry.has_serialised_data_map())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      data_map.reset(new encrypt::DataMap());
      encrypt::ParseDataMap(entry.serialised_data_map(), *data_map);
      break;

    case protobuf::Attributes::SYMLINK_FILE_TYPE:
      file_type = fs::symlink_file;
      if (entry.has_directory_id())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      if (entry.has_serialised_data_map())
        BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
      break;
  }
  using namespace std::chrono;
  creation_time = common::Clock::time_point(nanoseconds(attributes.creation_time()));
  last_status_time = common::Clock::time_point(nanoseconds(attributes.last_status_time()));
  last_write_time = common::Clock::time_point(nanoseconds(attributes.last_write_time()));
  last_access_time = common::Clock::time_point(nanoseconds(attributes.last_access_time()));

#ifdef MAIDSAFE_WIN32
  if (file_type == FileType::directory_file) {
	  this->attributes |= FILE_ATTRIBUTE_DIRECTORY;
    size = 0;
  }

  if (attributes.has_win_attributes())
	  this->attributes = static_cast<DWORD>(attributes.win_attributes());
#else
  if (file_type == fs::directory_file) {
    size = 4096;
  }
#endif
}

MetaData::MetaData(MetaData&& other)
    : MetaData(other.file_type) {
      swap(*this, other);
    }

MetaData& MetaData::operator=(MetaData other) {
  swap(*this, other);
  return *this;
}

void MetaData::ToProtobuf(protobuf::Attributes& attributes) const {
  switch (file_type) {
    case fs::directory_file:
      attributes.set_file_type(protobuf::Attributes::DIRECTORY_TYPE);
      break;
    case fs::regular_file:
      attributes.set_file_type(protobuf::Attributes::REGULAR_FILE_TYPE);
      break;
    case fs::symlink_file:
      attributes.set_file_type(protobuf::Attributes::SYMLINK_FILE_TYPE);
      break;
    default:
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
      break;
  }
  attributes.set_creation_time(creation_time.time_since_epoch().count());
  attributes.set_last_status_time(last_status_time.time_since_epoch().count());
  attributes.set_last_write_time(last_write_time.time_since_epoch().count());
  attributes.set_last_access_time(last_access_time.time_since_epoch().count());

  attributes.set_st_size(size);

#ifdef MAIDSAFE_WIN32
  attributes.set_win_attributes(this->attributes);
#else
  uint32_t win_attributes(0x10);  // FILE_ATTRIBUTE_DIRECTORY
  if (file_type == fs::regular_file)
    win_attributes = 0x80;  // FILE_ATTRIBUTE_NORMAL
  if (name.string()[0]  == '.')
    win_attributes |= 0x2;  // FILE_ATTRIBUTE_HIDDEN
  attributes.set_win_attributes(win_attributes);
#endif
}

bool MetaData::operator<(const MetaData& other) const {
  return boost::ilexicographical_compare(name.wstring(), other.name.wstring());
}

void MetaData::UpdateLastModifiedTime() {
  last_write_time = common::Clock::now();
}

uint64_t MetaData::GetAllocatedSize() const {
#ifdef MAIDSAFE_WIN32
  return allocation_size;
#else
  return size;
#endif
}

MetaData::Permissions MetaData::GetPermissions(
    MetaData::Permissions base_permissions) const {
  if (file_type != MetaData::FileType::directory_file)
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

void swap(MetaData& lhs, MetaData& rhs) MAIDSAFE_NOEXCEPT {
  using std::swap;
  swap(lhs.name, rhs.name);
  swap(lhs.creation_time, rhs.creation_time);
  swap(lhs.last_status_time, rhs.last_status_time);
  swap(lhs.last_write_time, rhs.last_write_time);
  swap(lhs.last_access_time, rhs.last_access_time);
  swap(lhs.size, rhs.size);
#ifdef MAIDSAFE_WIN32
  swap(lhs.allocation_size, rhs.allocation_size);
  swap(lhs.attributes, rhs.attributes);
#endif
  swap(lhs.data_map, rhs.data_map);
  swap(lhs.directory_id, rhs.directory_id);
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
