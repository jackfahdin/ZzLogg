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

if(CONFIG STREQUAL "Debug")
  set(deploy_mode --debug)
else()
  set(deploy_mode --release)
endif()

get_filename_component(deployed_executable_name "${APP_EXECUTABLE}" NAME)
set(deployed_executable "${DEPLOY_DIR}/${deployed_executable_name}")

# This application uses raster QWidget painting, not OpenGL/Qt Quick/PDF.
# Keep the Windows platform, SVG/icon support and native HTTPS backends.
set(lean_deploy_options
    --no-opengl-sw --no-system-d3d-compiler
    --skip-plugin-types generic,styles
    --exclude-plugins qpdf,qgif,qicns,qtga,qtiff,qwbmp,qwebp)
# DXC deployment is present in newer Qt releases; older deploy tools have no
# DXC option or payload. Do not require a newer Qt solely for this switch.
execute_process(COMMAND "${WINDEPLOYQT}" --help
  OUTPUT_VARIABLE deploy_help ERROR_VARIABLE deploy_help_error
  RESULT_VARIABLE help_result)
if(NOT help_result EQUAL 0)
  message(FATAL_ERROR "Unable to inspect windeployqt options: ${deploy_help_error}")
endif()
if(deploy_help MATCHES "--no-system-dxc-compiler")
  list(APPEND lean_deploy_options --no-system-dxc-compiler)
endif()

execute_process(
  COMMAND "${WINDEPLOYQT}"
          --dir "${DEPLOY_DIR}"
          ${deploy_mode}
          --no-translations
          ${lean_deploy_options}
          "${deployed_executable}"
  RESULT_VARIABLE deploy_result
  COMMAND_ECHO STDOUT
)

if(NOT deploy_result EQUAL 0)
  message(FATAL_ERROR "windeployqt failed with exit code ${deploy_result}")
endif()

message(STATUS "Portable folder created at ${DEPLOY_DIR}")
