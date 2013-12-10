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


#==================================================================================================#
# Callback File System / Fuse library search                                                       #
#==================================================================================================#
if(WIN32)
  include(maidsafe_find_cbfs)
else()
  include(maidsafe_find_fuse)
endif()


#==================================================================================================#
# Set up all files as GLOBs                                                                        #
#==================================================================================================#
set(DriveSourcesDir ${PROJECT_SOURCE_DIR}/src/maidsafe/drive)
set(DriveApiDir ${PROJECT_SOURCE_DIR}/include/maidsafe/drive)
glob_dir(Drive ${DriveSourcesDir} Drive)
set(WinServiceMain ${DriveSourcesDir}/win_service_main.cc)
set(UnixFiles ${DriveApiDir}/unix_drive.h ${DriveSourcesDir}/unix_drive.cc)
set(CbfsWinFiles ${DriveApiDir}/win_drive.h ${DriveSourcesDir}/win_drive.cc)
set(DummyWinFiles ${DriveApiDir}/dummy_win_drive.h)

if(WIN32)
  if(Cbfs_FOUND)
    list(REMOVE_ITEM DriveAllFiles ${WinServiceMain} ${UnixFiles} ${DummyWinFiles})
  else()
    list(REMOVE_ITEM DriveAllFiles ${WinServiceMain} ${UnixFiles} ${CbfsWinFiles})
  endif()
else()
  list(REMOVE_ITEM DriveAllFiles ${WinServiceMain} ${CbfsWinFiles} ${DummyWinFiles})
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
target_include_directories(maidsafe_drive
  PUBLIC ${PROJECT_SOURCE_DIR}/include ${Cbfs_INCLUDE_DIR} ${Fuse_INCLUDE_DIR}
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(maidsafe_drive maidsafe_encrypt maidsafe_nfs_client ${BoostRegexLibs})
if(WIN32)
  if(Cbfs_FOUND)
    ms_add_executable(cbfs_driver "Production" ${InstallerAllFiles})
    target_include_directories(cbfs_driver PRIVATE ${BoostSourceDir} ${Cbfs_INCLUDE_DIR})
    target_link_libraries(maidsafe_drive ${Cbfs_LIBRARIES})
    target_link_libraries(cbfs_driver ${BoostFilesystemLibs} ${BoostSystemLibs} ${Cbfs_LIBRARIES} ${JustThread_LIBRARIES})
  endif()
elseif(APPLE)
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY})
else()
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY} rt dl)
endif()

#TODO (TEAM) Create unit test target for directory listing to build even with dummy win dependency
if(MaidsafeTesting AND (Cbfs_FOUND OR NOT WIN32))
  ms_add_executable(filesystem_benchmark "Tools/Drive" ${DriveSourcesDir}/tests/filesystem_benchmark.cc)
  ms_add_executable(FilesystemTest "Tests/Drive" ${DriveSourcesDir}/tests/filesystem_test.cc)
  ms_add_executable(TESTdrive "Tests/Drive" ${DriveTestsAllFiles})
  target_include_directories(TESTdrive PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(filesystem_benchmark maidsafe_common)
  target_link_libraries(FilesystemTest maidsafe_common)
  add_dependencies(FilesystemTest catch)
  target_link_libraries(TESTdrive maidsafe_drive)
endif()

rename_outdated_built_exes()


#==================================================================================================#
# Set compiler and linker flags                                                                    #
#==================================================================================================#
include(standard_flags)

target_compile_definitions(maidsafe_drive
  PUBLIC
    $<$<BOOL:${Cbfs_FOUND}>:HAVE_CBFS>
    $<$<BOOL:${UNIX}>:FUSE_USE_VERSION=26>
  PRIVATE
    $<$<BOOL:${Cbfs_FOUND}>:CBFS_KEY=${Cbfs_KEY}>
)
if(TARGET FilesystemTest)
  target_compile_options(FilesystemTest PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-empty-body>)
endif()
if(Cbfs_FOUND)
  target_compile_definitions(cbfs_driver PRIVATE $<TARGET_PROPERTY:maidsafe_common,INTERFACE_COMPILE_DEFINITIONS>)
  target_compile_options(cbfs_driver PRIVATE $<TARGET_PROPERTY:maidsafe_common,INTERFACE_COMPILE_OPTIONS>)
  set_target_properties(cbfs_driver PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
endif()


#==================================================================================================#
# Tests                                                                                            #
#==================================================================================================#
if(MaidsafeTesting)
  add_style_test()
  if(Cbfs_FOUND OR NOT WIN32)
    add_gtests(TESTdrive)
  endif()
  add_project_experimental()
  test_summary_output()
endif()
