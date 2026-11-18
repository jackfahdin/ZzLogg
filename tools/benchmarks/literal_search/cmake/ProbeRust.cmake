# Shared by explicitly enabled, excluded-from-all main-project probes only.
if(NOT TARGET zzlogg_literal_probe_rust)
  find_program(LITERAL_PROBE_CARGO_EXECUTABLE cargo REQUIRED)
  find_package(Threads REQUIRED)
  set(_literal_rust "${PROJECT_SOURCE_DIR}/tools/benchmarks/literal_search/rust")
  set(_literal_cargo "${PROJECT_BINARY_DIR}/literal-probe-cargo")
  if(WIN32)
    set(_literal_library "${_literal_cargo}/release/zzlogg_literal_probe.lib")
  else()
    set(_literal_library "${_literal_cargo}/release/libzzlogg_literal_probe.a")
  endif()
  add_custom_command(OUTPUT "${_literal_library}"
    COMMAND "${LITERAL_PROBE_CARGO_EXECUTABLE}" build --release --locked --offline
      --manifest-path "${_literal_rust}/Cargo.toml" --target-dir "${_literal_cargo}"
    DEPENDS "${_literal_rust}/Cargo.toml" "${_literal_rust}/Cargo.lock" "${_literal_rust}/src/lib.rs"
    VERBATIM)
  add_custom_target(zzlogg_literal_probe_rust_build DEPENDS "${_literal_library}")
  add_library(zzlogg_literal_probe_rust STATIC IMPORTED GLOBAL)
  set_target_properties(zzlogg_literal_probe_rust PROPERTIES IMPORTED_LOCATION "${_literal_library}")
  add_dependencies(zzlogg_literal_probe_rust zzlogg_literal_probe_rust_build)
  target_link_libraries(zzlogg_literal_probe_rust INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
  if(WIN32)
    target_link_libraries(zzlogg_literal_probe_rust INTERFACE ws2_32 userenv bcrypt ntdll)
  elseif(UNIX)
    target_link_libraries(zzlogg_literal_probe_rust INTERFACE m)
  endif()
endif()
