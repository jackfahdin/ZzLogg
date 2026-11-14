# Explicitly requested experiment, excluded from the application and ci_build.
add_executable(zzlogg_filewatch_probe
  ${PROJECT_SOURCE_DIR}/tests/ui_acceptance/filewatch/filewatchertest.cpp)
target_compile_definitions(zzlogg_filewatch_probe PRIVATE ZZLOGG_WATCH_PROBE)
target_include_directories(zzlogg_filewatch_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(zzlogg_filewatch_probe PRIVATE klogg_filewatch klogg_settings Qt6::Test)
set_target_properties(zzlogg_filewatch_probe PROPERTIES AUTOMOC ON)
