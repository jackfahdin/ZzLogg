add_library(zzlogg_ui2_shell STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/zzloggfluentshell.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/zzloggfluentchrome_p.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/zzloggfluentshell_p.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/zzloggfluentshell.cpp)
target_include_directories(zzlogg_ui2_shell
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(zzlogg_ui2_shell PUBLIC
  Zz::WindowKit
  Zz::FluentFoundation
  Zz::FluentUI
  Qt6::Widgets
  PRIVATE klogg_logging)
target_compile_features(zzlogg_ui2_shell PUBLIC cxx_std_20)
set_target_properties(zzlogg_ui2_shell PROPERTIES AUTOMOC ON)

add_library(zzlogg_ui2_runtime STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/zzlogguiruntime.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/zzlogguiruntime.cpp)
target_include_directories(zzlogg_ui2_runtime
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(zzlogg_ui2_runtime
  PUBLIC zzlogg_ui2_shell klogg_settings
  PRIVATE klogg_ui klogg_crash_handler kdsingleapp)
target_compile_features(zzlogg_ui2_runtime PUBLIC cxx_std_20)
set_target_properties(zzlogg_ui2_runtime PROPERTIES AUTOMOC ON)
