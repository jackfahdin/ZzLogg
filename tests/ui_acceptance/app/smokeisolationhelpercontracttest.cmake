if(NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "TEST_ROOT is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/smokeisolationcheck.cmake")
set(app_config_dir "${TEST_ROOT}/app-config")
set(user_data_dir "${TEST_ROOT}/user-data")
zzlogg_run_isolated_smoke_process(
  LABEL "helper happy path"
  TEST_ROOT "${TEST_ROOT}"
  APP_CONFIG_DIR "${app_config_dir}"
  USER_DATA_DIR "${user_data_dir}"
  SMOKE_MS 321
  RESULT_VARIABLE helper_result
  OUTPUT_VARIABLE helper_stdout
  ERROR_VARIABLE helper_stderr
  TIMEOUT 5
  COMMAND "${CMAKE_COMMAND}"
    "-DEXPECTED_SMOKE_MS=321"
    "-DEXPECTED_APP_CONFIG_DIR=${app_config_dir}"
    "-DEXPECTED_USER_DATA_DIR=${user_data_dir}"
    -P "${CMAKE_CURRENT_LIST_DIR}/smokeisolationenvironmentfixture.cmake")
if(NOT helper_result EQUAL 0)
  message(FATAL_ERROR
    "The isolated smoke helper did not inject its environment: ${helper_stdout} ${helper_stderr}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DTEST_ROOT=${TEST_ROOT}/missing"
    -P "${CMAKE_CURRENT_LIST_DIR}/smokeisolationmissingoverridefixture.cmake"
  RESULT_VARIABLE missing_result
  OUTPUT_VARIABLE missing_stdout
  ERROR_VARIABLE missing_stderr)
if(missing_result EQUAL 0)
  message(FATAL_ERROR "A smoke launch without user-data override unexpectedly succeeded")
endif()
string(CONCAT missing_output "${missing_stdout}" "\n" "${missing_stderr}")
string(FIND "${missing_output}" "USER_DATA_DIR is required" missing_diagnostic_index)
if(missing_diagnostic_index EQUAL -1)
  message(FATAL_ERROR
    "Missing-override fixture did not report the required diagnostic: ${missing_output}")
endif()
