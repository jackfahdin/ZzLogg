function(zzlogg_update_qt_test suffix source)
  add_executable(zzlogg_update_${suffix}_test "${source}")
  target_link_libraries(zzlogg_update_${suffix}_test PRIVATE zzlogg_update_qt Qt6::Test)
  set_target_properties(zzlogg_update_${suffix}_test PROPERTIES AUTOMOC ON)
  add_test(NAME zzlogg_update.${suffix} COMMAND zzlogg_update_${suffix}_test
    -o "${CMAKE_CURRENT_BINARY_DIR}/${suffix}-$<CONFIG>.txt,txt")
  if(WIN32)
    set_property(TEST zzlogg_update.${suffix} PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
endfunction()
zzlogg_update_qt_test(state updatestatetest.cpp)
target_link_libraries(zzlogg_update_state_test PRIVATE zzlogg_update_json)
