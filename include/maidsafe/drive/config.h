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

#ifndef MAIDSAFE_DRIVE_CONFIG_H_
#define MAIDSAFE_DRIVE_CONFIG_H_

#include <memory>
#include <set>
#include <string>

#include "boost/date_time/posix_time/posix_time_duration.hpp"
#include "boost/filesystem/path.hpp"
#include "boost/thread/shared_mutex.hpp"
#include "boost/thread/locks.hpp"

#include "maidsafe/common/types.h"

namespace testing { class AssertionResult; }

namespace maidsafe {

namespace encrypt {
struct DataMap;
class SelfEncryptor;
}  // namespace encrypt

namespace drive {

class DirectoryListing;
class DirectoryListingHandler;
struct MetaData;
struct FileContext;
class FileContextInfo;

enum OpType { kCreated, kRenamed, kAdded, kRemoved, kMoved, kModified };

typedef std::shared_ptr<encrypt::DataMap> DataMapPtr;
typedef std::shared_ptr<encrypt::SelfEncryptor> SelfEncryptorPtr;

typedef Identity DirectoryId;
typedef std::shared_ptr<DirectoryId> DirectoryIdPtr;
typedef std::shared_ptr<DirectoryListing> DirectoryListingPtr;
typedef std::shared_ptr<DirectoryListingHandler> DirectoryListingHandlerPtr;
// typedef std::set<MetaData> MetaDataSet;
typedef std::unique_ptr<FileContext> FileContextPtr;
typedef std::unique_ptr<FileContextInfo> FileContextInfoPtr;

typedef std::string ShareId;

typedef boost::shared_lock<boost::shared_mutex> SharedLock;
typedef boost::upgrade_lock<boost::shared_mutex> UpgradeLock;
typedef boost::unique_lock<boost::shared_mutex> UniqueLock;
typedef boost::upgrade_to_unique_lock<boost::shared_mutex> UpgradeToUniqueLock;

extern const boost::filesystem::path kMsHidden;

extern const boost::filesystem::path kEmptyPath;
extern const boost::filesystem::path kRoot;
extern const boost::filesystem::path kOwner;
extern const boost::filesystem::path kGroup;
extern const boost::filesystem::path kWorld;
extern const boost::filesystem::path kServices;

extern const boost::posix_time::milliseconds kMinUpdateInterval;
extern const boost::posix_time::milliseconds kMaxUpdateInterval;

namespace test {
testing::AssertionResult DirectoriesMatch(DirectoryListingPtr directory1,
                                          DirectoryListingPtr directory2);
}  // namespace test

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_CONFIG_H_
