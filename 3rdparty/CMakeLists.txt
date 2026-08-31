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
  set(backward_cpp_source_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/backward-cpp")
  foreach(backward_cpp_file IN ITEMS
      CMakeLists.txt BackwardConfig.cmake backward.cpp backward.hpp LICENSE.txt)
    if(NOT EXISTS "${backward_cpp_source_dir}/${backward_cpp_file}")
      message(FATAL_ERROR
        "Vendored backward-cpp is incomplete: missing ${backward_cpp_file}")
    endif()
  endforeach()
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

# KDToolBox (KDSignalThrottler only)
add_library(
  kdtoolbox STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDToolBox/qt/KDSignalThrottler/src/KDSignalThrottler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDToolBox/qt/KDSignalThrottler/src/KDSignalThrottler.h
)
target_include_directories(kdtoolbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDToolBox/qt/KDSignalThrottler/src)
target_link_libraries(kdtoolbox Qt${QT_VERSION_MAJOR}::Core)
set_target_properties(kdtoolbox PROPERTIES AUTOMOC ON)

# efsw
set(BUILD_TEST_APP OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/efsw)
target_compile_definitions(efsw PRIVATE EFSW_FSEVENTS_NOT_SUPPORTED)

if(WIN32)
  set(BUILD_SHARED_LIBS ON)
endif()
# tbb
set(TBB_TEST OFF CACHE BOOL "" FORCE)
set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TBB_STRICT OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/oneTBB)

if(WIN32)
  set(BUILD_SHARED_LIBS OFF)
endif()

# mimalloc
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_SECURE OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/mimalloc)
message("Adding alias for mimalloc")
add_library(klogg_mimalloc_wrapper INTERFACE)
target_link_libraries(klogg_mimalloc_wrapper INTERFACE mimalloc-static)
target_include_directories(klogg_mimalloc_wrapper INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/mimalloc/include)

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

  add_subdirectory(vendor/sentry-native)

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


