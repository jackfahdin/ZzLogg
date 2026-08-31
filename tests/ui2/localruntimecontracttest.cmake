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
get_filename_component(app_name "${APP}" NAME)
file(REMOVE_RECURSE "${TEST_ROOT}")
set(runtime_dir "${TEST_ROOT}/runtime")
set(runtime_app "${runtime_dir}/${app_name}")
get_filename_component(app_dir "${APP}" DIRECTORY)
file(COPY "${app_dir}/" DESTINATION "${runtime_dir}"
  PATTERN "ZzLogg.conf" EXCLUDE)
file(WRITE "${runtime_dir}/qt.conf" "[Paths]\nPrefix=.\nPlugins=.\n")
foreach(expected_plugin IN LISTS expected_plugins)
  if(NOT EXISTS "${runtime_dir}/${expected_plugin}")
    message(FATAL_ERROR
      "Local UI2 runtime is missing expected Qt plugin: ${expected_plugin}")
  endif()
endforeach()

file(MAKE_DIRECTORY "${TEST_ROOT}/Roaming" "${TEST_ROOT}/Local")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          --unset=QT_PLUGIN_PATH
          --unset=QT_QPA_PLATFORM_PLUGIN_PATH
          --unset=QT_QPA_PLATFORM
          "APPDATA=${TEST_ROOT}/Roaming"
          "LOCALAPPDATA=${TEST_ROOT}/Local"
          "PATH=${runtime_dir}"
          ZZLOGG_UI2_SMOKE_MS=800
          "${runtime_app}" --multi --new-session
          "${CMAKE_CURRENT_LIST_DIR}/fixtures/ui2-first.log"
          "${CMAKE_CURRENT_LIST_DIR}/fixtures/ui2-second.log"
  RESULT_VARIABLE smoke_result
  OUTPUT_VARIABLE smoke_stdout
  ERROR_VARIABLE smoke_stderr
  TIMEOUT 15)
if(NOT smoke_result EQUAL 0)
  message(FATAL_ERROR
    "UI2 local runtime smoke failed: ${smoke_result}\n${smoke_stdout}\n${smoke_stderr}")
endif()
