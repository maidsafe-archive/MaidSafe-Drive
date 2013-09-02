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

void PrintResult(const bptime::ptime &start_time,
                 const bptime::ptime &stop_time,
                 size_t size, TestOperationCode operation_code) {
  uint64_t duration = (stop_time - start_time).total_microseconds();
  if (duration == 0)
    duration = 1;
  uint64_t rate((static_cast<uint64_t>(size) * 1000000) / duration);
  switch (operation_code) {
    case(kCopy) : {
      std::cout << "Copy " << BytesToBinarySiUnits(size)
                << " of data to drive in " << (duration / 1000000.0)
                << " seconds at a speed of " << BytesToBinarySiUnits(rate)
                << "/s" << std::endl;
      break;
    }
    case(kRead) : {
      std::cout << "Read " << BytesToBinarySiUnits(size)
                << " Bytes of data from drive in " << (duration / 1000000.0)
                << " seconds at a speed of " << BytesToBinarySiUnits(rate)
                << "/s" << std::endl;
      break;
    }
    case(kCompare) : {
      std::cout << "Compare " << BytesToBinarySiUnits(size)
                << " Bytes of data from drive in " << (duration / 1000000.0)
                << " seconds at a speed of " << BytesToBinarySiUnits(rate)
                << "/s" << std::endl;
    }
  }
}

fs::path CreateTestFile(fs::path const& parent, int64_t &file_size) {
  size_t size = RandomUint32() % 4096;
  file_size = size;
  return CreateTestFileWithSize(parent, size);
}

fs::path CreateTestFileWithSize(fs::path const& parent, size_t size) {
  std::string file_content = RandomString(size);
  return CreateTestFileWithContent(parent, file_content);
}

fs::path CreateTestFileWithContent(fs::path const& parent, const std::string &content) {
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
  EXPECT_TRUE(fs::exists(file, ec)) << file;
  EXPECT_EQ(0, ec.value());
  return file;
}

fs::path CreateTestDirectory(fs::path const& parent) {
  fs::path directory(parent / RandomAlphaNumericString(5));
  boost::system::error_code error_code;
  EXPECT_TRUE(fs::create_directories(directory, error_code)) << directory
              << ": " << error_code.message();
  EXPECT_EQ(0, error_code.value()) << directory << ": "
                                   << error_code.message();
  EXPECT_TRUE(fs::exists(directory, error_code)) << directory << ": "
                                                 << error_code.message();
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
      EXPECT_TRUE(fs::exists(check, error_code)) << check << ": "
                                                 << error_code.message();
      EXPECT_EQ(0, error_code.value()) << check << ": "
                                       << error_code.message();
    } else if (r2 > r3) {
      r4 = distribution(generator);
      for (size_t j = 0; j != r4; ++j) {
        check = CreateTestFile(directory, file_size);
        EXPECT_TRUE(fs::exists(check, error_code)) << check << ": "
                                                   << error_code.message();
        EXPECT_EQ(0, error_code.value()) << check << ": "
                                         << error_code.message();
      }
    } else {
      r4 = distribution(generator);
      for (size_t j = 0; j != r4; ++j) {
        check = CreateTestDirectory(directory);
        EXPECT_TRUE(fs::exists(check, error_code)) << check << ": "
                                                   << error_code.message();
        EXPECT_EQ(0, error_code.value()) << check << ": "
                                         << error_code.message();
      }
    }
  }
  return directory;
}

fs::path CreateNamedFile(fs::path const& path, const std::string &name, int64_t &file_size) {
  boost::system::error_code error_code;
  if (!fs::is_directory(path, error_code)) {
    LOG(kError) << "Expected a directory " << path << " "
                << error_code.value();
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
    catch(...) {
      LOG(kError) << "Write exception thrown.";
      return fs::path();
    }
  }
  return fs::exists(file) ? file : fs::path();
}

fs::path CreateNamedDirectory(fs::path const& path, const std::string &name) {
  boost::system::error_code error_code;
  if (!fs::is_directory(path, error_code)) {
    LOG(kError) << "Expected a directory " << path << " "
                << error_code.value();
    return fs::path();
  }
  fs::path directory(path / name);
  if (!fs::create_directory(directory, error_code)) {
    LOG(kError) << "Failed to create directory " << fs::path(path / name)
                << " " << error_code.value();
    return fs::path();
  }
  return fs::exists(directory) ? directory : fs::path();
}

bool ModifyFile(fs::path const& path, int64_t &file_size) {
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
    catch(...) {
      LOG(kError) << "Write exception thrown.";
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

uint64_t TotalSize(encrypt::DataMapPtr data_map) {
  uint64_t size(data_map->chunks.empty() ? data_map->content.size() : 0);
  std::for_each(data_map->chunks.begin(), data_map->chunks.end(),
                [&size] (encrypt::ChunkDetails chunk) { size += chunk.size; });
  return size;
}

void GenerateDirectoryListingEntryForFile(DirectoryListing& directory_listing,
                                          const fs::path& path,
                                          const uintmax_t& file_size) {
  MetaData meta_data(path.filename(), false);
#ifdef MAIDSAFE_WIN32
  meta_data.end_of_file = file_size;
  meta_data.attributes = FILE_ATTRIBUTE_NORMAL;
  GetSystemTimeAsFileTime(&meta_data.creation_time);
  GetSystemTimeAsFileTime(&meta_data.last_access_time);
  GetSystemTimeAsFileTime(&meta_data.last_write_time);
  meta_data.allocation_size = RandomUint32();
#else
  time(&meta_data.attributes.st_atime);
  time(&meta_data.attributes.st_mtime);
  meta_data.attributes.st_size = file_size;
#endif
  meta_data.data_map->content = RandomString(100);
  EXPECT_NO_THROW(directory_listing.AddChild(meta_data));
}

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
