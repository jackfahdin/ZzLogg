set(template "${SOURCE_ROOT}/cmake/StageLinuxPackageIntegration.cmake.in")
if(NOT EXISTS "${template}")
  message(FATAL_ERROR "Linux package integration staging script is missing")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(PROJECT_SOURCE_DIR "${SOURCE_ROOT}")
set(script "${TEST_ROOT}/StageLinuxPackageIntegration.cmake")
configure_file("${template}" "${script}" @ONLY)
execute_process(COMMAND "${CMAKE_COMMAND}"
  "-DCMAKE_INSTALL_PREFIX=${TEST_ROOT}/package-root" -P "${script}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Linux package integration staging failed:\n${output}\n${error}")
endif()

set(package_root "${TEST_ROOT}/package-root")
set(launcher "${package_root}/usr/bin/ZzLogg")
if(NOT EXISTS "${launcher}")
  message(FATAL_ERROR "Package staging lacks /usr/bin/ZzLogg")
endif()
file(READ "${launcher}" launcher_content)
if(NOT launcher_content STREQUAL
   "#!/bin/sh\nexec /opt/ZzLogg/bin/ZzLogg \"\$@\"\n")
  message(FATAL_ERROR "Package launcher does not exec the private runtime")
endif()

# Non-DESTDIR generators such as TGZ include the packaging install prefix
# (/opt/ZzLogg); system integration must still land at the package root.
file(REMOVE_RECURSE "${TEST_ROOT}/cpack-like")
set(cpack_script "${TEST_ROOT}/cpack-like/StageLinuxPackageIntegration.cmake")
file(MAKE_DIRECTORY "${TEST_ROOT}/cpack-like")
configure_file("${template}" "${cpack_script}" @ONLY)
execute_process(COMMAND "${CMAKE_COMMAND}"
  "-DCMAKE_INSTALL_PREFIX=${TEST_ROOT}/cpack-like/staging/opt/ZzLogg" -P "${cpack_script}"
  RESULT_VARIABLE cpack_result OUTPUT_VARIABLE cpack_output ERROR_VARIABLE cpack_error)
if(NOT cpack_result EQUAL 0)
  message(FATAL_ERROR "cpack-like staging failed:\n${cpack_output}\n${cpack_error}")
endif()
if(NOT EXISTS "${TEST_ROOT}/cpack-like/staging/usr/bin/ZzLogg")
  message(FATAL_ERROR "cpack staging lacks /usr/bin/ZzLogg (packaging prefix not stripped)")
endif()
if(EXISTS "${TEST_ROOT}/cpack-like/staging/opt/ZzLogg/usr/bin/ZzLogg")
  message(FATAL_ERROR "launcher leaked under the application prefix")
endif()

foreach(relative IN ITEMS
    "usr/share/applications/ZzLogg.desktop"
    "usr/share/icons/hicolor/16x16/apps/ZzLogg.png"
    "usr/share/icons/hicolor/512x512/apps/ZzLogg.png"
    "usr/share/icons/hicolor/scalable/apps/ZzLogg.svg")
  if(NOT EXISTS "${package_root}/${relative}")
    message(FATAL_ERROR "Package staging lacks system integration: /${relative}")
  endif()
endforeach()

# DEB/RPM use DESTDIR staging: the install prefix is only /opt/ZzLogg,
# while DESTDIR holds the package root. Exercise that calling convention.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
  "DESTDIR=${TEST_ROOT}/destdir-root"
  "${CMAKE_COMMAND}" "-DCMAKE_INSTALL_PREFIX=/opt/ZzLogg" -P "${script}"
  RESULT_VARIABLE destdir_result OUTPUT_VARIABLE destdir_output ERROR_VARIABLE destdir_error)
if(NOT destdir_result EQUAL 0)
  message(FATAL_ERROR "DESTDIR staging failed:\n${destdir_output}\n${destdir_error}")
endif()
if(NOT EXISTS "${TEST_ROOT}/destdir-root/usr/bin/ZzLogg")
  message(FATAL_ERROR "DESTDIR staging lacks /usr/bin/ZzLogg")
endif()

# Reject paths that would write to the host after removing the app prefix.
foreach(unsafe_prefix IN ITEMS "" "/" "/opt/ZzLogg" "/opt/ZzLogg/../..")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "DESTDIR="
    "${CMAKE_COMMAND}" "-DCMAKE_INSTALL_PREFIX=${unsafe_prefix}" -P "${script}"
    RESULT_VARIABLE unsafe_result OUTPUT_VARIABLE unsafe_output ERROR_VARIABLE unsafe_error)
  if(unsafe_result EQUAL 0 OR NOT unsafe_error MATCHES "Refusing to stage")
    message(FATAL_ERROR "Unsafe prefix was not rejected: '${unsafe_prefix}'\n${unsafe_error}")
  endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "DESTDIR=/"
  "${CMAKE_COMMAND}" "-DCMAKE_INSTALL_PREFIX=/opt/ZzLogg" -P "${script}"
  RESULT_VARIABLE unsafe_result OUTPUT_VARIABLE unsafe_output ERROR_VARIABLE unsafe_error)
if(unsafe_result EQUAL 0 OR NOT unsafe_error MATCHES "Refusing to stage")
  message(FATAL_ERROR "Root DESTDIR was not rejected:\n${unsafe_error}")
endif()

# Run real generators so their staging conventions cannot be hidden by a
# hand-crafted CMAKE_INSTALL_PREFIX. No Qt build is needed for this fixture.
set(fixture "${TEST_ROOT}/fixture")
file(MAKE_DIRECTORY "${fixture}")
file(WRITE "${fixture}/payload" "package runtime fixture\n")
file(WRITE "${fixture}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.23)
project(LinuxPackageIntegration NONE)
set(CMAKE_INSTALL_PREFIX "/opt/ZzLogg")
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/ZzLogg")
set(CPACK_PACKAGE_NAME "zzlogg-integration-test")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_CONTACT "test@example.invalid")
set(CPACK_PACKAGE_FILE_NAME "integration-test")
set(CPACK_INSTALL_SCRIPTS "${STAGING_SCRIPT}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/payload" DESTINATION bin RENAME ZzLogg)
include(CPack)
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${fixture}" -B "${fixture}/build"
  "-DSTAGING_SCRIPT=${script}"
  RESULT_VARIABLE configure_result OUTPUT_VARIABLE configure_output ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Package fixture configure failed:\n${configure_output}\n${configure_error}")
endif()
get_filename_component(cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
find_program(cpack NAMES cpack HINTS "${cmake_bin}" REQUIRED)
set(generators TGZ)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  find_program(dpkg_deb NAMES dpkg-deb)
  if(dpkg_deb)
    list(APPEND generators DEB)
  endif()
  find_program(rpmbuild NAMES rpmbuild)
  if(rpmbuild)
    list(APPEND generators RPM)
  endif()
endif()
foreach(generator IN LISTS generators)
  execute_process(COMMAND "${cpack}" --config "${fixture}/build/CPackConfig.cmake"
    -G "${generator}" -B "${fixture}/packages"
    RESULT_VARIABLE package_result OUTPUT_VARIABLE package_output ERROR_VARIABLE package_error)
  if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "${generator} packaging failed:\n${package_output}\n${package_error}")
  endif()
  file(GLOB staged_roots "${fixture}/packages/_CPack_Packages/*/${generator}/integration-test")
  list(LENGTH staged_roots root_count)
  if(NOT root_count EQUAL 1)
    message(FATAL_ERROR "Expected one ${generator} staging root: ${staged_roots}")
  endif()
  list(GET staged_roots 0 staged_root)
  foreach(relative IN ITEMS
      "usr/bin/ZzLogg"
      "usr/share/applications/ZzLogg.desktop"
      "usr/share/icons/hicolor/16x16/apps/ZzLogg.png"
      "usr/share/icons/hicolor/512x512/apps/ZzLogg.png"
      "usr/share/icons/hicolor/scalable/apps/ZzLogg.svg"
      "opt/ZzLogg/bin/ZzLogg")
    if(NOT EXISTS "${staged_root}/${relative}")
      message(FATAL_ERROR "${generator} package lacks /${relative}")
    endif()
  endforeach()
  if(EXISTS "${staged_root}/opt/ZzLogg/usr")
    message(FATAL_ERROR "${generator} system integration leaked under /opt/ZzLogg")
  endif()
  message(STATUS "${generator} package staging verified")
endforeach()
