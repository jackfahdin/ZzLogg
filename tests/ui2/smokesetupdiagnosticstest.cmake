if(NOT DEFINED APP OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "APP and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/runtime" "${TEST_ROOT}/config")
get_filename_component(app_name "${APP}" NAME)
set(smoke_app "${TEST_ROOT}/runtime/${app_name}")
file(COPY_FILE "${APP}" "${smoke_app}")

if(WIN32)
  file(MAKE_DIRECTORY "${TEST_ROOT}/config/Roaming" "${TEST_ROOT}/config/Local")
  set(config_env
    "APPDATA=${TEST_ROOT}/config/Roaming"
    "LOCALAPPDATA=${TEST_ROOT}/config/Local")
else()
  file(MAKE_DIRECTORY
    "${TEST_ROOT}/config/config"
    "${TEST_ROOT}/config/data"
    "${TEST_ROOT}/config/cache")
  set(config_env
    "XDG_CONFIG_HOME=${TEST_ROOT}/config/config"
    "XDG_DATA_HOME=${TEST_ROOT}/config/data"
    "XDG_CACHE_HOME=${TEST_ROOT}/config/cache"
    "QT_QPA_PLATFORM=offscreen")
endif()
set(relative_data_root "relative-smoke-data")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${config_env}
          ZZLOGG_UI2_SMOKE_MS=500 "${smoke_app}" --multi --new-session
          --data-dir "${relative_data_root}"
  RESULT_VARIABLE diagnostic_result
  OUTPUT_VARIABLE diagnostic_stdout
  ERROR_VARIABLE diagnostic_stderr
  TIMEOUT 10)
if(diagnostic_result EQUAL 0)
  message(FATAL_ERROR "UI2 smoke accepted relative --data-dir")
endif()
set(expected_diagnostic
  "ZzLogg storage bootstrap failure: command-line storage root must be an absolute path: ${relative_data_root}")
string(FIND "${diagnostic_stderr}" "${expected_diagnostic}" diagnostic_index)
if(diagnostic_index EQUAL -1)
  message(FATAL_ERROR
    "Missing relative --data-dir pre-logger diagnostic '${expected_diagnostic}': ${diagnostic_stderr}")
endif()
