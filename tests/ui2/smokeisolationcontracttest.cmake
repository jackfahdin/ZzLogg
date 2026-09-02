if(NOT DEFINED UI2_TEST_SOURCE_DIR)
  message(FATAL_ERROR "UI2_TEST_SOURCE_DIR is required")
endif()

set(cmake_lists "${UI2_TEST_SOURCE_DIR}/CMakeLists.txt")
file(READ "${cmake_lists}" cmake_contents)

set(smoke_contracts
  "application_smoke|applicationsmoke.cmake"
  "storage_application_smoke|storageapplicationsmoke.cmake"
  "session_restore_smoke|sessionrestoresmoke.cmake"
  "smoke_setup_diagnostics|smokesetupdiagnosticstest.cmake"
  "async_object_lifetime_smoke|asyncobjectlifetimesmoke.cmake"
  "titlebar_lifetime_smoke|titlebarlifetimesmoke.cmake"
  "windows_local_runtime_contract|localruntimecontracttest.cmake")

foreach(smoke_contract IN LISTS smoke_contracts)
  string(REPLACE "|" ";" smoke_fields "${smoke_contract}")
  list(GET smoke_fields 0 test_name)
  list(GET smoke_fields 1 script_name)

  string(FIND "${cmake_contents}" "NAME zzlogg_ui2.${test_name}" test_index)
  if(test_index EQUAL -1)
    message(FATAL_ERROR "Missing CTest registration: zzlogg_ui2.${test_name}")
  endif()
  string(FIND "${cmake_contents}" "${script_name}" script_registration_index)
  if(script_registration_index EQUAL -1)
    message(FATAL_ERROR
      "CTest zzlogg_ui2.${test_name} does not invoke ${script_name}")
  endif()

  set(script_path "${UI2_TEST_SOURCE_DIR}/${script_name}")
  file(READ "${script_path}" script_contents)
  foreach(required_assignment IN ITEMS
      "ZZLOGG_UI2_SMOKE_MS="
      "ZZLOGG_UI2_SMOKE_APP_CONFIG_DIR="
      "ZZLOGG_UI2_SMOKE_USER_DATA_DIR=")
    string(FIND "${script_contents}" "${required_assignment}" assignment_index)
    if(assignment_index EQUAL -1)
      message(FATAL_ERROR
        "${script_name} does not set ${required_assignment} for its GUI process")
    endif()
  endforeach()

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

file(READ "${UI2_TEST_SOURCE_DIR}/smokeisolationcheck.cmake" helper_contents)
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

cmake_path(GET UI2_TEST_SOURCE_DIR PARENT_PATH tests_directory)
cmake_path(GET tests_directory PARENT_PATH project_source_directory)
set(application_runner "${project_source_directory}/src/app/applicationrunner.cpp")
set(smoke_paths_source "${project_source_directory}/src/app/applicationsmokepaths.cpp")
file(READ "${application_runner}" application_runner_contents)
file(READ "${smoke_paths_source}" smoke_paths_contents)

string(FIND "${application_runner_contents}"
  "qEnvironmentVariable( \"ZZLOGG_UI2_SMOKE_USER_DATA_DIR\" ), [] {"
  lazy_provider_index)
if(lazy_provider_index EQUAL -1)
  message(FATAL_ERROR
    "applicationrunner.cpp must place production QStandardPaths lookups in the lazy smoke-path provider")
endif()

string(FIND "${application_runner_contents}"
  "ui2Smoke.requested,\n        QStandardPaths::writableLocation"
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
