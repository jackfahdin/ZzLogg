if(NOT DEFINED APP OR NOT DEFINED TEST_CONFIG_DIR)
  message(FATAL_ERROR "APP and TEST_CONFIG_DIR are required")
endif()

file(REMOVE_RECURSE "${TEST_CONFIG_DIR}")
file(MAKE_DIRECTORY "${TEST_CONFIG_DIR}")
get_filename_component(app_name "${APP}" NAME)
set(smoke_app "${TEST_CONFIG_DIR}/runtime/${app_name}")
file(MAKE_DIRECTORY "${TEST_CONFIG_DIR}/runtime")
file(COPY_FILE "${APP}" "${smoke_app}" ONLY_IF_DIFFERENT)
set(data_root "${TEST_CONFIG_DIR}/storage")
include("${CMAKE_CURRENT_LIST_DIR}/smokeisolationcheck.cmake")
zzlogg_capture_host_storage_state(host_state_before)
if(WIN32)
  file(MAKE_DIRECTORY "${TEST_CONFIG_DIR}/Roaming" "${TEST_CONFIG_DIR}/Local")
  set(config_env
    "APPDATA=${TEST_CONFIG_DIR}/Roaming"
    "LOCALAPPDATA=${TEST_CONFIG_DIR}/Local")
else()
  file(MAKE_DIRECTORY
    "${TEST_CONFIG_DIR}/config"
    "${TEST_CONFIG_DIR}/data"
    "${TEST_CONFIG_DIR}/cache")
  set(config_env
    "XDG_CONFIG_HOME=${TEST_CONFIG_DIR}/config"
    "XDG_DATA_HOME=${TEST_CONFIG_DIR}/data"
    "XDG_CACHE_HOME=${TEST_CONFIG_DIR}/cache"
    "QT_QPA_PLATFORM=offscreen")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${config_env}
          ZZLOGG_UI2_SMOKE_MODE=seed-session ZZLOGG_UI2_SMOKE_MS=3000
          "${smoke_app}" --multi --new-session --data-dir "${data_root}"
  RESULT_VARIABLE seed_result TIMEOUT 15)
zzlogg_assert_host_storage_unchanged("${host_state_before}")
zzlogg_assert_no_locator_or_probe(
  "${TEST_CONFIG_DIR}/runtime" "${TEST_CONFIG_DIR}" "${data_root}")
if(NOT seed_result EQUAL 0)
  message(FATAL_ERROR "UI2 session seed failed: ${seed_result}")
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
  RESULT_VARIABLE restore_result TIMEOUT 15)
zzlogg_assert_host_storage_unchanged("${host_state_before}")
zzlogg_assert_no_locator_or_probe(
  "${TEST_CONFIG_DIR}/runtime" "${TEST_CONFIG_DIR}" "${data_root}")
if(NOT restore_result EQUAL 0)
  message(FATAL_ERROR "UI2 session restore failed: ${restore_result}")
endif()
