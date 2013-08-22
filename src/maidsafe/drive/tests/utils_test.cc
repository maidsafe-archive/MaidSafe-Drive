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

#include "maidsafe/common/test.h"

#include "maidsafe/common/log.h"

#include "maidsafe/drive/utils.h"


namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

namespace test {

void FilesMatchMask(const std::vector<fs::path> &all_files,
                    const std::set<fs::path> &matching_files,
                    const std::wstring &mask) {
  std::for_each(all_files.begin(),
                all_files.end(),
                [&](const fs::path &file_name) {
    if (matching_files.find(file_name) != matching_files.end())
      ASSERT_TRUE(MatchesMask(mask, file_name)) << "File " << file_name
          << " should match for mask \"" << mask << "\"";
    else
      ASSERT_FALSE(MatchesMask(mask, file_name)) << "File " << file_name
          << " should NOT match for mask \"" << mask << "\"";
  });
}

TEST(UtilsTest, BEH_MatchesMask) {
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
  const std::vector<fs::path> kAllFiles(matching_files.begin(),
                                        matching_files.end());

  std::wstring mask(L"*");
  FilesMatchMask(kAllFiles, matching_files, mask);

  mask = L"*.*";
  matching_files.erase(L"btx");
  FilesMatchMask(kAllFiles, matching_files, mask);
#ifdef WIN32
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
#ifdef WIN32
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
#ifndef WIN32
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

}  // namespace drive

}  // namespace maidsafe
