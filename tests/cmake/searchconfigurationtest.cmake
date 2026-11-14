file(MAKE_DIRECTORY "${TEST_ROOT}/source")
file(WRITE "${TEST_ROOT}/source/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.30)\nproject(BackendSelection NONE)\ninclude(\"${SOURCE_ROOT}/cmake/ZzSearchBackend.cmake\")\nfile(WRITE \"\${CMAKE_BINARY_DIR}/selected.txt\" \"\${ZZLOGG_SEARCH_BACKEND}\")\n")
# Each case has its own cache, except the explicit persistence check.
function(configure_case name expected)
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${TEST_ROOT}/source"
    -B "${TEST_ROOT}/${name}" ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${name} configure failed: ${output}\n${error}")
  endif()
  file(READ "${TEST_ROOT}/${name}/selected.txt" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${name}: expected ${expected}, got ${actual}")
  endif()
endfunction()
configure_case(default Qt -UKLOGG_USE_HYPERSCAN -UKLOGG_USE_VECTORSCAN)
configure_case(hyperscan Hyperscan -DKLOGG_USE_HYPERSCAN=ON -DKLOGG_USE_VECTORSCAN=OFF)
configure_case(hyperscan Hyperscan) # Existing user cache survives reconfiguration.
configure_case(vectorscan Vectorscan -DKLOGG_USE_HYPERSCAN=OFF -DKLOGG_USE_VECTORSCAN=ON)
configure_case(qt Qt -DKLOGG_USE_HYPERSCAN=OFF -DKLOGG_USE_VECTORSCAN=OFF)
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${TEST_ROOT}/source"
  -B "${TEST_ROOT}/conflict" -DKLOGG_USE_HYPERSCAN=ON -DKLOGG_USE_VECTORSCAN=ON
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "Select only one accelerated search backend")
  message(FATAL_ERROR "Conflicting backends must fail explicitly: ${output}\n${error}")
endif()
