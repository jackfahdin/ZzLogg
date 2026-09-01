if(NOT DEFINED APP OR NOT DEFINED TEST_ROOT
   OR NOT DEFINED FIRST_LOG OR NOT DEFINED SECOND_LOG)
  message(FATAL_ERROR "APP, TEST_ROOT, FIRST_LOG and SECOND_LOG are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/runtime")
get_filename_component(app_name "${APP}" NAME)
set(smoke_app "${TEST_ROOT}/runtime/${app_name}")
file(COPY_FILE "${APP}" "${smoke_app}")
set(data_root "${TEST_ROOT}/storage")
include("${CMAKE_CURRENT_LIST_DIR}/smokeisolationcheck.cmake")
zzlogg_capture_host_storage_state(host_state_before)

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

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${config_env}
          ZZLOGG_UI2_SMOKE_MODE=destroy-search-edit
          ZZLOGG_UI2_SMOKE_MS=1800
          "${smoke_app}" --multi --new-session --data-dir "${data_root}"
          "${FIRST_LOG}" "${SECOND_LOG}"
  RESULT_VARIABLE lifetime_result
  OUTPUT_VARIABLE lifetime_stdout
  ERROR_VARIABLE lifetime_stderr
  TIMEOUT 15)
zzlogg_assert_host_storage_unchanged("${host_state_before}")
zzlogg_assert_no_locator_or_probe(
  "${TEST_ROOT}/runtime" "${TEST_ROOT}/config" "${data_root}")
if(lifetime_result EQUAL 0)
  message(FATAL_ERROR "UI2 smoke did not reject an invalidated asynchronous object")
endif()
string(FIND "${lifetime_stderr}"
  "mainSearchEdit disappeared while waiting for search" diagnostic_index)
if(diagnostic_index EQUAL -1)
  message(FATAL_ERROR
    "UI2 smoke did not report the invalidated object: ${lifetime_stderr}")
endif()
foreach(storage_entry storage-manifest.ini config/ZzLogg.ini
        session/ZzLogg_session.ini logs crashes)
  if(NOT EXISTS "${data_root}/${storage_entry}")
    message(FATAL_ERROR "UI2 async lifetime smoke storage entry missing: ${data_root}/${storage_entry}")
  endif()
endforeach()
