function(klogg_add_zzpuretools)
  set(zz_source_dir "${PROJECT_SOURCE_DIR}/3rdparty/vendor/ZzPureTools")
  if(NOT EXISTS "${zz_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR
      "ZzLogg requires ZzPureTools. Run: git submodule update --init --recursive")
  endif()
  if(CMAKE_VERSION VERSION_LESS 3.23)
    message(FATAL_ERROR "ZzLogg requires CMake 3.23 or newer")
  endif()
  if(Qt6Core_VERSION VERSION_LESS 6.8)
    message(FATAL_ERROR "ZzLogg requires Qt 6.8 or newer")
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
  set(zz_binary_dir "${CMAKE_BINARY_DIR}/_deps/zzpuretools")
  add_subdirectory("${zz_source_dir}"
                   "${zz_binary_dir}"
                   EXCLUDE_FROM_ALL)

  # The vendored subtree resolves the exact runtime selected by CXX. Its
  # install rules are excluded with the subtree, so expose those audited
  # inputs for the application's deliberately small runtime install closure.
  if(ZZ_BUNDLE_GNU_RUNTIME)
    get_directory_property(ZZLOGG_GNU_LIBSTDCXX_PATH
      DIRECTORY "${zz_binary_dir}"
      DEFINITION ZZ_GNU_LIBSTDCXX_PATH)
    get_directory_property(ZZLOGG_GNU_LIBGCC_PATH
      DIRECTORY "${zz_binary_dir}"
      DEFINITION ZZ_GNU_LIBGCC_PATH)
    foreach(zz_runtime_path IN ITEMS
        "${ZZLOGG_GNU_LIBSTDCXX_PATH}"
        "${ZZLOGG_GNU_LIBGCC_PATH}")
      if(NOT zz_runtime_path OR NOT EXISTS "${zz_runtime_path}")
        message(FATAL_ERROR "ZzPureTools did not resolve its GNU runtime")
      endif()
    endforeach()
    set(ZZLOGG_GNU_LIBSTDCXX_PATH
      "${ZZLOGG_GNU_LIBSTDCXX_PATH}" PARENT_SCOPE)
    set(ZZLOGG_GNU_LIBGCC_PATH
      "${ZZLOGG_GNU_LIBGCC_PATH}" PARENT_SCOPE)
    set(ZZLOGG_GNU_RUNTIME_LICENSE_DIR
      "${ZZ_GNU_RUNTIME_LICENSE_DIR}" PARENT_SCOPE)
  endif()
endfunction()

function(klogg_copy_ui_runtime_dlls target)
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_RUNTIME_DLLS:${target}>
              $<TARGET_FILE_DIR:${target}>
      COMMAND ${CMAKE_COMMAND} -E make_directory
              $<TARGET_FILE_DIR:${target}>/platforms
      COMMAND ${CMAKE_COMMAND} -E make_directory
              $<TARGET_FILE_DIR:${target}>/iconengines
      COMMAND ${CMAKE_COMMAND} -E make_directory
              $<TARGET_FILE_DIR:${target}>/imageformats
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
              $<TARGET_FILE_DIR:${target}>/platforms
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:Qt6::QSvgIconPlugin>
              $<TARGET_FILE_DIR:${target}>/iconengines
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:Qt6::QSvgPlugin>
              $<TARGET_FILE_DIR:${target}>/imageformats
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()
