file(READ "${SOURCE_ROOT}/.github/workflows/ci-build.yml" ci)
foreach(required IN ITEMS
    "cpack --config build-linux/CPackConfig.cmake -G DEB"
    "cpack --config build-linux/CPackConfig.cmake -G RPM"
    "actions/upload-artifact@v4"
    "name: packages-linux"
    "packages/*.deb"
    "packages/*.rpm"
    "if-no-files-found: error")
  string(FIND "${ci}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Formal Linux DEB/RPM publication is missing: ${required}")
  endif()
endforeach()
