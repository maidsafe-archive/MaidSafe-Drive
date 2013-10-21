#==================================================================================================#
#                                                                                                  #
#  Copyright 2012 MaidSafe.net limited                                                             #
#                                                                                                  #
#  This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,        #
#  version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which    #
#  licence you accepted on initial access to the Software (the "Licences").                        #
#                                                                                                  #
#  By contributing code to the MaidSafe Software, or to this project generally, you agree to be    #
#  bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root        #
#  directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also available   #
#  at: http://www.maidsafe.net/licenses                                                            #
#                                                                                                  #
#  Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed    #
#  under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF   #
#  ANY KIND, either express or implied.                                                            #
#                                                                                                  #
#  See the Licences for the specific language governing permissions and limitations relating to    #
#  use of the MaidSafe Software.                                                                   #
#                                                                                                  #
#==================================================================================================#


set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

project(drive)

include(../../cmake_modules/standard_setup.cmake)

include_directories(${common_SOURCE_DIR}/include)
include_directories(${private_SOURCE_DIR}/include)
include_directories(${passport_SOURCE_DIR}/include)
include_directories(${encrypt_SOURCE_DIR}/include)
include_directories(${routing_SOURCE_DIR}/include)
include_directories(${rudp_SOURCE_DIR}/include)
include_directories(${nfs_SOURCE_DIR}/include)


#==================================================================================================#
# Callback File System / Fuse library search                                                       #
#==================================================================================================#
set(DriveThirdPartyIncludes PARENT_SCOPE)
if(WIN32)
  include(maidsafe_find_cbfs)
  set(DriveThirdPartyIncludes ${Cbfs_INCLUDE_DIR} PARENT_SCOPE)
else()
  include(maidsafe_find_fuse)
  if(APPLE)
    set(DriveThirdPartyIncludes ${Fuse_INCLUDE_DIR} PARENT_SCOPE)
  endif()
endif()


#==================================================================================================#
# Set up all files as GLOBs                                                                        #
#==================================================================================================#
set(DriveSourcesDir ${PROJECT_SOURCE_DIR}/src/maidsafe/drive)
set(DriveApiDir ${PROJECT_SOURCE_DIR}/include/maidsafe/drive)
glob_dir(Drive ${DriveSourcesDir} Drive)
set(LocalDriveMain ${DriveSourcesDir}/local/local_drive.cc)
set(NetworkDriveMain ${DriveSourcesDir}/network/network_drive.cc)
set(WinServiceMain ${DriveSourcesDir}/win_service/win_service_main.cc)
set(UnixFiles ${DriveApiDir}/unix_drive.h ${DriveSourcesDir}/unix_drive.cc)
set(CbfsWinFiles ${DriveApiDir}/win_drive.h ${DriveSourcesDir}/win_drive.cc)
set(DummyWinFiles ${DriveApiDir}/dummy_win_drive.h ${DriveSourcesDir}/dummy_win_drive.cc)

if(WIN32)
  if(Cbfs_FOUND)
    list(REMOVE_ITEM DriveAllFiles ${UnixFiles} ${DummyWinFiles})
  else()
    list(REMOVE_ITEM DriveAllFiles ${UnixFiles} ${CbfsWinFiles})
  endif()
else()
  list(REMOVE_ITEM DriveAllFiles ${CbfsWinFiles} ${DummyWinFiles})
endif()

glob_dir(DriveTests ${DriveSourcesDir}/tests Tests)
list(REMOVE_ITEM DriveTestsAllFiles ${DriveSourcesDir}/tests/filesystem_benchmark.cc
                                    ${DriveSourcesDir}/tests/filesystem_test.cc
                                    ${DriveSourcesDir}/tests/shares_test.cc)
set(TestSharesFiles ${DriveSourcesDir}/tests/shares_test.cc
                    ${DriveSourcesDir}/tests/test_main.cc
                    ${DriveSourcesDir}/tests/test_utils.cc
                    ${DriveSourcesDir}/tests/test_utils.h)
source_group("Source Files" FILES ${DriveSourcesDir}/tests/filesystem_benchmark.cc
                                  ${DriveSourcesDir}/tests/filesystem_test.cc)
if(WIN32)
  glob_dir(Installer ${DriveSourcesDir}/installer Installer)
endif()


#==================================================================================================#
# Define MaidSafe libraries and executables                                                        #
#==================================================================================================#
ms_add_static_library(drive ${DriveAllFiles})
target_link_libraries(maidsafe_drive maidsafe_encrypt ${BoostRegexLibs})
if(WIN32)
  if(Cbfs_FOUND)
    ms_add_executable(cbfs_driver "Production" ${InstallerAllFiles})
    target_link_libraries(maidsafe_drive ${Cbfs_LIBRARIES})
    target_link_libraries(cbfs_driver ${BoostFilesystemLibs} ${BoostSystemLibs} ${Cbfs_LIBRARIES} ${JustThread_LIBRARIES})
    add_dependencies(cbfs_driver boost)
  endif()
elseif(APPLE)
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY})
else()
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY} rt dl)
endif()

ms_add_executable(local_drive "Production" ${LocalDriveMain})
target_link_libraries(local_drive maidsafe_drive)
ms_add_executable(network_drive "Production" ${LocalDriveMain})
target_link_libraries(network_drive maidsafe_drive maidsafe_nfs_client)

#TODO (TEAM) Create unit test target for directory listing to build even with dummy win dependency
if(MaidsafeTesting AND (Cbfs_FOUND OR NOT WIN32))
  ms_add_executable(filesystem_benchmark "Tools/Drive" ${DriveSourcesDir}/tests/filesystem_benchmark.cc)
  ms_add_executable(FilesystemTest "Tests/Drive" ${DriveSourcesDir}/tests/filesystem_test.cc)
  ms_add_executable(TESTLocalDrive "Tests/Drive" ${DriveTestsAllFiles})
  ms_add_executable(TESTNetworkDrive "Tests/Drive" ${DriveTestsAllFiles})

  target_link_libraries(filesystem_benchmark maidsafe_common)
  target_link_libraries(FilesystemTest maidsafe_common)
  add_dependencies(FilesystemTest catch)
  target_link_libraries(TESTLocalDrive maidsafe_drive)
  target_link_libraries(TESTNetworkDrive maidsafe_drive)
endif()

rename_outdated_built_exes()


#==================================================================================================#
# Set compiler and linker flags                                                                    #
#==================================================================================================#
include(standard_flags)
if(Cbfs_FOUND)
  add_definitions(-DCBFS_KEY=${Cbfs_KEY} -DHAVE_CBFS)
endif()

if(Cbfs_FOUND)
  set_target_properties(cbfs_driver PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
endif()


#==================================================================================================#
# Tests                                                                                            #
#==================================================================================================#
if(MaidsafeTesting)
  add_style_test()
  if(Cbfs_FOUND OR NOT WIN32)
    add_gtests(TESTLocalDrive)
    #add_gtests(TESTNetworkDrive)
  endif()
  add_project_experimental()
  test_summary_output()
endif()
