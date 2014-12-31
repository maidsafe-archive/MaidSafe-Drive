/*  Copyright 2014 MaidSafe.net limited

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
#include <cassert>
#include <memory>
#include <stdexcept>
#include <unordered_map>

#include "maidsafe/common/asio_service.h"
#include "maidsafe/common/config.h"
#include "maidsafe/common/data_types/immutable_data.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/test.h"
#include "maidsafe/drive/directory.h"
#include "maidsafe/drive/file.h"

namespace maidsafe {
namespace drive {
namespace detail {
namespace test {

const std::uint32_t kTestMemoryUsageMax = kMaxChunkSize;
const std::uint32_t kTestDiskUsageMax = kTestMemoryUsageMax;

namespace {
class TestListener : public Directory::Listener {
 public:
  boost::optional<std::pair<NonEmptyString, unsigned>> GetChunk(const std::string& name) const {
    const auto find_iter = chunk_map_.find(name);
    if (find_iter != chunk_map_.end()) {
      return find_iter->second;
    }

    return boost::none;
  }

  std::size_t TotalChunksStored() const { return chunk_map_.size(); }

 private:
  virtual void DirectoryPut(std::shared_ptr<Directory>) override {}
  virtual boost::future<void> DirectoryPutChunk(const ImmutableData& data) override {
    auto& map_storage = chunk_map_[data.name().value.string()];
    if (map_storage.second == 0) {
      map_storage.first = data.data();
    } else {
      if (map_storage.first != data.data()) {
        ADD_FAILURE() << "Two chunks with same key were not expected";
      }
    }
    ++(map_storage.second);
    return boost::make_ready_future();
  }

  virtual void DirectoryIncrementChunks(
      const std::vector<ImmutableData::Name>& increment) override {
    for (const auto& name : increment) {
      const auto find_iter = chunk_map_.find(name.value.string());
      if (find_iter != chunk_map_.end()) {
        ++(find_iter->second.second);
      } else {
        ADD_FAILURE() << "Request to increment chunk that does not exist";
      }
    }
  }

 private:
  std::unordered_map<std::string, std::pair<NonEmptyString, unsigned>> chunk_map_;
};

class FileTests : public ::testing::Test {
 protected:
  FileTests()
      : ::testing::Test(),
        asio_service_(),
        test_listener_(std::make_shared<TestListener>()),
        test_directory_(),
        test_path_() {}

  void ExpectChunks(const std::vector<std::pair<std::string, unsigned>>& expected) const {
    EXPECT_EQ(expected.size(), test_listener_->TotalChunksStored());
    for (const auto& expect : expected) {
      const auto actual = test_listener_->GetChunk(HexDecode(expect.first));
      if (actual) {
        EXPECT_EQ(expect.second, actual->second) << "Incorrect count on chunk " << expect.first;
      } else {
        ADD_FAILURE() << "Missing expected chunk " << expect.first;
      }
    }
  }

  void WaitForHandlers(const std::size_t number_handlers) {
    std::size_t completed = 0;
    unsigned iterations = 0;
    do {
      ASSERT_GE(3u, iterations);
      ++iterations;

      std::this_thread::sleep_for(detail::kFileInactivityDelay);
      asio_service_.reset();
      completed += asio_service_.poll();
    } while (completed < number_handlers);

    EXPECT_EQ(number_handlers, completed);
  }

  std::shared_ptr<File> CreateTestFile() { return File::Create(asio_service_, "foo", false); }

  // This isn't called automatically so that WaitForHandlers can identify
  // the close handler specifically in some tests (otherwise its 1 of 2
  // handlers executed).
  void SetListener(File& test_file) {
    if (test_directory_ == nullptr) {
      const boost::filesystem::path parent("test");
      const boost::filesystem::path child(parent / "path");

      test_directory_ = Directory::Create(ParentId(crypto::Hash<crypto::SHA512>(parent.string())),
                                          DirectoryId(crypto::Hash<crypto::SHA512>(child.string())),
                                          asio_service_, test_listener_, child);
    }

    test_file.SetParent(test_directory_);
  }

  void OpenTestFile(File& test_file) {
    if (test_path_ == nullptr) {
      test_path_ = ::maidsafe::test::CreateTestPath("MaidSafe_Test_Drive");
      if (test_path_ == nullptr || test_path_->string() == "") {
        throw std::runtime_error("Unable to create test path");
      }
    }

    const auto listener = test_listener_;
    test_file.Open([listener](const std::string& name) {
                     const auto chunk = listener->GetChunk(name);
                     if (chunk) {
                       return chunk->first;
                     }
                     BOOST_THROW_EXCEPTION(std::runtime_error("unexpected chunk missing"));
                   },
                   MemoryUsage(kTestMemoryUsageMax), DiskUsage(kTestDiskUsageMax), *test_path_);
  }

  static std::uint32_t WriteTestFile(File& test_file, const std::string contents,
                                     const std::uint32_t offset) {
    assert(contents.size() <= std::numeric_limits<std::uint32_t>::max());
    return test_file.Write(contents.data(), std::uint32_t(contents.size()), offset);
  }

  static std::string ReadTestFile(File& test_file, const std::uint32_t length,
                                  const std::uint32_t offset) {
    if (length == 0) {
      return std::string();
    }

    std::string file_contents;
    file_contents.resize(length);
    file_contents.resize(test_file.Read(&file_contents[0], length, offset));
    return file_contents;
  }

  static std::string ReadTestFile(File& test_file) {
    const auto file_size = test_file.meta_data.size();
    assert(file_size <= std::numeric_limits<std::uint32_t>::max());
    const std::string file_contents = ReadTestFile(test_file, std::uint32_t(file_size), 0);
    EXPECT_EQ(file_size, file_contents.size());
    return file_contents;
  }

 private:
  boost::asio::io_service asio_service_;
  const std::shared_ptr<TestListener> test_listener_;
  std::shared_ptr<Directory> test_directory_;
  ::maidsafe::test::TestPath test_path_;
};
}  // anonymous


TEST_F(FileTests, BEH_EmptyFile) {
  const std::shared_ptr<File> test_file = CreateTestFile();
  ASSERT_NE(nullptr, test_file.get());

  EXPECT_EQ("foo", test_file->meta_data.name().string());
  EXPECT_NE(nullptr, test_file->meta_data.data_map());
  EXPECT_EQ(nullptr, test_file->meta_data.directory_id());

  const auto creation_time = test_file->meta_data.creation_time();
  EXPECT_EQ(creation_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_access_time());

  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());
}

TEST_F(FileTests, BEH_WriteReadFile) {
  const std::shared_ptr<File> test_file = CreateTestFile();
  ASSERT_NE(nullptr, test_file.get());
  EXPECT_EQ("foo", test_file->meta_data.name().string());
  EXPECT_NE(nullptr, test_file->meta_data.data_map());
  EXPECT_EQ(nullptr, test_file->meta_data.directory_id());
  const auto creation_time = test_file->meta_data.creation_time();
  EXPECT_EQ(creation_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  const on_scope_exit close_file([test_file] { test_file->Close(); });
  OpenTestFile(*test_file);

  const std::string test_output("output text");
  EXPECT_EQ(test_output.size(), WriteTestFile(*test_file, test_output, 0));
  const auto last_write_time = test_file->meta_data.last_write_time();
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_LE(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(test_output.size(), test_file->meta_data.size());
  EXPECT_EQ(test_output.size(), test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  EXPECT_EQ(test_output, ReadTestFile(*test_file));
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_status_time());
  EXPECT_LE(last_write_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(test_output.size(), test_file->meta_data.size());
  EXPECT_EQ(test_output.size(), test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());
}

TEST_F(FileTests, BEH_ReadPastEnd) {
  const std::shared_ptr<File> test_file = CreateTestFile();
  ASSERT_NE(nullptr, test_file.get());
  EXPECT_EQ("foo", test_file->meta_data.name().string());
  EXPECT_NE(nullptr, test_file->meta_data.data_map());
  EXPECT_EQ(nullptr, test_file->meta_data.directory_id());
  const auto creation_time = test_file->meta_data.creation_time();
  EXPECT_EQ(creation_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  const on_scope_exit close_file([test_file] { test_file->Close(); });
  OpenTestFile(*test_file);

  EXPECT_EQ(std::string(), ReadTestFile(*test_file, 100, 0));
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_status_time());
  EXPECT_LE(creation_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  const std::string test_output(100, 'f');
  EXPECT_EQ(test_output.size(), WriteTestFile(*test_file, test_output, 0));
  const auto last_write_time = test_file->meta_data.last_write_time();
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_LE(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(test_output.size(), test_file->meta_data.size());
  EXPECT_EQ(test_output.size(), test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  EXPECT_EQ(std::string(), ReadTestFile(*test_file, 100, 101));
  const auto last_read_time = test_file->meta_data.last_write_time();
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_status_time());
  EXPECT_LE(last_write_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(test_output.size(), test_file->meta_data.size());
  EXPECT_EQ(test_output.size(), test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  EXPECT_EQ(std::string(50, 'f'), ReadTestFile(*test_file, 100, 50));
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_status_time());
  EXPECT_LE(last_read_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(test_output.size(), test_file->meta_data.size());
  EXPECT_EQ(test_output.size(), test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());
}

TEST_F(FileTests, BEH_TruncateIncrease) {
  const std::shared_ptr<File> test_file = CreateTestFile();
  ASSERT_NE(nullptr, test_file.get());
  EXPECT_EQ("foo", test_file->meta_data.name().string());
  EXPECT_NE(nullptr, test_file->meta_data.data_map());
  EXPECT_EQ(nullptr, test_file->meta_data.directory_id());
  const auto creation_time = test_file->meta_data.creation_time();
  EXPECT_EQ(creation_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  const on_scope_exit close_file([test_file] { test_file->Close(); });
  OpenTestFile(*test_file);

  const std::size_t new_file_size = 100;
  test_file->Truncate(new_file_size);
  const auto last_write_time = test_file->meta_data.last_write_time();
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_LE(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(new_file_size, test_file->meta_data.size());
  EXPECT_EQ(new_file_size, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  EXPECT_EQ(std::string(new_file_size, '\0'), ReadTestFile(*test_file));
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_status_time());
  EXPECT_LE(last_write_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(new_file_size, test_file->meta_data.size());
  EXPECT_EQ(new_file_size, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());
}

TEST_F(FileTests, BEH_TruncateDecrease) {
  const std::shared_ptr<File> test_file = CreateTestFile();
  ASSERT_NE(nullptr, test_file.get());
  EXPECT_EQ("foo", test_file->meta_data.name().string());
  EXPECT_NE(nullptr, test_file->meta_data.data_map());
  EXPECT_EQ(nullptr, test_file->meta_data.directory_id());
  const auto creation_time = test_file->meta_data.creation_time();
  EXPECT_EQ(creation_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(creation_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  const on_scope_exit close_file([test_file] { test_file->Close(); });
  OpenTestFile(*test_file);

  const std::string test_output(100, 'f');
  EXPECT_EQ(test_output.size(), WriteTestFile(*test_file, test_output, 0));
  const auto first_write_time = test_file->meta_data.last_write_time();
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_LE(creation_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(first_write_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(first_write_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(test_output.size(), test_file->meta_data.size());
  EXPECT_EQ(test_output.size(), test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  const std::size_t new_file_size = 50;
  test_file->Truncate(new_file_size);
  const auto last_write_time = test_file->meta_data.last_write_time();
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_LE(first_write_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_status_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(new_file_size, test_file->meta_data.size());
  EXPECT_EQ(new_file_size, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());

  EXPECT_EQ(std::string(new_file_size, 'f'), ReadTestFile(*test_file));
  EXPECT_EQ(creation_time, test_file->meta_data.creation_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_write_time());
  EXPECT_EQ(last_write_time, test_file->meta_data.last_status_time());
  EXPECT_LE(last_write_time, test_file->meta_data.last_access_time());
  EXPECT_EQ(new_file_size, test_file->meta_data.size());
  EXPECT_EQ(new_file_size, test_file->meta_data.allocation_size());
  EXPECT_EQ(MetaData::FileType::regular_file, test_file->meta_data.file_type());
}

TEST_F(FileTests, BEH_CloseTimer) {
  const std::shared_ptr<File> test_file = CreateTestFile();
  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());

  const std::size_t file_size = 500;
  {
    const on_scope_exit close_file([test_file] { test_file->Close(); });
    OpenTestFile(*test_file);
    test_file->Truncate(file_size);
    EXPECT_EQ(file_size, test_file->meta_data.size());
    EXPECT_EQ(file_size, test_file->meta_data.allocation_size());
  }

  WaitForHandlers(1);
  EXPECT_EQ(file_size, test_file->meta_data.size());
  EXPECT_EQ(file_size, test_file->meta_data.allocation_size());
}

TEST_F(FileTests, BEH_ExceedMaxDiskUsage) {
  const std::shared_ptr<File> test_file = CreateTestFile();
  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());

  const std::string random_data(RandomString((kTestMemoryUsageMax + kTestDiskUsageMax) * 2));
  {
    const on_scope_exit close_file([test_file] { test_file->Close(); });
    OpenTestFile(*test_file);
    WriteTestFile(*test_file, random_data, 0);
    EXPECT_EQ(random_data.size(), test_file->meta_data.size());
    EXPECT_EQ(random_data.size(), test_file->meta_data.allocation_size());
  }

  // This should throw an exception once the chunks are properly being stored
  EXPECT_THROW(WaitForHandlers(1), maidsafe::common_error);
}

TEST_F(FileTests, BEH_FlushFile) {
  /* Compression appears to differ slightly in windows, so this test was
    designed so that each chunk has a single value (the simple case
    for compression). Keep that in mind when updating. */
  const std::shared_ptr<File> test_file = CreateTestFile();
  SetListener(*test_file);
  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());

  const std::string original_file_contents(9000, 'e');
  std::string final_file_contents = original_file_contents;
  {
    const on_scope_exit close_file([test_file] { test_file->Close(); });
    OpenTestFile(*test_file);
    EXPECT_EQ(original_file_contents.size(), WriteTestFile(*test_file, original_file_contents, 0));
    EXPECT_EQ(original_file_contents.size(), test_file->meta_data.size());
    EXPECT_EQ(original_file_contents.size(), test_file->meta_data.allocation_size());

    // Flush (serialise)
    {
      protobuf::Directory actual_proto;
      std::vector<ImmutableData::Name> actual_chunks;
      test_file->Serialise(actual_proto, actual_chunks);

      EXPECT_TRUE(actual_chunks.empty());
      ASSERT_EQ(1, actual_proto.children_size());
      EXPECT_STREQ("foo", actual_proto.children(0).name().c_str());
      EXPECT_EQ(protobuf::Attributes::REGULAR_FILE_TYPE,
                actual_proto.children(0).attributes().file_type());
      EXPECT_EQ(original_file_contents.size(), actual_proto.children(0).attributes().st_size());
    }

    ExpectChunks(
        {{"819bf9270976e417e30f3d4d2a5b134173f01060cc8b98487487de6ab624e51c"
          "d406a6a934ca34156bbc2d91e06babf17155f692f3c04d42e88083080f67ee3f",
          3}});

    EXPECT_EQ(original_file_contents, ReadTestFile(*test_file));

    // Flush again (no changes this time)
    {
      protobuf::Directory actual_proto;
      std::vector<ImmutableData::Name> actual_chunks;
      test_file->Serialise(actual_proto, actual_chunks);

      EXPECT_EQ(std::vector<ImmutableData::Name>({
                    ImmutableData::Name(Identity(HexDecode(
                        "819bf9270976e417e30f3d4d2a5b134173f01060cc8b98487487de6ab624e51c"
                        "d406a6a934ca34156bbc2d91e06babf17155f692f3c04d42e88083080f67ee3f"))),
                    ImmutableData::Name(Identity(HexDecode(
                        "819bf9270976e417e30f3d4d2a5b134173f01060cc8b98487487de6ab624e51c"
                        "d406a6a934ca34156bbc2d91e06babf17155f692f3c04d42e88083080f67ee3f"))),
                    ImmutableData::Name(Identity(HexDecode(
                        "819bf9270976e417e30f3d4d2a5b134173f01060cc8b98487487de6ab624e51c"
                        "d406a6a934ca34156bbc2d91e06babf17155f692f3c04d42e88083080f67ee3f"))),
                }),
                actual_chunks);
      ASSERT_EQ(1, actual_proto.children_size());
      EXPECT_STREQ("foo", actual_proto.children(0).name().c_str());
      EXPECT_EQ(protobuf::Attributes::REGULAR_FILE_TYPE,
                actual_proto.children(0).attributes().file_type());
      EXPECT_EQ(original_file_contents.size(), actual_proto.children(0).attributes().st_size());
    }

    ExpectChunks(
        {{"819bf9270976e417e30f3d4d2a5b134173f01060cc8b98487487de6ab624e51c"
          "d406a6a934ca34156bbc2d91e06babf17155f692f3c04d42e88083080f67ee3f",
          3}});

    const std::string new_contents(4000, 'g');
    final_file_contents.resize(8000);
    final_file_contents += new_contents;
    EXPECT_EQ(new_contents.size(), WriteTestFile(*test_file, new_contents, 8000));
    EXPECT_EQ(final_file_contents.size(), test_file->meta_data.size());
    EXPECT_EQ(final_file_contents.size(), test_file->meta_data.allocation_size());

    EXPECT_EQ(final_file_contents, ReadTestFile(*test_file));
  }

  WaitForHandlers(4);
  EXPECT_EQ(final_file_contents.size(), test_file->meta_data.size());
  EXPECT_EQ(final_file_contents.size(), test_file->meta_data.allocation_size());

  ExpectChunks({{"819bf9270976e417e30f3d4d2a5b134173f01060cc8b98487487de6ab624e51c"
                 "d406a6a934ca34156bbc2d91e06babf17155f692f3c04d42e88083080f67ee3f",
                 3},
                {"216de0158db01e6b24fdc0487f10172d7c00009431a1f3205c412ac1fe73fe04"
                 "a8dbca19829f32daa3783d41c7a124f9e0d2c4d22e76f1605fa95c37e8a398b1",
                 1},
                {"77bea1dc1e74a4aa27454c0fb0e135ebdf53e8c647e777ed8e40eabbe5e0f822"
                 "3fbb9b0ed36210bd3c461ede01eb00a8b0a3b7760678feed6f5cab5f25885e89",
                 1},
                {"eee43d725f94d6b6a9cca52f04e44fe0238b48337328b437f69ce7660aec5a1c"
                 "1e81fa7f3a4108acffda4c8619fc4677f80ef5cdc4d8a1a15598fae3bbde547f",
                 1}});
}

TEST_F(FileTests, BEH_FileReopen) {
  /* Compression appears to differ slightly in windows, so this test was
    designed so that each chunk has a single value (the simple case
    for compression). Keep that in mind when updating. */
  const std::shared_ptr<File> test_file = CreateTestFile();
  SetListener(*test_file);
  EXPECT_EQ(0u, test_file->meta_data.size());
  EXPECT_EQ(0u, test_file->meta_data.allocation_size());

  const std::string original_file_contents(9000, 'e');
  {
    const on_scope_exit close_file([test_file] { test_file->Close(); });
    OpenTestFile(*test_file);
    EXPECT_EQ(original_file_contents.size(), WriteTestFile(*test_file, original_file_contents, 0));
    EXPECT_EQ(original_file_contents.size(), test_file->meta_data.size());
    EXPECT_EQ(original_file_contents.size(), test_file->meta_data.allocation_size());
  }

  WaitForHandlers(3);
  EXPECT_EQ(original_file_contents.size(), test_file->meta_data.size());
  EXPECT_EQ(original_file_contents.size(), test_file->meta_data.allocation_size());
  ExpectChunks(
      {{"819bf9270976e417e30f3d4d2a5b134173f01060cc8b98487487de6ab624e51c"
        "d406a6a934ca34156bbc2d91e06babf17155f692f3c04d42e88083080f67ee3f",
        3}});

  std::string final_file_contents = original_file_contents;
  {
    const on_scope_exit close_file([test_file] { test_file->Close(); });
    OpenTestFile(*test_file);
    EXPECT_EQ(original_file_contents, ReadTestFile(*test_file));

    const std::string new_contents(4000, 'g');
    final_file_contents.resize(8000);
    final_file_contents += new_contents;
    EXPECT_EQ(new_contents.size(), WriteTestFile(*test_file, new_contents, 8000));
    EXPECT_EQ(final_file_contents.size(), test_file->meta_data.size());
    EXPECT_EQ(final_file_contents.size(), test_file->meta_data.allocation_size());

    EXPECT_EQ(final_file_contents, ReadTestFile(*test_file));
  }

  WaitForHandlers(2);
  EXPECT_EQ(final_file_contents.size(), test_file->meta_data.size());
  EXPECT_EQ(final_file_contents.size(), test_file->meta_data.allocation_size());

  ExpectChunks({{"819bf9270976e417e30f3d4d2a5b134173f01060cc8b98487487de6ab624e51c"
                 "d406a6a934ca34156bbc2d91e06babf17155f692f3c04d42e88083080f67ee3f",
                 3},
                {"216de0158db01e6b24fdc0487f10172d7c00009431a1f3205c412ac1fe73fe04"
                 "a8dbca19829f32daa3783d41c7a124f9e0d2c4d22e76f1605fa95c37e8a398b1",
                 1},
                {"77bea1dc1e74a4aa27454c0fb0e135ebdf53e8c647e777ed8e40eabbe5e0f822"
                 "3fbb9b0ed36210bd3c461ede01eb00a8b0a3b7760678feed6f5cab5f25885e89",
                 1},
                {"eee43d725f94d6b6a9cca52f04e44fe0238b48337328b437f69ce7660aec5a1c"
                 "1e81fa7f3a4108acffda4c8619fc4677f80ef5cdc4d8a1a15598fae3bbde547f",
                 1}});

  // Flush (serialise) - no incrementing
  {
    protobuf::Directory actual_proto;
    std::vector<ImmutableData::Name> actual_chunks;
    test_file->Serialise(actual_proto, actual_chunks);

    EXPECT_TRUE(actual_chunks.empty());
    ASSERT_EQ(1, actual_proto.children_size());
    EXPECT_STREQ("foo", actual_proto.children(0).name().c_str());
    EXPECT_EQ(protobuf::Attributes::REGULAR_FILE_TYPE,
              actual_proto.children(0).attributes().file_type());
    EXPECT_EQ(final_file_contents.size(), actual_proto.children(0).attributes().st_size());
  }
  // Flush again (no changes this time)
  {
    protobuf::Directory actual_proto;
    std::vector<ImmutableData::Name> actual_chunks;
    test_file->Serialise(actual_proto, actual_chunks);

    EXPECT_EQ(std::vector<ImmutableData::Name>({
                  ImmutableData::Name(Identity(HexDecode(
                      "216de0158db01e6b24fdc0487f10172d7c00009431a1f3205c412ac1fe73fe04"
                      "a8dbca19829f32daa3783d41c7a124f9e0d2c4d22e76f1605fa95c37e8a398b1"))),
                  ImmutableData::Name(Identity(HexDecode(
                      "77bea1dc1e74a4aa27454c0fb0e135ebdf53e8c647e777ed8e40eabbe5e0f822"
                      "3fbb9b0ed36210bd3c461ede01eb00a8b0a3b7760678feed6f5cab5f25885e89"))),
                  ImmutableData::Name(Identity(HexDecode(
                      "eee43d725f94d6b6a9cca52f04e44fe0238b48337328b437f69ce7660aec5a1c"
                      "1e81fa7f3a4108acffda4c8619fc4677f80ef5cdc4d8a1a15598fae3bbde547f"))),
              }),
              actual_chunks);
    ASSERT_EQ(1, actual_proto.children_size());
    EXPECT_STREQ("foo", actual_proto.children(0).name().c_str());
    EXPECT_EQ(protobuf::Attributes::REGULAR_FILE_TYPE,
              actual_proto.children(0).attributes().file_type());
    EXPECT_EQ(final_file_contents.size(), actual_proto.children(0).attributes().st_size());
  }
}

}  // test
}  // detail
}  // drive
}  // maidsafe
