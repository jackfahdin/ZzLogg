if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED GIT_EXECUTABLE OR NOT EXISTS "${GIT_EXECUTABLE}")
  message(FATAL_ERROR "GIT_EXECUTABLE is required")
endif()

set(expected_zzpuretools_commit
  "d6b90f1d266b6aa97b0ffb3a75deaa4bc2314514")
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" ls-files -s --
          "3rdparty/vendor/ZzPureTools"
  RESULT_VARIABLE zzpuretools_index_result
  OUTPUT_VARIABLE zzpuretools_index
  ERROR_VARIABLE zzpuretools_index_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT zzpuretools_index_result EQUAL 0)
  message(FATAL_ERROR
    "Unable to inspect ZzPureTools gitlink: ${zzpuretools_index_error}")
endif()
if(NOT zzpuretools_index MATCHES
   "^160000 ${expected_zzpuretools_commit} 0")
  message(FATAL_ERROR
    "ZzPureTools gitlink is not pinned to ${expected_zzpuretools_commit}: ${zzpuretools_index}")
endif()

file(READ "${SOURCE_ROOT}/.gitmodules" gitmodules_content)
if(gitmodules_content MATCHES "3rdparty/vendor/backward-cpp|bombela/backward-cpp")
  message(FATAL_ERROR "backward-cpp must not remain in .gitmodules")
endif()
if(NOT gitmodules_content MATCHES "3rdparty/vendor/ZzPureTools")
  message(FATAL_ERROR "ZzPureTools submodule declaration is missing")
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" ls-files -s --
          "3rdparty/vendor/backward-cpp"
  RESULT_VARIABLE backward_index_result
  OUTPUT_VARIABLE backward_index
  ERROR_VARIABLE backward_index_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT backward_index_result EQUAL 0)
  message(FATAL_ERROR "Unable to inspect backward-cpp files: ${backward_index_error}")
endif()
if(backward_index STREQUAL "")
  message(FATAL_ERROR "backward-cpp vendored files are not tracked")
endif()
if(backward_index MATCHES "(^|\\n)160000 ")
  message(FATAL_ERROR "backward-cpp is still a gitlink")
endif()

set(backward_required_files
  CMakeLists.txt
  BackwardConfig.cmake
  backward.cpp
  backward.hpp
  LICENSE.txt
  README.md
  UPSTREAM.md)
foreach(backward_file IN LISTS backward_required_files)
  if(NOT EXISTS "${SOURCE_ROOT}/3rdparty/vendor/backward-cpp/${backward_file}")
    message(FATAL_ERROR "backward-cpp vendored file is missing: ${backward_file}")
  endif()
endforeach()

file(READ "${SOURCE_ROOT}/3rdparty/vendor/backward-cpp/UPSTREAM.md"
  backward_upstream)
foreach(required_upstream_literal IN ITEMS
    "https://github.com/bombela/backward-cpp"
    "v1.6"
    "3bb9240cb15459768adb3e7d963a20e1523a6294"
    "MIT")
  string(FIND "${backward_upstream}" "${required_upstream_literal}"
    upstream_position)
  if(upstream_position EQUAL -1)
    message(FATAL_ERROR
      "backward-cpp provenance omits: ${required_upstream_literal}")
  endif()
endforeach()

file(READ "${SOURCE_ROOT}/3rdparty/CMakeLists.txt" thirdparty_cmake)
string(REGEX MATCHALL
  "add_subdirectory\\([ \t\r\n]*vendor/backward-cpp[ \t\r\n]*\\)"
  backward_local_additions "${thirdparty_cmake}")
list(LENGTH backward_local_additions backward_local_addition_count)
if(NOT backward_local_addition_count EQUAL 1)
  message(FATAL_ERROR
    "backward-cpp must be added exactly once from its vendored directory")
endif()
string(TOLOWER "${thirdparty_cmake}" thirdparty_cmake_lower)
if(thirdparty_cmake_lower MATCHES
   "(fetchcontent_declare|cpmaddpackage)[ \t\r\n]*\\([^)]*backward")
  message(FATAL_ERROR "backward-cpp has a network download fallback")
endif()
string(FIND "${thirdparty_cmake}"
  "Vendored backward-cpp is incomplete: missing"
  backward_guard_position)
if(backward_guard_position EQUAL -1)
  message(FATAL_ERROR "backward-cpp vendored completeness guard is missing")
endif()

message(STATUS "Dependency layout contract passed")
