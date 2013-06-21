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

#ifndef MAIDSAFE_DRIVE_RETURN_CODES_H_
#define MAIDSAFE_DRIVE_RETURN_CODES_H_


namespace maidsafe {

namespace drive {

enum ReturnCode {
  kSuccess = 0,
  kGeneralError = -500001,

  // DirectoryListing
  kFailedToAddChild = -500100,
  kFailedToRemoveChild = -500101,

  // DirectoryListingHandler
  kFailedToInitialise = -500200,
  kFailedToGetDirectoryData = -500201,
  kFailedToAddDirectoryListing = -500202,
  kFailedToDeleteDirectoryListing = -500203,
  kFailedToRenameDirectoryListing = -500204,
  kFailedToCreateDirectory = -500205,
  kFailedToSaveParentDirectoryListing = -500206,
  kFailedToSaveChanges = -500207,
  kFailedToDeleteDirectoryListingNotEmpty = -500208,
  kFailedToStoreEncryptedDataMap = -500209,
  kFailedToModifyEncryptedDataMap = -500210,
  kFailedToDeleteEncryptedDataMap = -500211,
  kFailedToDecryptDataMap = -500212,
  kFailedToParseShares = - 500213,
  kNotAuthorised = -500214,
  kNestedShareDisallowed = -500215,
  kHiddenNotAllowed = -500216,
  kFailedToRetrieveData = -500217,
  kInvalidDataMap = -500218,
  kFailedToGetLock = -500219,

  // DriveInUserSpace
  kChildAlreadyExists = -500300,
  kFailedToGetChild = -500301,
  kFailedChunkStoreInit = -500302,
  kCBFSError = -500303,
  kCreateStorageError = -500304,
  kMountError = -500305,
  kFuseFailedToParseCommandLine = -500306,
  kFuseFailedToMount = -500307,
  kFuseNewFailed = -500308,
  kFuseFailedToDaemonise = -500309,
  kFuseFailedToSetSignalHandlers = -500310,
  kUnmountError = -500311,
  kInvalidSelfEncryptor = -500312,
  kReadError = -500313,
  kWriteError = -500314,
  kInvalidSeek = -500315,
  kNullParameter = -500316,
  kInvalidPath = -500317,
  kFailedToGetMetaData = -500318,
  kNoDataMap = -500319,
  kFailedToSerialiseDataMap = -500320,
  kFailedToParseDataMap = -500321,
  kNoDirectoryId = -500322,
  kInvalidIds = -500323,
  kInvalidKey = -500324,
  kParentShared = -500325,
  kFailedToUpdateShareKeys = -500326,
  kFailedToGetShareKeys = -500327,
  kNoMsHidden = -500328,
  kMsHiddenAlreadyExists = -500329,
  kShareAlreadyExistsInHierarchy = -500330,
  kDirectoryRecursionException = -500331,
  kNetworkStoreFailed = -500332,
  kNetworkBackUpStoreFailed = -500333,

  // meta_data_ops
  kNullPointer = -500400,
  kSerialisingError = -500401,
  kParsingError = -500402,

  // Shares
  kFailedToParseShareUsers = -500500,
  kFailedToSerialiseShareUsers = -500501,
  kShareUserAlreadyExists = -500502,
  kFailedToFindShareUser = -500503,
  kShareByIdNotFound = -500504
};

}  // namespace drive

}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_RETURN_CODES_H_
