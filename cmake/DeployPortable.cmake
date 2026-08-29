if(NOT DEFINED APP_EXECUTABLE OR NOT EXISTS "${APP_EXECUTABLE}")
  message(FATAL_ERROR "Portable application executable does not exist: ${APP_EXECUTABLE}")
endif()

if(NOT DEFINED DEPLOY_DIR OR DEPLOY_DIR STREQUAL "")
  message(FATAL_ERROR "DEPLOY_DIR is required")
endif()

if(NOT DEFINED WINDEPLOYQT OR NOT EXISTS "${WINDEPLOYQT}")
  message(FATAL_ERROR "windeployqt does not exist: ${WINDEPLOYQT}")
endif()

file(REMOVE_RECURSE "${DEPLOY_DIR}")
file(MAKE_DIRECTORY "${DEPLOY_DIR}")
file(COPY "${APP_EXECUTABLE}" DESTINATION "${DEPLOY_DIR}")

foreach(document IN LISTS PORTABLE_DOCUMENTS)
  if(EXISTS "${document}")
    file(COPY "${document}" DESTINATION "${DEPLOY_DIR}")
  endif()
endforeach()

if(CONFIG STREQUAL "Debug")
  set(deploy_mode --debug)
else()
  set(deploy_mode --release)
endif()

get_filename_component(deployed_executable_name "${APP_EXECUTABLE}" NAME)
set(deployed_executable "${DEPLOY_DIR}/${deployed_executable_name}")

execute_process(
  COMMAND "${WINDEPLOYQT}"
          --dir "${DEPLOY_DIR}"
          ${deploy_mode}
          --no-translations
          "${deployed_executable}"
  RESULT_VARIABLE deploy_result
  COMMAND_ECHO STDOUT
)

if(NOT deploy_result EQUAL 0)
  message(FATAL_ERROR "windeployqt failed with exit code ${deploy_result}")
endif()

message(STATUS "Portable folder created at ${DEPLOY_DIR}")
