if(NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "TEST_ROOT is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/smokeisolationcheck.cmake")
zzlogg_run_isolated_smoke_process(
  LABEL "missing override fixture"
  TEST_ROOT "${TEST_ROOT}"
  APP_CONFIG_DIR "${TEST_ROOT}/app-config"
  SMOKE_MS 100
  RESULT_VARIABLE fixture_result
  OUTPUT_VARIABLE fixture_stdout
  ERROR_VARIABLE fixture_stderr
  TIMEOUT 5
  COMMAND "${CMAKE_COMMAND}" -E true)
