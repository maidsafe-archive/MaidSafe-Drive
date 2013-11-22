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

#include "maidsafe/drive/utils.h"

#include <locale>
#include <algorithm>
#include <cassert>
#include <iterator>
#include <regex>
#include <vector>

#include "boost/algorithm/string/replace.hpp"

#include "maidsafe/common/log.h"

namespace maidsafe {

namespace drive {

namespace detail {

void ConvertToLowerCase(std::string& input, size_t count) {
  std::transform(std::begin(input), std::begin(input) + count, std::begin(input),
                 [](char c) { return std::tolower<char>(c, std::locale("")); });
}

void ConvertToLowerCase(std::string& input) {
  ConvertToLowerCase(input, input.size());
}

std::string GetLowerCase(std::string input) {
  ConvertToLowerCase(input);
  return input;
}

bool ExcludedFilename(const boost::filesystem::path& path) {
  std::string file_name(path.filename().stem().string());
  if (file_name.size() == 4 && isdigit(file_name[3])) {
    ConvertToLowerCase(file_name, 3);
    if (file_name == "com" || file_name == "lpt")
      return true;
  } else if (file_name.size() == 3) {
    ConvertToLowerCase(file_name);
    if (file_name == "con" || file_name == "prn" || file_name == "aux" || file_name == "nul")
      return true;
  } else if (file_name.size() == 6 && file_name[5] == '$') {
    ConvertToLowerCase(file_name, 5);
    if (file_name == "clock")
      return true;
  }
  static const char kExcluded[] = { '"', '*', '/', ':', '<', '>', '?', '\\', '|' };
  assert(std::is_sorted(std::begin(kExcluded), std::end(kExcluded)));
  std::sort(std::begin(file_name), std::end(file_name));
  std::vector<char> intersection;
  std::set_intersection(std::begin(file_name), std::end(file_name),
                        std::begin(kExcluded), std::end(kExcluded),
                        std::back_inserter(intersection));
  return !intersection.empty();
}

bool MatchesMask(std::wstring mask, const boost::filesystem::path& file_name) {
#if defined MAIDSAFE_WIN32
  static const std::wstring kNeedEscaped(L".[]{}()+|^$");
#elif defined MAIDSAFE_APPLE
  static const std::wstring kNeedEscaped(L".]{}()+|^$");
#else
  static const std::wstring kNeedEscaped(L".{}()+|^$");
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

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
