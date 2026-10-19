foreach(required_variable IN ITEMS
    ZZLOGG_UI_SMOKE_MS
    ZZLOGG_UI_SMOKE_APP_CONFIG_DIR
    ZZLOGG_UI_SMOKE_USER_DATA_DIR)
  if(NOT DEFINED ENV{${required_variable}} OR "$ENV{${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing isolated smoke environment: ${required_variable}")
  endif()
endforeach()

if(NOT "$ENV{ZZLOGG_UI_SMOKE_MS}" STREQUAL "${EXPECTED_SMOKE_MS}")
  message(FATAL_ERROR "Unexpected smoke deadline")
endif()
if(NOT "$ENV{ZZLOGG_UI_SMOKE_APP_CONFIG_DIR}" STREQUAL "${EXPECTED_APP_CONFIG_DIR}")
  message(FATAL_ERROR "Unexpected app-config override")
endif()
if(NOT "$ENV{ZZLOGG_UI_SMOKE_USER_DATA_DIR}" STREQUAL "${EXPECTED_USER_DATA_DIR}")
  message(FATAL_ERROR "Unexpected user-data override")
endif()
