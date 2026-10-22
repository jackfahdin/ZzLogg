add_library(
  klogg_version STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/klogg_version.h
                       ${CMAKE_CURRENT_SOURCE_DIR}/src/klogg_version.cpp
)

target_include_directories(klogg_version PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(klogg_version PUBLIC project_options project_warnings Qt6::Core)

add_dependencies(klogg_version generate_version)
# A target dependency only orders generation. Make recompilation depend on the
# header explicitly, including when MSVC reports a different spelling of its path.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/klogg_version.cpp" APPEND
  PROPERTY OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/generated/version.h")
