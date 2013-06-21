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

#include "maidsafe/drive/config.h"


namespace maidsafe {

namespace drive {

#ifdef __MSVC__
const boost::filesystem::path kMsHidden(L".ms_hidden");
#else
const boost::filesystem::path kMsHidden(".ms_hidden");
#endif

const boost::filesystem::path kEmptyPath("");
const boost::filesystem::path kRoot("\\");
const boost::filesystem::path kOwner("Owner");
const boost::filesystem::path kGroup("Group");
const boost::filesystem::path kWorld("World");
const boost::filesystem::path kServices("Services");

const boost::posix_time::milliseconds kMinUpdateInterval(5000);
const boost::posix_time::milliseconds kMaxUpdateInterval(20000);

}  // namespace drive

}  // namespace maidsafe
