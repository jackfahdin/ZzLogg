# Keep upstream KDE settings local to this dependency.
function(zzlogg_add_syntax_highlighting)
  set(zzlogg_ecm_SOURCE_DIR "${PROJECT_SOURCE_DIR}/3rdparty/vendor/extra-cmake-modules")
  set(zzlogg_ecm_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/zzlogg_ecm-build")
  set(zzlogg_syntax_SOURCE_DIR "${PROJECT_SOURCE_DIR}/3rdparty/vendor/syntax-highlighting")
  set(zzlogg_syntax_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/zzlogg_syntax-build")
  foreach(required_file IN ITEMS
      "${zzlogg_ecm_SOURCE_DIR}/CMakeLists.txt"
      "${zzlogg_ecm_SOURCE_DIR}/modules/ECMSetupVersion.cmake"
      "${zzlogg_syntax_SOURCE_DIR}/CMakeLists.txt"
      "${zzlogg_syntax_SOURCE_DIR}/src/lib/repository.cpp")
    if(NOT EXISTS "${required_file}")
      message(FATAL_ERROR "Vendored KDE sources are incomplete: ${required_file}")
    endif()
  endforeach()
  # ECM is a build-time module collection; provide a build-tree package config.
  file(MAKE_DIRECTORY "${zzlogg_ecm_BINARY_DIR}")
  file(WRITE "${zzlogg_ecm_BINARY_DIR}/ECMConfig.cmake"
    "set(ECM_MODULE_PATH \"${zzlogg_ecm_SOURCE_DIR}/modules\" \"${zzlogg_ecm_SOURCE_DIR}/find-modules\" \"${zzlogg_ecm_SOURCE_DIR}/kde-modules\")\n")
  set(ECM_GLOBAL_FIND_VERSION 6.31.0)
  set(ECM_MODULE_DIR "${zzlogg_ecm_SOURCE_DIR}/modules")
  set(ECM_KDE_MODULE_DIR "${zzlogg_ecm_SOURCE_DIR}/kde-modules")
  set(ECM_FIND_MODULE_DIR "${zzlogg_ecm_SOURCE_DIR}/find-modules")
  include(CMakePackageConfigHelpers)
  write_basic_package_version_file("${zzlogg_ecm_BINARY_DIR}/ECMConfigVersion.cmake"
    VERSION 6.31.0 COMPATIBILITY AnyNewerVersion ARCH_INDEPENDENT)
  set(ECM_DIR "${zzlogg_ecm_BINARY_DIR}")
  set(CMAKE_CXX_STANDARD 20)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_TESTING OFF)
  set(BUILD_QCH OFF)
  set(QRC_SYNTAX ON)
  set(NO_STANDARD_PATHS ON)
  set(CMAKE_DISABLE_FIND_PACKAGE_Qt6Quick ON)
  set(CMAKE_DISABLE_FIND_PACKAGE_XercesC ON)
  # Optional Jinja generation is unrelated to the supported code languages.
  set(CMAKE_DISABLE_FIND_PACKAGE_Python ON)
  set(KDE_SKIP_UNINSTALL_TARGET ON)
  # Keep upstream developer tooling from writing into the vendored source tree.
  set(KDE_CLANG_FORMAT_EXECUTABLE OFF)
  add_subdirectory("${zzlogg_syntax_SOURCE_DIR}" "${zzlogg_syntax_BINARY_DIR}" EXCLUDE_FROM_ALL)
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/3rdparty/licenses/KSyntaxHighlighting/"
    DESTINATION share/doc/ZzLogg/licenses/KSyntaxHighlighting)
endfunction()
