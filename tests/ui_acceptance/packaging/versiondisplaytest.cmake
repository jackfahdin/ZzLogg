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

# A package version bump must reach the compiled display string in the first
# incremental build, not just the generated header and Windows RC resource.
set(incremental_work "${BINARY_ROOT}/version-display-test/incremental")
file(REMOVE_RECURSE "${incremental_work}")
set(fixture "${incremental_work}/source")
set(build "${incremental_work}/build")
file(MAKE_DIRECTORY "${fixture}/cmake")
foreach(module IN ITEMS prepare_version.cmake generate_version_h.cmake
    ZzLoggVersion.cmake win32_rc.cmake version_info.h.in version_resource.rc.in)
  configure_file("${SOURCE_ROOT}/cmake/${module}" "${fixture}/cmake/${module}" COPYONLY)
endforeach()
file(WRITE "${fixture}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.25)
project(VersionProbe VERSION 26.10.9 LANGUAGES CXX)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(ZZLOGG_PRODUCT_NAME ZzLogg)
include(prepare_version)
add_library(display_version STATIC version.cpp)
add_dependencies(display_version generate_version)
add_executable(probe main.cpp)
target_link_libraries(probe PRIVATE display_version)
enable_testing()
add_test(NAME compiled_display_version COMMAND probe)
set_tests_properties(compiled_display_version PROPERTIES
  PASS_REGULAR_EXPRESSION "version=${ZZLOGG_DISPLAY_VERSION}!")
]=])
file(WRITE "${fixture}/version.cpp"
  "#include \"version.h\"\nconst char* displayVersion() { return KLOGG_VERSION; }\n")
file(WRITE "${fixture}/main.cpp"
  "#include <cstdio>\nconst char* displayVersion();\nint main() { std::printf(\"version=%s!\\n\", displayVersion()); }\n")
foreach(version IN ITEMS 26.10.09 26.10.10)
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${fixture}" -B "${build}"
    -G Ninja "-DBUILD_VERSION=${version}" RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output ERROR_VARIABLE configure_error)
  if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Version fixture configure failed:\n${configure_output}\n${configure_error}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}"
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Version fixture build failed:\n${build_output}\n${build_error}")
  endif()
  execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${build}" --output-on-failure
    RESULT_VARIABLE test_result OUTPUT_VARIABLE test_output ERROR_VARIABLE test_error)
  if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "Compiled display version is stale after building ${version}:\n${test_output}\n${test_error}")
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
