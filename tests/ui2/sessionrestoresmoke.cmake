if(NOT DEFINED APP OR NOT DEFINED TEST_CONFIG_DIR)
  message(FATAL_ERROR "APP and TEST_CONFIG_DIR are required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/smokeisolationcheck.cmake")
zzlogg_require_safe_test_root("${TEST_CONFIG_DIR}")
file(REMOVE_RECURSE "${TEST_CONFIG_DIR}")
file(MAKE_DIRECTORY
  "${TEST_CONFIG_DIR}/Roaming"
  "${TEST_CONFIG_DIR}/Local"
  "${TEST_CONFIG_DIR}/xdg/config"
  "${TEST_CONFIG_DIR}/xdg/data"
  "${TEST_CONFIG_DIR}/xdg/cache"
  "${TEST_CONFIG_DIR}/smoke/app-config"
  "${TEST_CONFIG_DIR}/smoke/user-data")
get_filename_component(app_name "${APP}" NAME)
set(smoke_app "${TEST_CONFIG_DIR}/runtime/${app_name}")
file(MAKE_DIRECTORY "${TEST_CONFIG_DIR}/runtime")
file(COPY_FILE "${APP}" "${smoke_app}" ONLY_IF_DIFFERENT)
set(data_root "${TEST_CONFIG_DIR}/storage")
zzlogg_assert_path_in_test_root("session smoke data" "${TEST_CONFIG_DIR}" "${data_root}")
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
  COMMAND "${CMAKE_COMMAND}" -E env ${config_env}
          ZZLOGG_UI2_SMOKE_MODE=seed-session ZZLOGG_UI2_SMOKE_MS=3000
          "${smoke_app}" --multi --new-session --data-dir "${data_root}"
  RESULT_VARIABLE seed_result
  OUTPUT_VARIABLE seed_stdout
  ERROR_VARIABLE seed_stderr
  TIMEOUT 15)
zzlogg_assert_output_isolated(
  "session seed smoke" "${TEST_CONFIG_DIR}" "${seed_stdout}" "${seed_stderr}")
zzlogg_assert_no_locator_or_probe(
  "${TEST_CONFIG_DIR}/runtime" "${TEST_CONFIG_DIR}" "${data_root}")
if(NOT seed_result EQUAL 0)
  message(FATAL_ERROR
    "UI2 session seed failed: ${seed_result}\n${seed_stdout}\n${seed_stderr}")
endif()
foreach(storage_entry storage-manifest.ini config/ZzLogg.ini
        session/ZzLogg_session.ini logs crashes)
  if(NOT EXISTS "${data_root}/${storage_entry}")
    message(FATAL_ERROR "UI2 session smoke storage entry missing: ${data_root}/${storage_entry}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${config_env}
          ZZLOGG_UI2_SMOKE_MODE=verify-restored ZZLOGG_UI2_SMOKE_MS=3000
          "${smoke_app}" --multi --load-session --data-dir "${data_root}"
  RESULT_VARIABLE restore_result
  OUTPUT_VARIABLE restore_stdout
  ERROR_VARIABLE restore_stderr
  TIMEOUT 15)
zzlogg_assert_output_isolated(
  "session restore smoke" "${TEST_CONFIG_DIR}" "${restore_stdout}" "${restore_stderr}")
zzlogg_assert_no_locator_or_probe(
  "${TEST_CONFIG_DIR}/runtime" "${TEST_CONFIG_DIR}" "${data_root}")
if(NOT restore_result EQUAL 0)
  message(FATAL_ERROR
    "UI2 session restore failed: ${restore_result}\n${restore_stdout}\n${restore_stderr}")
endif()
