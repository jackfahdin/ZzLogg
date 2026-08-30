if(NOT DEFINED APP OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "APP and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/runtime")
get_filename_component(app_name "${APP}" NAME)
set(smoke_app "${TEST_ROOT}/runtime/${app_name}")
file(COPY_FILE "${APP}" "${smoke_app}")

if(WIN32)
  set(root_name APPDATA)
else()
  set(root_name XDG_CONFIG_HOME)
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=${root_name}
          ZZLOGG_UI2_SMOKE_MS=500 "${smoke_app}" --multi --new-session
  RESULT_VARIABLE diagnostic_result
  OUTPUT_VARIABLE diagnostic_stdout
  ERROR_VARIABLE diagnostic_stderr
  TIMEOUT 10)
if(diagnostic_result EQUAL 0)
  message(FATAL_ERROR "UI2 smoke accepted missing ${root_name}")
endif()
set(expected_diagnostic "UI2 smoke settings root is missing: ${root_name}")
string(FIND "${diagnostic_stderr}" "${expected_diagnostic}" diagnostic_index)
if(diagnostic_index EQUAL -1)
  message(FATAL_ERROR
    "Missing pre-logger diagnostic '${expected_diagnostic}': ${diagnostic_stderr}")
endif()
