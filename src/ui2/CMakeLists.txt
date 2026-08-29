add_library(zzlogg_ui2_shell STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/zzloggfluentshell.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/zzloggfluentshell.cpp)
target_include_directories(zzlogg_ui2_shell
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(zzlogg_ui2_shell PUBLIC
  Zz::WindowKit
  Zz::FluentFoundation
  Zz::FluentUI
  Qt6::Widgets)
target_compile_features(zzlogg_ui2_shell PUBLIC cxx_std_20)
set_target_properties(zzlogg_ui2_shell PROPERTIES AUTOMOC ON)
