# Build a consumer using only the advertised utility interface.
add_executable(zzlogg_utils_dependency_test utilsdependencytest.cpp)
target_link_libraries(zzlogg_utils_dependency_test PRIVATE klogg_utils)
add_dependencies(ci_build zzlogg_utils_dependency_test)
add_test(NAME zzlogg_build.utils_dependencies COMMAND zzlogg_utils_dependency_test)

add_test(NAME zzlogg_build.search_configuration
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/backend-config
    -P ${CMAKE_CURRENT_SOURCE_DIR}/searchconfigurationtest.cmake)

if(KLOGG_BUILD_TESTS)
  add_executable(zzlogg_search_backend_test searchbackendtest.cpp)
  target_link_libraries(zzlogg_search_backend_test PRIVATE klogg_regex Catch2)
  add_dependencies(ci_build zzlogg_search_backend_test)
  add_test(NAME zzlogg_build.search_backend COMMAND zzlogg_search_backend_test)
endif()

if(WIN32)
  set_property(TEST zzlogg_build.utils_dependencies APPEND PROPERTY
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  if(KLOGG_BUILD_TESTS)
    set_property(TEST zzlogg_build.search_backend APPEND PROPERTY
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
endif()
