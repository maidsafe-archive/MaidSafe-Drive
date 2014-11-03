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

#ifndef MAIDSAFE_DRIVE_SYMLINK_H_
#define MAIDSAFE_DRIVE_SYMLINK_H_

#include "maidsafe/drive/path.h"

#include "boost/filesystem/path.hpp"

namespace maidsafe
{
namespace drive
{
namespace detail
{

class Symlink : public Path {
 public:
  template <typename... Types>
  static std::shared_ptr<Symlink> Create(Types&&... args) {
    std::shared_ptr<Symlink> self(new Symlink{std::forward<Types>(args)...});
    return self;
  }

  virtual std::string Serialise();
  virtual void Serialise(protobuf::Directory&,
                         std::vector<ImmutableData::Name>&);
  virtual void ScheduleForStoring();

  boost::filesystem::path Target() const;

 private:
  Symlink();
  Symlink(const boost::filesystem::path& target,
          const boost::filesystem::path& source);

  Symlink(Symlink&&) = delete;
  Symlink& operator=(Symlink) = delete;

  void Serialise(protobuf::Path&);

 private:
  boost::filesystem::path target;
  boost::filesystem::path source;
};

} // namespace detail
} // namespace drive
} // namespace maidsafe

#endif // MAIDSAFE_DRIVE_SYMLINK_H_
