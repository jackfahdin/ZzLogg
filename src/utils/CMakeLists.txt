add_library(
  klogg_utils
  ${CMAKE_CURRENT_SOURCE_DIR}/include/atomicflag.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/uuid.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/dispatch_to.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/perfcounter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/openfilehelper.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/progress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/synchronization.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/overload_visitor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/resourcewrapper.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/crc32.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cpu_info.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/runnable_lambda.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/signalthrottler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu_info.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/signalthrottler.cpp
)

set_target_properties(klogg_utils PROPERTIES AUTOMOC ON)

target_include_directories(klogg_utils PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  klogg_utils
  PUBLIC project_options
         project_warnings
         klogg_version
         klogg_logging
         Qt6::Gui
         Qt6::Widgets
         type_safe
         klogg_mimalloc_wrapper
)
