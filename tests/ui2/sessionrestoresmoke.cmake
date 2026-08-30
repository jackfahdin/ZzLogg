if(NOT DEFINED APP OR NOT DEFINED TEST_CONFIG_DIR)
  message(FATAL_ERROR "APP and TEST_CONFIG_DIR are required")
endif()

file(REMOVE_RECURSE "${TEST_CONFIG_DIR}")
file(MAKE_DIRECTORY "${TEST_CONFIG_DIR}")
get_filename_component(app_name "${APP}" NAME)
set(smoke_app "${TEST_CONFIG_DIR}/runtime/${app_name}")
file(MAKE_DIRECTORY "${TEST_CONFIG_DIR}/runtime")
file(COPY_FILE "${APP}" "${smoke_app}" ONLY_IF_DIFFERENT)
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
          "${smoke_app}" --multi --new-session
  RESULT_VARIABLE seed_result TIMEOUT 15)
if(NOT seed_result EQUAL 0)
  message(FATAL_ERROR "UI2 session seed failed: ${seed_result}")
endif()
if(WIN32)
  if(NOT EXISTS "${TEST_CONFIG_DIR}/Roaming/klogg/klogg.ini"
     OR NOT EXISTS "${TEST_CONFIG_DIR}/Roaming/klogg/klogg_session.ini")
    message(FATAL_ERROR "UI2 session seed did not write isolated INI files")
  endif()
else()
  file(GLOB_RECURSE isolated_settings LIST_DIRECTORIES false "${TEST_CONFIG_DIR}/*")
  list(LENGTH isolated_settings isolated_settings_count)
  if(isolated_settings_count LESS 2)
    message(FATAL_ERROR "UI2 session seed did not write isolated settings files")
  endif()
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${config_env}
          ZZLOGG_UI2_SMOKE_MODE=verify-restored ZZLOGG_UI2_SMOKE_MS=3000
          "${smoke_app}" --multi --load-session
  RESULT_VARIABLE restore_result TIMEOUT 15)
if(NOT restore_result EQUAL 0)
  message(FATAL_ERROR "UI2 session restore failed: ${restore_result}")
endif()
