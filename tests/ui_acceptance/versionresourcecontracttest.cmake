foreach(required_variable IN ITEMS
    BUILD_DIRECTORY
    CONFIG
    POWERSHELL
    CONTRACT_SCRIPT
    MAIN_APP
    GREP_APP)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIRECTORY}" --config "${CONFIG}"
          --target klogg klogg_grep
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Version resource target build failed:\n${build_output}\n${build_error}")
endif()

execute_process(
  COMMAND "${POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
          -File "${CONTRACT_SCRIPT}"
          -MainApp "${MAIN_APP}"
          -GrepApp "${GREP_APP}"
  RESULT_VARIABLE contract_result
  OUTPUT_VARIABLE contract_output
  ERROR_VARIABLE contract_error)
if(NOT contract_result EQUAL 0)
  message(FATAL_ERROR "Version resource contract failed:\n${contract_output}\n${contract_error}")
endif()
