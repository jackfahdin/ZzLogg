set(QAPPLICATION_CLASS QApplication)

include(CPM)

set(_TMP_CPM_USE_LOCAL_PACKAGES ${CPM_USE_LOCAL_PACKAGES})

# simdutf
add_subdirectory(vendor/simdutf)

# type_safe
add_subdirectory(vendor/type_safe)

# CRoaring
add_subdirectory(vendor/CRoaring)

# streamvbyte
add_subdirectory(vendor/streamvbyte)

if(APPLE)
  # macdeployqtfix 脚本本地存放，避免联网下载
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/macdeployqtfix/macdeployqtfix.py
    ${CMAKE_BINARY_DIR}/macdeployqtfix.py
    COPYONLY
  )
endif()

# maddy
add_subdirectory(vendor/maddy)

if(KLOGG_USE_HYPERSCAN)
  # hyperscan 使用本地仓库
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(vendor/hyperscan)
  message("Adding alias for hyperscan")
  add_library(klogg_hyperscan INTERFACE)
  target_link_libraries(klogg_hyperscan INTERFACE hs)
  target_include_directories(klogg_hyperscan INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/hyperscan/src)
elseif(KLOGG_USE_VECTORSCAN)
  # vectorscan 使用本地仓库
  set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(FAT_RUNTIME OFF CACHE BOOL "" FORCE)
  set(BUILD_AVX2 OFF CACHE BOOL "" FORCE)
  set(BUILD_AVX512 OFF CACHE BOOL "" FORCE)
  set(BUILD_AVX512VBMI OFF CACHE BOOL "" FORCE)
  set(BUILD_CHIMERA OFF CACHE BOOL "" FORCE)
  add_subdirectory(vendor/vectorscan)
  message("Adding alias for vectorscan")
  add_library(klogg_vectorscan INTERFACE)
  target_link_libraries(klogg_vectorscan INTERFACE hs)
  target_include_directories(klogg_vectorscan INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/vectorscan/src
    ${CMAKE_CURRENT_BINARY_DIR}/vendor/vectorscan
  )
endif()

# uchardet
set(BUILD_BINARY OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/uchardet)
message("Adding alias for uchardet")
add_library(klogg_uchardet_wrapper INTERFACE)
target_link_libraries(klogg_uchardet_wrapper INTERFACE libuchardet)
target_include_directories(klogg_uchardet_wrapper INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/uchardet/src)

# klogg_karchive（KF5Archive 的 Qt5/Qt6 兼容 fork）
add_subdirectory(vendor/klogg_karchive)

if(${QT_VERSION_MAJOR} EQUAL 6)
  message("Resetting CPM_USE_LOCAL_PACKAGES to ${_TMP_CPM_USE_LOCAL_PACKAGES}")
  set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})
endif()

# robin_hood
add_subdirectory(vendor/robin-hood-hashing)

if(KLOGG_BUILD_TESTS)
  # backward-cpp
  add_subdirectory(vendor/backward-cpp)

  # Catch2
  set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  add_subdirectory(vendor/Catch2)
endif()

# KDSingleApplication
add_library(
  kdsingleapp STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDSingleApplication/src/kdsingleapplication.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDSingleApplication/src/kdsingleapplication_localsocket.cpp
)
target_include_directories(
  kdsingleapp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDSingleApplication/src
)
target_link_libraries(kdsingleapp Qt${QT_VERSION_MAJOR}::Core)
target_compile_definitions(kdsingleapp PUBLIC KDSINGLEAPPLICATION_STATIC_BUILD)
set_target_properties(kdsingleapp PROPERTIES AUTOMOC ON)

# xxHash
set(XXHASH_BUILD_ENABLE_INLINE_API OFF CACHE BOOL "" FORCE)
set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/xxHash/cmake_unofficial ${CMAKE_BINARY_DIR}/xxhash)

# whereami
add_library(
  whereami STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whereami/src/whereami.h
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whereami/src/whereami.c
)
target_include_directories(whereami PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whereami/src)

# exprtk
add_library(exprtk INTERFACE)
target_link_libraries(exprtk INTERFACE robin_hood)
target_include_directories(exprtk SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/klogg_exprtk)
target_compile_definitions(
  exprtk
  INTERFACE -Dexprtk_disable_caseinsensitivity
            -Dexprtk_disable_comments
            -Dexprtk_disable_break_continue
            -Dexprtk_disable_return_statement
            -Dexprtk_disable_superscalar_unroll
            -Dexprtk_disable_rtl_io_file
            -Dexprtk_disable_rtl_vecops
            -Dexprtk_disable_string_capabilities
)

cpmaddpackage(
  NAME
  KDToolBox
  GITHUB_REPOSITORY
  KDAB/KDToolBox
  GIT_TAG
  6468867d1a46eabe1bcb2cd342f338fe66f06675
  DOWNLOAD_ONLY
  YES
)
if(KDToolBox_ADDED)
  add_library(
    kdtoolbox STATIC ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src/KDSignalThrottler.cpp
                     ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src/KDSignalThrottler.h
  )

  target_include_directories(kdtoolbox PUBLIC ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src)
  target_link_libraries(kdtoolbox Qt${QT_VERSION_MAJOR}::Core)
  set_target_properties(kdtoolbox PROPERTIES AUTOMOC ON)
endif()

# efsw
cpmaddpackage(
  NAME
  efsw
  GITHUB_REPOSITORY
  SpartanJ/efsw
  GIT_TAG
  1.4.1
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_TEST_APP OFF"
)
if(efsw_ADDED)
  target_compile_definitions(efsw PRIVATE EFSW_FSEVENTS_NOT_SUPPORTED)
endif()

if(WIN32)
  set(BUILD_SHARED_LIBS ON)
endif()
# tbb
cpmaddpackage(
  NAME
  tbb
  GITHUB_REPOSITORY
  variar/oneTBB
  GIT_TAG
  c9be1ac2930f02dea523003ed801b4489f3e6b6e
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "TBB_TEST OFF"
  "TBB_EXAMPLES OFF"
  "TBB_STRICT OFF"
)

if(WIN32)
  set(BUILD_SHARED_LIBS OFF)
endif()

# mimalloc
set(CPM_USE_LOCAL_PACKAGES OFF)
cpmaddpackage(
  NAME
  mimalloc
  GITHUB_REPOSITORY
  microsoft/mimalloc
  VERSION
  2.1.7
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "MI_BUILD_TESTS OFF"
  "MI_SECURE OFF"
  "MI_BUILD_SHARED OFF"
  "MI_BUILD_STATIC ON"
  "MI_BUILD_OBJECT OFF"
  "MI_OVERRIDE OFF"
)
if(mimalloc_ADDED)
  message("Adding alias for mimalloc")
  add_library(klogg_mimalloc_wrapper INTERFACE)
  target_link_libraries(klogg_mimalloc_wrapper INTERFACE mimalloc-static)
  target_include_directories(klogg_mimalloc_wrapper INTERFACE ${mimalloc_SOURCE_DIR}/include)
else()
  add_library(klogg_mimalloc_wrapper INTERFACE)
  target_link_libraries(klogg_mimalloc_wrapper INTERFACE ${MIMALLOC_LIBRARY})
  target_include_directories(klogg_mimalloc_wrapper INTERFACE ${MIMALLOC_INCLUDE_DIR})
endif()

set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})

if(KLOGG_USE_SENTRY)
  set(SENTRY_BACKEND
     "crashpad"
     CACHE INTERNAL "" FORCE
  )
  set(SENTRY_TRANSPORT
     "none"
     CACHE INTERNAL "" FORCE
  )
  set(SENTRY_BUILD_EXAMPLES
     OFF
     CACHE INTERNAL "" FORCE
  )
  set(CRASHPAD_ENABLE_INSTALL
     OFF
     CACHE INTERNAL "" FORCE
  )
  set(CRASHPAD_ENABLE_INSTALL_DEV
     OFF
     CACHE INTERNAL "" FORCE
  )

  cpmaddpackage(
    NAME
    sentry
    GITHUB_REPOSITORY
    getsentry/sentry-native
    GIT_TAG
    a3d58622a807b9dda174cb9fc18fa0f98c89d043
    EXCLUDE_FROM_ALL
    YES
  )

  if(WIN32)
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "klogg_crashpad_handler.exe")
  else()
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "klogg_crashpad_handler")
  endif()
endif(KLOGG_USE_SENTRY)

set(klogg_cpm_targets
    xxhash
    Catch2
    roaring
    roaring-headers-cpp
    libuchardet
    robin_hood
    whereami
    simdutf
    efsw
    SingleApplication
    hs
    tbb
    exprtk
    type_safe
    mimalloc-static
    sentry
    crashpad_client
    crashpad_compat
    crashpad_util
    mini_chromium
    streamvbyte
    FastPFor
)
foreach(target ${klogg_cpm_targets})
  if(TARGET ${target})
    get_property(
      ${target}_include_dirs
      TARGET ${target}
      PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    )
    #message("Marking ${${target}_include_dirs} as system incldue")
    set_property(TARGET ${target} PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${${target}_include_dirs})
  endif()
endforeach()


