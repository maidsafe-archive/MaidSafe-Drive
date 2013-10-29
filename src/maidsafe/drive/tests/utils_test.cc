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

#include "maidsafe/common/test.h"

#include "maidsafe/common/log.h"

#include "maidsafe/drive/utils.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace detail {

namespace test {

void FilesMatchMask(const std::vector<fs::path>& all_files,
                    const std::set<fs::path>& matching_files, const std::wstring& mask) {
  std::for_each(all_files.begin(), all_files.end(), [&](const fs::path & file_name) {
    if (matching_files.find(file_name) != matching_files.end()) {
      INFO((std::wstring(L"File ") + file_name.wstring() + L" should match for mask \"" + mask +
            L"\"").c_str());
      REQUIRE(MatchesMask(mask, file_name));
    } else {
      INFO((std::wstring(L"File ") + file_name.wstring() + L" should NOT match for mask \"" + mask +
            L"\"").c_str());
      REQUIRE_FALSE(MatchesMask(mask, file_name));
    }
  });
}

TEST_CASE("Mask match", "[behavioural] [drive]" ) {
  std::set<fs::path> matching_files;
  matching_files.insert(L"1.txt");
  matching_files.insert(L"a.txt");
  matching_files.insert(L"1].txt");
  matching_files.insert(L"1[.txt");
  matching_files.insert(L"1{.txt");
  matching_files.insert(L"1}.txt");
  matching_files.insert(L"1).txt");
  matching_files.insert(L"1+.TXT");
  matching_files.insert(L"1^.txt");
  matching_files.insert(L"1^f.txt");
  matching_files.insert(L"1$.txt");
  matching_files.insert(L"b.tx");
  matching_files.insert(L"bt.x");
  matching_files.insert(L"btx.");
  matching_files.insert(L"btx");
  const std::vector<fs::path> kAllFiles(matching_files.begin(), matching_files.end());

  std::wstring mask(L"*");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*.*";
  matching_files.erase(L"btx");
  FilesMatchMask(kAllFiles, matching_files, mask);
#ifdef MAIDSAFE_WIN32
  mask = L"*[*";
  matching_files.clear();
  matching_files.insert(L"1[.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);
#endif
  mask = L"*]*";
  matching_files.clear();
  matching_files.insert(L"1].txt");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*{*";
  matching_files.clear();
  matching_files.insert(L"1{.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*}*";
  matching_files.clear();
  matching_files.insert(L"1}.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*)*";
  matching_files.clear();
  matching_files.insert(L"1).txt");
  FilesMatchMask(kAllFiles, matching_files, mask);
#ifdef MAIDSAFE_WIN32
  mask = L"*+*";
  matching_files.clear();
  matching_files.insert(L"1+.TXT");
  FilesMatchMask(kAllFiles, matching_files, mask);
#endif
  mask = L"*^*";
  matching_files.clear();
  matching_files.insert(L"1^.txt");
  matching_files.insert(L"1^f.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*$*";
  matching_files.clear();
  matching_files.insert(L"1$.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*.txt";
  matching_files.insert(kAllFiles.begin(), kAllFiles.end());
  matching_files.erase(L"b.tx");
  matching_files.erase(L"bt.x");
  matching_files.erase(L"btx.");
  matching_files.erase(L"btx");
#ifndef MAIDSAFE_WIN32
#ifndef MAIDSAFE_APPLE
  matching_files.erase(L"1+.TXT");
#endif
#endif
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*.?";
  matching_files.clear();
  matching_files.insert(L"bt.x");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*.??";
  matching_files.clear();
  matching_files.insert(L"b.tx");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*.???";
  matching_files.insert(kAllFiles.begin(), kAllFiles.end());
  matching_files.erase(L"b.tx");
  matching_files.erase(L"bt.x");
  matching_files.erase(L"btx.");
  matching_files.erase(L"btx");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*.????";
  matching_files.clear();
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"?????";
  matching_files.clear();
  matching_files.insert(L"1.txt");
  matching_files.insert(L"a.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"??????";
  matching_files.clear();
  matching_files.insert(L"1].txt");
  matching_files.insert(L"1[.txt");
  matching_files.insert(L"1{.txt");
  matching_files.insert(L"1}.txt");
  matching_files.insert(L"1).txt");
  matching_files.insert(L"1+.TXT");
  matching_files.insert(L"1^.txt");
  matching_files.insert(L"1$.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"?.*";
  matching_files.clear();
  matching_files.insert(L"1.txt");
  matching_files.insert(L"a.txt");
  matching_files.insert(L"b.tx");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"1?.*";
  matching_files.clear();
  matching_files.insert(L"1].txt");
  matching_files.insert(L"1[.txt");
  matching_files.insert(L"1{.txt");
  matching_files.insert(L"1}.txt");
  matching_files.insert(L"1).txt");
  matching_files.insert(L"1+.TXT");
  matching_files.insert(L"1^.txt");
  matching_files.insert(L"1$.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"1??.*";
  matching_files.clear();
  matching_files.insert(L"1^f.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"1*.*";
  matching_files.clear();
  matching_files.insert(L"1.txt");
  matching_files.insert(L"1].txt");
  matching_files.insert(L"1[.txt");
  matching_files.insert(L"1{.txt");
  matching_files.insert(L"1}.txt");
  matching_files.insert(L"1).txt");
  matching_files.insert(L"1+.TXT");
  matching_files.insert(L"1^.txt");
  matching_files.insert(L"1^f.txt");
  matching_files.insert(L"1$.txt");
  FilesMatchMask(kAllFiles, matching_files, mask);
}

}  // namespace test

}  // namespace detail

}  // namespace drive

}  // namespace maidsafe
