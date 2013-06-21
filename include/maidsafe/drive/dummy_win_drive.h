#ifndef MAIDSAFE_DRIVE_DUMMY_WIN_DRIVE_H
#define MAIDSAFE_DRIVE_DUMMY_WIN_DRIVE_H

#include "boost/filesystem/path.hpp"

#include "maidsafe/drive/drive_api.h"

namespace maidsafe {

namespace drive {

class DummyWinDriveInUserSpace : public DriveInUserSpace {
 public:
  DummyWinDriveInUserSpace(ClientNfs& client_nfs,
                           DataStore& data_store,
                           const Maid& maid,
                           const Identity& unique_user_id,
                           const std::string& root_parent_id,
                           const fs::path &mount_dir,
                           const fs::path &drive_name,
                           const int64_t &max_space,
                           const int64_t &used_space);
 virtual int Unmount(int64_t &max_space, int64_t &used_space);
 virtual void NotifyRename(const fs::path& /*from_relative_path*/,
                           const fs::path& /*to_relative_path*/) const;

 virtual void SetNewAttributes(FileContext* /*file_context*/,
                               bool /*is_directory*/,
                               bool /*read_only*/);
};

}  // namespace drive

}  // namespace maidsafe

#endif // MAIDSAFE_DRIVE_DUMMY_WIN_DRIVE_H
