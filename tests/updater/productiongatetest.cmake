string(RANDOM LENGTH 24 ALPHABET 0123456789abcdef transaction)
set(isolated "${CMAKE_CURRENT_BINARY_DIR}/isolated-${transaction}")
file(MAKE_DIRECTORY "${isolated}")
file(COPY "${EXECUTABLE}" DESTINATION "${isolated}")
get_filename_component(name "${EXECUTABLE}" NAME)
set(program "${isolated}/${name}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=$ENV{SystemRoot}/System32" "${program}"
  WORKING_DIRECTORY "${isolated}" RESULT_VARIABLE result TIMEOUT 5)
if(NOT result EQUAL 41)
  message(FATAL_ERROR "Missing bootstrap must return BootstrapRejected (41), got ${result}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=$ENV{SystemRoot}/System32" "${program}" --installer arbitrary.exe
  WORKING_DIRECTORY "${isolated}" RESULT_VARIABLE result TIMEOUT 5)
if(NOT result EQUAL 41)
  message(FATAL_ERROR "Arbitrary installer arguments must be rejected, got ${result}")
endif()
execute_process(COMMAND "${program}" 18446744073709551616 RESULT_VARIABLE result TIMEOUT 5)
if(NOT result EQUAL 41)
  message(FATAL_ERROR "Invalid inherited-handle argument must reject without hanging: ${result}")
endif()
file(REMOVE "${program}")
# Only this freshly created, now empty test directory is removed.
execute_process(COMMAND "${CMAKE_COMMAND}" -E remove_directory "${isolated}" RESULT_VARIABLE cleaned)
if(NOT cleaned EQUAL 0)
  message(FATAL_ERROR "Could not clean isolated gate fixture")
endif()

# The transaction engine speaks only the restricted argv contract: flag/value
# pairs with non-secret parameters. Transaction credentials travel solely
# through the current-user-private credential file named by the txid locator;
# a token on the command line is a usage rejection, and a missing credential
# file rejects before any handshake.
execute_process(COMMAND "${TXEXECUTABLE}" RESULT_VARIABLE engine_noargs TIMEOUT 5)
if(NOT engine_noargs EQUAL 2)
  message(FATAL_ERROR "Engine without arguments must be a usage rejection (2), got ${engine_noargs}")
endif()
execute_process(COMMAND "${TXEXECUTABLE}" --install x --staging y --txroot z
  --txid 0000000000000001 --version 2 RESULT_VARIABLE engine_missing TIMEOUT 5)
if(NOT engine_missing EQUAL 41)
  message(FATAL_ERROR "Engine with a missing credential file must reject (41), got ${engine_missing}")
endif()
execute_process(COMMAND "${TXEXECUTABLE}" --install x --staging y --txroot z
  --txid 0000000000000001 --version 2 f1e2d3c4b5a69788 RESULT_VARIABLE engine_token TIMEOUT 5)
if(NOT engine_token EQUAL 2)
  message(FATAL_ERROR "Engine must never accept a token on the command line, got ${engine_token}")
endif()
