# Keep upstream KDE settings local to this dependency.
function(zzlogg_add_syntax_highlighting)
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  include(FetchContent)
  FetchContent_Declare(zzlogg_ecm
    URL https://download.kde.org/stable/frameworks/6.22/extra-cmake-modules-6.22.0.tar.xz
    SOURCE_SUBDIR zzlogg-download-only
    URL_HASH SHA256=cb83a69571b277c20b3a6567ef0b6f39bf29c43a619282bf4bb076feb4c609a6)
  FetchContent_MakeAvailable(zzlogg_ecm)
  # ECM is a build-time module collection; provide a build-tree package config.
  file(MAKE_DIRECTORY "${zzlogg_ecm_BINARY_DIR}")
  file(WRITE "${zzlogg_ecm_BINARY_DIR}/ECMConfig.cmake"
    "set(ECM_MODULE_PATH \"${zzlogg_ecm_SOURCE_DIR}/modules\" \"${zzlogg_ecm_SOURCE_DIR}/find-modules\" \"${zzlogg_ecm_SOURCE_DIR}/kde-modules\")\n")
  set(ECM_GLOBAL_FIND_VERSION 6.22.0)
  set(ECM_MODULE_DIR "${zzlogg_ecm_SOURCE_DIR}/modules")
  set(ECM_KDE_MODULE_DIR "${zzlogg_ecm_SOURCE_DIR}/kde-modules")
  set(ECM_FIND_MODULE_DIR "${zzlogg_ecm_SOURCE_DIR}/find-modules")
  include(CMakePackageConfigHelpers)
  write_basic_package_version_file("${zzlogg_ecm_BINARY_DIR}/ECMConfigVersion.cmake"
    VERSION 6.22.0 COMPATIBILITY AnyNewerVersion ARCH_INDEPENDENT)
  set(ECM_DIR "${zzlogg_ecm_BINARY_DIR}")
  FetchContent_Declare(zzlogg_syntax
    URL https://download.kde.org/stable/frameworks/6.22/syntax-highlighting-6.22.0.tar.xz
    SOURCE_SUBDIR zzlogg-download-only
    URL_HASH SHA256=50b73ea99413dd988fa34fd169129bcdbfe1dc3b43c48d5f92bbadb2511a728a)
  FetchContent_MakeAvailable(zzlogg_syntax)
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
  add_subdirectory("${zzlogg_syntax_SOURCE_DIR}" "${zzlogg_syntax_BINARY_DIR}" EXCLUDE_FROM_ALL)
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/3rdparty/licenses/KSyntaxHighlighting/"
    DESTINATION share/doc/ZzLogg/licenses/KSyntaxHighlighting)
endfunction()
