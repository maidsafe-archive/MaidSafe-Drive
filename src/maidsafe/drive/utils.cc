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
#include <vector>

#include "maidsafe/common/log.h"
#include "maidsafe/common/profiler.h"

namespace maidsafe {

namespace drive {

namespace detail {

void ConvertToLowerCase(std::string& input, size_t count) {
  std::transform(std::begin(input), std::begin(input) + count, std::begin(input),
                 [](char c) { return std::tolower<char>(c, std::locale("")); });
}

void ConvertToLowerCase(std::string& input) { ConvertToLowerCase(input, input.size()); }

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
  static const char kExcluded[] = {'"', '*', '/', ':', '<', '>', '?', '\\', '|'};
  assert(std::is_sorted(std::begin(kExcluded), std::end(kExcluded)));
  std::sort(std::begin(file_name), std::end(file_name));
  std::vector<char> intersection;
  std::set_intersection(std::begin(file_name), std::end(file_name), std::begin(kExcluded),
                        std::end(kExcluded), std::back_inserter(intersection));
  return !intersection.empty();
}

bool MatchesMask(std::wstring mask, const boost::filesystem::path& file_name) {
  SCOPED_PROFILE
  bool result(true);
  auto mask_ptr(mask.c_str());
  auto name_ptr(file_name.wstring().c_str());
  const wchar_t* last_star_ptr(nullptr);
  int last_star_dec = 0;
  while (result && (*name_ptr != 0)) {
    if (*mask_ptr == L'?') {
      ++mask_ptr;
      ++name_ptr;
    } else if (*mask_ptr == L'*') {
      last_star_ptr = mask_ptr;
      last_star_dec = 0;
      ++mask_ptr;
      for (;;) {
        if (*mask_ptr == L'?') {
          ++mask_ptr;
          ++name_ptr;
          ++last_star_dec;
          if (*name_ptr == 0)
            break;
        } else if (*mask_ptr != L'*') {
          break;
        } else {
          ++mask_ptr;
        }
      }
      while ((*name_ptr != 0) && ((*mask_ptr == 0) || (*mask_ptr != *name_ptr)))
        ++name_ptr;
    } else {
      result = (*mask_ptr == *name_ptr);
      if (result) {
        ++mask_ptr;
        ++name_ptr;
      } else {
        result = last_star_ptr != nullptr;
        if (result) {
          mask_ptr = last_star_ptr;
          name_ptr -= last_star_dec;
        }
      }
    }
  }
  if (result) {
    while (*mask_ptr == L'*')
      ++mask_ptr;
    result = (*mask_ptr == 0);
  }
  return result;
}

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
