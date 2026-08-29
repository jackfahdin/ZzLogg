function(klogg_add_zzpuretools)
  set(zz_source_dir "${PROJECT_SOURCE_DIR}/3rdparty/vendor/ZzPureTools")
  if(NOT EXISTS "${zz_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR
      "KLOGG_BUILD_UI2 requires ZzPureTools. Run: git submodule update --init --recursive")
  endif()
  if(CMAKE_VERSION VERSION_LESS 3.23)
    message(FATAL_ERROR "KLOGG_BUILD_UI2 requires CMake 3.23 or newer")
  endif()
  if(Qt6Core_VERSION VERSION_LESS 6.8)
    message(FATAL_ERROR "KLOGG_BUILD_UI2 requires Qt 6.8 or newer")
  endif()

  set(BUILD_SHARED_LIBS ON)
  set(BUILD_TESTING OFF)
  set(ZZ_BUILD_TESTS OFF)
  set(ZZ_BUILD_EXAMPLES OFF)
  set(ZZ_BUILD_BENCHMARKS OFF)
  set(ZZ_ENABLE_ASAN OFF)
  set(ZZ_ENABLE_UBSAN OFF)
  set(ZZ_ENABLE_CLANG_TIDY OFF)
  set(ZZ_ENABLE_MSVC_ANALYZE OFF)
  set(ZZ_WARNINGS_AS_ERRORS OFF)
  set(ZZ_ENABLE_LTO OFF)
  set(ZZ_BUILD_FLUENT_QUICK OFF)
  set(ZZ_RELEASE_BUILD OFF)
  add_subdirectory("${zz_source_dir}"
                   "${CMAKE_BINARY_DIR}/_deps/zzpuretools"
                   EXCLUDE_FROM_ALL)
endfunction()

function(klogg_copy_ui2_runtime_dlls target)
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_RUNTIME_DLLS:${target}>
              $<TARGET_FILE_DIR:${target}>
      COMMAND ${CMAKE_COMMAND} -E make_directory
              $<TARGET_FILE_DIR:${target}>/platforms
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
              $<TARGET_FILE_DIR:${target}>/platforms
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()
