cmake_minimum_required(VERSION 3.23)
project(zzlogg_release_identity_fixture LANGUAGES CXX)

# Test-only descriptor overrides exercise routing, not cross-platform execution.
if(DEFINED TEST_TARGET_DESCRIPTION_FILE)
  include("${TEST_TARGET_DESCRIPTION_FILE}")
endif()
set(native_architecture "${CMAKE_SYSTEM_PROCESSOR}")
if(NOT "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
  set(native_architecture "${CMAKE_GENERATOR_PLATFORM}")
elseif(NOT "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "")
  set(native_architecture "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
endif()
string(TOLOWER "${native_architecture}" native_architecture)
set(native_official_supported FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_SIZEOF_VOID_P EQUAL 8
    AND native_architecture MATCHES "^(x64|amd64|x86_64)$")
  set(native_official_supported TRUE)
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/native-target.cmake"
  "set(native_official_supported ${native_official_supported})\n")

set(PROJECT_VERSION_TWEAK "${TEST_VERSION_TWEAK}")

if(DEFINED TEST_SYSTEM_NAME)
  set(CMAKE_SYSTEM_NAME "${TEST_SYSTEM_NAME}")
endif()
if(DEFINED TEST_GENERATOR_PLATFORM_OVERRIDE)
  set(CMAKE_GENERATOR_PLATFORM "${TEST_GENERATOR_PLATFORM_OVERRIDE}")
endif()
if(DEFINED TEST_COMPILER_ARCHITECTURE_ID)
  set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID "${TEST_COMPILER_ARCHITECTURE_ID}")
endif()
if(DEFINED TEST_SYSTEM_PROCESSOR)
  set(CMAKE_SYSTEM_PROCESSOR "${TEST_SYSTEM_PROCESSOR}")
endif()
if(DEFINED TEST_POINTER_SIZE)
  set(CMAKE_SIZEOF_VOID_P "${TEST_POINTER_SIZE}")
endif()
if(TEST_CLEAR_GENERATOR_PLATFORM)
  set(CMAKE_GENERATOR_PLATFORM "")
endif()
if(TEST_CLEAR_TARGET_ARCHITECTURE)
  set(CMAKE_GENERATOR_PLATFORM "")
  set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID "")
  set(CMAKE_SYSTEM_PROCESSOR "")
endif()

list(APPEND CMAKE_MODULE_PATH "${SOURCE_ROOT}/cmake")
include(ZzReleaseIdentity)

zzlogg_configure_release_identity(
  "${CMAKE_CURRENT_BINARY_DIR}/generated/zzlogg_release_identity.h")

set(release_identity_source "${SOURCE_ROOT}/src/update/src/releaseidentity.cpp")
if(TEST_ARM64EC_CPP_GUARD)
  configure_file(arm64ecwrapper.cpp.in arm64ecwrapper.cpp @ONLY)
  set(release_identity_source "${CMAKE_CURRENT_BINARY_DIR}/arm64ecwrapper.cpp")
endif()

add_executable(release_identity_consumer
  consumer.cpp
  "${release_identity_source}"
  "${SOURCE_ROOT}/src/update/src/version.cpp")
target_compile_features(release_identity_consumer PRIVATE cxx_std_17)
target_include_directories(release_identity_consumer PRIVATE
  "${SOURCE_ROOT}/src/update/include"
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(release_identity_consumer PRIVATE
  TEST_EXPECT_AVAILABLE=$<BOOL:${TEST_EXPECT_AVAILABLE}>
  TEST_EXPECT_SEQUENCE=${TEST_EXPECT_SEQUENCE}ULL
  TEST_EXPECT_PATCH=${TEST_EXPECT_PATCH}U
  TEST_EXPECT_SCHEMA=${TEST_EXPECT_SCHEMA}U
  TEST_EXPECT_CHANNEL="${TEST_EXPECT_CHANNEL}")

enable_testing()
add_test(NAME release_identity_consumer COMMAND release_identity_consumer)
