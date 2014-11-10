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

#ifndef MAIDSAFE_DRIVE_FILE_H_
#define MAIDSAFE_DRIVE_FILE_H_

#include <memory>
#include <string>

#include "boost/asio/steady_timer.hpp"
#include "boost/filesystem/path.hpp"

#include "maidsafe/common/asio_service.h"
#include "maidsafe/common/config.h"
#include "maidsafe/common/data_buffer.h"

#include "maidsafe/drive/path.h"

namespace maidsafe {

namespace drive {

namespace detail {

class Directory;

class File : public Path {
 public:
  typedef DataBuffer<std::string> Buffer;

  // This class must always be constructed using a Create() call to ensure that it will be
  // a shared_ptr. See the private constructors for the argument lists.
  template <typename... Types>
  static std::shared_ptr<File> Create(Types&&... args) {
    std::shared_ptr<File> self(new File{std::forward<Types>(args)...});
    return self;
  }

  ~File();

  //
  // All public methods are thread-safe.
  //

  virtual std::string Serialise();
  virtual void Serialise(protobuf::Directory&,
                         std::vector<ImmutableData::Name>&);
  virtual void ScheduleForStoring();

  void Open(
      const std::function<NonEmptyString(const std::string&)>& get_chunk_from_store,
      const MemoryUsage max_memory_usage,
      const DiskUsage max_disk_usage,
      const boost::filesystem::path& disk_buffer_location);
  std::uint32_t Read(char* data, std::uint32_t length, std::uint64_t offset);
  std::uint32_t Write(const char* data, std::uint32_t length, std::uint64_t offset);
  void Truncate(std::uint64_t offset);
  void Close();

 private:
  File(
      boost::asio::io_service& asio_service,
      MetaData meta_data_in,
      std::shared_ptr<Directory> parent_in);
  File(
      boost::asio::io_service& asio_service,
      const boost::filesystem::path& name,
      bool is_directory);
  File(File&&) = delete;
  File& operator=(File) = delete;

  //
  // Private methods require caller to hold data_mutex_
  //

  bool HasBuffer() const;
  // Throw exception if file is not open
  void VerifyHasBuffer() const;

  void CloseEncryptor(
      std::vector<ImmutableData::Name>& chunks_to_be_incremented);

  void Serialise(protobuf::Path&);

 private:

  struct Data {

    // Stores some of the original constructor values that are encapsulated in
    // other objects. Needed to "flush" self encryptor (only close is given).
    struct OriginalParameters {
      OriginalParameters(
          const MemoryUsage max_memory_usage,
          const DiskUsage max_disk_usage,
          const boost::filesystem::path& disk_buffer_location,
          std::function<NonEmptyString(const std::string&)> get_chunk_from_store);

      OriginalParameters(OriginalParameters&& rhs); // alow move construction
      OriginalParameters(const OriginalParameters&) = delete;
      OriginalParameters& operator==(const OriginalParameters&) = delete;
      OriginalParameters& operator==(OriginalParameters&&) = delete;

      boost::filesystem::path disk_buffer_location_;
      std::function<NonEmptyString(const std::string&)> get_chunk_from_store_;
      MemoryUsage max_memory_usage_;
      DiskUsage max_disk_usage_;
    };

    Data(
        OriginalParameters original_parameters,
        const boost::filesystem::path& name,
        encrypt::DataMap& data_map);

    bool IsOpen() const { return open_count_ > 0; }

    OriginalParameters original_parameters_;
    Buffer buffer_;
    encrypt::SelfEncryptor self_encryptor_;
    unsigned open_count_;
  };

  std::unique_ptr<Data> file_data_;
  boost::asio::steady_timer close_timer_;
  std::mutex data_mutex_;
  // True if close completed since last serialisation
  bool skip_chunk_incrementing_;
};

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_FILE_H_
