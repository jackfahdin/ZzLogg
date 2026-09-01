if(NOT DEFINED APP OR NOT DEFINED TEST_ROOT
   OR NOT DEFINED FIRST_LOG OR NOT DEFINED SECOND_LOG)
  message(FATAL_ERROR "APP, TEST_ROOT, FIRST_LOG and SECOND_LOG are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/runtime")
get_filename_component(app_name "${APP}" NAME)
set(smoke_app "${TEST_ROOT}/runtime/${app_name}")
file(COPY_FILE "${APP}" "${smoke_app}")
file(WRITE "${TEST_ROOT}/runtime/ZzLogg.conf" "")
set(data_root "${TEST_ROOT}/storage")

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
          ZZLOGG_UI2_SMOKE_MODE=close-second-window-during-search
          ZZLOGG_UI2_SMOKE_MS=1800
          "${smoke_app}" --multi --new-session --data-dir "${data_root}"
          "${FIRST_LOG}" "${SECOND_LOG}"
  RESULT_VARIABLE lifetime_result
  OUTPUT_VARIABLE lifetime_stdout
  ERROR_VARIABLE lifetime_stderr
  TIMEOUT 15)
if(lifetime_result EQUAL 0)
  message(FATAL_ERROR "UI2 smoke accepted a closed second window during search")
endif()
string(FIND "${lifetime_stderr}"
  "second title bar disappeared while waiting for search" diagnostic_index)
if(diagnostic_index EQUAL -1)
  message(FATAL_ERROR
    "UI2 smoke did not report the closed window title bar: ${lifetime_stderr}")
endif()
foreach(storage_entry storage-manifest.ini config session logs crashes)
  if(NOT EXISTS "${data_root}/${storage_entry}")
    message(FATAL_ERROR "UI2 titlebar lifetime smoke storage entry missing: ${data_root}/${storage_entry}")
  endif()
endforeach()
