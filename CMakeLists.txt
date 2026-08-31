cmake_minimum_required(VERSION 3.12)

include(cmake/ZzLoggBrand.cmake)

# CMake 4 removed compatibility with policy versions older than 3.5. Some
# bundled third-party projects still declare an older minimum, so provide a
# compatibility floor while they are being built as part of klogg.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

project(
  klogg
  VERSION 26.07.0
  DESCRIPTION "${ZZLOGG_PRODUCT_DESCRIPTION}"
  HOMEPAGE_URL "${ZZLOGG_HOMEPAGE_URL}"
  LANGUAGES C CXX ASM
)

zzlogg_configure_brand()

string(TIMESTAMP PRODUCT_CURRENT_YEAR "%Y")
set(COMPANY "${ZZLOGG_VENDOR}")
set(COPYRIGHT "Copyright (c) ${PRODUCT_CURRENT_YEAR} Nicolas Bonnefon, Anton Filimonov and other contributors.")
set(IDENTIFIER "${ZZLOGG_IDENTIFIER}")
set(PROJECT_DESCRIPTION "${ZZLOGG_PRODUCT_DESCRIPTION}")

option(KLOGG_BUILD_TESTS "Build tests" ON)
option(KLOGG_BUILD_UI2 "Build the experimental ZzPureTools UI" OFF)
option(KLOGG_BUILD_UI2_TESTS "Build tests for the experimental UI" ${KLOGG_BUILD_UI2})
option(ZZLOGG_BUILD_ICON_TOOL "Build the ZzLogg icon asset generator" OFF)
if(KLOGG_BUILD_UI2_TESTS AND NOT KLOGG_BUILD_UI2)
  message(FATAL_ERROR "KLOGG_BUILD_UI2_TESTS requires KLOGG_BUILD_UI2=ON")
endif()
option(KLOGG_USE_LTO "Use link time optimization" ON)
option(KLOGG_USE_SENTRY "Use Sentry" OFF)
option(KLOGG_GENERIC_CPU "Build for generic CPU" OFF)
option(KLOGG_OSX_DEPLOYMENT_TARGET "Override target MacOS version" "")

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(KLOGG_ARCH 32)
else()
  set(KLOGG_ARCH 64)
endif()

if(WIN32)
  if(KLOGG_ARCH EQUAL 64)
    option(KLOGG_USE_HYPERSCAN "Use Hyperscan" ON)
    option(KLOGG_USE_VECTORSCAN "Use Vectorscan" OFF)
  else()
    option(KLOGG_USE_HYPERSCAN "Use Hyperscan" OFF)
    option(KLOGG_USE_VECTORSCAN "Use Vectorscan" OFF)
  endif()
else()
    option(KLOGG_USE_HYPERSCAN "Use Hyperscan" OFF)
    option(KLOGG_USE_VECTORSCAN "Use Vectorscan" ON)
endif()

set(BUILD_VERSION
    $ENV{KLOGG_VERSION}
    CACHE STRING "build version"
)
set(BUILD_NUMBER
    ""
    CACHE STRING "build number"
)

set(NOTICE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/NOTICE)
set(COPYING_FILE ${CMAKE_CURRENT_SOURCE_DIR}/COPYING)

set(README_FILE ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

set(DOCUMENTATION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/docs/DOCUMENTATION.md)
set(DOCUMENTATION_HTML ${CMAKE_BINARY_DIR}/generated/documentation.html)
set(DOCUMENTATION_RESOURCE ${CMAKE_BINARY_DIR}/generated/documentation.qrc)

if(WIN32)
  set(ICON_NAME ZzLogg.ico)
elseif(APPLE)
  set(ICON_NAME ZzLogg.icns)
else()
  set(ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/app/images/hicolor/scalable/ZzLogg.svg)
endif()

if(DEFINED ICON_NAME AND NOT ICON_NAME STREQUAL "")
  set(ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Resources/${ICON_NAME})
endif()

if(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.14)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /experimental:external /external:W0 ")
  set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/external:I ")
endif()

set(out_dir "${CMAKE_BINARY_DIR}/output")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${out_dir}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${out_dir}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${out_dir}")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

include(StandardProjectSettings)
include(ucm)
include(prepare_version)
include(FindPkgConfig)
include(GNUInstallDirs)
set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/${ZZLOGG_PRODUCT_NAME}")

if(MSVC)
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(
      REGEX
      REPLACE "/W[0-4]"
              "/W4"
              CMAKE_CXX_FLAGS
              "${CMAKE_CXX_FLAGS}"
    )
  else(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")

  if(CMAKE_C_FLAGS MATCHES "/W[0-4]")
    string(
      REGEX
      REPLACE "/W[0-4]"
              "/W4"
              CMAKE_C_FLAGS
              "${CMAKE_C_FLAGS}"
    )
  else(CMAKE_C_FLAGS MATCHES "/W[0-4]")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
  endif(CMAKE_C_FLAGS MATCHES "/W[0-4]")

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /D __SSE4_1__=1 /D __SSE4_2__=1")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D __SSE4_1__=1 /D __SSE4_2__=1")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj ")
else()
  ucm_add_flags(CXX "-fno-sized-deallocation -fno-omit-frame-pointer")

  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
    ucm_add_flags(C CXX "-march=native -mtune=generic")
  else()
    ucm_add_flags(C CXX "-mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mpopcnt")
    if(KLOGG_GENERIC_CPU)
      ucm_add_flags(C CXX "-march=x86-64 -mtune=generic")
    else()
      ucm_add_flags(C CXX "-march=native -mtune=generic")
    endif()
  endif()
endif(MSVC)

if(WIN32)
  set(SYMS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dump_syms/win)
elseif(APPLE)
  set(SYMS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dump_syms/mac)
else()
  set(SYMS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dump_syms/linux)
endif()

message("Dump symbols tools path ${SYMS_TOOLS}")

# standard compiler warnings
# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
include(CompilerWarnings)
add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

# Link this 'library' to set the c++ standard / compile-time options requested
include(CompilerFlags)
add_library(project_options INTERFACE)
set_project_compile_flags(project_options)

# sanitizer options if supported by compiler
include(Sanitizers)
enable_sanitizers(project_options)

# allow for static analysis options
include(StaticAnalyzers)
include(ZzLoggIconInstall)

ucm_print_flags()

if(APPLE)
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
    message("Adding missing Threads workaround")
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    set(CMAKE_HAVE_THREADS_LIBRARY 1)
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
    set(CMAKE_USE_PTHREADS_INIT 1)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
  endif()
endif()

find_package(
  Qt6
  COMPONENTS Core
             Widgets
             Concurrent
             Network
             Xml
  REQUIRED
)

if(ZZLOGG_BUILD_ICON_TOOL)
  add_subdirectory(tools)
endif()

set(QT_VERSION_MAJOR 6)
set(KLOGG_QT_VERSION ${Qt6Core_VERSION})

message("Qt version ${KLOGG_QT_VERSION}")
message("LTO ${KLOGG_USE_LTO}")

if(APPLE)
  if(${KLOGG_OSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET ${KLOGG_OSX_DEPLOYMENT_TARGET})
    message("OSX target ${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
endif()

set(CMAKE_AUTOMOC OFF)
set(CMAKE_INCLUDE_CURRENT_DIR OFF)
set(BUILD_SHARED_LIBS OFF)

if(KLOGG_BUILD_UI2)
  include(ZzPureTools)
  klogg_add_zzpuretools()
endif()

add_custom_target(ci_build)

add_subdirectory(3rdparty)
add_subdirectory(src)
add_subdirectory(website)

enable_testing()

add_test(
  NAME zzlogg_ui2.brand_external_files
  COMMAND ${CMAKE_COMMAND}
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/VerifyExternalBrand.cmake"
)

if(TARGET zzlogg_icon_generator)
  add_test(NAME zzlogg.icon_generator.atomicity
    COMMAND "${CMAKE_COMMAND}"
      "-DGENERATOR=$<TARGET_FILE:zzlogg_icon_generator>"
      "-DSOURCE=${PROJECT_SOURCE_DIR}/src/app/images/hicolor/scalable/ZzLogg.svg"
      "-DTEST_ROOT=${CMAKE_BINARY_DIR}/icon-generator-atomicity-contract"
      -P "${PROJECT_SOURCE_DIR}/tools/icon_generator/icon-generator-atomicity-test.cmake")
endif()
add_test(
  NAME klogg_smoke
  COMMAND ${CMAKE_COMMAND} -E sha256sum $<TARGET_FILE:klogg>
)
set_tests_properties(klogg_smoke PROPERTIES TIMEOUT 30)

if(${KLOGG_BUILD_TESTS})
  add_subdirectory(tests)
endif()

if(KLOGG_BUILD_UI2_TESTS)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
  add_subdirectory(tests/ui2)
endif()

# Installation and packaging
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY true)
set(CPACK_SOURCE_IGNORE_FILES "build_root;.git")

set(CPACK_OUTPUT_FILE_PREFIX "packages")
set(CPACK_PACKAGE_NAME "${ZZLOGG_PRODUCT_NAME}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${ZZLOGG_PRODUCT_DESCRIPTION}")
set(CPACK_PACKAGE_VENDOR "${ZZLOGG_VENDOR}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${ZZLOGG_HOMEPAGE_URL}")
set(CPACK_PACKAGE_CONTACT "anton.filimonov@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/description.txt")
set(CPACK_RESOURCE_FILE_LICENSE "${COPYING_FILE}")
set(CPACK_PACKAGE_VERSION
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}"
)
set(CPACK_PACKAGE_ICON "${ICON_FILE}")
set(CPACK_STRIP_FILES "bin/ZzLogg")

if(UNIX)
  include(InstallRequiredSystemLibraries)

  if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_FORMAT "UDBZ")
    set(CPACK_DMG_VOLUME_NAME "${ZZLOGG_PRODUCT_NAME}")
    set(CPACK_SYSTEM_NAME "OSX")
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/osx/dmg_background.tif")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/packaging/osx/dmg_setup.scpt")

  else()
    zzlogg_install_legacy_linux_icons("${CMAKE_CURRENT_SOURCE_DIR}")

    install(
      FILES ${COPYING_FILE}
            ${NOTICE_FILE}
            ${README_FILE}
            ${DOCUMENTATION_HTML}
            ${DOCUMENTATION_FILE}
      DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/ZzLogg.desktop DESTINATION share/applications)

    set(CPACK_GENERATOR DEB)

    #deb
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${ZZLOGG_HOMEPAGE_URL}")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Anton Filimonov <${CPACK_PACKAGE_CONTACT}>")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS YES)

    #RPM
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
    set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")
    set(CPACK_RPM_PACKAGE_AUTOREQ YES)
    set(CPACK_RPM_PACKAGE_URL "${ZZLOGG_HOMEPAGE_URL}")
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications
        /usr/share/icons
        /usr/share/icons/hicolor
        /usr/share/icons/hicolor/16x16
        /usr/share/icons/hicolor/16x16/apps
        /usr/share/icons/hicolor/32x32
        /usr/share/icons/hicolor/32x32/apps
        /usr/share/icons/hicolor/48x48
        /usr/share/icons/hicolor/48x48/apps
        /usr/share/icons/hicolor/64x64
        /usr/share/icons/hicolor/64x64/apps
        /usr/share/icons/hicolor/128x128
        /usr/share/icons/hicolor/128x128/apps
        /usr/share/icons/hicolor/256x256
        /usr/share/icons/hicolor/256x256/apps
        /usr/share/icons/hicolor/512x512
        /usr/share/icons/hicolor/512x512/apps
        /usr/share/icons/hicolor/scalable
        /usr/share/icons/hicolor/scalable/apps
    )
  endif()
endif()
include(CPack)

# for IDE grouping
add_custom_target(
  installation
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/ZzLogg.nsi
          ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/ZzLogg.desktop
          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in
)

add_custom_target(
  klogg_docs
  SOURCES ${README_FILE}
          ${COPYING_FILE}
          ${NOTICE_FILE}
          ${DOCUMENTATION_FILE}
)

add_custom_target(klogg_stuff SOURCES .clang-format .clang-tidy .cmake-format)
