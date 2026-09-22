function(zzlogg_update_test suffix source)
  add_executable(zzlogg_update_${suffix}_test "${source}")
  target_link_libraries(zzlogg_update_${suffix}_test PRIVATE zzlogg_update_core Qt6::Core Qt6::Test)
  set_target_properties(zzlogg_update_${suffix}_test PROPERTIES AUTOMOC ON)
  add_test(NAME zzlogg_update.${suffix} COMMAND zzlogg_update_${suffix}_test)
  if(WIN32)
    set_property(TEST zzlogg_update.${suffix} PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
endfunction()
zzlogg_update_test(version versiontest.cpp)
zzlogg_update_test(release_identity releaseidentitytest.cpp)
target_compile_definitions(zzlogg_update_release_identity_test PRIVATE
  TEST_OFFICIAL_RELEASE=$<BOOL:${ZZLOGG_OFFICIAL_RELEASE}>)
if(ZZLOGG_OFFICIAL_RELEASE)
  target_compile_definitions(zzlogg_update_release_identity_test PRIVATE
    TEST_RELEASE_VERSION="${ZZLOGG_DISPLAY_VERSION}"
    TEST_RELEASE_SEQUENCE=${ZZLOGG_RELEASE_SEQUENCE}ULL
    TEST_RELEASE_CHANNEL="${ZZLOGG_RELEASE_CHANNEL}"
    TEST_RELEASE_DATA_SCHEMA=${ZZLOGG_RELEASE_DATA_SCHEMA}U)
endif()
zzlogg_update_test(signature signaturetest.cpp)
zzlogg_update_test(strictjson strictjsontest.cpp)
zzlogg_update_test(manifest manifesttest.cpp)
zzlogg_update_test(policy policytest.cpp)
zzlogg_update_test(execution_selection executionselectiontest.cpp)
if(WIN32)
  zzlogg_update_test(package_verification packageverificationtest.cpp)
  target_include_directories(zzlogg_update_package_verification_test PRIVATE "../../src/update/src")
  target_link_libraries(zzlogg_update_package_verification_test PRIVATE zzlogg_update_execution
    zzlogg_update_json zzlogg_update_monocypher)
  foreach(architecture IN ITEMS ARM64 ARM64EC)
    add_executable(zzlogg_update_execution_${architecture}_descriptor_test executionplatformtest.cpp)
    target_include_directories(zzlogg_update_execution_${architecture}_descriptor_test PRIVATE "../../src/update/src")
    target_compile_definitions(zzlogg_update_execution_${architecture}_descriptor_test PRIVATE ZZLOGG_TEST_${architecture}=1)
    target_link_libraries(zzlogg_update_execution_${architecture}_descriptor_test PRIVATE zzlogg_update_execution)
    add_test(NAME zzlogg_update.execution_${architecture}_descriptor
      COMMAND zzlogg_update_execution_${architecture}_descriptor_test)
  endforeach()
endif()
target_include_directories(zzlogg_update_policy_test PRIVATE "../../src/update/src")
target_link_libraries(zzlogg_update_policy_test PRIVATE zzlogg_update_json zzlogg_update_monocypher)
target_include_directories(zzlogg_update_execution_selection_test PRIVATE "../../src/update/src")
target_link_libraries(zzlogg_update_execution_selection_test PRIVATE zzlogg_update_json zzlogg_update_monocypher)
target_include_directories(zzlogg_update_manifest_test PRIVATE "../../src/update/src")
target_link_libraries(zzlogg_update_manifest_test PRIVATE zzlogg_update_json zzlogg_update_monocypher)
target_include_directories(zzlogg_update_strictjson_test PRIVATE "../../src/update/src")
target_link_libraries(zzlogg_update_strictjson_test PRIVATE zzlogg_update_json)
target_compile_definitions(zzlogg_update_signature_test PRIVATE
  UPDATE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

set(release_configuration_test_root
  "${CMAKE_CURRENT_BINARY_DIR}/release-configuration")
set(release_configuration_caller_sentinel
  "${release_configuration_test_root}/caller-sentinel.txt")
file(MAKE_DIRECTORY "${release_configuration_test_root}")
file(WRITE "${release_configuration_caller_sentinel}" "caller-owned\n")

function(zzlogg_release_configuration_test suffix)
  add_test(
    NAME zzlogg_update.release_configuration${suffix}
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
      "-DFIXTURE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/releaseidentityfixture"
      "-DTEST_ROOT=${release_configuration_test_root}"
      "-DTEST_CALLER_SENTINEL=${release_configuration_caller_sentinel}"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
      "-DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DTEST_CONFIGURATION=$<CONFIG>"
      ${ARGN}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/releaseconfigurationtest.cmake")
endfunction()
add_test(NAME zzlogg_update.release_sequence COMMAND "${CMAKE_COMMAND}"
  "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/releasesequencetest.cmake")
zzlogg_release_configuration_test("")
# These exercise descriptor-based test routing using the current compiler;
# they are not executions on Linux or ARM64.
foreach(descriptor IN ITEMS linux arm64)
  zzlogg_release_configuration_test(_${descriptor}_descriptor
    "-DTEST_TARGET_DESCRIPTION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/releaseidentityfixture/${descriptor}-description.cmake"
    -DTEST_NATIVE_CASES_ONLY=ON)
endforeach()

# Warning relaxation for test-only targets (see the function comment in the
# top-level CMakeLists.txt; -Werror arrives via linked interfaces).
zzlogg_relax_test_warnings_in_current_dir()
