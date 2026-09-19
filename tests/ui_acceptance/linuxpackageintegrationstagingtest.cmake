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

# The real cpack invocation passes a prefix ending in the packaging install
# prefix (/opt/ZzLogg); system integration must still land at the fs root.
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
