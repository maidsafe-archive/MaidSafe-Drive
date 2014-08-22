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

const uint32_t kAttributesDir = 0x4000;

#ifdef MAIDSAFE_WIN32
namespace {

const uint32_t kAttributesFormat = 0x0FFF;
const uint32_t kAttributesRegular = 0x8000;

}  // unnamed namespace
#endif



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
      attributes(),
      link_to(),
      data_map(),
      directory_id() {
  attributes.st_gid = getgid();
  attributes.st_uid = getuid();
  attributes.st_mode = 0644;
  attributes.st_nlink = 1;
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
      attributes(is_directory?FILE_ATTRIBUTE_DIRECTORY:0xFFFFFFFF),
#else
      attributes(),
      link_to(),
#endif
      data_map((file_type == fs::directory_file)
               ? nullptr
               : new encrypt::DataMap()),
      directory_id((file_type == fs::directory_file)
                   ? new DirectoryId(RandomString(64))
                   : nullptr) {
#ifdef MAIDSAFE_WIN32
#else
  attributes.st_gid = getgid();
  attributes.st_uid = getuid();
  attributes.st_mode = 0644;
  attributes.st_nlink = 1;

  if (file_type == fs::directory_file) {
    attributes.st_mode = (0755 | S_IFDIR);
    size = 4096;  // #BEFORE_RELEASE detail::kDirectorySize;
  }
#endif
}

MetaData::MetaData(const protobuf::MetaData& protobuf_meta_data)
    : name(protobuf_meta_data.name()),
      size(protobuf_meta_data.attributes_archive().st_size()),
#ifdef MAIDSAFE_WIN32
      allocation_size(protobuf_meta_data.attributes_archive().st_size()),
      attributes(0xFFFFFFFF),
#else
      attributes(),
      link_to(),
#endif
      data_map(),
      directory_id(protobuf_meta_data.has_directory_id() ?
                   new DirectoryId(protobuf_meta_data.directory_id()) : nullptr) {
  if ((name == "\\") || (name == "/"))
    name = kRoot;

  const protobuf::AttributesArchive& attributes_archive = protobuf_meta_data.attributes_archive();

  switch (attributes_archive.file_type()) {
    case protobuf::AttributesArchive::DIRECTORY_TYPE:
      file_type = fs::directory_file;
      break;
    case protobuf::AttributesArchive::REGULAR_FILE_TYPE:
      file_type = fs::regular_file;
      break;
  }
  using namespace std::chrono;
  creation_time = common::Clock::time_point(nanoseconds(attributes_archive.creation_time()));
  last_status_time = common::Clock::time_point(nanoseconds(attributes_archive.last_status_time()));
  last_write_time = common::Clock::time_point(nanoseconds(attributes_archive.last_write_time()));
  last_access_time = common::Clock::time_point(nanoseconds(attributes_archive.last_access_time()));

#ifdef MAIDSAFE_WIN32
  if ((attributes_archive.st_mode() & kAttributesDir) == kAttributesDir) {
    attributes |= FILE_ATTRIBUTE_DIRECTORY;
    size = 0;
  }

  if (attributes_archive.has_win_attributes())
    attributes = static_cast<DWORD>(attributes_archive.win_attributes());
#else
  if (attributes_archive.has_link_to())
    link_to = attributes_archive.link_to();

  attributes.st_mode = attributes_archive.st_mode();

  if (attributes_archive.has_st_dev())
    attributes.st_dev = attributes_archive.st_dev();
  if (attributes_archive.has_st_nlink())
    attributes.st_nlink = attributes_archive.st_nlink();
  if (attributes_archive.has_st_rdev())
    attributes.st_rdev = attributes_archive.st_rdev();

  if ((attributes_archive.st_mode() & kAttributesDir) == kAttributesDir)
    size = 4096;
#endif

  if (protobuf_meta_data.has_serialised_data_map()) {
    if (directory_id)
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
    data_map.reset(new encrypt::DataMap());
    encrypt::ParseDataMap(protobuf_meta_data.serialised_data_map(), *data_map);
  } else if (!directory_id) {
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
  }
}

MetaData::MetaData(MetaData&& other)
    : MetaData(other.file_type) {
      swap(*this, other);
    }

MetaData& MetaData::operator=(MetaData other) {
  swap(*this, other);
  return *this;
}

void MetaData::ToProtobuf(protobuf::MetaData* protobuf_meta_data) const {
  protobuf_meta_data->set_name(name.string());
  auto attributes_archive = protobuf_meta_data->mutable_attributes_archive();

  switch (file_type) {
    case fs::directory_file:
      attributes_archive->set_file_type(protobuf::AttributesArchive::DIRECTORY_TYPE);
      break;
    case fs::regular_file:
      attributes_archive->set_file_type(protobuf::AttributesArchive::REGULAR_FILE_TYPE);
      break;
    default:
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
      break;
  }
  attributes_archive->set_creation_time(creation_time.time_since_epoch().count());
  attributes_archive->set_last_status_time(last_status_time.time_since_epoch().count());
  attributes_archive->set_last_write_time(last_write_time.time_since_epoch().count());
  attributes_archive->set_last_access_time(last_access_time.time_since_epoch().count());

  attributes_archive->set_st_size(size);

#ifdef MAIDSAFE_WIN32

  uint32_t st_mode(0x01FF);
  st_mode &= kAttributesFormat;
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
    st_mode |= kAttributesDir;
  else
    st_mode |= kAttributesRegular;
  attributes_archive->set_st_mode(st_mode);
  attributes_archive->set_win_attributes(attributes);
#else
  attributes_archive->set_link_to(link_to.string());

  attributes_archive->set_st_dev(attributes.st_dev);
  attributes_archive->set_st_mode(attributes.st_mode);
  attributes_archive->set_st_nlink(attributes.st_nlink);
  attributes_archive->set_st_rdev(attributes.st_rdev);

  uint32_t win_attributes(0x10);  // FILE_ATTRIBUTE_DIRECTORY
  if ((attributes.st_mode & S_IFREG) == S_IFREG)
    win_attributes = 0x80;  // FILE_ATTRIBUTE_NORMAL
  if (((attributes.st_mode & S_IRUSR) == S_IRUSR) && (win_attributes == 0x80))
    win_attributes = 0x20 | 0x1;  // FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY
  if (name.string()[0]  == '.')
    win_attributes |= 0x2;  // FILE_ATTRIBUTE_HIDDEN
  attributes_archive->set_win_attributes(win_attributes);
#endif

  if (directory_id) {
    protobuf_meta_data->set_directory_id(directory_id->string());
  } else {
    std::string serialised_data_map;
    encrypt::SerialiseDataMap(*data_map, serialised_data_map);
    protobuf_meta_data->set_serialised_data_map(serialised_data_map);
  }
}

bool MetaData::operator<(const MetaData& other) const {
  return boost::ilexicographical_compare(name.wstring(), other.name.wstring());
}

void MetaData::UpdateLastModifiedTime() {
#ifdef MAIDSAFE_WIN32
  GetSystemTimeAsFileTime(&last_write_time);
#else
  time(&attributes.st_mtime);
#endif
}

uint64_t MetaData::GetAllocatedSize() const {
#ifdef MAIDSAFE_WIN32
  return allocation_size;
#else
  return size;
#endif
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
#else
  swap(lhs.attributes, rhs.attributes);
  swap(lhs.link_to, rhs.link_to);
#endif
  swap(lhs.data_map, rhs.data_map);
  swap(lhs.directory_id, rhs.directory_id);
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
