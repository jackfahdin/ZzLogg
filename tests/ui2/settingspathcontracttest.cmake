if(NOT DEFINED CHECK_SCRIPT OR NOT DEFINED VERIFY_SCRIPT OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "CHECK_SCRIPT, VERIFY_SCRIPT and TEST_ROOT are required")
endif()

foreach(settings_suffix IN ITEMS .ini .conf)
  set(case_root "${TEST_ROOT}/${settings_suffix}")
  file(REMOVE_RECURSE "${case_root}")
  file(MAKE_DIRECTORY "${case_root}/runtime" "${case_root}/klogg")
  file(WRITE "${case_root}/runtime/staged-app" "unrelated")
  file(WRITE "${case_root}/klogg/klogg${settings_suffix}" "app settings")
  set(expected_session "${case_root}/klogg/klogg_session${settings_suffix}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DCHECK_SCRIPT=${CHECK_SCRIPT}"
            "-DSETTINGS_ROOT=${case_root}"
            "-DSETTINGS_SUFFIX=${settings_suffix}"
            -P "${VERIFY_SCRIPT}"
    RESULT_VARIABLE missing_result)
  if(missing_result EQUAL 0)
    message(FATAL_ERROR
      "settings verifier accepted missing exact session path ${expected_session}")
  endif()
  file(WRITE "${expected_session}" "session settings")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DCHECK_SCRIPT=${CHECK_SCRIPT}"
            "-DSETTINGS_ROOT=${case_root}"
            "-DSETTINGS_SUFFIX=${settings_suffix}"
            -P "${VERIFY_SCRIPT}"
    RESULT_VARIABLE complete_result)
  if(NOT complete_result EQUAL 0)
    message(FATAL_ERROR
      "settings verifier rejected complete exact paths under ${case_root}")
  endif()
endforeach()
