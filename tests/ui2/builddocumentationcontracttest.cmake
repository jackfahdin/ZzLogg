if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(READ "${SOURCE_ROOT}/docs/BUILD.md" build_documentation)
file(READ
  "${SOURCE_ROOT}/3rdparty/vendor/ZzPureTools/cmake/ZzCompilerCapabilities.cmake"
  compiler_capabilities)

set(vendor_thresholds
  "CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.1"
  "CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17.0"
  "CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15.0"
  "CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 13.3"
  "MSVC_VERSION LESS 1938")
foreach(vendor_threshold IN LISTS vendor_thresholds)
  string(FIND "${compiler_capabilities}" "${vendor_threshold}" threshold_index)
  if(threshold_index EQUAL -1)
    message(FATAL_ERROR
      "Unexpected vendored compiler capability floor: ${vendor_threshold}")
  endif()
endforeach()

set(documented_compiler_floors
  "GCC 13.1 or newer"
  "Clang 17 or newer"
  "Apple Clang 15 or newer"
  "macOS deployment target 13.3 or newer"
  "MSVC 19.38 or newer"
  "Visual Studio 2022 17.8+")
foreach(documented_floor IN LISTS documented_compiler_floors)
  string(FIND "${build_documentation}" "${documented_floor}" floor_index)
  if(floor_index EQUAL -1)
    message(FATAL_ERROR
      "docs/BUILD.md does not match the vendored compiler floor: ${documented_floor}")
  endif()
endforeach()

foreach(obsolete_floor IN ITEMS
    "MSVC 2022 17.14+" "GCC 13+" "Clang 16+")
  string(FIND "${build_documentation}" "${obsolete_floor}" obsolete_index)
  if(NOT obsolete_index EQUAL -1)
    message(FATAL_ERROR
      "docs/BUILD.md still advertises an obsolete compiler floor: ${obsolete_floor}")
  endif()
endforeach()
