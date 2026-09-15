add_executable(zzlogg_update_testfeed testfeed.cpp)
target_include_directories(zzlogg_update_testfeed PRIVATE
  "${PROJECT_SOURCE_DIR}/src/update/src" "${PROJECT_SOURCE_DIR}/tests/update")
target_link_libraries(zzlogg_update_testfeed PRIVATE
  zzlogg_update_core zzlogg_update_json zzlogg_update_monocypher Qt6::Core)
# Deliberately no install or runtime deployment rule.
add_test(NAME zzlogg_update.fixture_tool
  COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:zzlogg_update_testfeed>"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/fixture-test"
    -P "${PROJECT_SOURCE_DIR}/tests/update/fixturetooltest.cmake")
if(WIN32)
  set_property(TEST zzlogg_update.fixture_tool PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
