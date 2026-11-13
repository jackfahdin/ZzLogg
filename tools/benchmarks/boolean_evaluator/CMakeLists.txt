add_executable(zzlogg_boolean_probe main.cpp)
target_link_libraries(zzlogg_boolean_probe PRIVATE klogg_regex Qt6::Core ${CMAKE_DL_LIBS})
