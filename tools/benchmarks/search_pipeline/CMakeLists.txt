# Explicit opt-in, never a dependency of the application or ci_build.
find_program(PIPELINE_CARGO_EXECUTABLE cargo REQUIRED)
find_package(Threads REQUIRED)
set(_literal_rust "${PROJECT_SOURCE_DIR}/tools/benchmarks/literal_search/rust")
if(WIN32)
  set(_pipeline_rust_lib "${CMAKE_CURRENT_BINARY_DIR}/cargo/release/zzlogg_literal_probe.lib")
else()
  set(_pipeline_rust_lib "${CMAKE_CURRENT_BINARY_DIR}/cargo/release/libzzlogg_literal_probe.a")
endif()
add_custom_command(OUTPUT "${_pipeline_rust_lib}"
  COMMAND "${PIPELINE_CARGO_EXECUTABLE}" build --release --locked --offline
    --manifest-path "${_literal_rust}/Cargo.toml" --target-dir "${CMAKE_CURRENT_BINARY_DIR}/cargo"
  DEPENDS "${_literal_rust}/Cargo.toml" "${_literal_rust}/Cargo.lock" "${_literal_rust}/src/lib.rs"
  VERBATIM)
add_custom_target(pipeline_rust_build DEPENDS "${_pipeline_rust_lib}")
add_library(pipeline_rust STATIC IMPORTED)
set_target_properties(pipeline_rust PROPERTIES IMPORTED_LOCATION "${_pipeline_rust_lib}")
add_dependencies(pipeline_rust pipeline_rust_build)
add_executable(zzlogg_search_pipeline_probe EXCLUDE_FROM_ALL
  main.cpp pipeline.cpp contract.cpp ../literal_search/matcher.cpp)
target_include_directories(zzlogg_search_pipeline_probe PRIVATE ../literal_search)
target_compile_definitions(zzlogg_search_pipeline_probe PRIVATE LITERAL_ENABLE_RUST)
target_link_libraries(zzlogg_search_pipeline_probe PRIVATE
  klogg_logdata klogg_settings pipeline_rust Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(zzlogg_search_pipeline_probe PRIVATE ws2_32 userenv bcrypt ntdll)
elseif(UNIX)
  target_link_libraries(zzlogg_search_pipeline_probe PRIVATE m)
endif()
