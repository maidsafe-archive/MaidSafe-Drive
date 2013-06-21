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

#ifndef MAIDSAFE_DRIVE_UTILS_H_
#define MAIDSAFE_DRIVE_UTILS_H_


#include <memory>
#include <string>
#include <vector>

#include "boost/filesystem/path.hpp"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory_listing.h"

#include "maidsafe/encrypt/self_encryptor.h"

namespace fs = boost::filesystem;


namespace maidsafe {

namespace drive {

static const uint32_t kDirectorySize = 4096;

struct FileContext {
  FileContext();
  FileContext(const fs::path& name, bool is_directory);
  explicit FileContext(std::shared_ptr<MetaData> meta_data_in);

  std::shared_ptr<MetaData> meta_data;
  SelfEncryptorPtr self_encryptor;
  bool content_changed;
  DirectoryId grandparent_directory_id, parent_directory_id;
};

int ForceFlush(DirectoryListingHandlerPtr directory_listing_handler,
               FileContext* file_context);

bool ExcludedFilename(const fs::path& path);

bool MatchesMask(std::wstring mask, const fs::path& file_name);
bool SearchesMask(std::wstring mask, const fs::path& file_name);

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_UTILS_H_
