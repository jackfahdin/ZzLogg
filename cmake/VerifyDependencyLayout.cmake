if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED GIT_EXECUTABLE OR NOT EXISTS "${GIT_EXECUTABLE}")
  message(FATAL_ERROR "GIT_EXECUTABLE is required")
endif()

set(expected_zzpuretools_commit
  "7c9fecf44fc8823e78dce45f4328f147ed75567a")
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
if(NOT gitmodules_content MATCHES "3rdparty/vendor/ZzPureTools")
  message(FATAL_ERROR "ZzPureTools submodule declaration is missing")
endif()

message(STATUS "Dependency layout contract passed")
