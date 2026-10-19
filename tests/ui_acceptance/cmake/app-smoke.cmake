add_test(NAME zzlogg_ui.application_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/application-smoke-runtime"
          "-DTEST_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/application-smoke-config"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/app/applicationsmoke.cmake")
set_tests_properties(zzlogg_ui.application_smoke PROPERTIES
  TIMEOUT 35)

add_test(NAME zzlogg_ui.smoke_isolation_contract
  COMMAND "${CMAKE_COMMAND}"
          "-DUI_TEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/app/smokeisolationcontracttest.cmake")
add_test(NAME zzlogg_ui.smoke_isolation_helper_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/smoke-isolation-helper-contract"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/app/smokeisolationhelpercontracttest.cmake")
add_test(NAME zzlogg_ui.storage_path_isolation_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/storage/storagepathisolationcontracttest.cmake")
if(WIN32)
  add_test(NAME zzlogg_ui.manual_storage_acceptance_contract
    COMMAND "${ZZLOGG_POWERSHELL_EXECUTABLE}"
      -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/storage/manualstorageacceptancecontracttest.ps1"
      -Launcher "${CMAKE_CURRENT_SOURCE_DIR}/storage/manualstorageacceptance.ps1"
      -RepositoryRoot "${PROJECT_SOURCE_DIR}"
      -AllowedRoot "${PROJECT_SOURCE_DIR}/out/manual-storage-acceptance-contracts")
endif()

add_test(NAME zzlogg_ui.storage_application_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/storage-application-smoke"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/storage/storageapplicationsmoke.cmake")
set_tests_properties(zzlogg_ui.storage_application_smoke PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 60)

if(WIN32)
  add_test(NAME zzlogg_ui.windows_local_runtime_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DAPP=$<TARGET_FILE:klogg>"
            "-DCONFIG=$<CONFIG>"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/local-runtime-contract"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/localruntimecontracttest.cmake")
  set_tests_properties(zzlogg_ui.windows_local_runtime_contract PROPERTIES
    TIMEOUT 20)
endif()

add_test(NAME zzlogg_ui.session_restore_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/session-smoke-config"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/app/sessionrestoresmoke.cmake")
set_tests_properties(zzlogg_ui.session_restore_smoke PROPERTIES TIMEOUT 45)

add_test(NAME zzlogg_ui.smoke_setup_diagnostics
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/smoke-setup-diagnostics"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/app/smokesetupdiagnosticstest.cmake")
set_tests_properties(zzlogg_ui.smoke_setup_diagnostics PROPERTIES TIMEOUT 15)

add_test(NAME zzlogg_ui.async_object_lifetime_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/async-object-lifetime-smoke"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/app/asyncobjectlifetimesmoke.cmake")
set_tests_properties(zzlogg_ui.async_object_lifetime_smoke PROPERTIES TIMEOUT 20)

add_test(NAME zzlogg_ui.titlebar_lifetime_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/titlebar-lifetime-smoke"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/app/titlebarlifetimesmoke.cmake")
set_tests_properties(zzlogg_ui.titlebar_lifetime_smoke PROPERTIES TIMEOUT 20)

if(WIN32)
  set_property(TEST
    zzlogg_ui.application_smoke zzlogg_ui.session_restore_smoke
    zzlogg_ui.smoke_setup_diagnostics
    zzlogg_ui.async_object_lifetime_smoke zzlogg_ui.titlebar_lifetime_smoke
    PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/..")
  set_property(TEST
    zzlogg_ui.application_smoke zzlogg_ui.session_restore_smoke
    zzlogg_ui.smoke_setup_diagnostics
    zzlogg_ui.async_object_lifetime_smoke zzlogg_ui.titlebar_lifetime_smoke
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:klogg>")
endif()
