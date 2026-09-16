zzlogg_update_clean_legacy_crt_flags()
foreach(suffix IN ITEMS protocol installlock)
  add_executable(zzlogg_updater_${suffix}_test ${suffix}test.cpp)
  target_link_libraries(zzlogg_updater_${suffix}_test PRIVATE zzlogg_updater_handoff)
  zzlogg_update_runtime(zzlogg_updater_${suffix}_test TRUE)
  add_test(NAME zzlogg_updater.${suffix} COMMAND zzlogg_updater_${suffix}_test)
  set_tests_properties(zzlogg_updater.${suffix} PROPERTIES TIMEOUT 30)
endforeach()

if(MSVC)
  get_filename_component(tool_directory "${CMAKE_LINKER}" DIRECTORY)
  find_program(updater_dumpbin NAMES dumpbin HINTS "${tool_directory}" REQUIRED)
  foreach(suffix IN ITEMS protocol installlock)
    add_test(NAME zzlogg_updater.${suffix}_dependencies
      COMMAND "${CMAKE_COMMAND}"
        "-DDUMPBIN=${updater_dumpbin}"
        "-DEXECUTABLE=$<TARGET_FILE:zzlogg_updater_${suffix}_test>"
        "-DSTATIC_LIBRARIES=$<TARGET_FILE:zzlogg_updater_handoff>;$<TARGET_FILE:zzlogg_updater_execution>;$<TARGET_FILE:zzlogg_updater_core>;$<TARGET_FILE:zzlogg_updater_monocypher>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/dependencytest.cmake")
  endforeach()
endif()
add_executable(zzlogg_updater_runtimecopy_test runtimecopytest.cpp)
target_link_libraries(zzlogg_updater_runtimecopy_test PRIVATE zzlogg_updater_handoff)
zzlogg_update_runtime(zzlogg_updater_runtimecopy_test TRUE)
add_test(NAME zzlogg_updater.runtimecopy COMMAND zzlogg_updater_runtimecopy_test $<TARGET_FILE:zzlogg_updater_protocol_test>)
foreach(name IN ITEMS handofffixture coordinator)
  if(name STREQUAL "handofffixture")
    set(source handofffixture.cpp)
  else()
    set(source coordinatortest.cpp)
  endif()
  add_executable(zzlogg_updater_${name}_test ${source})
  target_link_libraries(zzlogg_updater_${name}_test PRIVATE zzlogg_updater_handoff)
  zzlogg_update_runtime(zzlogg_updater_${name}_test TRUE)
endforeach()
add_dependencies(zzlogg_updater_coordinator_test zzlogg_updater_handofffixture_test ZzLoggUpdate)
add_test(NAME zzlogg_updater.coordinator COMMAND zzlogg_updater_coordinator_test $<TARGET_FILE:zzlogg_updater_handofffixture_test> $<TARGET_FILE:ZzLoggUpdate>)
set_tests_properties(zzlogg_updater.coordinator PROPERTIES TIMEOUT 30)
add_test(NAME zzlogg_updater.production_gate COMMAND "${CMAKE_COMMAND}" "-DEXECUTABLE=$<TARGET_FILE:ZzLoggUpdate>" -P "${CMAKE_CURRENT_SOURCE_DIR}/productiongatetest.cmake")
add_executable(zzlogg_updater_channel_test channeltest.cpp)
target_link_libraries(zzlogg_updater_channel_test PRIVATE zzlogg_updater_handoff)
zzlogg_update_runtime(zzlogg_updater_channel_test TRUE)
add_test(NAME zzlogg_updater.channel COMMAND zzlogg_updater_channel_test)
set_tests_properties(zzlogg_updater.channel PROPERTIES TIMEOUT 15)
add_test(NAME zzlogg_updater.production_dependencies
  COMMAND "${CMAKE_COMMAND}" "-DDUMPBIN=${updater_dumpbin}" "-DEXECUTABLE=$<TARGET_FILE:ZzLoggUpdate>"
    "-DSTATIC_LIBRARIES=$<TARGET_FILE:zzlogg_updater_handoff>;$<TARGET_FILE:zzlogg_updater_execution>;$<TARGET_FILE:zzlogg_updater_core>;$<TARGET_FILE:zzlogg_updater_monocypher>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/dependencytest.cmake")
