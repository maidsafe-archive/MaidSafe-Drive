/*  Copyright 2011 MaidSafe.net limited

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

#include "maidsafe/drive/win_drive.h"

#include "maidsafe/common/log.h"

#include "maidsafe/drive/cbfs_key.h"

namespace maidsafe {

namespace drive {

namespace {

DWORD GetDisableLastAccessUpdateRegKey() {
  HKEY handle_to_key(nullptr);
  std::wstring name(L"SYSTEM\\CurrentControlSet\\Control\\FileSystem");
  auto open_result(RegOpenKeyExW(HKEY_LOCAL_MACHINE, name.c_str(), NULL, KEY_READ, &handle_to_key));

  // If the key doesn't exist, the meaning is equivalent to the key's value being 0.
  if (open_result == ERROR_FILE_NOT_FOUND)
    return 0;
  // If we can't access the key, assume the value is 1 (the default for Windows 7 onwards).
  if (open_result != ERROR_SUCCESS)
    return 1;

  DWORD value_data(0);
  DWORD buffer_size(sizeof(value_data));
  std::wstring value_name(L"NtfsDisableLastAccessUpdate");
  auto query_result(RegQueryValueExW(handle_to_key, value_name.c_str(), NULL, NULL,
                                     reinterpret_cast<LPBYTE>(&value_data), &buffer_size));
  // If the key doesn't exist, the meaning is equivalent to the key's value being 0.
  if (query_result == ERROR_FILE_NOT_FOUND)
    return 0;
  // If we can't access the key, assume the value is 1 (the default for Windows 7 onwards).
  return query_result == ERROR_SUCCESS ? value_data : 1;
}

}  // unnamed namespace

namespace detail {

bool LastAccessUpdateIsDisabled() {
  static const bool kIsDisabled(GetDisableLastAccessUpdateRegKey() == 1U);
  return kIsDisabled;
}

bool SetAttributes(DWORD& attributes, DWORD new_value) {
  if (new_value != 0 && attributes != new_value) {
    attributes = new_value;
    return true;
  }
  return false;
}

bool SetFiletime(FILETIME& filetime, PFILETIME new_value) {
  if (new_value && filetime.dwLowDateTime != new_value->dwLowDateTime &&
      filetime.dwHighDateTime != new_value->dwHighDateTime) {
    filetime = *new_value;
    return true;
  }
  return false;
}

void ErrorMessage(const std::string &method_name, ECBFSError error) {
  LOG(kError) << "Cbfs::" << method_name << ": " << WstringToString(error.Message());
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
