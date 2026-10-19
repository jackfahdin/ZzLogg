# Exercise the real header generator, including unpadded CI overrides.
foreach(case IN ITEMS "26.9.0.0|26.09.00" "26.10.3.42|26.10.03" "26.09.08|26.09.08" "26.12.100|26.12.100")
  string(REPLACE "|" ";" fields "${case}")
  list(GET fields 0 input)
  list(GET fields 1 expected)
  set(work "${BINARY_ROOT}/version-display-test/${input}")
  file(MAKE_DIRECTORY "${work}/generated")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DBUILD_VERSION=${input}"
    -P "${SOURCE_ROOT}/cmake/generate_version_h.cmake"
    WORKING_DIRECTORY "${work}" RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Version generation failed for ${input}")
  endif()
  file(READ "${work}/generated/version.h" header)
  string(FIND "${header}" "#define KLOGG_VERSION \"${expected}\"" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Expected display version ${expected} for ${input}, got:\n${header}")
  endif()
endforeach()

# CI must export the project version, not the historical upstream version.
set(ci_work "${BINARY_ROOT}/version-display-test/ci")
file(MAKE_DIRECTORY "${ci_work}")
file(WRITE "${ci_work}/CMakeLists.txt" "project(\n  Example\n  VERSION 26.10.3\n)\n")
file(WRITE "${ci_work}/environment.txt" "EXISTING=value\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${ci_work}"
  "-DVERSION_ENV_FILE=${ci_work}/environment.txt"
  -P "${SOURCE_ROOT}/cmake/ExportCiVersion.cmake" RESULT_VARIABLE ci_result)
file(READ "${ci_work}/environment.txt" ci_environment)
if(NOT ci_result EQUAL 0 OR NOT ci_environment STREQUAL "EXISTING=value\nKLOGG_VERSION=26.10.03\n")
  message(FATAL_ERROR "CI version export failed: ${ci_environment}")
endif()

# Reject malformed/overflowing input instead of silently producing invalid resources.
foreach(input IN ITEMS "26.9" "v26.09.00" "26.9.0.1.2" "26.9.65536" "26.9.0.-1")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DBUILD_VERSION=${input}"
    -P "${SOURCE_ROOT}/cmake/generate_version_h.cmake"
    WORKING_DIRECTORY "${ci_work}" RESULT_VARIABLE invalid_result
    OUTPUT_QUIET ERROR_QUIET)
  if(invalid_result EQUAL 0)
    message(FATAL_ERROR "Invalid version was accepted: ${input}")
  endif()
endforeach()
