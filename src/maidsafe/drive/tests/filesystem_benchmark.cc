/*  Copyright 2013 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.novinet.com/license

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#include <cstdint>
#include <string>

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/program_options.hpp"

#include "maidsafe/common/error.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/utils.h"


namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace maidsafe {

namespace test {

namespace {

fs::path root_, temp_;

fs::path GenerateFile(const fs::path& parent, uint32_t size) {
  if (size == 0)
    ThrowError(CommonErrors::invalid_parameter);

  auto file_name(parent / (RandomAlphaNumericString((RandomUint32() % 4) + 4) + ".txt"));

  std::ofstream output_stream(file_name.c_str(), std::ios::binary);
  if (!output_stream.is_open() || output_stream.bad())
    ThrowError(CommonErrors::filesystem_io_error);

  std::string random_string(RandomString(size % 1024));
  size_t rounds = size / 1024, count = 0;
  while (count++ < rounds)
    output_stream.write(random_string.data(), random_string.size());

  output_stream.close();
  return file_name;
}

fs::path GenerateDirectory(const fs::path& parent) {
  auto directory_name(parent / (RandomAlphaNumericString((RandomUint32() % 8) + 4)));
  boost::system::error_code ec;
  fs::create_directory(directory_name, ec);
  if (ec)
    ThrowError(CommonErrors::filesystem_io_error);
  return directory_name;
}

std::vector<uint32_t> GenerateFileSizes(uint32_t max_size, uint32_t min_size, size_t count) {
  std::vector<uint32_t> file_sizes;
  file_sizes.reserve(count);
  while (file_sizes.size() < count)
    file_sizes.push_back((RandomUint32() % max_size) + min_size);
  return file_sizes;
}

uint32_t CreateTestTreeStructure(const fs::path& base_path,
                                 std::vector<fs::path>& directories,
                                 std::set<fs::path>& files,
                                 uint32_t directory_node_count,
                                 uint32_t file_node_count = 100,
                                 uint32_t max_filesize = 5 * 1024 * 1024,
                                 uint32_t min_size = 1024) {
  fs::path directory(GenerateDirectory(base_path));
  directories.reserve(directory_node_count);
  directories.push_back(directory);
  while (directories.size() < directory_node_count) {
    size_t random_element(RandomUint32() % directories.size());
    fs::path p = GenerateDirectory(directories.at(random_element));
    if (!p.empty())
      directories.push_back(p);
  }

  auto file_sizes(GenerateFileSizes(max_filesize, min_size, 20));
  uint32_t total_file_size(0);
  while (files.size() < file_node_count) {
    size_t random_element(RandomUint32() % directory_node_count);
    uint32_t file_size = file_sizes.at(files.size() % file_sizes.size());
    fs::path p = GenerateFile(directories.at(random_element), file_size);
    if (!p.empty()) {
      total_file_size += file_size;
      files.insert(p);
    }
  }
  return total_file_size;
}

void CopyRecursiveDirectory(const fs::path &src, const fs::path &dest) {
  boost::system::error_code ec;
  fs::copy_directory(src, dest / src.filename(), ec);
  for (fs::recursive_directory_iterator end, current(src); current != end; ++current) {
    std::string str = current->path().string();
    std::string str2 = src.string();
    boost::algorithm::replace_last(str2, src.filename().string(), "");
    boost::algorithm::replace_first(str, str2, dest.string() + "/");
    if (!fs::exists(current->path()))
      ThrowError(CommonErrors::filesystem_io_error);
    if (fs::is_directory(*current)) {
      fs::copy_directory(current->path(), str, ec);
    } else {
      fs::copy_file(current->path(), str, fs::copy_option::overwrite_if_exists, ec);
    }
    if (!fs::exists(str));
      ThrowError(CommonErrors::filesystem_io_error);
  }
}

}  // namespace

void CopyThenReadLargeFile() {
  boost::system::error_code error_code;

  // Create file on disk...
  size_t size = 300 * 1024 * 1024;
  fs::path file(CreateTestFileWithSize(g_test_mirror, size));
  ASSERT_TRUE(fs::exists(file, error_code));
  ASSERT_EQ(error_code.value(), 0);

  // Copy file to virtual drive...
  bptime::ptime copy_start_time(bptime::microsec_clock::universal_time());
  fs::copy_file(file, g_mount_dir / file.filename(), fs::copy_option::fail_if_exists, error_code);
  bptime::ptime copy_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(copy_start_time, copy_stop_time, size, kCopy);
  ASSERT_EQ(error_code.value(), 0);
  ASSERT_TRUE(fs::exists(g_mount_dir / file.filename(), error_code));
  ASSERT_EQ(error_code.value(), 0);

  // Read the file back to a disk file...
  // Because of the system caching, the pure read can't reflect the real speed
  fs::path test_file(g_test_mirror / (RandomAlphaNumericString(5) + ".txt"));
  bptime::ptime read_start_time(bptime::microsec_clock::universal_time());
  fs::copy_file(g_mount_dir / file.filename(), test_file, fs::copy_option::overwrite_if_exists);
  bptime::ptime read_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(read_start_time, read_stop_time, size, kRead);
  ASSERT_TRUE(fs::exists(test_file, error_code));
  ASSERT_EQ(error_code.value(), 0);

  // Compare content in the two files...
  ASSERT_EQ(fs::file_size(g_mount_dir / file.filename()), fs::file_size(file));
  bptime::ptime compare_start_time(bptime::microsec_clock::universal_time());
  ASSERT_TRUE(this->CompareFileContents(g_mount_dir / file.filename(), file));
  bptime::ptime compare_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(compare_start_time, compare_stop_time, size, kCompare);
}

void CopyThenReadManySmallFiles() {
  std::vector<fs::path> directories;
  std::set<fs::path> files;
  uint32_t num_of_directories(100);
  uint32_t num_of_files(300);
  uint32_t max_filesize(102);
  uint32_t min_filesize(1);
  std::cout << "Creating a test tree with " << num_of_directories << " directories holding "
            << num_of_files << " files with file size range from "
            << BytesToBinarySiUnits(min_filesize) << " to "
            << BytesToBinarySiUnits(max_filesize) << std::endl;
  uint32_t total_data_size = CreateTestTreeStructure(g_test_mirror, directories, files,
                                                     num_of_directories, num_of_files,
                                                     max_filesize, min_filesize);

  // Copy test_tree to virtual drive...
  bptime::ptime copy_start_time(bptime::microsec_clock::universal_time());
  CopyRecursiveDirectory(directories.at(0), g_mount_dir);
  bptime::ptime copy_stop_time(bptime::microsec_clock::universal_time());
  PrintResult(copy_start_time, copy_stop_time, total_data_size, kCopy);

   // Read the test_tree back to a disk file...
   std::string str = directories.at(0).string();
   boost::algorithm::replace_first(str, g_test_mirror.string(), g_mount_dir.string());
   fs::path from_directory(str);
   fs::path read_back_directory(GenerateDirectory(g_test_mirror));
   bptime::ptime read_start_time(bptime::microsec_clock::universal_time());
   CopyRecursiveDirectory(from_directory, read_back_directory);
   bptime::ptime read_stop_time(bptime::microsec_clock::universal_time());
   PrintResult(read_start_time, read_stop_time, total_data_size, kRead);

   // Compare content in the two test_trees...
   bptime::ptime compare_start_time(bptime::microsec_clock::universal_time());
   for (auto it = files.begin(); it != files.end(); ++it) {
     std::string str = (*it).string();
     boost::algorithm::replace_first(str, g_test_mirror.string(), g_mount_dir.string());
     if (!fs::exists(str))
       Sleep(std::chrono::seconds(1));
     ASSERT_TRUE(fs::exists(str))  << "Missing " << str;
     ASSERT_TRUE(this->CompareFileContents(*it, str)) << "Comparing " << *it << " with " << str;
   }
   bptime::ptime compare_stop_time(bptime::microsec_clock::universal_time());
   PrintResult(compare_start_time, compare_stop_time, total_data_size, kCompare);

   for (size_t i = 0; i < directories.size(); ++i) {
     std::string str = directories[i].string();
     boost::algorithm::replace_first(str, g_test_mirror.string(), g_mount_dir.string());
     ASSERT_TRUE(fs::exists(str)) << "Missing " << str;
   }
}

}  // namespace test

}  // namespace maidsafe



int main(int argc, char** argv) {
  auto unused_options(maidsafe::log::Logging::Instance().Initialise(argc, argv));

  // Handle passing path to test root via command line
  po::options_description command_line_options("Command line options");
  std::string description("Path to root directory for test, e.g. " +
                          fs::temp_directory_path().string());
  bool no_big_test(false), no_small_test(false);
  command_line_options.add_options()
      ("help,h", "Show help message.")
      ("root", po::value<std::string>(), description.c_str())
      ("no_big_test", po::bool_switch(&no_big_test), "Disable single large file test.")
      ("no_small_test", po::bool_switch(&no_small_test), "Disable multiple small files test.");
  po::parsed_options parsed(po::command_line_parser(unused_options).
      options(command_line_options).allow_unregistered().run());

  po::variables_map variables_map;
  po::store(parsed, variables_map);
  po::notify(variables_map);

  if (variables_map.count("help")) {
    std::cout << command_line_options << '\n';
    return 0;
  }

  // Set up root_ directory
  maidsafe::test::root_ = maidsafe::GetPathFromProgramOptions("root", variables_map, true, false);
  if (!maidsafe::test::ValidateRoot())
    return -1;

  // Set up 'temp_' directory
  maidsafe::test::temp_ = fs::unique_path(fs::temp_directory_path() /
                                          "MaidSafe_Test_Filesystem_%%%%-%%%%-%%%%");
  if (!fs::create_directories(maidsafe::test::temp_)) {
    LOG(kWarning) << "Failed to create test directory " << maidsafe::test::temp_;
    return -2;
  }
  LOG(kInfo) << "Created test directory " << maidsafe::test::temp_;

  // Run benchmark tests
  if (!no_big_test)
    maidsafe::test::CopyThenReadLargeFile();
  if (!no_small_test)
    maidsafe::test::CopyThenReadManySmallFiles();

  // Clean up 'temp_' directory
  boost::system::error_code error_code;
  if (fs::remove_all(maidsafe::test::temp_, error_code) == 0) {
    LOG(kWarning) << "Failed to remove " << maidsafe::test::temp_;
  }
  if (error_code.value() != 0) {
    LOG(kWarning) << "Error removing " << maidsafe::test::temp_ << "  " << error_code.message();
  }

  return 0;
}
