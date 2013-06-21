#==================================================================================================#
#                                                                                                  #
#  Copyright (c) 2012 MaidSafe.net limited                                                         #
#                                                                                                  #
#  The following source code is property of MaidSafe.net limited and is not meant for external     #
#  use.  The use of this code is governed by the license file licence.txt found in the root        #
#  directory of this project and also on www.maidsafe.net.                                         #
#                                                                                                  #
#  You are not free to copy, amend or otherwise use this source code without the explicit written  #
#  permission of the board of directors of MaidSafe.net.                                           #
#                                                                                                  #
#==================================================================================================#


set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

cmake_minimum_required(VERSION 2.8.10 FATAL_ERROR)
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
  set(DriveThirdPartyIncludes ${Cbfs_INCLUDE_DIR})
else()
  include(maidsafe_find_fuse)
  if(APPLE)
    set(DriveThirdPartyIncludes ${Fuse_INCLUDE_DIR})
  endif()
endif()
set(DriveThirdPartyIncludes ${DriveThirdPartyIncludes} PARENT_SCOPE)
include_directories("${DriveThirdPartyIncludes}")


#==================================================================================================#
# Set up all files as GLOBs                                                                        #
#==================================================================================================#
set(DriveSourcesDir ${PROJECT_SOURCE_DIR}/src/maidsafe/drive)
glob_dir(Drive ${DriveSourcesDir} Drive)
glob_dir(DriveTests ${DriveSourcesDir}/tests Tests)
set(DemoMain ${DriveSourcesDir}/demo_main.cc)
set(WinServiceMain ${DriveSourcesDir}/win_service_main.cc)
list(REMOVE_ITEM DriveAllFiles ${DemoMain})
list(REMOVE_ITEM DriveAllFiles ${WinServiceMain})
                                                                                    set(Cbfs_FOUND OFF) # debugging only delete me

set(UnixDriveFiles ${PROJECT_SOURCE_DIR}/include/maidsafe/drive/unix_drive.h
                   ${DriveSourcesDir}/unix_drive.cc)
set(CbfsDependantFiles ${DriveSourcesDir}/win_drive.cc
                       ${PROJECT_SOURCE_DIR}/include/maidsafe/drive/win_drive.h)
set(DummyWinFiles ${PROJECT_SOURCE_DIR}/include/maidsafe/drive/dummy_win_drive.h
                  ${DriveSourcesDir}/dummy_win_drive.cc)
if(WIN32)
  if(Cbfs_FOUND)
    if(NOT EldosLicenceKey)
      message(FATAL_ERROR " Failed to get valid cbfs keys")
	endif()
	list(REMOVE_ITEM DriveAllFiles ${DummyWinFiles})
	list(REMOVE_ITEM DriveTestsAllFiles ${DummyWinFiles})
  else()
    set(DummyThirdPartWinLibrary ON)
    list(REMOVE_ITEM DriveAllFiles ${CbfsDependantFiles})
    list(REMOVE_ITEM DriveTestsAllFiles ${CbfsDependantFiles})
  endif()
  list(REMOVE_ITEM DriveAllFiles ${UnixDriveFiles})
  list(REMOVE_ITEM DriveTestsAllFiles ${UnixDriveFiles})
else()
  list(REMOVE_ITEM DriveAllFiles ${DummyWinFiles})
  list(REMOVE_ITEM DriveTestsAllFiles ${DummyWinFiles})
  list(REMOVE_ITEM DriveAllFiles ${CbfsDependantFiles})
  list(REMOVE_ITEM DriveTestsAllFiles ${CbfsDependantFiles})
endif()

list(REMOVE_ITEM DriveTestsAllFiles ${DriveSourcesDir}/tests/virtual_filesystem_test.cc)
list(REMOVE_ITEM DriveTestsAllFiles ${DriveSourcesDir}/tests/shares_test.cc)
set(TestSharesFiles ${DriveSourcesDir}/tests/shares_test.cc
                    ${DriveSourcesDir}/tests/test_main.cc
                    ${DriveSourcesDir}/tests/test_utils.cc
                    ${DriveSourcesDir}/tests/test_utils.h)
set(TestVfsFiles ${DriveSourcesDir}/tests/virtual_filesystem_test.cc
                 ${DriveSourcesDir}/tests/test_utils.cc
                 ${DriveSourcesDir}/tests/test_utils.h)
if(WIN32)
  glob_dir(Installer ${DriveSourcesDir}/installer Installer)
endif()


#==================================================================================================#
# Define MaidSafe libraries and executables                                                        #
#==================================================================================================#
ms_add_static_library(drive ${DriveAllFiles})
target_link_libraries(maidsafe_drive maidsafe_encrypt maidsafe_nfs)
if(WIN32)
  if(NOT DummyThirdPartWinLibrary)
    ms_add_executable(cbfs_driver "Production" ${InstallerAllFiles})
    target_link_libraries(maidsafe_drive optimized ${Cbfs_LIBRARY} debug ${Cbfs_LIBRARY_DEBUG})
    target_link_libraries(cbfs_driver ${BoostFilesystemLibs} ${BoostSystemLibs} ${Cbfs_LIBRARY} ${JustThread_LIBRARIES})
    add_dependencies(cbfs_driver boost)
  endif()
elseif(APPLE)
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY})
else()
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY} rt dl)
endif()
#TODO (TEAM) Create unit test target for directory listing to build even with dummy win dependency
if(MaidsafeTesting AND NOT DummyThirdPartWinLibrary)
  ms_add_executable(drive_demo Tools ${DemoMain})
  ms_add_executable(TESTdrive_listings_api "Tests/Drive" ${DriveTestsAllFiles})
  #ms_add_executable(TESTdrive_shares "Tests/Drive" ${TestSharesFiles})
  ms_add_executable(TESTdrive_virtual_filesystem "Tests/Drive" ${TestVfsFiles})
  ms_add_executable(TESTdrive_real_filesystem "Tests/Drive" ${TestVfsFiles})

  target_link_libraries(drive_demo maidsafe_drive)
  target_link_libraries(TESTdrive_listings_api maidsafe_drive)
  #target_link_libraries(TESTdrive_shares maidsafe_drive)
  target_link_libraries(TESTdrive_virtual_filesystem maidsafe_drive)
  target_link_libraries(TESTdrive_real_filesystem maidsafe_drive)
endif()

rename_outdated_built_exes()


#==================================================================================================#
# Set compiler and linker flags                                                                    #
#==================================================================================================#
include(standard_flags)

if(MaidsafeTesting AND NOT DummyThirdPartWinLibrary)
  set_target_properties(TESTdrive_real_filesystem PROPERTIES COMPILE_DEFINITIONS "DISK_TEST")
endif()
if(WIN32 AND NOT DummyThirdPartWinLibrary)
  set_target_properties(cbfs_driver PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
endif()
add_coverage_exclude(demo_main\\\\.cc)


#==================================================================================================#
# Tests                                                                                            #
#==================================================================================================#
if(MaidsafeTesting AND NOT DummyThirdPartWinLibrary)
  add_style_test()
  add_gtests(TESTdrive_listings_api)
  #add_gtests(TESTdrive_shares)
  add_gtests(TESTdrive_virtual_filesystem)
  add_project_experimental()
  test_summary_output()
endif()

if(UNIX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-effc++")
endif()
