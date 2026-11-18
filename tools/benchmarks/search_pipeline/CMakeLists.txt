# Explicit opt-in, never a dependency of the application or ci_build.
include(../literal_search/cmake/ProbeRust.cmake)
add_executable(zzlogg_search_pipeline_probe EXCLUDE_FROM_ALL
  main.cpp pipeline.cpp contract.cpp ../literal_search/matcher.cpp)
target_include_directories(zzlogg_search_pipeline_probe PRIVATE ../literal_search)
target_compile_definitions(zzlogg_search_pipeline_probe PRIVATE LITERAL_ENABLE_RUST)
target_link_libraries(zzlogg_search_pipeline_probe PRIVATE
  klogg_logdata klogg_settings zzlogg_literal_probe_rust)
