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

#ifndef MAIDSAFE_DRIVE_PATH_H_
#define MAIDSAFE_DRIVE_PATH_H_

#include <memory>
#include <atomic>
#include <thread>
#include <string>

#include "maidsafe/common/config.h"
#include "maidsafe/encrypt/self_encryptor.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/proto_structs.pb.h"

namespace maidsafe {

namespace drive {

namespace detail {

class Directory;

class Path : public std::enable_shared_from_this<Path> {
 public:
  class Listener {
  private:
    virtual void PathPut(std::shared_ptr<Path>) = 0;
    virtual void PathPutChunk(const ImmutableData&) = 0;
    virtual void PathIncrementChunks(const std::vector<ImmutableData::Name>&) = 0;

  public:

    virtual ~Listener() {}

    void Put(std::shared_ptr<Path> path) {
      PathPut(path);
    }

    void PutChunk(const ImmutableData& data) {
      PathPutChunk(data);
    }

    void IncrementChunks(const std::vector<ImmutableData::Name>& names) {
      PathIncrementChunks(names);
    }
  };

  ~Path() {}

  virtual std::string Serialise() = 0;
  virtual void Serialise(protobuf::Directory&,
                         std::vector<ImmutableData::Name>&) = 0;
  virtual void ScheduleForStoring() = 0;

  std::shared_ptr<Directory> Parent() const;
  void SetParent(std::shared_ptr<Directory>);
  std::shared_ptr<Listener> GetListener() const;

 protected:
  explicit Path(MetaData::FileType);
  Path(const Path&) = delete;
  Path(std::shared_ptr<Directory> parent, MetaData::FileType);

 protected:
  std::weak_ptr<Listener> listener_;

 private:
  std::weak_ptr<Directory> parent_;

 public:
  MetaData meta_data;
};

bool operator<(const Path& lhs, const Path& rhs);

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_PATH_H_
