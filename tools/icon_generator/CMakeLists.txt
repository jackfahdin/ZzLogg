find_package(Qt6 REQUIRED COMPONENTS Core Gui Svg)

add_executable(zzlogg_icon_generator main.cpp)
target_link_libraries(zzlogg_icon_generator PRIVATE Qt6::Core Qt6::Gui Qt6::Svg)

if(WIN32)
  add_custom_command(TARGET zzlogg_icon_generator POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:Qt6::Core>"
      "$<TARGET_FILE:Qt6::Gui>"
      "$<TARGET_FILE:Qt6::Svg>"
      "$<TARGET_FILE_DIR:zzlogg_icon_generator>"
    VERBATIM)
endif()
