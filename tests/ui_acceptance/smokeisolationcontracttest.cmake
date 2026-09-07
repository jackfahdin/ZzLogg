if(NOT DEFINED UI_TEST_SOURCE_DIR)
  message(FATAL_ERROR "UI_TEST_SOURCE_DIR is required")
endif()

set(cmake_lists "${UI_TEST_SOURCE_DIR}/CMakeLists.txt")
file(READ "${cmake_lists}" cmake_contents)

set(smoke_contracts
  "application_smoke|applicationsmoke.cmake|1"
  "storage_application_smoke|storageapplicationsmoke.cmake|5"
  "session_restore_smoke|sessionrestoresmoke.cmake|2"
  "smoke_setup_diagnostics|smokesetupdiagnosticstest.cmake|1"
  "async_object_lifetime_smoke|asyncobjectlifetimesmoke.cmake|1"
  "titlebar_lifetime_smoke|titlebarlifetimesmoke.cmake|1"
  "windows_local_runtime_contract|localruntimecontracttest.cmake|1")

foreach(smoke_contract IN LISTS smoke_contracts)
  string(REPLACE "|" ";" smoke_fields "${smoke_contract}")
  list(GET smoke_fields 0 test_name)
  list(GET smoke_fields 1 script_name)
  list(GET smoke_fields 2 expected_helper_calls)

  string(FIND "${cmake_contents}" "NAME zzlogg_ui.${test_name}" test_index)
  if(test_index EQUAL -1)
    message(FATAL_ERROR "Missing CTest registration: zzlogg_ui.${test_name}")
  endif()
  string(FIND "${cmake_contents}" "${script_name}" script_registration_index)
  if(script_registration_index EQUAL -1)
    message(FATAL_ERROR
      "CTest zzlogg_ui.${test_name} does not invoke ${script_name}")
  endif()

  set(script_path "${UI_TEST_SOURCE_DIR}/${script_name}")
  file(READ "${script_path}" script_contents)
  string(FIND "${script_contents}" "execute_process(" raw_process_index)
  if(NOT raw_process_index EQUAL -1)
    message(FATAL_ERROR
      "${script_name} launches a process outside zzlogg_run_isolated_smoke_process")
  endif()
  string(REGEX MATCHALL "zzlogg_run_isolated_smoke_process\\("
    helper_calls "${script_contents}")
  list(LENGTH helper_calls helper_call_count)
  if(NOT helper_call_count EQUAL expected_helper_calls)
    message(FATAL_ERROR
      "${script_name} expected ${expected_helper_calls} isolated launches, found ${helper_call_count}")
  endif()

  foreach(forbidden_host_access IN ITEMS
      "zzlogg_capture_host_storage_state"
      "zzlogg_assert_host_storage_unchanged"
      "GetFolderPath('ApplicationData')"
      "GetFolderPath('LocalApplicationData')")
    string(FIND "${script_contents}" "${forbidden_host_access}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
      message(FATAL_ERROR
        "${script_name} still reads a real host KnownFolder via ${forbidden_host_access}")
    endif()
  endforeach()
endforeach()

file(READ "${UI_TEST_SOURCE_DIR}/smokeisolationcheck.cmake" helper_contents)
foreach(required_helper_fragment IN ITEMS
    "function(zzlogg_run_isolated_smoke_process)"
    "ZZLOGG_UI_SMOKE_MS=\${SMOKE_SMOKE_MS}"
    "ZZLOGG_UI_SMOKE_APP_CONFIG_DIR=\${SMOKE_APP_CONFIG_DIR}"
    "ZZLOGG_UI_SMOKE_USER_DATA_DIR=\${SMOKE_USER_DATA_DIR}")
  string(FIND "${helper_contents}" "${required_helper_fragment}" helper_fragment_index)
  if(helper_fragment_index EQUAL -1)
    message(FATAL_ERROR
      "smokeisolationcheck.cmake lost centralized injection: ${required_helper_fragment}")
  endif()
endforeach()
foreach(forbidden_host_access IN ITEMS
    "zzlogg_capture_host_storage_state"
    "zzlogg_assert_host_storage_unchanged"
    "GetFolderPath('ApplicationData')"
    "GetFolderPath('LocalApplicationData')")
  string(FIND "${helper_contents}" "${forbidden_host_access}" forbidden_index)
  if(NOT forbidden_index EQUAL -1)
    message(FATAL_ERROR
      "smokeisolationcheck.cmake still exposes real host access via ${forbidden_host_access}")
  endif()
endforeach()

foreach(helper_registration_fragment IN ITEMS
    "NAME zzlogg_ui.smoke_isolation_helper_contract"
    "smokeisolationhelpercontracttest.cmake")
  string(FIND "${cmake_contents}" "${helper_registration_fragment}"
    helper_contract_index)
  if(helper_contract_index EQUAL -1)
    message(FATAL_ERROR
      "Missing centralized helper regression coverage: ${helper_registration_fragment}")
  endif()
endforeach()
file(READ "${UI_TEST_SOURCE_DIR}/smokeisolationhelpercontracttest.cmake"
  helper_contract_contents)
string(FIND "${helper_contract_contents}" "smokeisolationmissingoverridefixture.cmake"
  missing_override_fixture_index)
if(missing_override_fixture_index EQUAL -1)
  message(FATAL_ERROR "The helper contract lacks a missing-override negative fixture")
endif()

cmake_path(GET UI_TEST_SOURCE_DIR PARENT_PATH tests_directory)
cmake_path(GET tests_directory PARENT_PATH project_source_directory)
set(application_runner "${project_source_directory}/src/app/applicationrunner.cpp")
set(smoke_paths_source "${project_source_directory}/src/app/applicationsmokepaths.cpp")
file(READ "${application_runner}" application_runner_contents)
file(READ "${smoke_paths_source}" smoke_paths_contents)

string(FIND "${application_runner_contents}"
  "qEnvironmentVariable( \"ZZLOGG_UI_SMOKE_USER_DATA_DIR\" ), [] {"
  lazy_provider_index)
if(lazy_provider_index EQUAL -1)
  message(FATAL_ERROR
    "applicationrunner.cpp must place production QStandardPaths lookups in the lazy smoke-path provider")
endif()

string(FIND "${application_runner_contents}"
  "uiSmoke.requested,\n        QStandardPaths::writableLocation"
  eager_provider_index)
if(NOT eager_provider_index EQUAL -1)
  message(FATAL_ERROR
    "applicationrunner.cpp eagerly resolves a production KnownFolder before validating smoke overrides")
endif()

foreach(required_lazy_fragment IN ITEMS
    "const ApplicationStoragePathsProvider& productionPathsProvider"
    "if ( !appConfigDirectory.isEmpty() && !userDataDirectory.isEmpty() )"
    "auto productionPaths = productionPathsProvider();")
  string(FIND "${smoke_paths_contents}" "${required_lazy_fragment}" fragment_index)
  if(fragment_index EQUAL -1)
    message(FATAL_ERROR
      "applicationsmokepaths.cpp lost lazy-provider contract: ${required_lazy_fragment}")
  endif()
endforeach()
