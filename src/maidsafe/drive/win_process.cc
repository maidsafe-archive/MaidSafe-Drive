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

#include "maidsafe/drive/win_process.h"

#include <utility>

#include "maidsafe/common/make_unique.h"

#include <iostream>
namespace maidsafe {
namespace drive {
namespace detail {

WinProcess::WinProcess()
  : process_handle_(),
    sid_memory_() {
  {
    HANDLE temp_handle{};
    const bool fail =
        (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &temp_handle) == 0);
    process_handle_.reset(temp_handle);

    if (fail) {
      return;
    }
  }

  DWORD user_token_size = 0;
  if (!GetTokenInformation(process_handle_.get(), TokenOwner, NULL, 0, &user_token_size)) {
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      return;
    }
  }

  sid_memory_ = maidsafe::make_unique<char[]>(user_token_size);
  if (!GetTokenInformation(
        process_handle_.get(),
        TokenOwner,
        sid_memory_.get(),
        user_token_size,
        &user_token_size)) {
    sid_memory_.reset();
    return;
  }
}

} // detail
}  // drive
} // maidsafe