set(UI_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qtests_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/searchhighlight_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logdata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logfiltereddata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_test.cpp
)

if(NOT APPLE)
    LIST(APPEND UI_TEST_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow_test.cpp)
endif()

add_executable(klogg_itests ${UI_TEST_SOURCES})

target_link_libraries(klogg_itests klogg_ui klogg_utils Catch2 Qt6::Test test_utils)
set_target_properties(klogg_itests PROPERTIES AUTOMOC ON)


add_test(
    NAME klogg_itests
    COMMAND klogg_itests -platform offscreen
)

# The Windows CI action bounds retries of this known intermittent parallel
# search scenario via ctest --repeat. REPEAT_UNTIL_PASS is not a test property.

# Separate process and hard CTest deadline: a scheduler regression must not hang the suite.
add_executable(zzlogg_search_worker_recovery_test search_worker_recovery_test.cpp)
target_link_libraries(zzlogg_search_worker_recovery_test PRIVATE klogg_logdata klogg_settings Qt6::Test)
add_test(NAME zzlogg_ui.search_worker_recovery COMMAND zzlogg_search_worker_recovery_test)
set_tests_properties(zzlogg_ui.search_worker_recovery PROPERTIES TIMEOUT 30)
add_dependencies(ci_build zzlogg_search_worker_recovery_test)

add_executable(zzlogg_search_capacity_gate_test search_capacity_gate_test.cpp)
target_link_libraries(zzlogg_search_capacity_gate_test PRIVATE klogg_utils)
add_test(NAME zzlogg_ui.search_capacity_gate COMMAND zzlogg_search_capacity_gate_test)
set_tests_properties(zzlogg_ui.search_capacity_gate PROPERTIES TIMEOUT 30)
add_dependencies(ci_build zzlogg_search_capacity_gate_test)

# Keep corrupt-stream regressions bounded even if a worker stops responding.
add_executable(zzlogg_decompressor_test decompressor_test.cpp)
target_link_libraries(zzlogg_decompressor_test PRIVATE klogg_ui Qt6::Test)
target_compile_definitions(zzlogg_decompressor_test PRIVATE
    DECOMPRESS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compression"
    STREAM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/streams")
set_target_properties(zzlogg_decompressor_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.decompressor COMMAND zzlogg_decompressor_test)
set_tests_properties(zzlogg_ui.decompressor PROPERTIES TIMEOUT 45)
add_dependencies(ci_build zzlogg_decompressor_test)

add_executable(zzlogg_archive_test archive_test.cpp)
target_link_libraries(zzlogg_archive_test PRIVATE klogg_ui Qt6::Test)
target_compile_definitions(zzlogg_archive_test PRIVATE ARCHIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/archives")
set_target_properties(zzlogg_archive_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.archive COMMAND zzlogg_archive_test)
set_tests_properties(zzlogg_ui.archive PROPERTIES TIMEOUT 45)
add_dependencies(ci_build zzlogg_archive_test)

add_executable(zzlogg_compression_device_test compression_device_test.cpp)
target_link_libraries(zzlogg_compression_device_test PRIVATE klogg_ui Qt6::Test)
target_compile_definitions(zzlogg_compression_device_test PRIVATE STREAM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/streams")
set_target_properties(zzlogg_compression_device_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.compression_device COMMAND zzlogg_compression_device_test)
set_tests_properties(zzlogg_ui.compression_device PROPERTIES TIMEOUT 45)
add_dependencies(ci_build zzlogg_compression_device_test)

add_executable(zzlogg_archive_safety_test archive_safety_test.cpp)
target_link_libraries(zzlogg_archive_safety_test PRIVATE klogg_ui Qt6::Test)
target_compile_definitions(zzlogg_archive_safety_test PRIVATE SAFETY_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/archive-safety")
set_target_properties(zzlogg_archive_safety_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.archive_safety COMMAND zzlogg_archive_safety_test)
set_tests_properties(zzlogg_ui.archive_safety PROPERTIES TIMEOUT 45)
add_dependencies(ci_build zzlogg_archive_safety_test)

# Warning relaxation for test-only targets (see the function comment in the
# top-level CMakeLists.txt; -Werror arrives via linked interfaces).
zzlogg_relax_test_warnings_in_current_dir()
