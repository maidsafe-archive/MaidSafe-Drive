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

#include "maidsafe/drive/drive_api.h"

#include <regex>
#include <algorithm>

#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/encrypt/data_map.h"
#include "maidsafe/encrypt/self_encryptor.h"

#include "maidsafe/drive/config.h"
#include "maidsafe/drive/directory_listing.h"
#include "maidsafe/drive/directory_listing_handler.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/return_codes.h"
#include "maidsafe/drive/utils.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace drive {

DriveInUserSpace::DriveInUserSpace(ClientNfs& client_nfs,
                                   DataStore& data_store,
                                   const Maid& maid,
                                   const Identity& unique_user_id,
                                   const std::string& root_parent_id,
                                   const fs::path& mount_dir,
                                   const int64_t& max_space,
                                   const int64_t& used_space)
    : drive_stage_(kUnInitialised),
      client_nfs_(client_nfs),
      data_store_(data_store),
      maid_(maid),
      directory_listing_handler_(new DirectoryListingHandler(client_nfs,
                                                             data_store,
                                                             maid,
                                                             unique_user_id,
                                                             root_parent_id)),
      mount_dir_(mount_dir),
      max_space_(max_space),
      used_space_(used_space),
      drive_changed_signal_(),
      share_renamed_signal_(),
      unmount_mutex_(),
      api_mutex_(),
      unmount_condition_variable_(),
      mount_mutex_(),
      mount_condition_variable_() {}

DriveInUserSpace::~DriveInUserSpace() {}

std::string DriveInUserSpace::unique_user_id() const {
  std::lock_guard<std::mutex> guard(api_mutex_);
  return directory_listing_handler_->unique_user_id().string();
}

std::string DriveInUserSpace::root_parent_id() const {
  std::lock_guard<std::mutex> guard(api_mutex_);
  return directory_listing_handler_->root_parent_id().string();
}

int64_t DriveInUserSpace::GetUsedSpace() const {
  std::lock_guard<std::mutex> guard(api_mutex_);
  return used_space_;
}

void DriveInUserSpace::SetMountState(bool mounted) {
  boost::mutex::scoped_lock lock(mount_mutex_);
  drive_stage_ = (mounted ? kMounted : kUnMounted);
  mount_condition_variable_.notify_one();
}

bool DriveInUserSpace::WaitUntilMounted() {
  boost::mutex::scoped_lock lock(mount_mutex_);
  bool result(mount_condition_variable_.timed_wait(
                  lock,
                  boost::get_system_time() + boost::posix_time::seconds(10),
                  [&]()->bool { return drive_stage_ == kMounted; }));  // NOLINT (Fraser)
#ifdef MAIDSAFE_APPLE
  Sleep(boost::posix_time::seconds(1));
#endif
  return result;
}

void DriveInUserSpace::WaitUntilUnMounted() {
  boost::mutex::scoped_lock lock(mount_mutex_);
  mount_condition_variable_.wait(lock, [&]()->bool { return drive_stage_ == kUnMounted; });  // NOLINT (Fraser)
}

void DriveInUserSpace::GetMetaData(const fs::path& relative_path,
                                   MetaData& meta_data,
                                   DirectoryId* grandparent_directory_id,
                                   DirectoryId* parent_directory_id) {
  typedef DirectoryListingHandler::DirectoryType DirectoryType;
  DirectoryType parent(directory_listing_handler_->GetFromPath(relative_path.parent_path()));
  parent.first.listing->GetChild(relative_path.filename(), meta_data);

  if (grandparent_directory_id)
    *grandparent_directory_id = parent.first.parent_id;
  if (parent_directory_id)
    *parent_directory_id = parent.first.listing->directory_id();
  return;
}

void DriveInUserSpace::UpdateParent(FileContext* file_context, const fs::path& parent_path) {
  directory_listing_handler_->UpdateParentDirectoryListing(parent_path, *file_context->meta_data);
  return;
}

void DriveInUserSpace::AddFile(const fs::path& relative_path,
                               const MetaData& meta_data,
                               DirectoryId* grandparent_directory_id,
                               DirectoryId* parent_directory_id) {
  directory_listing_handler_->AddElement(relative_path,
                                         meta_data,
                                         grandparent_directory_id,
                                         parent_directory_id);
}

bool DriveInUserSpace::CanRemove(const fs::path& relative_path) {
  return directory_listing_handler_->CanDelete(relative_path);
}

void DriveInUserSpace::RemoveFile(const fs::path& relative_path) {
  MetaData meta_data;
  directory_listing_handler_->DeleteElement(relative_path, meta_data);

  if (meta_data.data_map && !meta_data.directory_id) {
    encrypt::SelfEncryptor delete_this(meta_data.data_map, client_nfs_, data_store_);
    delete_this.DeleteAllChunks();
  }
  return;
}

void DriveInUserSpace::RenameFile(const fs::path& old_relative_path,
                                  const fs::path& new_relative_path,
                                  MetaData& meta_data,
                                  int64_t& reclaimed_space) {
  directory_listing_handler_->RenameElement(old_relative_path,
                                            new_relative_path,
                                            meta_data,
                                            reclaimed_space);
  return;
}

bool DriveInUserSpace::TruncateFile(FileContext* file_context, const uint64_t& size) {
  if (!file_context->self_encryptor) {
    file_context->self_encryptor.reset(
        new encrypt::SelfEncryptor(file_context->meta_data->data_map, client_nfs_, data_store_));
  }
  bool result = file_context->self_encryptor->Truncate(size);
  if (result) {
    file_context->content_changed = true;
  }
  return result;
}

// ********************** File / Folder Transfers ******************************

void DriveInUserSpace::GetDataMap(const fs::path& relative_path,
                                  std::string* serialised_data_map) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  ReadDataMap(relative_path, serialised_data_map);
}

void DriveInUserSpace::GetDataMapHidden(const fs::path& relative_path,
                                        std::string* serialised_data_map) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  ReadDataMap(relative_path, serialised_data_map);
}

void DriveInUserSpace::ReadDataMap(const fs::path& relative_path,
                                   std::string* serialised_data_map) {
  if (relative_path.empty() || !serialised_data_map)
    ThrowError(CommonErrors::invalid_parameter);

  serialised_data_map->clear();
  FileContext file_context;
  file_context.meta_data->name = relative_path.filename();
  GetMetaData(relative_path, *file_context.meta_data.get(), nullptr, nullptr);

  if (!file_context.meta_data->data_map)
    ThrowError(CommonErrors::invalid_parameter);

  try {
    encrypt::SerialiseDataMap(*file_context.meta_data->data_map, *serialised_data_map);
  }
  catch(const std::exception& exception) {
    serialised_data_map->clear();
    boost::throw_exception(exception);
  }
  return;
}

void DriveInUserSpace::InsertDataMap(const fs::path& relative_path,
                                     const std::string& serialised_data_map) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  LOG(kInfo) << "InsertDataMap - " << relative_path;

  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  FileContext file_context(relative_path.filename(), false);
  encrypt::ParseDataMap(serialised_data_map, *file_context.meta_data->data_map);

  SetNewAttributes(&file_context, false, false);

  AddFile(relative_path,
          *file_context.meta_data.get(),
          &file_context.grandparent_directory_id,
          &file_context.parent_directory_id);
  return;
}

// int DriveInUserSpace::MoveDirectory(const fs::path& from, const fs::path& to) {
//  std::lock_guard<std::mutex> guard(api_mutex_);
//  fs::path from_relative_path(RelativePath(mount_dir_, from)),
//           to_relative_path(RelativePath(mount_dir_, to));
//  MetaData from_meta_data;
//  if (!GetMetaData(from_relative_path, from_meta_data, nullptr, nullptr)) {
//    LOG(kError) << "Failed to get meta data for  " << from;
//    return kFailedToGetMetaData;
//  }
//  assert(from_meta_data.directory_id);
//  int result(directory_listing_handler_->MoveDirectory(from_relative_path,
//                                                       from_meta_data,
//                                                       to_relative_path));
//  if (result != kSuccess) {
//    LOG(kError) << "Failed to move directory  " << from << " to " << to;
//    return result;
//  }
//  NotifyRename(from_relative_path, to_relative_path);
//  return result;
// }

// **************************** Hidden Files ***********************************

void DriveInUserSpace::ReadHiddenFile(const fs::path& relative_path, std::string* content) {
  if (relative_path.empty() || (relative_path.extension() != kMsHidden) || !content)
    ThrowError(CommonErrors::invalid_parameter);

  FileContext file_context;
  file_context.meta_data->name = relative_path.filename();
  GetMetaData(relative_path,
              *file_context.meta_data.get(),
              &file_context.grandparent_directory_id,
              &file_context.parent_directory_id);
  BOOST_ASSERT(!file_context.meta_data->directory_id);

  file_context.self_encryptor.reset(new encrypt::SelfEncryptor(file_context.meta_data->data_map,
                                                               client_nfs_,
                                                               data_store_));
  if (file_context.self_encryptor->size() > std::numeric_limits<uint32_t>::max())
    ThrowError(CommonErrors::invalid_parameter);

  uint32_t bytes_to_read(static_cast<uint32_t>(file_context.self_encryptor->size()));
  content->resize(bytes_to_read);
  if (!file_context.self_encryptor->Read(const_cast<char*>(content->data()), bytes_to_read, 0))
    ThrowError(CommonErrors::invalid_parameter);

  return;
}

void DriveInUserSpace::WriteHiddenFile(const fs::path &relative_path,
                                       const std::string &content,
                                       bool overwrite_existing) {
  if (relative_path.empty() || (relative_path.extension() != kMsHidden))
    ThrowError(CommonErrors::invalid_parameter);

  fs::path hidden_file_path(relative_path);
  // Try getting FileContext to existing
  FileContext file_context;
  file_context.meta_data->name = relative_path.filename();
  try {
    GetMetaData(relative_path,
                *file_context.meta_data.get(),
                &file_context.grandparent_directory_id,
                &file_context.parent_directory_id);
    if (!overwrite_existing)
      ThrowError(CommonErrors::invalid_parameter);
  }
  catch(...) {
    // Try adding a new entry if the hidden file doesn't already exist
    file_context = FileContext(hidden_file_path.filename(), false);
    AddFile(hidden_file_path,
            *file_context.meta_data.get(),
            &file_context.grandparent_directory_id,
            &file_context.parent_directory_id);
  }

  if (content.size() > std::numeric_limits<uint32_t>::max())
    ThrowError(CommonErrors::invalid_parameter);

  // Write the data
  file_context.self_encryptor.reset(new encrypt::SelfEncryptor(file_context.meta_data->data_map,
                                                               client_nfs_,
                                                               data_store_));

  if (file_context.self_encryptor->size() > content.size())
    file_context.self_encryptor->Truncate(content.size());
  if (!file_context.self_encryptor->Write(content.c_str(),
                                          static_cast<uint32_t>(content.size()),
                                          0U))
    ThrowError(CommonErrors::invalid_parameter);

  file_context.self_encryptor.reset();
  SetNewAttributes(&file_context, false, false);

  return;
}

void DriveInUserSpace::DeleteHiddenFile(const fs::path &relative_path) {
  if (relative_path.empty() || (relative_path.extension() != kMsHidden))
    ThrowError(CommonErrors::invalid_parameter);
  RemoveFile(relative_path);
  return;
}

void DriveInUserSpace::SearchHiddenFiles(const fs::path &relative_path,
                                         std::vector<std::string> *results) {
  typedef DirectoryListingHandler::DirectoryType DirectoryType;
  DirectoryType directory(directory_listing_handler_->GetFromPath(relative_path));
  directory.first.listing->GetHiddenChildNames(results);
  return;
}

// ***************************** File Notes ************************************

void DriveInUserSpace::GetNotes(const fs::path& relative_path, std::vector<std::string>* notes) {
  LOG(kInfo) << "GetNotes - " << relative_path;
  std::lock_guard<std::mutex> guard(api_mutex_);
  if (relative_path.empty() || !notes)
    ThrowError(CommonErrors::invalid_parameter);

  notes->clear();
  FileContext file_context;
  file_context.meta_data->name = relative_path.filename();
  GetMetaData(relative_path, *file_context.meta_data.get(), nullptr, nullptr);
  *notes = file_context.meta_data->notes;
  return;
}

void DriveInUserSpace::AddNote(const fs::path& relative_path, const std::string& note) {
  LOG(kInfo) << "AddNote - " << relative_path;
  std::lock_guard<std::mutex> guard(api_mutex_);
  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  FileContext file_context;
  file_context.meta_data->name = relative_path.filename();
  GetMetaData(relative_path,
              *file_context.meta_data.get(),
              &file_context.grandparent_directory_id,
              &file_context.parent_directory_id);
  file_context.meta_data->notes.push_back(note);
  UpdateParent(&file_context, relative_path.parent_path());
  return;
}

// ************************** Signals Handling *********************************

bs2::connection DriveInUserSpace::ConnectToDriveChanged(DriveChangedSlotPtr slot) {
  std::lock_guard<std::mutex> guard(api_mutex_);
  return drive_changed_signal_.connect(DriveChangedSignal::slot_type(*slot).track_foreign(slot));
}

}  // namespace drive

}  // namespace maidsafe
