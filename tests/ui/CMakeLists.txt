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

# Warning relaxation for test-only targets (see the function comment in the
# top-level CMakeLists.txt; -Werror arrives via linked interfaces).
zzlogg_relax_test_warnings_in_current_dir()
