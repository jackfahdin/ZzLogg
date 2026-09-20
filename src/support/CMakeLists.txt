add_library(
  klogg_support STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/issuereporter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/memory_info.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/issuereporter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/memory_info.cpp
)

set_target_properties(klogg_support PROPERTIES AUTOMOC ON)

target_include_directories(klogg_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  klogg_support
  PUBLIC project_options
         project_warnings
         zzlogg_brand
         klogg_version
         klogg_storage
         klogg_utils
         tbb
         Qt6::Gui
         Qt6::Widgets
)


if(KLOGG_USE_LTO)
  set_property(TARGET klogg_support PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
