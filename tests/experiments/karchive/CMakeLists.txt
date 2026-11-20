# Standalone comparison only; never included by the production build.
cmake_minimum_required(VERSION 3.30)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
project(ZzLoggKArchiveEvaluation LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
get_filename_component(ZZLOGG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ARCHIVE_BACKEND "fork" CACHE STRING "fork or upstream")
set(KARCHIVE_SOURCE "" CACHE PATH "Extracted upstream KArchive source (not downloaded automatically)")
set(BUILD_TESTING OFF CACHE BOOL "Disable upstream tests; run ZzLogg contracts" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static archive library for isolated probes" FORCE)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Concurrent Test)
if(ARCHIVE_BACKEND STREQUAL "fork")
    add_subdirectory("${ZZLOGG_ROOT}/3rdparty/vendor/klogg_karchive" backend)
    set(archive_library klogg_karchive)
elseif(ARCHIVE_BACKEND STREQUAL "upstream")
    if(NOT EXISTS "${KARCHIVE_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR "Set KARCHIVE_SOURCE to the extracted official release")
    endif()
    add_subdirectory("${KARCHIVE_SOURCE}" backend)
    # Upstream uses ranges/bit_cast; do not inherit the application's C++17 setting.
    set_target_properties(KF6Archive PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    set(archive_library KF6::Archive)
else()
    message(FATAL_ERROR "ARCHIVE_BACKEND must be fork or upstream")
endif()

# Reuse actual production code. Only log emission is disabled in this test host.
add_library(decompressor STATIC
    "${ZZLOGG_ROOT}/src/ui/src/decompressor.cpp"
    "${ZZLOGG_ROOT}/src/ui/include/decompressor.h" logging_stub.cpp)
target_include_directories(decompressor PUBLIC "${ZZLOGG_ROOT}/src/ui/include"
    "${ZZLOGG_ROOT}/src/utils/include" "${ZZLOGG_ROOT}/src/logging/include")
target_link_libraries(decompressor PUBLIC ${archive_library} Qt6::Concurrent)

function(add_contract target source test_name)
    add_executable(${target} "${ZZLOGG_ROOT}/${source}")
    target_link_libraries(${target} PRIVATE decompressor Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
enable_testing()
add_contract(zzlogg_decompressor_test tests/ui/decompressor_test.cpp zzlogg_ui.decompressor)
target_compile_definitions(zzlogg_decompressor_test PRIVATE
    DECOMPRESS_FIXTURES="${ZZLOGG_ROOT}/tests/ui/fixtures/compression"
    STREAM_FIXTURES="${ZZLOGG_ROOT}/tests/ui/fixtures/streams")
add_contract(zzlogg_archive_test tests/ui/archive_test.cpp zzlogg_ui.archive)
target_compile_definitions(zzlogg_archive_test PRIVATE ARCHIVE_FIXTURES="${ZZLOGG_ROOT}/tests/ui/fixtures/archives")
add_contract(zzlogg_compression_device_test tests/ui/compression_device_test.cpp zzlogg_ui.compression_device)
target_compile_definitions(zzlogg_compression_device_test PRIVATE STREAM_FIXTURES="${ZZLOGG_ROOT}/tests/ui/fixtures/streams")
add_contract(zzlogg_portable_package_test tests/portableupdater/packagetest.cpp zzlogg_portable.package)
target_sources(zzlogg_portable_package_test PRIVATE
    "${ZZLOGG_ROOT}/src/portableupdater/package.cpp" "${ZZLOGG_ROOT}/src/portableupdater/transaction.cpp")
target_include_directories(zzlogg_portable_package_test PRIVATE "${ZZLOGG_ROOT}/src/portableupdater")
target_compile_definitions(zzlogg_portable_package_test PRIVATE
    PORTABLE_ZIP_FIXTURE="${ZZLOGG_ROOT}/tests/ui/fixtures/archives/portable.zip")
