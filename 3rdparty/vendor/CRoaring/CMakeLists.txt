cmake_minimum_required(VERSION 3.12)

project(CRoaringAmalgamation
  DESCRIPTION "Compressed bitmap indexes"
  LANGUAGES C CXX
)

add_library(roaring STATIC
  roaring.c
)

target_include_directories(roaring
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(roaring PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

add_library(roaring-headers INTERFACE)
target_include_directories(roaring-headers
  INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(roaring-headers-cpp INTERFACE)
target_include_directories(roaring-headers-cpp
  INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(roaring-headers-cpp
  INTERFACE
    roaring-headers
)
