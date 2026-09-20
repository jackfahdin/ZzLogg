set(UI_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qtests_main.cpp
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

# Warning relaxation for test-only targets (see the function comment in the
# top-level CMakeLists.txt; -Werror arrives via linked interfaces).
zzlogg_relax_test_warnings_in_current_dir()
