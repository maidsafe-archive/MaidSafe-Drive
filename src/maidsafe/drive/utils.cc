/*******************************************************************************
 *  Copyright 2011 maidsafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 *******************************************************************************
 */

#include "maidsafe/drive/utils.h"

#include <regex>
#include <algorithm>

#include "maidsafe/common/log.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/directory_listing_handler.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/meta_data.h"

namespace maidsafe {

namespace drive {

FileContext::FileContext()
    : meta_data(new MetaData),
      self_encryptor(),
      content_changed(false),
      grandparent_directory_id(),
      parent_directory_id() {}

FileContext::FileContext(const fs::path& name, bool is_directory)
      : meta_data(new MetaData(name, is_directory)),
        self_encryptor(),
        content_changed(!is_directory),
        grandparent_directory_id(),
        parent_directory_id() {}

FileContext::FileContext(std::shared_ptr<MetaData> meta_data_in)
    : meta_data(meta_data_in),
      self_encryptor(),
      content_changed(false),
      grandparent_directory_id(),
      parent_directory_id() {}

#ifndef MAIDSAFE_WIN32
// Not called by Windows...
int ForceFlush(DirectoryListingHandlerPtr directory_listing_handler, FileContext* file_context) {
  BOOST_ASSERT(file_context);
  file_context->self_encryptor->Flush();

  try {
    directory_listing_handler->UpdateParentDirectoryListing(
        file_context->meta_data->name.parent_path(), *file_context->meta_data.get());
  } catch(...) {
      return kFailedToSaveParentDirectoryListing;
  }
  return kSuccess;
}
#endif

bool ExcludedFilename(const fs::path& path) {
  std::string file_name(path.filename().stem().string());
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

}  // namespace drive

}  // namespace maidsafe
