#==============================================================================#
#                                                                              #
#  Copyright (c) 2012 MaidSafe.net limited                                     #
#                                                                              #
#  The following source code is property of MaidSafe.net limited and is not    #
#  meant for external use.  The use of this code is governed by the license    #
#  file licence.txt found in the root directory of this project and also on    #
#  www.maidsafe.net.                                                           #
#                                                                              #
#  You are not free to copy, amend or otherwise use this source code without   #
#  the explicit written permission of the board of directors of MaidSafe.net.  #
#                                                                              #
#==============================================================================#
#                                                                              #
#  Module used to set up tests using fstest from                               #
#  http://www.tuxera.com/community/posix-test-suite/.                          #
#                                                                              #
#==============================================================================#


if(NOT UNIX OR NOT CMAKE_BUILD_TYPE MATCHES "Release")
  return()
endif()

# Copy fstest source directory to build/Linux/<config>
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${DRIVE_SOURCE_DIR}/tests/pjd-fstest-20080816" "${CMAKE_BINARY_DIR}/pjd-fstest-20080816")

# Set up dirs to allow DriveDemo to be run from new fstest directory
set(CHUNKDIR "${CMAKE_BINARY_DIR}/pjd-fstest-20080816/chunkdir")
set(METADATADIR "${CMAKE_BINARY_DIR}/pjd-fstest-20080816/metadatadir")
set(MOUNTDIR "${CMAKE_BINARY_DIR}/pjd-fstest-20080816/mountdir")
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${CHUNKDIR}")
execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${METADATADIR}")
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${METADATADIR}")
execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${MOUNTDIR}")
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${MOUNTDIR}")

# Add fstest target to be built in new fstest directory
add_executable(fstest "${CMAKE_BINARY_DIR}/pjd-fstest-20080816/fstest.c")
set_source_files_properties("${CMAKE_BINARY_DIR}/pjd-fstest-20080816/fstest.c" PROPERTIES COMPILE_FLAGS "-Wall")
set_target_properties(fstest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pjd-fstest-20080816")
add_dependencies(fstest DriveDemo)

# Copy DriveDemo exe to new fstest directory once fstest is built and strip per-configuration suffix from exe name
#ADD_CUSTOM_COMMAND(TARGET fstest POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:DriveDemo> "${CMAKE_BINARY_DIR}/pjd-fstest-20080816/DriveDemo")

# Remove fstest.c from coverage stats
add_coverage_exclude(fstest\\\\.c)

# Add lines to CTestCustom.cmake to invoke execution of DriveDemo before tests start and killing after tests complete
# file(APPEND ${CMAKE_BINARY_DIR}/CTestCustom.cmake "SET(CTEST_CUSTOM_PRE_TEST \"${CMAKE_BINARY_DIR}/pjd-fstest-20080816/DriveDemo -C ${CHUNKDIR} -M ${METADATADIR} -D ${MOUNTDIR}\")\n")
# file(APPEND ${CMAKE_BINARY_DIR}/CTestCustom.cmake "SET(CTEST_CUSTOM_POST_TEST \"fusermount -u ${CMAKE_BINARY_DIR}/pjd-fstest-20080816/mountdir\")\n")

# Add individual tests
file(GLOB_RECURSE TEST_SCRIPTS RELATIVE "${CMAKE_BINARY_DIR}/pjd-fstest-20080816/tests" "${CMAKE_BINARY_DIR}/pjd-fstest-20080816/tests/*.t")
list(SORT TEST_SCRIPTS)
foreach(TEST_SCRIPT ${TEST_SCRIPTS})
  get_filename_component(TEST_NAME_BEGIN ${TEST_SCRIPT} PATH)
  get_filename_component(TEST_NAME_END ${TEST_SCRIPT} NAME_WE)
  add_test(NAME fstest-${TEST_NAME_BEGIN}-${TEST_NAME_END} WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/pjd-fstest-20080816/mountdir
             COMMAND prove -v ${CMAKE_BINARY_DIR}/pjd-fstest-20080816/tests/${TEST_SCRIPT})
  set_property(TEST fstest-${TEST_NAME_BEGIN}-${TEST_NAME_END} PROPERTY LABELS Behavioural POSIX)
  set_property(TEST fstest-${TEST_NAME_BEGIN}-${TEST_NAME_END} PROPERTY TIMEOUT 60)
endforeach()

#WILL_FAIL
