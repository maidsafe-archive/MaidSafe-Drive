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

#ifndef MAIDSAFE_DRIVE_DRIVE_API_H_
#define MAIDSAFE_DRIVE_DRIVE_API_H_

#ifdef MAIDSAFE_WIN32
#ifdef HAVE_CBFS
#include "maidsafe/drive/win_drive.h"
#endif
#else
#include "maidsafe/drive/unix_drive.h"
#endif

namespace maidsafe {

namespace drive {

template <typename Storage>
struct VirtualDrive {
#ifdef MAIDSAFE_WIN32
#ifdef HAVE_CBFS
  typedef CbfsDrive<Storage> value_type;
#endif
#else
  typedef FuseDrive<Storage> value_type;
#endif
};

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DRIVE_API_H_
