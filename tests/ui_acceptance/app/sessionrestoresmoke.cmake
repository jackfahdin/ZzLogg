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
  "XDG_CACHE_HOME=${TEST_CONFIG_DIR}/xdg/cache")
if(NOT WIN32)
  list(APPEND config_env "QT_QPA_PLATFORM=offscreen")
endif()

zzlogg_run_isolated_smoke_process(
  LABEL "session seed smoke"
  TEST_ROOT "${TEST_CONFIG_DIR}"
  APP_CONFIG_DIR "${TEST_CONFIG_DIR}/smoke/app-config"
  USER_DATA_DIR "${TEST_CONFIG_DIR}/smoke/user-data"
  SMOKE_MS 3000
  RESULT_VARIABLE seed_result
  OUTPUT_VARIABLE seed_stdout
  ERROR_VARIABLE seed_stderr
  TIMEOUT 15
  ENVIRONMENT ${config_env} "ZZLOGG_UI_SMOKE_MODE=seed-session"
  COMMAND "${smoke_app}" --multi --new-session --data-dir "${data_root}")
zzlogg_assert_output_isolated(
  "session seed smoke" "${TEST_CONFIG_DIR}" "${seed_stdout}" "${seed_stderr}")
zzlogg_assert_no_locator_or_probe(
  "${TEST_CONFIG_DIR}/runtime" "${TEST_CONFIG_DIR}" "${data_root}")
if(NOT seed_result EQUAL 0)
  message(FATAL_ERROR
    "UI session seed failed: ${seed_result}\n${seed_stdout}\n${seed_stderr}")
endif()
foreach(storage_entry storage-manifest.ini config/ZzLogg.ini
        session/ZzLogg_session.ini logs)
  if(NOT EXISTS "${data_root}/${storage_entry}")
    message(FATAL_ERROR "UI session smoke storage entry missing: ${data_root}/${storage_entry}")
  endif()
endforeach()

zzlogg_run_isolated_smoke_process(
  LABEL "session restore smoke"
  TEST_ROOT "${TEST_CONFIG_DIR}"
  APP_CONFIG_DIR "${TEST_CONFIG_DIR}/smoke/app-config"
  USER_DATA_DIR "${TEST_CONFIG_DIR}/smoke/user-data"
  SMOKE_MS 3000
  RESULT_VARIABLE restore_result
  OUTPUT_VARIABLE restore_stdout
  ERROR_VARIABLE restore_stderr
  TIMEOUT 15
  ENVIRONMENT ${config_env} "ZZLOGG_UI_SMOKE_MODE=verify-restored"
  COMMAND "${smoke_app}" --multi --load-session --data-dir "${data_root}")
zzlogg_assert_output_isolated(
  "session restore smoke" "${TEST_CONFIG_DIR}" "${restore_stdout}" "${restore_stderr}")
zzlogg_assert_no_locator_or_probe(
  "${TEST_CONFIG_DIR}/runtime" "${TEST_CONFIG_DIR}" "${data_root}")
if(NOT restore_result EQUAL 0)
  message(FATAL_ERROR
    "UI session restore failed: ${restore_result}\n${restore_stdout}\n${restore_stderr}")
endif()
