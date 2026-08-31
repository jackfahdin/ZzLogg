if(NOT DEFINED APP OR NOT DEFINED TEST_ROOT OR NOT DEFINED FIRST_LOG)
  message(FATAL_ERROR "APP, TEST_ROOT and FIRST_LOG are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/runtime" "${TEST_ROOT}/working")
get_filename_component(app_name "${APP}" NAME)
file(COPY_FILE "${APP}" "${TEST_ROOT}/runtime/${app_name}")
file(WRITE "${TEST_ROOT}/runtime/ZzLogg.conf" "[General]\nversion=1\n")

if(WIN32)
  set(path_separator ";")
  file(MAKE_DIRECTORY "${TEST_ROOT}/config/Roaming" "${TEST_ROOT}/config/Local")
  set(config_env
    "APPDATA=${TEST_ROOT}/config/Roaming"
    "LOCALAPPDATA=${TEST_ROOT}/config/Local")
else()
  set(path_separator ":")
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
set(path_env "PATH=${TEST_ROOT}/runtime${path_separator}$ENV{PATH}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "${path_env}" ${config_env}
          ZZLOGG_UI2_SMOKE_MS=1200
          "${app_name}" --multi --new-session "${FIRST_LOG}"
  WORKING_DIRECTORY "${TEST_ROOT}/working"
  RESULT_VARIABLE guard_result
  OUTPUT_VARIABLE guard_stdout
  ERROR_VARIABLE guard_stderr
  TIMEOUT 15)

set(expected_path "${TEST_ROOT}/runtime/ZzLogg.conf")
if(guard_result EQUAL 0)
  message(FATAL_ERROR
    "UI2 portable guard accepted a PATH launch; expected rejection of ${expected_path}")
endif()
string(REPLACE "\\" "/" normalized_guard_stderr "${guard_stderr}")
string(FIND "${normalized_guard_stderr}" "${expected_path}" diagnostic_path_index)
if(diagnostic_path_index EQUAL -1)
  message(FATAL_ERROR
    "UI2 portable guard diagnostic omitted ${expected_path}: ${guard_stderr}")
endif()
