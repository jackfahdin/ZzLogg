add_library(
  klogg_storage STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/storagecontext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/storagecontext.h
)

target_include_directories(klogg_storage PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(klogg_storage PUBLIC
  project_options
  project_warnings
  Qt${QT_VERSION_MAJOR}::Core)
