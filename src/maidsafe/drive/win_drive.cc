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

boost::optional<common::Clock::time_point> GetNewFiletime(
    const common::Clock::time_point filetime, const PFILETIME new_value) {
  if (new_value) {
    const common::Clock::time_point t = ToTimePoint(*new_value);
    if (filetime != t) {
      return t;
    }
  }
  return boost::none;
}

void ErrorMessage(const std::string &method_name, ECBFSError error) {
  LOG(kError) << "Cbfs::" << method_name << ": " << WstringToString(error.Message());
}

FILETIME ToFileTime(const common::Clock::time_point& input) {
  // FILETIME epoch = 1601-01-01T00:00:00Z in 100 nanosecond ticks
  // MaidSafe epoch = 1970-01-01T00:00:00Z in 1 nanosecond ticks
  using namespace std::chrono;
  const ULONGLONG filetimeTicks = 100ULL;
  const ULONGLONG chronoTicks = 1ULL;
  // 369 years
  const ULONGLONG epochDifference = 11644473600ULL * (chronoTicks / filetimeTicks);
  ULONGLONG stamp
      = epochDifference
      + (duration_cast<nanoseconds>(input.time_since_epoch()).count() / filetimeTicks);
  FILETIME result;
  result.dwHighDateTime = stamp >> 32;
  result.dwLowDateTime = stamp & 0xFFFFFFFF;
  return result;
}

common::Clock::time_point ToTimePoint(const FILETIME& input) {
  // See ToFileTime
  using namespace std::chrono;
  const ULONGLONG filetimeTicks = 100ULL;
  const ULONGLONG chronoTicks = 1ULL;
  const ULONGLONG epochDifference = 11644473600ULL * (chronoTicks / filetimeTicks);
  ULONGLONG filetime = ((ULONGLONG)(input.dwHighDateTime) << 32) + input.dwLowDateTime;
  ULONGLONG stamp = (filetime - epochDifference) * (filetimeTicks / chronoTicks);
  return common::Clock::time_point(nanoseconds(stamp));
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
