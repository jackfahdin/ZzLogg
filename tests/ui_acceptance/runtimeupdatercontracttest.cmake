foreach(required_variable IN ITEMS RUNTIME_DIR DUMPBIN UPDATER_NAME FIXTURE_NAME)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

if(NOT IS_DIRECTORY "${RUNTIME_DIR}")
  message(FATAL_ERROR "RUNTIME_DIR must exist: ${RUNTIME_DIR}")
endif()

set(helper "${RUNTIME_DIR}/${UPDATER_NAME}")
if(NOT EXISTS "${helper}")
  message(FATAL_ERROR "Runtime deployment is missing the update helper: ${helper}")
endif()
if(EXISTS "${RUNTIME_DIR}/${FIXTURE_NAME}")
  message(FATAL_ERROR "Test fixtures must not be deployed: ${FIXTURE_NAME}")
endif()

# The deployed helper keeps its static-CRT, Qt-free closure: no Qt or MSVC
# dynamic runtime imports may appear in the dependency table.
execute_process(
  COMMAND "${DUMPBIN}" /DEPENDENTS "${helper}"
  RESULT_VARIABLE dumpbin_result
  OUTPUT_VARIABLE dumpbin_output
  ERROR_VARIABLE dumpbin_error)
if(NOT dumpbin_result EQUAL 0)
  message(FATAL_ERROR "dumpbin /DEPENDENTS failed: ${dumpbin_error}")
endif()
string(TOUPPER "${dumpbin_output}" dumpbin_upper)
foreach(forbidden_pattern IN ITEMS "QT6" "MSVCP" "VCRUNTIME" "MFC" "UCRTBASE")
  if(dumpbin_upper MATCHES "${forbidden_pattern}")
    message(FATAL_ERROR
      "Deployed update helper has a forbidden dynamic dependency "
      "(${forbidden_pattern}):\n${dumpbin_output}")
  endif()
endforeach()

# The deployed helper still rejects arbitrary arguments with the closed
# bootstrap gate, using only System32 on PATH like the production gate test.
string(RANDOM LENGTH 24 ALPHABET 0123456789abcdef gate_nonce)
set(isolated "${CMAKE_CURRENT_BINARY_DIR}/runtime-updater-gate-${gate_nonce}")
file(MAKE_DIRECTORY "${isolated}")
file(COPY "${helper}" DESTINATION "${isolated}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "PATH=$ENV{SystemRoot}/System32"
          "${isolated}/${UPDATER_NAME}" --installer arbitrary.exe
  WORKING_DIRECTORY "${isolated}"
  RESULT_VARIABLE gate_result
  TIMEOUT 5)
if(NOT gate_result EQUAL 41)
  message(FATAL_ERROR
    "Deployed helper must reject arbitrary arguments with BootstrapRejected (41), "
    "got ${gate_result}")
endif()
file(REMOVE_RECURSE "${isolated}")

message(STATUS "Runtime update helper contract passed: ${helper}")
