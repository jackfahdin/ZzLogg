if(NOT DEFINED APP OR NOT DEFINED CONFIG OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "APP, CONFIG and TEST_ROOT are required")
endif()

if(CONFIG STREQUAL "Debug")
  set(qt_debug_suffix d)
else()
  set(qt_debug_suffix "")
endif()

set(expected_plugins
  "platforms/qwindows${qt_debug_suffix}.dll"
  "iconengines/qsvgicon${qt_debug_suffix}.dll"
  "imageformats/qsvg${qt_debug_suffix}.dll")
get_filename_component(app_dir "${APP}" DIRECTORY)
foreach(expected_plugin IN LISTS expected_plugins)
  if(NOT EXISTS "${app_dir}/${expected_plugin}")
    message(FATAL_ERROR
      "Local UI2 runtime is missing expected Qt plugin: ${expected_plugin}")
  endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/Roaming" "${TEST_ROOT}/Local")
set(portable_config "${app_dir}/ZzLogg.conf")
set(portable_config_backup "${TEST_ROOT}/ZzLogg.conf")
if(EXISTS "${portable_config}")
  file(RENAME "${portable_config}" "${portable_config_backup}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=QT_PLUGIN_PATH
          "APPDATA=${TEST_ROOT}/Roaming"
          "LOCALAPPDATA=${TEST_ROOT}/Local"
          ZZLOGG_UI2_SMOKE_MS=800
          "${APP}" --multi --new-session
          "${CMAKE_CURRENT_LIST_DIR}/fixtures/ui2-first.log"
          "${CMAKE_CURRENT_LIST_DIR}/fixtures/ui2-second.log"
  RESULT_VARIABLE smoke_result
  OUTPUT_VARIABLE smoke_stdout
  ERROR_VARIABLE smoke_stderr
  TIMEOUT 15)
file(REMOVE "${portable_config}")
if(EXISTS "${portable_config_backup}")
  file(RENAME "${portable_config_backup}" "${portable_config}")
endif()
if(NOT smoke_result EQUAL 0)
  message(FATAL_ERROR
    "UI2 local runtime smoke failed: ${smoke_result}\n${smoke_stdout}\n${smoke_stderr}")
endif()
