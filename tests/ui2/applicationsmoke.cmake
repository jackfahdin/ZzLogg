if(NOT DEFINED APP OR NOT DEFINED TEST_RUNTIME_DIR OR NOT DEFINED TEST_CONFIG_DIR
   OR NOT DEFINED FIRST_LOG OR NOT DEFINED SECOND_LOG)
  message(FATAL_ERROR
    "APP, TEST_RUNTIME_DIR, TEST_CONFIG_DIR, FIRST_LOG and SECOND_LOG are required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/smokeisolationcheck.cmake")
zzlogg_require_safe_test_root("${TEST_RUNTIME_DIR}")
zzlogg_require_safe_test_root("${TEST_CONFIG_DIR}")
file(REMOVE_RECURSE "${TEST_RUNTIME_DIR}" "${TEST_CONFIG_DIR}")
file(MAKE_DIRECTORY
  "${TEST_RUNTIME_DIR}"
  "${TEST_CONFIG_DIR}/Roaming"
  "${TEST_CONFIG_DIR}/Local"
  "${TEST_CONFIG_DIR}/xdg/config"
  "${TEST_CONFIG_DIR}/xdg/data"
  "${TEST_CONFIG_DIR}/xdg/cache"
  "${TEST_CONFIG_DIR}/smoke/app-config"
  "${TEST_CONFIG_DIR}/smoke/user-data")
get_filename_component(app_name "${APP}" NAME)
set(smoke_app "${TEST_RUNTIME_DIR}/${app_name}")
file(COPY_FILE "${APP}" "${smoke_app}" ONLY_IF_DIFFERENT)
set(data_root "${TEST_CONFIG_DIR}/storage")
zzlogg_assert_path_in_test_root("application smoke data" "${TEST_CONFIG_DIR}" "${data_root}")
set(config_env
  "APPDATA=${TEST_CONFIG_DIR}/Roaming"
  "LOCALAPPDATA=${TEST_CONFIG_DIR}/Local"
  "XDG_CONFIG_HOME=${TEST_CONFIG_DIR}/xdg/config"
  "XDG_DATA_HOME=${TEST_CONFIG_DIR}/xdg/data"
  "XDG_CACHE_HOME=${TEST_CONFIG_DIR}/xdg/cache"
  "ZZLOGG_UI2_SMOKE_APP_CONFIG_DIR=${TEST_CONFIG_DIR}/smoke/app-config"
  "ZZLOGG_UI2_SMOKE_USER_DATA_DIR=${TEST_CONFIG_DIR}/smoke/user-data")
if(NOT WIN32)
  list(APPEND config_env "QT_QPA_PLATFORM=offscreen")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${config_env} ZZLOGG_UI2_SMOKE_MS=1800
          "${smoke_app}" --multi --new-session --data-dir "${data_root}"
          "${FIRST_LOG}" "${SECOND_LOG}"
  RESULT_VARIABLE smoke_result
  OUTPUT_VARIABLE smoke_stdout
  ERROR_VARIABLE smoke_stderr
  TIMEOUT 30)
zzlogg_assert_output_isolated(
  "application smoke" "${TEST_CONFIG_DIR}" "${smoke_stdout}" "${smoke_stderr}")
zzlogg_assert_no_locator_or_probe("${TEST_RUNTIME_DIR}" "${TEST_CONFIG_DIR}" "${data_root}")
if(NOT smoke_result EQUAL 0)
  message(FATAL_ERROR
    "UI2 application smoke failed: ${smoke_result}\n${smoke_stdout}\n${smoke_stderr}")
endif()

foreach(storage_entry storage-manifest.ini config/ZzLogg.ini
        session/ZzLogg_session.ini logs crashes)
  if(NOT EXISTS "${data_root}/${storage_entry}")
    message(FATAL_ERROR "UI2 application smoke storage entry missing: ${data_root}/${storage_entry}")
  endif()
endforeach()
