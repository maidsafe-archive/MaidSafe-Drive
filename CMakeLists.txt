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
  if(NOT Cbfs_FOUND AND NOT DONT_USE_CBFS)
    set(Msg "\nFailed to find Callback File System.  This is a prerequisite for Drive on Windows, so ")
    set(Msg "${Msg}Drive will be excluded from the default build.  See ")
    set(Msg "${Msg}https://github.com/maidsafe/MaidSafe/wiki/Build-Instructions#optional-components")
    set(Msg "${Msg} for further info.\nIf Cbfs is already installed, run:\n")
    set(Msg "${Msg}cmake . -DCBFS_ROOT_DIR=<Path to Cbfs root directory>\n")
    set(Msg "${Msg}e.g.\ncmake . -DCBFS_ROOT_DIR=\"C:\\Program Files\\EldoS\\Callback File System\"\n")
    set(Msg "${Msg}To permanently disable this warning, run:\ncmake . -DDONT_USE_CBFS=TRUE\n")
    message(WARNING "${Msg}")
  endif()
else()
  include(maidsafe_find_fuse)
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
set(WinFiles ${DriveApiDir}/win_drive.h ${DriveSourcesDir}/win_drive.cc)

if(WIN32)
  list(REMOVE_ITEM DriveAllFiles ${UnixFiles})
else()
  list(REMOVE_ITEM DriveAllFiles ${WinFiles})
endif()

glob_dir(DriveTests ${DriveSourcesDir}/tests Tests)
glob_dir(DriveTools ${DriveSourcesDir}/tools Tools)
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
target_link_libraries(maidsafe_drive maidsafe_encrypt ${BoostRegexLibs})
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

ms_add_executable(local_drive "Production" ${LocalDriveMain})
target_link_libraries(local_drive maidsafe_drive)
ms_add_executable(network_drive "Production" ${NetworkDriveMain})
target_link_libraries(network_drive maidsafe_drive maidsafe_nfs_client)
if(WIN32)
  set_property(TARGET local_drive network_drive PROPERTY WIN32_EXECUTABLE TRUE)
  if(MaidsafeTesting)
    ms_add_executable(local_drive_console "Production" ${LocalDriveMain})
    target_link_libraries(local_drive_console maidsafe_drive)
    ms_add_executable(network_drive_console "Production" ${NetworkDriveMain})
    target_link_libraries(network_drive_console maidsafe_drive maidsafe_nfs_client)
  endif()
endif()

if(MaidsafeTesting)
  ms_add_executable(filesystem_benchmark "Tools/Drive"
                    ${DriveSourcesDir}/tools/filesystem_benchmark.cc
                    ${DriveSourcesDir}/tools/tool_main.cc)
  ms_add_executable(filesystem_test "Tools/Drive"
                    ${DriveSourcesDir}/tools/filesystem_test.cc
                    ${DriveSourcesDir}/tools/tool_main.cc)
  ms_add_executable(test_drive "Tests/Drive" ${DriveTestsAllFiles})
  target_include_directories(test_drive PRIVATE ${PROJECT_SOURCE_DIR}/src)
  set_property(TARGET test_drive APPEND PROPERTY COMPILE_DEFINITIONS "CBFS_GUID=713CC6CE-B3E2-4fd9-838D-E28F558F6866")

  add_dependencies(filesystem_benchmark local_drive network_drive)
  add_dependencies(filesystem_test local_drive network_drive)
  if(WIN32)
    add_dependencies(filesystem_benchmark local_drive_console network_drive_console)
    add_dependencies(filesystem_test local_drive_console network_drive_console)
  endif()
  target_include_directories(filesystem_benchmark PRIVATE $<TARGET_PROPERTY:maidsafe_drive,INTERFACE_INCLUDE_DIRECTORIES>)
  target_include_directories(filesystem_test PRIVATE $<TARGET_PROPERTY:maidsafe_drive,INTERFACE_INCLUDE_DIRECTORIES>)

  target_link_libraries(filesystem_benchmark maidsafe_common)
  target_link_libraries(filesystem_test maidsafe_common)
  add_dependencies(filesystem_test catch)
  target_link_libraries(test_drive maidsafe_drive)
endif()

rename_outdated_built_exes()

if(WIN32 AND NOT Cbfs_FOUND)
  foreach(Target maidsafe_drive cbfs_driver local_drive network_drive local_drive_console network_drive_console filesystem_benchmark filesystem_test test_drive)
    if(TARGET ${Target})
      set_target_properties(${Target} PROPERTIES EXCLUDE_FROM_ALL ON EXCLUDE_FROM_DEFAULT_BUILD ON)
    endif()
  endforeach()
endif()


#==================================================================================================#
# Set compiler and linker flags                                                                    #
#==================================================================================================#
include(standard_flags)

target_compile_definitions(maidsafe_drive
  PUBLIC
    $<$<BOOL:${UNIX}>:FUSE_USE_VERSION=26>
  PRIVATE
    $<$<BOOL:${Cbfs_FOUND}>:CBFS_KEY=${Cbfs_KEY}>
)
target_compile_definitions(local_drive PRIVATE $<$<BOOL:${WIN32}>:USES_WINMAIN>)
target_compile_definitions(network_drive PRIVATE $<$<BOOL:${WIN32}>:USES_WINMAIN>)

if(MaidsafeTesting)
  target_compile_definitions(filesystem_benchmark PRIVATE $<TARGET_PROPERTY:maidsafe_drive,INTERFACE_COMPILE_DEFINITIONS>)
  target_compile_definitions(filesystem_test PRIVATE $<TARGET_PROPERTY:maidsafe_drive,INTERFACE_COMPILE_DEFINITIONS>)
endif()
if(Cbfs_FOUND)
  target_compile_definitions(cbfs_driver PRIVATE $<TARGET_PROPERTY:maidsafe_common,INTERFACE_COMPILE_DEFINITIONS> "CBFS_KEY=${Cbfs_KEY}")
  target_compile_options(cbfs_driver PRIVATE $<TARGET_PROPERTY:maidsafe_common,INTERFACE_COMPILE_OPTIONS>)
  set_property(TARGET cbfs_driver APPEND_STRING PROPERTY LINK_FLAGS " /level='requireAdministrator' /uiAccess='false' ")
  if(MaidsafeTesting)
    set_property(TARGET local_drive network_drive local_drive_console network_drive_console
                 APPEND PROPERTY COMPILE_DEFINITIONS "CBFS_GUID=713CC6CE-B3E2-4fd9-838D-E28F558F6866" "CBFS_KEY=${Cbfs_KEY}")
  else()
    # TODO(Brian) BEFORE_RELEASE - Set these UIDs appropriately
    set_property(TARGET local_drive APPEND PROPERTY COMPILE_DEFINITIONS "CBFS_GUID=MAIDSAFE-SUREFILE" "CBFS_KEY=${Cbfs_KEY}")
    set_property(TARGET network_drive APPEND PROPERTY COMPILE_DEFINITIONS "CBFS_GUID=MAIDSAFE-LIFESTUFF" "CBFS_KEY=${Cbfs_KEY}")
  endif()
endif()


#==================================================================================================#
# Tests                                                                                            #
#==================================================================================================#
if(MaidsafeTesting)
  add_style_test()
  add_catch_tests(test_drive)
  add_project_experimental()
  test_summary_output()
endif()
