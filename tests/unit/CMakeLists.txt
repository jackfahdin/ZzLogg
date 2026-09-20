# Add test cpp file
add_executable(klogg_tests
    linepositionarray_test.cpp
    patternmatcher_test.cpp
    tests_main.cpp
)

target_link_libraries(klogg_tests klogg_ui klogg_utils klogg_logging Catch2 Qt6::Test)
set_target_properties(klogg_tests PROPERTIES AUTOMOC ON)

add_test(
    NAME klogg_tests
    COMMAND klogg_tests -platform offscreen
)
# Warning relaxation for test-only targets (see the function comment in the
# top-level CMakeLists.txt; -Werror arrives via linked interfaces).
zzlogg_relax_test_warnings_in_current_dir()
