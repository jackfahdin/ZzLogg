cmake_minimum_required(VERSION 3.30)
project(regex_decode_benchmark LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(NOT EXISTS "${BASELINE_INCLUDE_DIR}/hsregularexpression.h" OR
   NOT EXISTS "${CURRENT_INCLUDE_DIR}/hsregularexpression.h" OR NOT EXISTS "${MIMALLOC_LIBRARY}")
  message(FATAL_ERROR "Set BASELINE_INCLUDE_DIR, CURRENT_INCLUDE_DIR to header snapshots and MIMALLOC_LIBRARY to a built static library")
endif()
find_package(Qt6 REQUIRED COMPONENTS Core)
add_library(benchmark_dependencies INTERFACE)
target_include_directories(benchmark_dependencies INTERFACE
  "${REPO_ROOT}/src/regex/include"
  "${REPO_ROOT}/src/utils/include"
  "${REPO_ROOT}/3rdparty/vendor/mimalloc/include"
  "${REPO_ROOT}/3rdparty/vendor/type_safe/include"
  "${REPO_ROOT}/3rdparty/vendor/type_safe/external/debug_assert")
target_compile_definitions(benchmark_dependencies INTERFACE TYPE_SAFE_ENABLE_ASSERTIONS=0)
target_link_libraries(benchmark_dependencies INTERFACE Qt6::Core "${MIMALLOC_LIBRARY}")
foreach(version Baseline Current)
  string(TOUPPER "${version}" upper_version)
  add_library(matcher_${version} STATIC adapter.cpp)
  target_include_directories(matcher_${version} BEFORE PRIVATE "${${upper_version}_INCLUDE_DIR}")
  target_link_libraries(matcher_${version} PRIVATE benchmark_dependencies)
  # Keep both real header implementations in one process without ODR collisions.
  target_compile_definitions(matcher_${version} PRIVATE
    DefaultRegularExpressionMatcher=BenchmarkDefault${version}
    HsRegularExpression=BenchmarkHs${version}
    MatcherVariant=BenchmarkVariant${version}
    BENCHMARK_FACTORY=make${version})
endforeach()
add_executable(regex_decode_benchmark main.cpp)
target_link_libraries(regex_decode_benchmark PRIVATE benchmark_dependencies matcher_Baseline matcher_Current ${CMAKE_DL_LIBS})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Optional LD_PRELOAD probe: glibc malloc/calloc/realloc requests only.
  add_library(regex_alloc_probe SHARED alloc_probe.c)
endif()
