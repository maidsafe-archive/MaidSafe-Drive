/* Copyright 2011 MaidSafe.net limited

This MaidSafe Software is licensed under the MaidSafe.net Commercial License, version 1.0 or later,
and The General Public License (GPL), version 3. By contributing code to this project You agree to
the terms laid out in the MaidSafe Contributor Agreement, version 1.0, found in the root directory
of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also available at:

http://www.novinet.com/license

Unless required by applicable law or agreed to in writing, software distributed under the License is
distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing permissions and limitations under the
License.
*/

#include "maidsafe/drive/utils.h"

#include <regex>
#include <algorithm>

#include "maidsafe/common/log.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/meta_data.h"


namespace maidsafe {

namespace drive {

namespace detail {

bool ExcludedFilename(const std::string& file_name) {
  if (file_name.size() == 4 && isdigit(file_name[3])) {
    if (file_name[3] != '0') {
      std::string name(file_name.substr(0, 3));
      std::transform(name.begin(), name.end(), name.begin(), tolower);
      if (name.compare(0, 3, "com", 0, 3) == 0) {
        return true;
      }
      if (name.compare(0, 3, "lpt", 0, 3) == 0) {
        return true;
      }
    }
  } else if (file_name.size() == 3) {
    std::string name(file_name);
    std::transform(name.begin(), name.end(), name.begin(), tolower);
    if (name.compare(0, 3, "con", 0, 3) == 0) {
      return true;
    }
    if (name.compare(0, 3, "prn", 0, 3) == 0) {
      return true;
    }
    if (name.compare(0, 3, "aux", 0, 3) == 0) {
      return true;
    }
    if (name.compare(0, 3, "nul", 0, 3) == 0) {
      return true;
    }
  } else if (file_name.size() == 6) {
    if (file_name[5] == '$') {
      std::string name(file_name);
      std::transform(name.begin(), name.end(), name.begin(), tolower);
      if (name.compare(0, 5, "clock", 0, 5) == 0) {
        return true;
      }
    }
  }
  static const std::string excluded = "\"\\/<>?:*|";
  std::string::const_iterator first(file_name.begin()), last(file_name.end());
  for (; first != last; ++first) {
    if (find(excluded.begin(), excluded.end(), *first) != excluded.end())
      return true;
  }
  return false;
}

bool MatchesMask(std::wstring mask, const fs::path& file_name) {
#ifdef MAIDSAFE_WIN32
  static const std::wstring kNeedEscaped(L".[]{}()+|^$");
#else
  #ifdef MAIDSAFE_APPLE
  static const std::wstring kNeedEscaped(L".]{}()+|^$");
  #else
  static const std::wstring kNeedEscaped(L".{}()+|^$");
  #endif
#endif
  static const std::wstring kEscape(L"\\");
  try {
    // Apply escapes
    std::for_each(kNeedEscaped.begin(), kNeedEscaped.end(), [&mask](wchar_t i) {
      boost::replace_all(mask, std::wstring(1, i), kEscape + std::wstring(1, i));
    });

    // Apply wildcards
    boost::replace_all(mask, L"*", L".*");
    boost::replace_all(mask, L"?", L".");

    // Check for match
    std::wregex reg_ex(mask, std::regex_constants::icase);
    return std::regex_match(file_name.wstring(), reg_ex);
  }
  catch(const std::exception& e) {
    LOG(kError) << e.what() << " - file_name: " << file_name << ", mask: "
                << std::string(mask.begin(), mask.end());
    return false;
  }
}

bool SearchesMask(std::wstring mask, const fs::path& file_name) {
  static const std::wstring kNeedEscaped(L".[]{}()+|^$");
  static const std::wstring kEscape(L"\\");
  try {
    // Apply escapes
    std::for_each(kNeedEscaped.begin(), kNeedEscaped.end(), [&mask](wchar_t i) {
      boost::replace_all(mask, std::wstring(1, i), kEscape + std::wstring(1, i));
    });

    // Apply wildcards
    boost::replace_all(mask, L"*", L".*");
    boost::replace_all(mask, L"?", L".");

    // Check for match
    std::wregex reg_ex(mask, std::regex_constants::icase);
    return std::regex_search(file_name.wstring(), reg_ex);
  }
  catch(const std::exception& e) {
    LOG(kError) << e.what() << " - file_name: " << file_name << ", mask: "
                << std::string(mask.begin(), mask.end());
    return false;
  }
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
