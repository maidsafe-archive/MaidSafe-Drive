/*  Copyright 2013 MaidSafe.net limited

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

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
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

#ifdef _MSC_VER
// This function is needed to avoid use of po::bool_switch causing MSVC warning C4505:
// 'boost::program_options::typed_value<bool>::name' : unreferenced local function has been removed.
void UseUnreferenced() {
  auto dummy = po::typed_value<bool>(nullptr);
  static_cast<void>(dummy);
}
#endif

fs::path root_, temp_;

bool ValidateRoot() {
  if (root_.empty()) {
    LOG(kError)
        << "Failed to pass valid root directory.\nRun with '--root <path to empty root dir>'";
    return false;
  }

  boost::system::error_code error_code;
  if (!fs::is_directory(root_, error_code) || error_code) {
    LOG(kError) << root_ << " is not a directory.\nRun with '--root <path to empty root dir>'";
    return false;
  }

  if (!fs::is_empty(root_, error_code) || error_code) {
    LOG(kError) << root_ << " is not empty.\nRun with '--root <path to empty root dir>'";
    return false;
  }

  if (!WriteFile(root_ / "a.check", "check\n")) {
    LOG(kError) << root_ << " is not writable.\nRun with '--root <path to writable empty dir>'";
    return false;
  }
  fs::remove(root_ / "a.check", error_code);

  return true;
}

std::function<void()> clean_root([] {
  boost::system::error_code error_code;
  fs::directory_iterator end;
  for (fs::directory_iterator directory_itr(root_); directory_itr != end; ++directory_itr)
    fs::remove_all(*directory_itr, error_code);
});

fs::path GenerateFile(const fs::path& parent, size_t size) {
  if (size == 0)
    ThrowError(CommonErrors::invalid_parameter);

  auto file_name(parent / (RandomAlphaNumericString((RandomUint32() % 4) + 4) + ".txt"));

  std::ofstream output_stream(file_name.c_str(), std::ios::binary);
  if (!output_stream.is_open() || output_stream.bad())
    ThrowError(CommonErrors::filesystem_io_error);

  auto random_string(RandomString(1024 * 1024));
  size_t written(0);
  while (written < size) {
    size_t to_write(std::min(random_string.size(), size - written));
    output_stream.write(random_string.data(), to_write);
    written += to_write;
  }

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

uint32_t CreateTestTreeStructure(const fs::path& base_path, std::vector<fs::path>& directories,
                                 std::set<fs::path>& files, uint32_t directory_node_count,
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

void CopyRecursiveDirectory(const fs::path& src, const fs::path& dest) {
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
    if (!fs::exists(str))
      ThrowError(CommonErrors::filesystem_io_error);
  }
}

bool CompareFileContents(const fs::path& path1, const fs::path& path2) {
  std::ifstream efile, ofile;
  efile.open(path1.c_str());
  ofile.open(path2.c_str());

  if (efile.bad() || ofile.bad() || !efile.is_open() || !ofile.is_open())
    return false;
  while (efile.good() && ofile.good()) {
    if (efile.get() != ofile.get())
      return false;
  }
  return efile.eof() && ofile.eof();
}

void PrintResult(const std::chrono::high_resolution_clock::time_point& start,
                 const std::chrono::high_resolution_clock::time_point& stop, size_t size,
                 std::string action_type) {
  auto duration(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());
  if (duration == 0)
    duration = 1;
  uint64_t rate((static_cast<uint64_t>(size) * 1000000) / duration);
  printf("%s %s of data in %f seconds at a speed of %s/s\n", action_type.c_str(),
         BytesToBinarySiUnits(size).c_str(), (duration / 1000000.0),
         BytesToBinarySiUnits(rate).c_str());
}

}  // namespace

void CopyThenReadLargeFile() {
  on_scope_exit cleanup(clean_root);

  // Create file on disk...
  size_t size = 300 * 1024 * 1024;
  fs::path file(GenerateFile(temp_, size));
  if (!fs::exists(file) || fs::file_size(file) != size)
    ThrowError(CommonErrors::filesystem_io_error);

  // Copy file to virtual drive...
  auto copy_start_time(std::chrono::high_resolution_clock::now());
  fs::copy_file(file, root_ / file.filename(), fs::copy_option::fail_if_exists);
  auto copy_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(copy_start_time, copy_stop_time, size, "Copied");
  if (!fs::exists(root_ / file.filename()))
    ThrowError(CommonErrors::filesystem_io_error);

  // Read the file back to a disk file...
  // Because of the system caching, the pure read can't reflect the real speed
  fs::path test_file(temp_ / (RandomAlphaNumericString(5) + ".txt"));
  auto read_start_time(std::chrono::high_resolution_clock::now());
  fs::copy_file(root_ / file.filename(), test_file, fs::copy_option::overwrite_if_exists);
  auto read_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(read_start_time, read_stop_time, size, "Read");
  if (!fs::exists(test_file))
    ThrowError(CommonErrors::filesystem_io_error);

  // Compare content in the two files...
  if (fs::file_size(root_ / file.filename()) != fs::file_size(file))
    ThrowError(CommonErrors::filesystem_io_error);
  auto compare_start_time(std::chrono::high_resolution_clock::now());
  if (!CompareFileContents(root_ / file.filename(), file))
    ThrowError(CommonErrors::filesystem_io_error);
  auto compare_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(compare_start_time, compare_stop_time, size, "Compared");
}

void CopyThenReadManySmallFiles() {
  on_scope_exit cleanup(clean_root);

  std::vector<fs::path> directories;
  std::set<fs::path> files;
  uint32_t num_of_directories(100);
  uint32_t num_of_files(300);
  uint32_t max_filesize(102);
  uint32_t min_filesize(1);
  std::cout << "Creating a test tree with " << num_of_directories << " directories holding "
            << num_of_files << " files with file size range from "
            << BytesToBinarySiUnits(min_filesize) << " to " << BytesToBinarySiUnits(max_filesize)
            << '\n';
  uint32_t total_data_size = CreateTestTreeStructure(temp_, directories, files, num_of_directories,
                                                     num_of_files, max_filesize, min_filesize);

  // Copy test_tree to virtual drive...
  auto copy_start_time(std::chrono::high_resolution_clock::now());
  CopyRecursiveDirectory(directories.at(0), root_);
  auto copy_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(copy_start_time, copy_stop_time, total_data_size, "Copied");

  // Read the test_tree back to a disk file...
  std::string str = directories.at(0).string();
  boost::algorithm::replace_first(str, temp_.string(), root_.string());
  fs::path from_directory(str);
  fs::path read_back_directory(GenerateDirectory(temp_));
  auto read_start_time(std::chrono::high_resolution_clock::now());
  CopyRecursiveDirectory(from_directory, read_back_directory);
  auto read_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(read_start_time, read_stop_time, total_data_size, "Read");

  // Compare content in the two test_trees...
  auto compare_start_time(std::chrono::high_resolution_clock::now());
  for (const auto& file : files) {
    auto str = file.string();
    boost::algorithm::replace_first(str, temp_.string(), root_.string());
    if (!fs::exists(str))
      Sleep(std::chrono::seconds(1));
    if (!fs::exists(str))
      ThrowError(CommonErrors::filesystem_io_error);
    if (!CompareFileContents(file, str))
      ThrowError(CommonErrors::filesystem_io_error);
  }
  auto compare_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(compare_start_time, compare_stop_time, total_data_size, "Compared");

  for (const auto& directory : directories) {
    auto str = directory.string();
    boost::algorithm::replace_first(str, temp_.string(), root_.string());
    if (!fs::exists(str))
      ThrowError(CommonErrors::filesystem_io_error);
  }
}

}  // namespace test

}  // namespace maidsafe

int main(int argc, char** argv) {
  auto unuseds(maidsafe::log::Logging::Instance().Initialise(argc, argv));

  try {
    // Handle passing path to test root via command line
    po::options_description command_line_options("Command line options");
    std::string description("Path to root directory for test, e.g. " +
                            fs::temp_directory_path().string());
    bool no_big_test(false), no_small_test(false);
    command_line_options.add_options()("help,h", "Show help message.")(
        "root", po::value<std::string>(), description.c_str())(
        "no_big_test", po::bool_switch(&no_big_test), "Disable single large file test.")(
        "no_small_test", po::bool_switch(&no_small_test), "Disable multiple small files test.");
    std::vector<std::string> unused_options;
    for (size_t i(1); i < unuseds.size(); ++i)
      unused_options.emplace_back(&unuseds[i][0]);
    po::parsed_options parsed(po::command_line_parser(unused_options)
                                  .options(command_line_options)
                                  .allow_unregistered()
                                  .run());

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
    maidsafe::test::temp_ =
        fs::unique_path(fs::temp_directory_path() / "MaidSafe_Test_Filesystem_%%%%-%%%%-%%%%");
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
  }
  catch (const std::exception& e) {
    std::cout << "Error: " << e.what() << '\n';
    return -3;
  }
  return 0;
}
