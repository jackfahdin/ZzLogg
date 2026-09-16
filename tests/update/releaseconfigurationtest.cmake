cmake_minimum_required(VERSION 3.23)
if(POLICY CMP0174)
  cmake_policy(SET CMP0174 NEW)
endif()

if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED FIXTURE_SOURCE OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "Release configuration test paths are required")
endif()

if(DEFINED TEST_CALLER_SENTINEL AND NOT EXISTS "${TEST_CALLER_SENTINEL}")
  message(FATAL_ERROR "Release configuration caller sentinel is missing")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_suffix)
set(test_run_root "${TEST_ROOT}/run-${run_suffix}")
if(EXISTS "${test_run_root}")
  message(FATAL_ERROR "Release configuration run directory already exists")
endif()
file(MAKE_DIRECTORY "${test_run_root}")
if(DEFINED TEST_CALLER_SENTINEL AND NOT EXISTS "${TEST_CALLER_SENTINEL}")
  message(FATAL_ERROR "Release configuration test removed its caller sentinel")
endif()
set(TEST_ROOT "${test_run_root}")

function(run_release_case name expect_success)
  set(options
    ARM64EC_CPP_GUARD CLEAR_GENERATOR_PLATFORM CLEAR_TARGET_ARCHITECTURE)
  set(one_value_args
    OFFICIAL SEQUENCE CHANNEL SCHEMA VERSION TWEAK EXPECT_AVAILABLE
    EXPECT_SEQUENCE EXPECT_SCHEMA EXPECT_CHANNEL SYSTEM_NAME
    GENERATOR_PLATFORM_OVERRIDE COMPILER_ARCHITECTURE_ID SYSTEM_PROCESSOR
    POINTER_SIZE)
  cmake_parse_arguments(PARSE_ARGV 2 CASE "${options}" "${one_value_args}" "")

  set(binary_dir "${TEST_ROOT}/${name}")
  set(configure_command
    "${CMAKE_COMMAND}"
    -S "${FIXTURE_SOURCE}"
    -B "${binary_dir}"
    -G "${GENERATOR}"
    "-DSOURCE_ROOT:PATH=${SOURCE_ROOT}"
    "-DZZLOGG_OFFICIAL_RELEASE:BOOL=${CASE_OFFICIAL}"
    "-DZZLOGG_RELEASE_SEQUENCE:STRING=${CASE_SEQUENCE}"
    "-DZZLOGG_RELEASE_CHANNEL:STRING=${CASE_CHANNEL}"
    "-DZZLOGG_RELEASE_DATA_SCHEMA:STRING=${CASE_SCHEMA}"
    "-DZZLOGG_DISPLAY_VERSION:STRING=${CASE_VERSION}"
    "-DTEST_VERSION_TWEAK:STRING=${CASE_TWEAK}"
    "-DCMAKE_BUILD_TYPE:STRING=${TEST_BUILD_TYPE}"
    "-DTEST_EXPECT_AVAILABLE:BOOL=${CASE_EXPECT_AVAILABLE}"
    "-DTEST_EXPECT_SEQUENCE:STRING=${CASE_EXPECT_SEQUENCE}"
    "-DTEST_EXPECT_SCHEMA:STRING=${CASE_EXPECT_SCHEMA}"
    "-DTEST_EXPECT_CHANNEL:STRING=${CASE_EXPECT_CHANNEL}")

  if(NOT GENERATOR_PLATFORM STREQUAL "")
    list(APPEND configure_command -A "${GENERATOR_PLATFORM}")
  endif()
  if(NOT GENERATOR_TOOLSET STREQUAL "")
    list(APPEND configure_command -T "${GENERATOR_TOOLSET}")
  endif()
  if(NOT GENERATOR_INSTANCE STREQUAL "")
    list(APPEND configure_command
      "-DCMAKE_GENERATOR_INSTANCE:STRING=${GENERATOR_INSTANCE}")
  endif()
  if(NOT CXX_COMPILER STREQUAL "")
    list(APPEND configure_command "-DCMAKE_CXX_COMPILER:FILEPATH=${CXX_COMPILER}")
  endif()
  if(DEFINED TEST_TARGET_DESCRIPTION_FILE)
    list(APPEND configure_command
      "-DTEST_TARGET_DESCRIPTION_FILE:FILEPATH=${TEST_TARGET_DESCRIPTION_FILE}")
  endif()
  foreach(variable IN ITEMS SYSTEM_NAME GENERATOR_PLATFORM_OVERRIDE
      COMPILER_ARCHITECTURE_ID SYSTEM_PROCESSOR POINTER_SIZE)
    if(DEFINED CASE_${variable})
      string(REPLACE ";" "\\;" escaped_value "${CASE_${variable}}")
      list(APPEND configure_command "-DTEST_${variable}:STRING=${escaped_value}")
    endif()
  endforeach()
  if(CASE_CLEAR_TARGET_ARCHITECTURE)
    list(APPEND configure_command -DTEST_CLEAR_TARGET_ARCHITECTURE:BOOL=ON)
  endif()
  if(CASE_CLEAR_GENERATOR_PLATFORM)
    list(APPEND configure_command -DTEST_CLEAR_GENERATOR_PLATFORM:BOOL=ON)
  endif()
  if(CASE_ARM64EC_CPP_GUARD)
    list(APPEND configure_command -DTEST_ARM64EC_CPP_GUARD:BOOL=ON)
  endif()

  execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr)
  set(configure_output "${configure_stdout}\n${configure_stderr}")

  if(expect_success)
    if(NOT configure_result EQUAL 0)
      message(FATAL_ERROR
        "${name}: configuration unexpectedly failed:\n${configure_output}")
    endif()
    execute_process(
      COMMAND "${CMAKE_COMMAND}" --build "${binary_dir}"
        --config "${TEST_CONFIGURATION}" --target release_identity_consumer
      RESULT_VARIABLE build_result
      OUTPUT_VARIABLE build_stdout
      ERROR_VARIABLE build_stderr)
    if(NOT build_result EQUAL 0)
      message(FATAL_ERROR
        "${name}: build failed:\n${build_stdout}\n${build_stderr}")
    endif()
    if(NOT DEFINED CTEST_COMMAND OR CTEST_COMMAND STREQUAL "")
      message(FATAL_ERROR "${name}: CTest command is required")
    endif()
    set(test_command "${CTEST_COMMAND}" --test-dir "${binary_dir}"
      --output-on-failure)
    if(NOT TEST_CONFIGURATION STREQUAL "")
      list(APPEND test_command -C "${TEST_CONFIGURATION}")
    endif()
    execute_process(
      COMMAND ${test_command}
      RESULT_VARIABLE consumer_result
      OUTPUT_VARIABLE consumer_stdout
      ERROR_VARIABLE consumer_stderr)
    if(NOT consumer_result EQUAL 0)
      message(FATAL_ERROR
        "${name}: consumer test failed:\n${consumer_stdout}\n${consumer_stderr}")
    endif()
  else()
    if(configure_result EQUAL 0)
      message(FATAL_ERROR "${name}: invalid configuration was accepted")
    endif()
    if(NOT configure_output MATCHES "Official release identity:")
      message(FATAL_ERROR
        "${name}: failure did not come from release validation:\n${configure_output}")
    endif()
  endif()
endfunction()

function(run_invalid_case name)
  set(options EMPTY_SEQUENCE EMPTY_CHANNEL EMPTY_SCHEMA CLEAR_TARGET_ARCHITECTURE)
  set(one_value_args
    SEQUENCE CHANNEL SCHEMA VERSION TWEAK SYSTEM_NAME
    GENERATOR_PLATFORM_OVERRIDE POINTER_SIZE)
  cmake_parse_arguments(PARSE_ARGV 1 INVALID "${options}" "${one_value_args}" "")

  set(sequence 123)
  set(channel stable)
  set(schema 0)
  set(version 26.09.00)
  set(tweak 0)
  if(DEFINED INVALID_SEQUENCE)
    set(sequence "${INVALID_SEQUENCE}")
  endif()
  if(DEFINED INVALID_CHANNEL)
    set(channel "${INVALID_CHANNEL}")
  endif()
  if(DEFINED INVALID_SCHEMA)
    set(schema "${INVALID_SCHEMA}")
  endif()
  if(DEFINED INVALID_VERSION)
    set(version "${INVALID_VERSION}")
  endif()
  if(DEFINED INVALID_TWEAK)
    set(tweak "${INVALID_TWEAK}")
  endif()
  if(INVALID_EMPTY_SEQUENCE)
    set(sequence "")
  endif()
  if(INVALID_EMPTY_CHANNEL)
    set(channel "")
  endif()
  if(INVALID_EMPTY_SCHEMA)
    set(schema "")
  endif()

  set(target_args)
  foreach(variable IN ITEMS SYSTEM_NAME GENERATOR_PLATFORM_OVERRIDE POINTER_SIZE)
    if(DEFINED INVALID_${variable})
      list(APPEND target_args ${variable} "${INVALID_${variable}}")
    endif()
  endforeach()
  if(INVALID_CLEAR_TARGET_ARCHITECTURE)
    list(APPEND target_args CLEAR_TARGET_ARCHITECTURE)
  endif()

  run_release_case(${name} FALSE
    OFFICIAL ON SEQUENCE "${sequence}" CHANNEL "${channel}" SCHEMA "${schema}"
    VERSION "${version}" TWEAK "${tweak}"
    EXPECT_AVAILABLE ON EXPECT_SEQUENCE 123 EXPECT_SCHEMA 0 EXPECT_CHANNEL stable
    ${target_args})
endfunction()

run_release_case(development_with_residual_fields TRUE
  OFFICIAL OFF SEQUENCE 123 CHANNEL stable SCHEMA 0 VERSION 26.09.00 TWEAK 0
  EXPECT_AVAILABLE OFF EXPECT_SEQUENCE 0 EXPECT_SCHEMA 0 EXPECT_CHANNEL unused)
include("${TEST_ROOT}/development_with_residual_fields/native-target.cmake")
message(STATUS "Native target supports official identity: ${native_official_supported}")
run_release_case(stable ${native_official_supported}
  OFFICIAL ON SEQUENCE 123 CHANNEL stable SCHEMA 0 VERSION 26.09.00 TWEAK 0
  EXPECT_AVAILABLE ON EXPECT_SEQUENCE 123 EXPECT_SCHEMA 0 EXPECT_CHANNEL stable)
if(native_official_supported)
  run_release_case(compiler_architecture_descriptor TRUE
    OFFICIAL ON SEQUENCE 123 CHANNEL stable SCHEMA 0 VERSION 26.09.00 TWEAK 0
    EXPECT_AVAILABLE ON EXPECT_SEQUENCE 123 EXPECT_SCHEMA 0 EXPECT_CHANNEL stable
    CLEAR_GENERATOR_PLATFORM COMPILER_ARCHITECTURE_ID X64 SYSTEM_PROCESSOR ARM64)
  run_release_case(arm64ec_cpp_guard TRUE
    OFFICIAL ON SEQUENCE 123 CHANNEL stable SCHEMA 0 VERSION 26.09.00 TWEAK 0
    EXPECT_AVAILABLE OFF EXPECT_SEQUENCE 0 EXPECT_SCHEMA 0 EXPECT_CHANNEL unused
    ARM64EC_CPP_GUARD)
endif()
run_release_case(preview_maximum ${native_official_supported}
  OFFICIAL ON SEQUENCE 18446744073709551615 CHANNEL preview SCHEMA 4294967295
  VERSION 26.09.00 TWEAK 0 EXPECT_AVAILABLE ON
  EXPECT_SEQUENCE 18446744073709551615 EXPECT_SCHEMA 4294967295
  EXPECT_CHANNEL preview)

if(TEST_NATIVE_CASES_ONLY)
  return()
endif()

run_invalid_case(missing_sequence EMPTY_SEQUENCE)
run_invalid_case(missing_channel EMPTY_CHANNEL)
run_invalid_case(missing_schema EMPTY_SCHEMA)
run_invalid_case(zero_sequence SEQUENCE 0)
run_invalid_case(sequence_above_maximum SEQUENCE 18446744073709551616)
run_invalid_case(sequence_leading_zero SEQUENCE 01)
run_invalid_case(sequence_negative SEQUENCE -1)
run_invalid_case(sequence_whitespace SEQUENCE " 123")
run_invalid_case(sequence_decimal SEQUENCE 1.5)
run_invalid_case(sequence_exponent SEQUENCE 1e3)
run_invalid_case(sequence_semicolon_injection
  SEQUENCE [=[1\;message(FATAL_ERROR injected)]=])
run_invalid_case(channel_quote_injection CHANNEL [=[stable"]=])
run_invalid_case(wrong_channel CHANNEL beta)
run_invalid_case(schema_above_maximum SCHEMA 4294967296)
run_invalid_case(schema_leading_zero SCHEMA 00)
run_invalid_case(invalid_month VERSION 26.13.00)
run_invalid_case(extra_display_component VERSION 26.09.00.1)
run_invalid_case(nonzero_resource_tweak TWEAK 1)
run_invalid_case(non_windows_target SYSTEM_NAME Linux)
run_invalid_case(arm64_target GENERATOR_PLATFORM_OVERRIDE ARM64)
run_release_case(compiler_arm64_precedes_processor FALSE
  OFFICIAL ON SEQUENCE 123 CHANNEL stable SCHEMA 0 VERSION 26.09.00 TWEAK 0
  EXPECT_AVAILABLE ON EXPECT_SEQUENCE 123 EXPECT_SCHEMA 0 EXPECT_CHANNEL stable
  CLEAR_GENERATOR_PLATFORM COMPILER_ARCHITECTURE_ID ARM64
  SYSTEM_PROCESSOR AMD64)
run_invalid_case(thirty_two_bit_target POINTER_SIZE 4)
run_invalid_case(unknown_target_architecture CLEAR_TARGET_ARCHITECTURE)
