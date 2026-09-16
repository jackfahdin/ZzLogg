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
