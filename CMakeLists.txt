#==================================================================================================#
#                                                                                                  #
#  Copyright 2012 MaidSafe.net limited                                                             #
#                                                                                                  #
#  This MaidSafe Software is licensed under the MaidSafe.net Commercial License, version 1.0 or    #
#  later, and The General Public License (GPL), version 3. By contributing code to this project    #
#  You agree to the terms laid out in the MaidSafe Contributor Agreement, version 1.0, found in    #
#  the root directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also    #
#  available at:                                                                                   #
#                                                                                                  #
#    http://www.novinet.com/license                                                                #
#                                                                                                  #
#  Unless required by applicable law or agreed to in writing, software distributed under the       #
#  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,       #
#  either express or implied. See the License for the specific language governing permissions      #
#  and limitations under the License.                                                              #
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
set(DemoMain ${DriveSourcesDir}/demo_main.cc)
set(WinServiceMain ${DriveSourcesDir}/win_service_main.cc)
set(UnixFiles ${DriveApiDir}/unix_drive.h ${DriveSourcesDir}/unix_drive.cc)
set(CbfsWinFiles ${DriveApiDir}/win_drive.h ${DriveSourcesDir}/win_drive.cc)
set(DummyWinFiles ${DriveApiDir}/dummy_win_drive.h ${DriveSourcesDir}/dummy_win_drive.cc)

if(WIN32)
  if(Cbfs_FOUND)
    list(REMOVE_ITEM DriveAllFiles ${DemoMain} ${WinServiceMain} ${UnixFiles} ${DummyWinFiles})
  else()
    list(REMOVE_ITEM DriveAllFiles ${DemoMain} ${WinServiceMain} ${UnixFiles} ${CbfsWinFiles})
  endif()
else()
  list(REMOVE_ITEM DriveAllFiles ${DemoMain} ${WinServiceMain} ${CbfsWinFiles} ${DummyWinFiles})
endif()

glob_dir(DriveTests ${DriveSourcesDir}/tests Tests)
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
ms_add_static_library(demo_drive ${DriveAllFiles})
target_link_libraries(maidsafe_demo_drive maidsafe_demo_encrypt maidsafe_nfs)
set_target_properties(maidsafe_demo_drive PROPERTIES COMPILE_DEFINITIONS "MAIDSAFE_DRIVE_DEMO")
if(WIN32)
  if(Cbfs_FOUND)
    ms_add_executable(cbfs_driver "Production" ${InstallerAllFiles})
    target_link_libraries(maidsafe_drive ${Cbfs_LIBRARIES})
    target_link_libraries(maidsafe_demo_drive ${Cbfs_LIBRARIES})
    target_link_libraries(cbfs_driver ${BoostFilesystemLibs} ${BoostSystemLibs} ${Cbfs_LIBRARIES} ${JustThread_LIBRARIES})
    add_dependencies(cbfs_driver boost)
  endif()
elseif(APPLE)
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY})
  target_link_libraries(maidsafe_demo_drive ${Fuse_LIBRARY})
else()
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY} rt dl)
  target_link_libraries(maidsafe_demo_drive ${Fuse_LIBRARY} rt dl)
endif()

#TODO (TEAM) Create unit test target for directory listing to build even with dummy win dependency
if(MaidsafeTesting AND (Cbfs_FOUND OR NOT WIN32))
  ms_add_executable(drive_demo Tools ${DemoMain})
  set_target_properties(drive_demo PROPERTIES COMPILE_DEFINITIONS "MAIDSAFE_DRIVE_DEMO")
  target_link_libraries(drive_demo maidsafe_demo_drive)

  ms_add_executable(TESTdrive_listings_api "Tests/Drive" ${DriveTestsAllFiles})
  ms_add_executable(TESTdrive_virtual_filesystem "Tests/Drive" ${TestVfsFiles})
  ms_add_executable(TESTdrive_real_filesystem "Tests/Drive" ${TestVfsFiles})

  target_link_libraries(TESTdrive_listings_api maidsafe_drive)
  target_link_libraries(TESTdrive_virtual_filesystem maidsafe_drive)
  target_link_libraries(TESTdrive_real_filesystem maidsafe_drive)
endif()

rename_outdated_built_exes()


#==================================================================================================#
# Set compiler and linker flags                                                                    #
#==================================================================================================#
include(standard_flags)
if(Cbfs_FOUND)
  add_definitions(-DCBFS_KEY=${Cbfs_KEY} -DHAVE_CBFS)
endif()

if(MaidsafeTesting AND (Cbfs_FOUND OR NOT WIN32))
  set_target_properties(TESTdrive_real_filesystem PROPERTIES COMPILE_DEFINITIONS "DISK_TEST")
endif()
if(Cbfs_FOUND)
  set_target_properties(cbfs_driver PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
endif()
add_coverage_exclude(demo_main\\\\.cc)


#==================================================================================================#
# Tests                                                                                            #
#==================================================================================================#
if(MaidsafeTesting)
  add_style_test()
  if(Cbfs_FOUND OR NOT WIN32)
    add_gtests(TESTdrive_listings_api)
    #add_gtests(TESTdrive_shares)
    add_gtests(TESTdrive_virtual_filesystem)
  endif()
  add_project_experimental()
  test_summary_output()
endif()
