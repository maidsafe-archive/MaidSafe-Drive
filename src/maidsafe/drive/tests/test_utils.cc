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

#include "maidsafe/drive/tests/test_utils.h"

#include <random>
#include <string>

#include "maidsafe/common/utils.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/log.h"

namespace maidsafe {

namespace drive {

namespace detail {

namespace test {

namespace {

void Exists(const fs::path& path, bool required = false, bool should_succeed = true) {
  boost::system::error_code error_code;
  auto result(fs::exists(path, error_code));
  if (required) {
    if (should_succeed)
      ASSERT_TRUE(result) << "fs::exists(" << path << ", error_code) returned \"" << std::boolalpha
                          << result << "\" with error_code \"" << error_code << " ("
                          << error_code.message() << ")\"";
    else
      ASSERT_TRUE(!result) << "fs::exists(" << path << ", error_code) returned \"" << std::boolalpha
                           << result << "\" with error_code \"" << error_code << " ("
                           << error_code.message() << ")\"";
  } else {
    if (should_succeed)
      EXPECT_TRUE(result) << "fs::exists(" << path << ", error_code) returned \"" << std::boolalpha
                          << result << "\" with error_code \"" << error_code << " ("
                          << error_code.message() << ")\"";
    else
      EXPECT_TRUE(!result) << "fs::exists(" << path << ", error_code) returned \"" << std::boolalpha
                           << result << "\" with error_code \"" << error_code << " ("
                           << error_code.message() << ")\"";
  }
}

void Remove(const fs::path& path, bool required = false, bool should_succeed = true) {
  boost::system::error_code error_code;
  auto result(fs::remove(path, error_code));
  if (required) {
    if (should_succeed)
      ASSERT_TRUE(result) << "fs::remove(" << path << ", error_code) returned \"" << std::boolalpha
                          << result << "\" with error_code \"" << error_code << " ("
                          << error_code.message() << ")\"";
    else
      ASSERT_TRUE(!result) << "fs::remove(" << path << ", error_code) returned \"" << std::boolalpha
                           << result << "\" with error_code \"" << error_code << " ("
                           << error_code.message() << ")\"";
  } else {
    if (should_succeed)
      EXPECT_TRUE(result) << "fs::remove(" << path << ", error_code) returned \"" << std::boolalpha
                          << result << "\" with error_code \"" << error_code << " ("
                          << error_code.message() << ")\"";
    else
      EXPECT_TRUE(!result) << "fs::remove(" << path << ", error_code) returned \"" << std::boolalpha
                           << result << "\" with error_code \"" << error_code << " ("
                           << error_code.message() << ")\"";
  }
}

void Rename(const fs::path& old_path, const fs::path& new_path, bool required = false,
            bool should_succeed = true) {
  boost::system::error_code error_code;
  fs::rename(old_path, new_path, error_code);
  if (required) {
    if (should_succeed)
      ASSERT_TRUE(!error_code) << "fs::rename(" << old_path << ", " << new_path
                               << ", error_code) returned with error_code \"" << error_code << " ("
                               << error_code.message() << ")\"";
    else
      ASSERT_TRUE(!!error_code) << "fs::rename(" << old_path << ", " << new_path
                                << ", error_code) returned with error_code \"" << error_code << " ("
                                << error_code.message() << ")\"";
  } else {
    if (should_succeed)
      EXPECT_TRUE(!error_code) << "fs::rename(" << old_path << ", " << new_path
                               << ", error_code) returned with error_code \"" << error_code << " ("
                               << error_code.message() << ")\"";
    else
      EXPECT_TRUE(!!error_code) << "fs::rename(" << old_path << ", " << new_path
                                << ", error_code) returned with error_code \"" << error_code << " ("
                                << error_code.message() << ")\"";
  }
}

void CreateDirectories(const fs::path& path, bool required = false, bool should_succeed = true) {
  boost::system::error_code error_code;
  auto result(fs::create_directories(path, error_code));
  if (required) {
    if (should_succeed)
      ASSERT_TRUE(result) << "fs::create_directories(" << path << ", error_code) returned \""
                          << std::boolalpha << result << "\" with error_code \"" << error_code
                          << " (" << error_code.message() << ")\"";
    else
      ASSERT_TRUE(!result) << "fs::create_directories(" << path << ", error_code) returned \""
                           << std::boolalpha << result << "\" with error_code \"" << error_code
                           << " (" << error_code.message() << ")\"";
  } else {
    if (should_succeed)
      EXPECT_TRUE(result) << "fs::create_directories(" << path << ", error_code) returned \""
                          << std::boolalpha << result << "\" with error_code \"" << error_code
                          << " (" << error_code.message() << ")\"";
    else
      EXPECT_TRUE(!result) << "fs::create_directories(" << path << ", error_code) returned \""
                           << std::boolalpha << result << "\" with error_code \"" << error_code
                           << " (" << error_code.message() << ")\"";
  }
}

}  // unnamed namespace

void PrintResult(const bptime::ptime& start_time, const bptime::ptime& stop_time, size_t size,
                 TestOperationCode operation_code) {
  uint64_t duration = (stop_time - start_time).total_microseconds();
  if (duration == 0)
    duration = 1;
  uint64_t rate((static_cast<uint64_t>(size) * 1000000) / duration);
  switch (operation_code) {
    case(kCopy) : {
      std::cout << "Copy " << BytesToBinarySiUnits(size) << " of data to drive in "
                << (duration / 1000000.0) << " seconds at a speed of " << BytesToBinarySiUnits(rate)
                << "/s" << std::endl;
      break;
    }
    case(kRead) : {
      std::cout << "Read " << BytesToBinarySiUnits(size) << " Bytes of data from drive in "
                << (duration / 1000000.0) << " seconds at a speed of " << BytesToBinarySiUnits(rate)
                << "/s" << std::endl;
      break;
    }
    case(kCompare) : {
      std::cout << "Compare " << BytesToBinarySiUnits(size) << " Bytes of data from drive in "
                << (duration / 1000000.0) << " seconds at a speed of " << BytesToBinarySiUnits(rate)
                << "/s" << std::endl;
    }
  }
}

fs::path CreateTestFile(fs::path const& parent, int64_t& file_size) {
  size_t size = RandomUint32() % 4096;
  file_size = size;
  return CreateTestFileWithSize(parent, size);
}

fs::path CreateTestFileWithSize(fs::path const& parent, size_t size) {
  std::string file_content = RandomString(size);
  return CreateTestFileWithContent(parent, file_content);
}

fs::path CreateTestFileWithContent(fs::path const& parent, const std::string& content) {
  fs::path file(parent / (RandomAlphaNumericString(5) + ".txt"));
  std::ofstream ofs;
  ofs.open(file.native().c_str(), std::ios_base::out | std::ios_base::binary);
  if (ofs.bad()) {
    LOG(kError) << "Can't open " << file;
  } else {
    ofs << content;
    ofs.close();
  }
  boost::system::error_code ec;
  EXPECT_TRUE(fs::exists(file, ec));
  EXPECT_TRUE(0 == ec.value());
  return file;
}

fs::path CreateTestDirectory(fs::path const& parent) {
  fs::path directory(parent / RandomAlphaNumericString(5));
  boost::system::error_code error_code;
  EXPECT_TRUE(fs::create_directories(directory, error_code));
  EXPECT_TRUE(0 == error_code.value());
  EXPECT_TRUE(fs::exists(directory, error_code));
  return directory;
}

fs::path CreateTestDirectoriesAndFiles(fs::path const& parent) {
  const size_t kMaxPathLength(200);
  fs::path directory(CreateTestDirectory(parent)), check;
  int64_t file_size(0);
  std::mt19937 generator(RandomUint32());
  std::uniform_int_distribution<> distribution(2, 4);
  size_t r1 = distribution(generator), r2, r3, r4;

  boost::system::error_code error_code;
  for (size_t i = 0; i != r1; ++i) {
    r2 = distribution(generator);
    r3 = distribution(generator);

    if (parent.string().size() > kMaxPathLength)
      break;
    if (r2 < r3) {
      check = CreateTestDirectoriesAndFiles(directory);
      EXPECT_TRUE(fs::exists(check, error_code));
      EXPECT_TRUE(0 == error_code.value());
    } else if (r2 > r3) {
      r4 = distribution(generator);
      for (size_t j = 0; j != r4; ++j) {
        check = CreateTestFile(directory, file_size);
        EXPECT_TRUE(fs::exists(check, error_code));
        EXPECT_TRUE(0 == error_code.value());
      }
    } else {
      r4 = distribution(generator);
      for (size_t j = 0; j != r4; ++j) {
        check = CreateTestDirectory(directory);
        EXPECT_TRUE(fs::exists(check, error_code));
        EXPECT_TRUE(0 == error_code.value());
      }
    }
  }
  return directory;
}

fs::path CreateNamedFile(fs::path const& path, const std::string& name, int64_t& file_size) {
  boost::system::error_code error_code;
  if (!fs::is_directory(path, error_code)) {
    LOG(kError) << "Expected a directory " << path << " " << error_code.value();
    return fs::path();
  }
  size_t size = RandomUint32() % 4096;
  file_size = size;
  fs::path file(path / name);
  std::string file_content = RandomString(size);
  std::ofstream ofs;
  ofs.open(file.native().c_str(), std::ios_base::out | std::ios_base::binary);
  if (ofs.bad()) {
    LOG(kError) << "Can't open " << file;
    return fs::path();
  } else {
    try {
      ofs << file_content;
      if (ofs.bad()) {
        LOG(kError) << "Can't write to " << file;
        ofs.close();
        return fs::path();
      }
      ofs.close();
    }
    catch (const std::exception& e) {
      LOG(kError) << "CreateNamedFile error: " << e.what();
      return fs::path();
    }
  }
  return fs::exists(file) ? file : fs::path();
}

fs::path CreateNamedDirectory(fs::path const& path, const std::string& name) {
  boost::system::error_code error_code;
  if (!fs::is_directory(path, error_code)) {
    LOG(kError) << "Expected a directory " << path << " " << error_code.value();
    return fs::path();
  }
  fs::path directory(path / name);
  if (!fs::create_directory(directory, error_code)) {
    LOG(kError) << "Failed to create directory " << fs::path(path / name) << " "
                << error_code.value();
    return fs::path();
  }
  return fs::exists(directory) ? directory : fs::path();
}

bool ModifyFile(fs::path const& path, int64_t& file_size) {
  size_t size = maidsafe::RandomInt32() % 1048576;  // 2^20
  file_size = size;
  LOG(kInfo) << "ModifyFile: filename = " << path << " new size " << size;

  std::string new_file_content(maidsafe::RandomAlphaNumericString(size));
  std::ofstream ofs(path.c_str(), std::ios_base::out | std::ios_base::binary);
  if (!ofs.is_open() || ofs.bad()) {
    LOG(kError) << "Can't open " << path;
    return false;
  } else {
    try {
      ofs << new_file_content;
      if (ofs.bad()) {
        ofs.close();
        return false;
      }
      ofs.close();
    }
    catch (const std::exception& e) {
      LOG(kError) << "ModifyFile error: " << e.what();
      return false;
    }
  }
  return true;
}

bool SameFileContents(fs::path const& path1, fs::path const& path2) {
  std::ifstream efile, ofile;
  efile.open(path1.c_str());
  ofile.open(path2.c_str());

  if (efile.bad() || ofile.bad() || !efile.is_open() || !ofile.is_open())
    return false;
  while (efile.good() && ofile.good())
    if (efile.get() != ofile.get())
      return false;
  if (!(efile.eof() && ofile.eof()))
    return false;
  return true;
}

uint64_t TotalSize(const encrypt::DataMap& data_map) {
  uint64_t size(data_map.chunks.empty() ? data_map.content.size() : 0);
  std::for_each(data_map.chunks.begin(), data_map.chunks.end(),
                [&size](encrypt::ChunkDetails chunk) { size += chunk.size; });
  return size;
}

void GenerateDirectoryListingEntryForFile(std::shared_ptr<Directory> directory,
                                          const fs::path& path,
                                          const uintmax_t& file_size) {
  FileContext file_context(path.filename(), false);
#ifdef MAIDSAFE_WIN32
  file_context.meta_data.end_of_file = file_size;
  file_context.meta_data.attributes = FILE_ATTRIBUTE_NORMAL;
  GetSystemTimeAsFileTime(&file_context.meta_data.creation_time);
  GetSystemTimeAsFileTime(&file_context.meta_data.last_access_time);
  GetSystemTimeAsFileTime(&file_context.meta_data.last_write_time);
  file_context.meta_data.allocation_size = RandomUint32();
#else
  time(&file_context.meta_data.attributes.st_atime);
  time(&file_context.meta_data.attributes.st_mtime);
  file_context.meta_data.attributes.st_size = file_size;
#endif
  file_context.meta_data.data_map->content = RandomString(100);
  EXPECT_NO_THROW(directory->AddChild(std::move(file_context)));
}

void CheckedExists(const fs::path& path) { Exists(path, false, true); }

void CheckedNotExists(const fs::path& path) { Exists(path, false, false); }

void RequiredExists(const fs::path& path) { Exists(path, true, true); }

void RequiredNotExists(const fs::path& path) { Exists(path, true, false); }

void CheckedRemove(const fs::path& path) { Remove(path, false, true); }

void CheckedNotRemove(const fs::path& path) { Remove(path, false, false); }

void RequiredRemove(const fs::path& path) { Remove(path, true, true); }

void RequiredNotRemove(const fs::path& path) { Remove(path, true, false); }

void CheckedRename(const fs::path& old_path, const fs::path& new_path) {
  Rename(old_path, new_path, false, true);
}

void CheckedNotRename(const fs::path& old_path, const fs::path& new_path) {
  Rename(old_path, new_path, false, false);
}

void RequiredRename(const fs::path& old_path, const fs::path& new_path) {
  Rename(old_path, new_path, true, true);
}

void RequiredNotRename(const fs::path& old_path, const fs::path& new_path) {
  Rename(old_path, new_path, true, false);
}

void CheckedCreateDirectories(const fs::path& path) { CreateDirectories(path, false, true); }

void CheckedNotCreateDirectories(const fs::path& path) { CreateDirectories(path, false, false); }

void RequiredCreateDirectories(const fs::path& path) { CreateDirectories(path, true, true); }

void RequiredNotCreateDirectories(const fs::path& path) { CreateDirectories(path, true, false); }

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
