foreach(required_variable IN ITEMS BUILD_DIRECTORY CONFIG RUNTIME_DIR)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${RUNTIME_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIRECTORY}" --config "${CONFIG}"
          --target zzlogg_runtime_folder
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR
    "Failed to build clean runtime folder (${build_result}):\n"
    "${build_output}\n${build_error}")
endif()
