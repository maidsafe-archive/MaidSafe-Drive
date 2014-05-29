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

#ifdef __FreeBSD__
extern "C" char **environ;
#endif

#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/program_options.hpp"
#include "boost/process.hpp"
#include "boost/process/initializers.hpp"

#include "maidsafe/common/error.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/process.h"

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

fs::path g_root, g_temp, g_storage;

std::function<void()> clean_root([] {
  boost::system::error_code error_code;
  fs::directory_iterator end;
  for (fs::directory_iterator directory_itr(g_root); directory_itr != end; ++directory_itr)
    fs::remove_all(*directory_itr, error_code);
});

fs::path GenerateFile(const fs::path& parent, size_t size) {
  if (size == 0)
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));

  auto file_name(parent / (RandomAlphaNumericString((RandomUint32() % 4) + 4) + ".txt"));

  std::ofstream output_stream(file_name.c_str(), std::ios::binary);
  if (!output_stream.is_open() || output_stream.bad())
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));

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
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
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
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
    if (fs::is_directory(*current)) {
      fs::copy_directory(current->path(), str, ec);
    } else {
      fs::copy_file(current->path(), str, fs::copy_option::overwrite_if_exists, ec);
    }
    if (!fs::exists(str))
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
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

  // Create file on disk
  size_t size = 300 * 1024 * 1024;
  fs::path file(GenerateFile(g_temp, size));
  if (!fs::exists(file) || fs::file_size(file) != size)
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));

  // Copy file to virtual drive
  auto copy_start_time(std::chrono::high_resolution_clock::now());
  fs::copy_file(file, g_root / file.filename(), fs::copy_option::fail_if_exists);
  auto copy_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(copy_start_time, copy_stop_time, size, "Copied");
  if (!fs::exists(g_root / file.filename()))
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));

  // Read the file back to a disk file
  // Because of the system caching, the pure read can't reflect the real speed
  fs::path test_file(g_temp / (RandomAlphaNumericString(5) + ".txt"));
  auto read_start_time(std::chrono::high_resolution_clock::now());
  fs::copy_file(g_root / file.filename(), test_file, fs::copy_option::overwrite_if_exists);
  auto read_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(read_start_time, read_stop_time, size, "Read");
  if (!fs::exists(test_file))
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));

  // Compare content in the two files
  if (fs::file_size(g_root / file.filename()) != fs::file_size(file))
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  auto compare_start_time(std::chrono::high_resolution_clock::now());
  if (!CompareFileContents(g_root / file.filename(), file))
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
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
  uint32_t total_data_size = CreateTestTreeStructure(g_temp, directories, files, num_of_directories,
                                                     num_of_files, max_filesize, min_filesize);

  // Copy test_tree to virtual drive
  auto copy_start_time(std::chrono::high_resolution_clock::now());
  CopyRecursiveDirectory(directories.at(0), g_root);
  auto copy_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(copy_start_time, copy_stop_time, total_data_size, "Copied");

  // Read the test_tree back to a disk file
  std::string str = directories.at(0).string();
  boost::algorithm::replace_first(str, g_temp.string(), g_root.string());
  fs::path from_directory(str);
  fs::path read_back_directory(GenerateDirectory(g_temp));
  auto read_start_time(std::chrono::high_resolution_clock::now());
  CopyRecursiveDirectory(from_directory, read_back_directory);
  auto read_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(read_start_time, read_stop_time, total_data_size, "Read");

  // Compare content in the two test_trees
  auto compare_start_time(std::chrono::high_resolution_clock::now());
  for (const auto& file : files) {
    auto str = file.string();
    boost::algorithm::replace_first(str, g_temp.string(), g_root.string());
    if (!fs::exists(str))
      Sleep(std::chrono::seconds(1));
    if (!fs::exists(str))
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
    if (!CompareFileContents(file, str))
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
  auto compare_stop_time(std::chrono::high_resolution_clock::now());
  PrintResult(compare_start_time, compare_stop_time, total_data_size, "Compared");

  for (const auto& directory : directories) {
    auto str = directory.string();
    boost::algorithm::replace_first(str, g_temp.string(), g_root.string());
    if (!fs::exists(str))
      BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));
  }
}

void CloneMaidSafeAndBuildDefaults(const fs::path& start_directory) {
  boost::system::error_code error_code;
  std::string command_args, cmake_generator(BOOST_PP_STRINGIZE(CMAKE_GENERATOR));
  fs::path resources_path(BOOST_PP_STRINGIZE(DRIVE_TESTS_RESOURCES)), script,
           shell_path(boost::process::shell_path());

#ifdef MAIDSAFE_WIN32
  fs::directory_iterator itr(resources_path), end;
  while (itr != end) {
    if (itr->path().filename().string() == "maidsafe.bat") {
      script = itr->path();
      break;
    }
    ++itr;
  }
  if (itr == end)
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::no_such_element));

  std::string vs_dev_cmd(BOOST_PP_STRINGIZE(VS_DEV_CMD));
  command_args = "/C " + script.filename().string() + " " + vs_dev_cmd + " " + cmake_generator;
#else
  fs::directory_iterator itr(resources_path), end;
  while (itr != end) {
    if (itr->path().filename().string() == "maidsafe.sh") {
      script = itr->path();
      break;
    }
    ++itr;
  }
  if (itr == end)
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::no_such_element));

  command_args = script.filename().string() + " " + cmake_generator;
#endif

  fs::copy_file(script, start_directory / script.filename().string(),
                fs::copy_option::fail_if_exists);
  if (!fs::exists(start_directory / script.filename().string(), error_code))
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::filesystem_io_error));

  std::vector<std::string> process_args;
  process_args.emplace_back(shell_path.string());
  process_args.emplace_back(command_args);
  const auto command_line(process::ConstructCommandLine(process_args));

  std::chrono::time_point<std::chrono::system_clock> start, stop;
  start = std::chrono::system_clock::now();

  boost::process::child child = boost::process::execute(
      boost::process::initializers::start_in_dir(start_directory.string()),
      boost::process::initializers::run_exe(shell_path),
      boost::process::initializers::set_cmd_line(command_line),
      boost::process::initializers::inherit_env(),
      boost::process::initializers::set_on_error(error_code));

  stop = std::chrono::system_clock::now();

  if (error_code.value() != 0)
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::unknown));
  auto exit_code = boost::process::wait_for_exit(child, error_code);
  if (error_code.value() != 0 || exit_code != 0)
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::unknown));

  std::chrono::duration<double> duration = stop - start;
  std::cout << "Test duration: " << duration.count() << " secs" << std::endl;
}

int RunTool(int argc, char** argv, const fs::path& root, const fs::path& temp,
            const fs::path& storage) {
  std::vector<std::string> arguments(argv, argv + argc);
  bool no_big_test(std::any_of(std::begin(arguments), std::end(arguments),
                               [](const std::string& arg) { return arg == "--no_big_test"; }));
  bool no_small_test(std::any_of(std::begin(arguments), std::end(arguments),
                                 [](const std::string& arg) { return arg == "--no_small_test"; }));
  bool no_clone_and_build_maidsafe_test(std::any_of(std::begin(arguments), std::end(arguments),
              [](const std::string& arg) { return arg == "--no_clone_and_build_maidsafe_test"; }));
  g_root = root;
  g_temp = temp;
  g_storage = storage;
  if (!no_big_test)
    CopyThenReadLargeFile();
  if (!no_small_test)
    CopyThenReadManySmallFiles();
  if (!no_clone_and_build_maidsafe_test) {
    CloneMaidSafeAndBuildDefaults(g_temp);
    CloneMaidSafeAndBuildDefaults(g_root);
    // RequireDirectoriesEqual(g_root, g_temp, false);
  }
  return 0;
}

}  // namespace test

}  // namespace maidsafe
