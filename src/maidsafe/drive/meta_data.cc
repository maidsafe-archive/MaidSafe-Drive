/*******************************************************************************
 *  Copyright 2011 maidsafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 *******************************************************************************
 */

#include "maidsafe/drive/meta_data.h"

#include "boost/algorithm/string/predicate.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/drive/proto_structs.pb.h"
#include "maidsafe/drive/return_codes.h"
#include "maidsafe/drive/utils.h"


namespace bptime = boost::posix_time;
namespace fs = boost::filesystem;

// TODO(Fraser#5#): 2012-09-02 - Get rid of these
#define ATTRIBUTES_IFMT    0x0FFF
#define ATTRIBUTES_IFREG   0x8000
#define ATTRIBUTES_IFDIR   0x4000

#define ATTRIBUTES_IRWXU   0x01C0
#define ATTRIBUTES_IRUSR   0x0100
#define ATTRIBUTES_IWUSR   0x0080
#define ATTRIBUTES_IXUSR   0x0040

#define ATTRIBUTES_IRWXG   0x0038
#define ATTRIBUTES_IRGRP   0x0020
#define ATTRIBUTES_IWGRP   0x0010
#define ATTRIBUTES_IXGRP   0x0008

#define ATTRIBUTES_IRWXO   0x0007
#define ATTRIBUTES_IROTH   0x0004
#define ATTRIBUTES_IWOTH   0x0002
#define ATTRIBUTES_IXOTH   0x0001


namespace maidsafe {

namespace drive {


#ifdef MAIDSAFE_WIN32
namespace {

FILETIME BptimeToFileTime(bptime::ptime const &ptime) {
  SYSTEMTIME system_time;
  boost::gregorian::date::ymd_type year_month_day = ptime.date().year_month_day();

  system_time.wYear = year_month_day.year;
  system_time.wMonth = year_month_day.month;
  system_time.wDay = year_month_day.day;
  system_time.wDayOfWeek = ptime.date().day_of_week();

  // Now extract the hour/min/second field from time_duration
  bptime::time_duration time_duration = ptime.time_of_day();
  system_time.wHour = static_cast<WORD>(time_duration.hours());
  system_time.wMinute = static_cast<WORD>(time_duration.minutes());
  system_time.wSecond = static_cast<WORD>(time_duration.seconds());

  // Although ptime has a fractional second field, SYSTEMTIME millisecond
  // field is 16 bit, and will not store microsecond. We will treat this
  // field separately later.
  system_time.wMilliseconds = 0;

  // Convert SYSTEMTIME to FILETIME structure
  FILETIME file_time;
  SystemTimeToFileTime(&system_time, &file_time);

  // Now we are almost done. The FILETIME has date, and time. It is
  // only missing fractional second.

  // Extract the raw FILETIME into a 64 bit integer.
  boost::uint64_t hundreds_of_nanosecond_since_1601 = file_time.dwHighDateTime;
  hundreds_of_nanosecond_since_1601 <<= 32;
  hundreds_of_nanosecond_since_1601 |= file_time.dwLowDateTime;

  // Add in the fractional second, which is in microsecond * 10 to get
  // 100s of nanosecond
  hundreds_of_nanosecond_since_1601 += time_duration.fractional_seconds() * 10;

  // Now put the time back inside filetime.
  file_time.dwHighDateTime = hundreds_of_nanosecond_since_1601 >> 32;
  file_time.dwLowDateTime = hundreds_of_nanosecond_since_1601 & 0x00000000FFFFFFFF;

  return file_time;
}

bptime::ptime FileTimeToBptime(FILETIME const &ftime) {
  return bptime::from_ftime<bptime::ptime>(ftime);
}

}  // unnamed namespace
#endif



MetaData::MetaData()
    : name(),
#ifdef MAIDSAFE_WIN32
      end_of_file(0),
      allocation_size(0),
      attributes(0xFFFFFFFF),
      creation_time(),
      last_access_time(),
      last_write_time(),
      data_map(),
      directory_id() {}
#else
      attributes(),
      link_to(),
      data_map(),
      directory_id(),
      notes() {
  attributes.st_gid = getgid();
  attributes.st_uid = getuid();
  attributes.st_mode = 0644;
  attributes.st_nlink = 1;
}
#endif

MetaData::MetaData(const fs::path &name, bool is_directory)
    : name(name),
#ifdef MAIDSAFE_WIN32
      end_of_file(0),
      allocation_size(0),
      attributes(is_directory?FILE_ATTRIBUTE_DIRECTORY:0xFFFFFFFF),
      creation_time(),
      last_access_time(),
      last_write_time(),
      data_map(is_directory ? nullptr : std::make_shared<encrypt::DataMap>()),
      directory_id(is_directory ? std::make_shared<DirectoryId>(RandomString(64)) : nullptr),
      notes() {
    FILETIME file_time;
    GetSystemTimeAsFileTime(&file_time);
    creation_time = file_time;
    last_access_time = file_time;
    last_write_time = file_time;
}
#else
      attributes(),
      link_to(),
      data_map(is_directory ? nullptr : std::make_shared<encrypt::DataMap>()),
      directory_id(is_directory ? std::make_shared<DirectoryId>(RandomString(64)) : nullptr),
      notes() {
  attributes.st_gid = getgid();
  attributes.st_uid = getuid();
  attributes.st_mode = 0644;
  attributes.st_nlink = 1;
  attributes.st_ctime = attributes.st_mtime = time(&attributes.st_atime);

  if (is_directory) {
    attributes.st_mode = (0755 | S_IFDIR);
    attributes.st_size = kDirectorySize;
  }
}
#endif

MetaData::MetaData(const MetaData& meta_data)
  : name(meta_data.name),
#ifdef MAIDSAFE_WIN32
    end_of_file(meta_data.end_of_file),
    allocation_size(meta_data.allocation_size),
    attributes(meta_data.attributes),
    creation_time(meta_data.creation_time),
    last_access_time(meta_data.last_access_time),
    last_write_time(meta_data.last_write_time),
#else
    attributes(meta_data.attributes),
    link_to(meta_data.link_to),
#endif
    data_map(nullptr),
    directory_id(nullptr),
    notes(meta_data.notes) {
  if (meta_data.data_map)
    data_map.reset(new encrypt::DataMap(*meta_data.data_map));
  if (meta_data.directory_id)
    directory_id.reset(new DirectoryId(*meta_data.directory_id));
}

void MetaData::Serialise(std::string& serialised_meta_data) const {
  serialised_meta_data.clear();
  protobuf::MetaData pb_meta_data;

  pb_meta_data.set_name(name.string());
  protobuf::AttributesArchive* attributes_archive = pb_meta_data.mutable_attributes_archive();

#ifdef MAIDSAFE_WIN32
  attributes_archive->set_creation_time(bptime::to_iso_string(FileTimeToBptime(creation_time)));
  attributes_archive->set_last_access_time(
      bptime::to_iso_string(FileTimeToBptime(last_access_time)));
  attributes_archive->set_last_write_time(bptime::to_iso_string(FileTimeToBptime(last_write_time)));
  attributes_archive->set_st_size(end_of_file);

  uint32_t st_mode(0x01FF);
  st_mode &= ATTRIBUTES_IFMT;
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
    st_mode |= ATTRIBUTES_IFDIR;
  else
    st_mode |= ATTRIBUTES_IFREG;
  attributes_archive->set_st_mode(st_mode);
  attributes_archive->set_win_attributes(attributes);
#else
  attributes_archive->set_link_to(link_to.string());
  attributes_archive->set_st_size(attributes.st_size);

  attributes_archive->set_last_access_time(
      bptime::to_iso_string(bptime::from_time_t(attributes.st_atime)));
  attributes_archive->set_last_write_time(
      bptime::to_iso_string(bptime::from_time_t(attributes.st_mtime)));
  attributes_archive->set_creation_time(
      bptime::to_iso_string(bptime::from_time_t(attributes.st_ctime)));

  attributes_archive->set_st_dev(attributes.st_dev);
  attributes_archive->set_st_ino(attributes.st_ino);
  attributes_archive->set_st_mode(attributes.st_mode);
  attributes_archive->set_st_nlink(attributes.st_nlink);
  attributes_archive->set_st_uid(attributes.st_uid);
  attributes_archive->set_st_gid(attributes.st_gid);
  attributes_archive->set_st_rdev(attributes.st_rdev);
  attributes_archive->set_st_blksize(attributes.st_blksize);
  attributes_archive->set_st_blocks(attributes.st_blocks);
#endif

  if (data_map) {
    std::string serialised_data_map;
    encrypt::SerialiseDataMap(*data_map, serialised_data_map);
    pb_meta_data.set_serialised_data_map(serialised_data_map);
  } else {
    pb_meta_data.set_directory_id(directory_id->string());
  }

  for (auto note : notes)
    pb_meta_data.add_notes(note);

  if (!pb_meta_data.SerializeToString(&serialised_meta_data))
    ThrowError(CommonErrors::serialisation_error);

  return;
}

void MetaData::Parse(const std::string& serialised_meta_data) {
  if (!name.empty())
    ThrowError(CommonErrors::invalid_parameter);

  protobuf::MetaData pb_meta_data;
  if (!pb_meta_data.ParseFromString(serialised_meta_data))
    ThrowError(CommonErrors::parsing_error);

  name = pb_meta_data.name();
  if ((name == "\\") || (name == "/"))
    name = fs::path("/").make_preferred();

  const protobuf::AttributesArchive& attributes_archive = pb_meta_data.attributes_archive();

#ifdef MAIDSAFE_WIN32
  creation_time = BptimeToFileTime(bptime::from_iso_string(attributes_archive.creation_time()));
  last_access_time =
      BptimeToFileTime(bptime::from_iso_string(attributes_archive.last_access_time()));
  last_write_time = BptimeToFileTime(bptime::from_iso_string(attributes_archive.last_write_time()));
  end_of_file = attributes_archive.st_size();

  if ((attributes_archive.st_mode() & ATTRIBUTES_IFDIR) == ATTRIBUTES_IFDIR) {
    attributes |= FILE_ATTRIBUTE_DIRECTORY;
    end_of_file = 0;
  }
  allocation_size = end_of_file;

  if (attributes_archive.has_win_attributes())
    attributes = static_cast<DWORD>(attributes_archive.win_attributes());
#else
  if (attributes_archive.has_link_to())
    link_to = attributes_archive.link_to();
  attributes.st_size = attributes_archive.st_size();

  static bptime::ptime epoch(boost::gregorian::date(1970, 1, 1));
  bptime::time_duration diff(
      bptime::from_iso_string(attributes_archive.last_access_time()) - epoch);
  attributes.st_atime = diff.ticks() / diff.ticks_per_second();
  diff = bptime::from_iso_string(attributes_archive.last_write_time()) - epoch;
  attributes.st_mtime = diff.ticks() / diff.ticks_per_second();
  diff = bptime::from_iso_string(attributes_archive.creation_time()) - epoch;
  attributes.st_ctime = diff.ticks() / diff.ticks_per_second();

  attributes.st_mode = attributes_archive.st_mode();

  if (attributes_archive.has_st_dev())
    attributes.st_dev = attributes_archive.st_dev();
  if (attributes_archive.has_st_ino())
    attributes.st_ino = attributes_archive.st_ino();
  if (attributes_archive.has_st_nlink())
    attributes.st_nlink = attributes_archive.st_nlink();
  if (attributes_archive.has_st_uid())
    attributes.st_uid = attributes_archive.st_uid();
  if (attributes_archive.has_st_gid())
    attributes.st_gid = attributes_archive.st_gid();
  if (attributes_archive.has_st_rdev())
    attributes.st_rdev = attributes_archive.st_rdev();
  if (attributes_archive.has_st_blksize())
    attributes.st_blksize = attributes_archive.st_blksize();
  if (attributes_archive.has_st_blocks())
    attributes.st_blocks = attributes_archive.st_blocks();

  if ((attributes_archive.st_mode() & ATTRIBUTES_IFDIR) == ATTRIBUTES_IFDIR)
    attributes.st_size = 4096;
#endif

  if (pb_meta_data.has_serialised_data_map()) {
    if (pb_meta_data.has_directory_id())
      ThrowError(CommonErrors::parsing_error);
    data_map.reset(new encrypt::DataMap);
    encrypt::ParseDataMap(pb_meta_data.serialised_data_map(), *data_map);
  } else if (pb_meta_data.has_directory_id()) {
    directory_id.reset(new DirectoryId(pb_meta_data.directory_id()));
  } else {
    ThrowError(CommonErrors::invalid_parameter);
  }

  for (int i(0); i != pb_meta_data.notes_size(); ++i)
    notes.push_back(pb_meta_data.notes(i));

  return;
}

bptime::ptime MetaData::creation_posix_time() const {
#ifdef MAIDSAFE_WIN32
  return FileTimeToBptime(creation_time);
#else
  return bptime::from_time_t(attributes.st_ctime);
#endif
}

bptime::ptime MetaData::last_write_posix_time() const {
#ifdef MAIDSAFE_WIN32
  return FileTimeToBptime(last_write_time);
#else
  return bptime::from_time_t(attributes.st_mtime);
#endif
}

bool MetaData::operator<(const MetaData &other) const {
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
  return attributes.st_size;
#endif
}

}  // namespace drive

}  // namespace maidsafe
