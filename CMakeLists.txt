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

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake_modules/standard_setup.cmake")
  cmake_minimum_required(VERSION 2.8)  # To suppress warning cluttering error message
  set(Msg "\nThis project can currently only be build as part of the MaidSafe super-project.  For")
  set(Msg "${Msg} full details, see https://github.com/maidsafe/MaidSafe/wiki/Build-Instructions\n")
  message(FATAL_ERROR "${Msg}")
endif()

project(drive)

include(../../cmake_modules/standard_setup.cmake)


#==================================================================================================#
# Callback File System / Fuse library search                                                       #
#==================================================================================================#
if(WIN32)
  include(maidsafe_find_cbfs)
  if(NOT CbfsFound AND NOT DONT_USE_CBFS)
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
ms_glob_dir(Drive ${DriveSourcesDir} Drive)
set(LocalDriveMain ${DriveSourcesDir}/local/local_drive.cc)
set(NetworkDriveMain ${DriveSourcesDir}/network/network_drive.cc)
set(WinServiceMain ${DriveSourcesDir}/win_service/win_service_main.cc)
set(UnixFiles ${DriveApiDir}/unix_drive.h ${DriveSourcesDir}/unix_drive.cc)
set(WinFiles ${DriveApiDir}/win_drive.h ${DriveSourcesDir}/win_drive.cc)
set(UnixFileCommands ${DriveSourcesDir}/tools/commands/unix_file_commands.h
                     ${DriveSourcesDir}/tools/commands/unix_file_commands.cc)
set(WindowsFileCommands ${DriveSourcesDir}/tools/commands/windows_file_commands.h
                        ${DriveSourcesDir}/tools/commands/windows_file_commands.cc)
if(CbfsFound)
  set(CbfsKeyFile ${CMAKE_CURRENT_BINARY_DIR}/cbfs_conf/maidsafe/drive/cbfs_key.h)
  configure_file("${DriveSourcesDir}/cbfs_key.h.in" "${CbfsKeyFile}" @ONLY)
  list(APPEND DriveAllFiles ${CbfsKeyFile})
endif()

if(WIN32)
  list(REMOVE_ITEM DriveAllFiles ${UnixFiles})
else()
  list(REMOVE_ITEM DriveAllFiles ${WinFiles})
endif()

ms_glob_dir(DriveTests ${DriveSourcesDir}/tests Tests)
ms_glob_dir(DriveTools ${DriveSourcesDir}/tools Tools)
set(DriveLauncherFiles ${DriveApiDir}/tools/launcher.h ${DriveSourcesDir}/tools/launcher.cc)
list(REMOVE_ITEM DriveToolsAllFiles ${DriveLauncherFiles})
ms_glob_dir(DriveToolsCommands ${DriveSourcesDir}/tools/commands Commands)
list(REMOVE_ITEM DriveToolsCommandsAllFiles ${UnixFileCommands} ${WindowsFileCommands})
if(CbfsFound)
  ms_glob_dir(Installer ${DriveSourcesDir}/installer Installer)
  set(CbfsPathsFile ${CMAKE_CURRENT_BINARY_DIR}/cbfs_conf/maidsafe/drive/cbfs_paths.h)
  file(TO_NATIVE_PATH "${CbfsCab}" CbfsCabNative)
  file(TO_NATIVE_PATH "${CbfsInstaller}" CbfsInstallerNative)
  string(REPLACE "\\" "\\\\" CbfsCabNative "${CbfsCabNative}")
  string(REPLACE "\\" "\\\\" CbfsInstallerNative "${CbfsInstallerNative}")
  configure_file("${DriveSourcesDir}/installer/cbfs_paths.h.in" "${CbfsPathsFile}" @ONLY)
  list(APPEND InstallerAllFiles ${CbfsPathsFile})
endif()
set(DriveTestsResourcesDir ${DriveSourcesDir}/tests/resources)


#==================================================================================================#
# Define MaidSafe libraries and executables                                                        #
#==================================================================================================#
ms_add_static_library(maidsafe_drive ${DriveAllFiles})
target_include_directories(maidsafe_drive
  PUBLIC ${PROJECT_SOURCE_DIR}/include ${CbfsIncludeDir} ${Fuse_INCLUDE_DIR}
  PRIVATE ${PROJECT_SOURCE_DIR}/src $<$<BOOL:${CbfsFound}>:${CMAKE_CURRENT_BINARY_DIR}/cbfs_conf>)
if(WIN32)
  if(CbfsFound)
    ms_add_executable(cbfs_driver_installer "Production" ${InstallerAllFiles})
    target_include_directories(cbfs_driver_installer PRIVATE ${BoostSourceDir} ${CbfsIncludeDir} ${CMAKE_CURRENT_BINARY_DIR}/cbfs_conf)
    target_link_libraries(maidsafe_drive ${CbfsLibraries})
    target_link_libraries(cbfs_driver_installer maidsafe_common ${CbfsLibraries})
  endif()
elseif(APPLE)
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY})
elseif(BSD)
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY})
else()
  target_link_libraries(maidsafe_drive ${Fuse_LIBRARY} rt dl)
endif()

ms_add_static_library(maidsafe_drive_launcher ${DriveLauncherFiles})
target_include_directories(maidsafe_drive_launcher PUBLIC ${PROJECT_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/GeneratedProtoFiles PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(maidsafe_drive maidsafe_drive_launcher maidsafe_encrypt)
target_link_libraries(maidsafe_drive_launcher maidsafe_common maidsafe_nfs_client protobuf_lite)

ms_add_executable(local_drive "Tools/Drive" ${LocalDriveMain})
target_link_libraries(local_drive maidsafe_drive maidsafe_drive_launcher maidsafe_nfs_client)
ms_add_executable(drive "Production" ${NetworkDriveMain})
target_link_libraries(drive maidsafe_drive maidsafe_drive_launcher maidsafe_nfs_client)

if(WIN32)
  set_property(TARGET local_drive drive PROPERTY WIN32_EXECUTABLE TRUE)
  target_compile_options(drive PUBLIC $<$<CONFIG:Debug>: /bigobj>)
  if(MaidsafeTesting)
    ms_add_executable(local_drive_console "Tools/Drive" ${LocalDriveMain})
    target_link_libraries(local_drive_console maidsafe_drive maidsafe_drive_launcher maidsafe_nfs_client)
    ms_add_executable(drive_console "Tools/Drive" ${NetworkDriveMain})
    target_link_libraries(drive_console maidsafe_drive maidsafe_drive_launcher maidsafe_nfs_client)
    target_compile_options(drive_console PUBLIC $<$<CONFIG:Debug>: /bigobj>)
  endif()
endif()

if(MaidsafeTesting)
  ms_add_executable(weekly_test_filesystem "Tests/Drive"
                    ${DriveSourcesDir}/tools/filesystem_weekly.cc
                    ${DriveSourcesDir}/tools/tool_main.cc)
  if(WIN32)
    ms_add_executable(test_filesystem "Tests/Drive"
                      ${DriveSourcesDir}/tools/filesystem_test.cc
                      ${DriveSourcesDir}/tools/tool_main.cc
                      ${WindowsFileCommands})
  else()
    ms_add_executable(test_filesystem "Tests/Drive"
                      ${DriveSourcesDir}/tools/filesystem_test.cc
                      ${DriveSourcesDir}/tools/tool_main.cc
                      ${UnixFileCommands})
  endif()
  ms_add_executable(filesystem_commands "Tools/Drive"
                    ${DriveSourcesDir}/tools/filesystem_commands.h
                    ${DriveSourcesDir}/tools/filesystem_commands.cc
                    ${DriveSourcesDir}/tools/tool_main.cc
                    ${DriveToolsCommandsAllFiles})
  ms_add_executable(test_drive "Tests/Drive" ${DriveTestsAllFiles})
  target_include_directories(test_drive PRIVATE ${PROJECT_SOURCE_DIR}/src)

  add_dependencies(weekly_test_filesystem local_drive drive)
  add_dependencies(test_filesystem local_drive drive)
  add_dependencies(filesystem_commands local_drive drive)
  if(WIN32)
    add_dependencies(weekly_test_filesystem local_drive_console drive_console)
    add_dependencies(test_filesystem local_drive_console drive_console)
    add_dependencies(filesystem_commands local_drive_console drive_console)
  endif()
  target_include_directories(weekly_test_filesystem PRIVATE $<TARGET_PROPERTY:maidsafe_drive,INTERFACE_INCLUDE_DIRECTORIES>)
  target_include_directories(test_filesystem PRIVATE $<TARGET_PROPERTY:maidsafe_drive,INTERFACE_INCLUDE_DIRECTORIES>)
  target_include_directories(test_filesystem PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_include_directories(filesystem_commands PRIVATE ${PROJECT_SOURCE_DIR}/src)

  target_link_libraries(weekly_test_filesystem maidsafe_drive_launcher maidsafe_test)
  target_link_libraries(test_filesystem maidsafe_drive_launcher maidsafe_test)
  target_link_libraries(filesystem_commands maidsafe_drive_launcher maidsafe_drive)
  target_link_libraries(test_drive maidsafe_drive maidsafe_nfs_client maidsafe_test)
endif()

ms_rename_outdated_built_exes()

if(WIN32 AND NOT CbfsFound)
  foreach(Target maidsafe_drive cbfs_driver_installer local_drive drive local_drive_console drive_console weekly_test_filesystem test_filesystem filesystem_commands test_drive)
    if(TARGET ${Target})
      set_target_properties(${Target} PROPERTIES EXCLUDE_FROM_ALL ON EXCLUDE_FROM_DEFAULT_BUILD ON)
    endif()
  endforeach()
endif()


#==================================================================================================#
# Set compiler and linker flags                                                                    #
#==================================================================================================#
include(standard_flags)

target_compile_definitions(maidsafe_drive PUBLIC $<$<BOOL:${UNIX}>:FUSE_USE_VERSION=26>)
target_compile_definitions(local_drive PRIVATE $<$<BOOL:${WIN32}>:USES_WINMAIN>)
target_compile_definitions(drive PRIVATE $<$<BOOL:${WIN32}>:USES_WINMAIN>)

if(MaidsafeTesting)
  target_compile_definitions(weekly_test_filesystem PRIVATE $<TARGET_PROPERTY:maidsafe_drive,INTERFACE_COMPILE_DEFINITIONS>)
  target_compile_definitions(test_filesystem PRIVATE $<TARGET_PROPERTY:maidsafe_drive,INTERFACE_COMPILE_DEFINITIONS>)
  set_property(TARGET weekly_test_filesystem APPEND PROPERTY COMPILE_DEFINITIONS "CMAKE_GENERATOR=\"${CMAKE_GENERATOR}\"")
  set_property(TARGET test_filesystem APPEND PROPERTY COMPILE_DEFINITIONS "CMAKE_GENERATOR=\"${CMAKE_GENERATOR}\"")
  set_property(TARGET weekly_test_filesystem APPEND PROPERTY COMPILE_DEFINITIONS "DRIVE_TESTS_RESOURCES=${DriveTestsResourcesDir}")
  set_property(TARGET test_filesystem APPEND PROPERTY COMPILE_DEFINITIONS "DRIVE_TESTS_RESOURCES=${DriveTestsResourcesDir}")
  if(WIN32)
    if(CMAKE_CL_64)
      set_property(TARGET weekly_test_filesystem APPEND PROPERTY COMPILE_DEFINITIONS "VS_DEV_CMD=\"$ENV{VCInstallDir}vcvarsall.bat\" x86_amd64")
      set_property(TARGET test_filesystem APPEND PROPERTY COMPILE_DEFINITIONS "VS_DEV_CMD=\"$ENV{VCInstallDir}vcvarsall.bat\" x86_amd64")
    else()
      set_property(TARGET weekly_test_filesystem APPEND PROPERTY COMPILE_DEFINITIONS "VS_DEV_CMD=\"$ENV{VCInstallDir}vcvarsall.bat\" x86")
      set_property(TARGET test_filesystem APPEND PROPERTY COMPILE_DEFINITIONS "VS_DEV_CMD=\"$ENV{VCInstallDir}vcvarsall.bat\" x86")
    endif()
  endif()
endif()
if(CbfsFound)
  target_compile_definitions(cbfs_driver_installer PRIVATE $<TARGET_PROPERTY:maidsafe_common,INTERFACE_COMPILE_DEFINITIONS>)
  target_compile_options(cbfs_driver_installer PRIVATE $<TARGET_PROPERTY:maidsafe_common,INTERFACE_COMPILE_OPTIONS>)
  set_property(TARGET cbfs_driver_installer APPEND_STRING PROPERTY LINK_FLAGS " /level='requireAdministrator' /uiAccess='false' ")
  set_property(TARGET cbfs_driver_installer APPEND PROPERTY COMPILE_DEFINITIONS "CBFS_ROOT_DIR=${CBFS_ROOT_DIR}")
  set(ProductId "47584d51-4651-487a-534c-675242655154")
  set_property(TARGET drive local_drive cbfs_driver_installer APPEND PROPERTY COMPILE_DEFINITIONS "PRODUCT_ID=${ProductId}")
  if(MaidsafeTesting)
    set_property(TARGET drive_console local_drive_console APPEND PROPERTY COMPILE_DEFINITIONS "PRODUCT_ID=${ProductId}")
  endif()
endif()


#==================================================================================================#
# Tests                                                                                            #
#==================================================================================================#
if(MaidsafeTesting)
  if(WIN32)
    set(Exclusions "maidsafe/drive/unix_drive.h")
  else()
    set(Exclusions "maidsafe/drive/win_drive.h")
  endif()
  ms_add_default_tests()
  ms_add_gtests(test_drive)

  if(RUNNING_AS_CTEST_SCRIPT)
    set(CATCH_EXCEPTIONS "1")
  else()
    set(CATCH_EXCEPTIONS "0")
  endif()
  if(WIN32)
    set(Console _console)
  endif()

  add_test(NAME "\"Real Disk Filesystem Test\""
           COMMAND test_filesystem --disk --gtest_catch_exceptions=${CATCH_EXCEPTIONS})

  add_test(NAME "\"Local Drive Filesystem Test\""
           COMMAND test_filesystem --local${Console} --gtest_catch_exceptions=${CATCH_EXCEPTIONS})

  set(TEST_TARGET test_filesystem)
  get_network_test_arg()
  unset(TEST_TARGET)
  add_test(NAME "\"Network Drive Filesystem Test\""
           COMMAND test_filesystem --network${Console} --gtest_catch_exceptions=${CATCH_EXCEPTIONS}
                   $<$<BOOL:${NetworkTestArg}>:--bootstrap_file> $<$<BOOL:${NetworkTestArg}>:${NetworkTestArg}>)

  set(Timeout 180)
  ms_update_test_timeout(Timeout)
  set_tests_properties("\"Real Disk Filesystem Test\"" "\"Local Drive Filesystem Test\"" "\"Network Drive Filesystem Test\"" PROPERTIES
                       TIMEOUT ${Timeout} LABELS "Drive;Filesystem;${TASK_LABEL}")

  if(WEEKLY)
    add_test(NAME "\"Real Disk Weekly Test\""
             COMMAND weekly_test_filesystem --disk --gtest_catch_exceptions=${CATCH_EXCEPTIONS})
    add_test(NAME "\"Local Drive Weekly Test\""
             COMMAND weekly_test_filesystem --local${Console} --gtest_catch_exceptions=${CATCH_EXCEPTIONS})
    add_test(NAME "\"Network Drive Weekly Test\""
             COMMAND weekly_test_filesystem --network${Console} --gtest_catch_exceptions=${CATCH_EXCEPTIONS}
                     $<$<BOOL:${NetworkTestArg}>:--bootstrap_file> $<$<BOOL:${NetworkTestArg}>:${NetworkTestArg}>)
    set(Timeout 10800)
    ms_update_test_timeout(Timeout)
    set_tests_properties("\"Real Disk Weekly Test\"" "\"Local Drive Weekly Test\"" "\"Network Drive Weekly Test\"" PROPERTIES
                         TIMEOUT ${Timeout} LABELS "Drive;Filesystem;Weekly;${TASK_LABEL}")
  endif()

  ms_test_summary_output()
endif()


#==================================================================================================#
# Package                                                                                          #
#==================================================================================================#
# We can't install targets which are excluded from the ALL target.  Since all Drive targets are excluded if CBFS isn't found
# we'll omit the install/package section in that case.
if(WIN32 AND NOT CbfsFound)
  return()
endif()

install(TARGETS maidsafe_drive maidsafe_drive_launcher COMPONENT Development CONFIGURATIONS Debug Release ARCHIVE DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ COMPONENT Development DESTINATION include)

foreach(Target cbfs_driver_installer local_drive local_drive_console drive_console)
  if(TARGET ${Target})
    install(TARGETS ${Target} COMPONENT Tools CONFIGURATIONS Debug RUNTIME DESTINATION bin/debug)
    install(TARGETS ${Target} COMPONENT Tools CONFIGURATIONS Release RUNTIME DESTINATION bin)
  endif()
endforeach()

install(TARGETS drive COMPONENT Apps CONFIGURATIONS Debug RUNTIME DESTINATION bin/debug)
install(TARGETS drive COMPONENT Apps CONFIGURATIONS Release RUNTIME DESTINATION bin)

foreach(Target weekly_test_filesystem test_filesystem filesystem_commands test_drive)
  if(TARGET ${Target})
    install(TARGETS ${Target} COMPONENT Tests CONFIGURATIONS Debug RUNTIME DESTINATION bin/debug)
    install(TARGETS ${Target} COMPONENT Tests CONFIGURATIONS Release RUNTIME DESTINATION bin)
  endif()
endforeach()
