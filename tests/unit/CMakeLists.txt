# Add test cpp file
add_executable(klogg_tests
    linepositionarray_test.cpp
    patternmatcher_test.cpp
    booleanevaluator_test.cpp
    tests_main.cpp
)

target_link_libraries(klogg_tests klogg_ui klogg_utils klogg_logging Catch2 Qt6::Test)
target_compile_definitions(klogg_tests PRIVATE
  BOOLEAN_COMPATIBILITY_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/data/boolean-compatibility.json")
set_target_properties(klogg_tests PROPERTIES AUTOMOC ON)

add_test(
    NAME klogg_tests
    COMMAND klogg_tests -platform offscreen
)
# Warning relaxation for test-only targets (see the function comment in the
# top-level CMakeLists.txt; -Werror arrives via linked interfaces).
zzlogg_relax_test_warnings_in_current_dir()

# Explicitly built compatibility/performance probe; excluded from ci_build.
add_subdirectory(${PROJECT_SOURCE_DIR}/tools/benchmarks/boolean_evaluator
                 ${CMAKE_CURRENT_BINARY_DIR}/boolean_probe EXCLUDE_FROM_ALL)
