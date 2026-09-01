if(NOT DEFINED OUTPUT_CONTRACT OR NOT EXISTS "${OUTPUT_CONTRACT}")
  message(FATAL_ERROR "OUTPUT_CONTRACT is required")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}"
  "-DMAIN_APP=ZzLogg"
  "-DRUNTIME_DIR=${TEST_ROOT}/must-not-exist/wrong-name"
  "-DEXECUTABLE_SUFFIX="
  "-DTARGET_PORTABLE_EXISTS=FALSE"
  "-DTARGET_UI2_EXISTS=FALSE"
  "-DGREP_EXCLUDED_FROM_ALL=TRUE"
  "-DCHECK_RUNTIME_FOLDER=FALSE"
  -P "${OUTPUT_CONTRACT}"
  RESULT_VARIABLE graph_result OUTPUT_VARIABLE graph_output ERROR_VARIABLE graph_error)
if(NOT graph_result EQUAL 0)
  message(FATAL_ERROR "Non-Windows graph-only contract failed:\n${graph_output}\n${graph_error}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}"
  "-DMAIN_APP=ZzLogg"
  "-DRUNTIME_DIR=${TEST_ROOT}/must-not-exist/ZzLogg-runtime"
  "-DEXECUTABLE_SUFFIX="
  "-DTARGET_PORTABLE_EXISTS=FALSE"
  "-DTARGET_UI2_EXISTS=FALSE"
  "-DGREP_EXCLUDED_FROM_ALL=TRUE"
  "-DCHECK_RUNTIME_FOLDER=TRUE"
  -P "${OUTPUT_CONTRACT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(runtime_result EQUAL 0)
  message(FATAL_ERROR "Windows runtime branch accepted a missing runtime executable")
endif()
