if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED GIT_EXECUTABLE OR NOT EXISTS "${GIT_EXECUTABLE}")
  message(FATAL_ERROR "GIT_EXECUTABLE is required")
endif()

set(expected_zzpuretools_commit
  "f9e6c6f25af8061666cc04da2a43c0a1d3cfe271")
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

message(STATUS "Dependency layout contract passed")
