# Run the real Windows runtime notice staging against the pinned dependency
# checkout on every host, including hosts without windeployqt.
if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT and TEST_ROOT are required")
endif()
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${SOURCE_ROOT}"
  "-DDEPLOY_DIR=${TEST_ROOT}" -P "${SOURCE_ROOT}/cmake/StageRuntimeLicenses.cmake"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Runtime notice staging failed: ${output}\n${error}")
endif()
foreach(entry IN ITEMS "COPYING|licenses/ZzLogg/COPYING"
    "3rdparty/vendor/ZzPureTools/ZzThirdParty/qwindowkit/LICENSE|licenses/ZzPureTools/qwindowkit/LICENSE"
    "3rdparty/vendor/ZzPureTools/docs/third-party/THIRD_PARTY_NOTICES.md|licenses/ZzPureTools/THIRD_PARTY_NOTICES.md")
  string(REPLACE "|" ";" pair "${entry}")
  list(GET pair 0 source)
  list(GET pair 1 destination)
  file(SHA256 "${SOURCE_ROOT}/${source}" expected)
  file(SHA256 "${TEST_ROOT}/${destination}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Staged notice differs from pinned source: ${destination}")
  endif()
endforeach()
