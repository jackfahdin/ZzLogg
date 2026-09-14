if(NOT DEFINED APP OR NOT DEFINED CONFIG OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "APP, CONFIG and TEST_ROOT are required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/smokeisolationcheck.cmake")
zzlogg_require_safe_test_root("${TEST_ROOT}")

if(CONFIG STREQUAL "Debug")
  set(qt_debug_suffix d)
else()
  set(qt_debug_suffix "")
endif()

set(expected_plugins
  "platforms/qwindows${qt_debug_suffix}.dll"
  "iconengines/qsvgicon${qt_debug_suffix}.dll"
  "imageformats/qsvg${qt_debug_suffix}.dll")
get_filename_component(app_name "${APP}" NAME)
file(REMOVE_RECURSE "${TEST_ROOT}")
set(runtime_dir "${TEST_ROOT}/runtime")
set(runtime_app "${runtime_dir}/${app_name}")
get_filename_component(app_dir "${APP}" DIRECTORY)
file(COPY "${app_dir}/" DESTINATION "${runtime_dir}"
  PATTERN "ZzLogg.conf" EXCLUDE
  PATTERN "ZzLogg.storage.ini" EXCLUDE
  PATTERN "data" EXCLUDE)
file(WRITE "${runtime_dir}/qt.conf" "[Paths]\nPrefix=.\nPlugins=.\n")
foreach(expected_plugin IN LISTS expected_plugins)
  if(NOT EXISTS "${runtime_dir}/${expected_plugin}")
    message(FATAL_ERROR
      "Local UI runtime is missing expected Qt plugin: ${expected_plugin}")
  endif()
endforeach()

file(MAKE_DIRECTORY
  "${TEST_ROOT}/Roaming"
  "${TEST_ROOT}/Local"
  "${TEST_ROOT}/xdg/config"
  "${TEST_ROOT}/xdg/data"
  "${TEST_ROOT}/xdg/cache"
  "${TEST_ROOT}/smoke/app-config"
  "${TEST_ROOT}/smoke/user-data")
zzlogg_run_isolated_smoke_process(
  LABEL "local runtime smoke"
  TEST_ROOT "${TEST_ROOT}"
  APP_CONFIG_DIR "${TEST_ROOT}/smoke/app-config"
  USER_DATA_DIR "${TEST_ROOT}/smoke/user-data"
  SMOKE_MS 800
  RESULT_VARIABLE smoke_result
  OUTPUT_VARIABLE smoke_stdout
  ERROR_VARIABLE smoke_stderr
  TIMEOUT 15
  ENVIRONMENT
    --unset=QT_PLUGIN_PATH
    --unset=QT_QPA_PLATFORM_PLUGIN_PATH
    --unset=QT_QPA_PLATFORM
    "APPDATA=${TEST_ROOT}/Roaming"
    "LOCALAPPDATA=${TEST_ROOT}/Local"
    "XDG_CONFIG_HOME=${TEST_ROOT}/xdg/config"
    "XDG_DATA_HOME=${TEST_ROOT}/xdg/data"
    "XDG_CACHE_HOME=${TEST_ROOT}/xdg/cache"
    "PATH=${runtime_dir}"
  COMMAND "${runtime_app}" --multi --new-session --data-dir "${TEST_ROOT}/storage"
          "${CMAKE_CURRENT_LIST_DIR}/fixtures/ui-first.log"
          "${CMAKE_CURRENT_LIST_DIR}/fixtures/ui-second.log")
zzlogg_assert_output_isolated(
  "local runtime smoke" "${TEST_ROOT}" "${smoke_stdout}" "${smoke_stderr}")
zzlogg_assert_no_locator_or_probe(
  "${runtime_dir}" "${TEST_ROOT}" "${TEST_ROOT}/storage")
if(NOT smoke_result EQUAL 0)
  message(FATAL_ERROR
    "UI local runtime smoke failed: ${smoke_result}\n${smoke_stdout}\n${smoke_stderr}")
endif()

foreach(storage_entry storage-manifest.ini config/ZzLogg.ini
        session/ZzLogg_session.ini logs)
  if(NOT EXISTS "${TEST_ROOT}/storage/${storage_entry}")
    message(FATAL_ERROR
      "Local runtime smoke storage entry missing: ${TEST_ROOT}/storage/${storage_entry}")
  endif()
endforeach()
