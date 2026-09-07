file(READ "${SOURCE_ROOT}/src/app/CMakeLists.txt" app_cmake)
file(READ "${SOURCE_ROOT}/CMakeLists.txt" root_cmake)

foreach(required IN ITEMS
    "qt_generate_deploy_app_script("
    "TARGET klogg"
    "NO_UNSUPPORTED_PLATFORM_ERROR"
    "install(SCRIPT \${KLOGG_LINUX_DEPLOY_SCRIPT})")
  string(FIND "${app_cmake}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Linux install lacks Qt self-contained deployment: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "configure_file("
    "StageLinuxPackageIntegration.cmake.in"
    "set(CPACK_INSTALL_SCRIPTS")
  string(FIND "${root_cmake}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Linux packages lack CPack-owned system integration: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    [=[set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/ZzLogg")]=]
    [=[set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS NO)]=]
    [=[set(CPACK_DEBIAN_PACKAGE_DEPENDS]=]
    "libc6")
  string(FIND "${root_cmake}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Linux package policy is not self-contained: ${required}")
  endif()
endforeach()

string(REGEX MATCH
  [=[set\(CPACK_DEBIAN_PACKAGE_DEPENDS[^\)]*\)]=]
  debian_dependencies "${root_cmake}")
foreach(forbidden IN ITEMS "libstdc++6" "libgcc-s1")
  string(FIND "${debian_dependencies}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Self-contained package still depends on host ${forbidden}")
  endif()
endforeach()
