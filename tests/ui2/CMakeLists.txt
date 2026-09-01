if(CMAKE_VERSION VERSION_LESS 3.23)
  message(FATAL_ERROR "KLOGG_BUILD_UI2_TESTS requires CMake 3.23 or newer")
endif()

find_package(Qt6 REQUIRED COMPONENTS Gui Svg)

add_executable(zzlogg_storage_context_test storagecontexttest.cpp)
target_link_libraries(zzlogg_storage_context_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_context_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.storage_context
         COMMAND zzlogg_storage_context_test)

add_executable(zzlogg_storage_validator_test storagevalidatortest.cpp)
target_link_libraries(zzlogg_storage_validator_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_validator_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.storage_validator
         COMMAND zzlogg_storage_validator_test)

add_executable(zzlogg_storage_locator_test storagelocatortest.cpp)
target_link_libraries(zzlogg_storage_locator_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_locator_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.storage_locator
         COMMAND zzlogg_storage_locator_test)

add_executable(zzlogg_legacy_storage_test legacystoragetest.cpp)
target_link_libraries(zzlogg_legacy_storage_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_legacy_storage_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.legacy_storage
         COMMAND zzlogg_legacy_storage_test)

add_executable(zzlogg_storage_migrator_test storagemigratortest.cpp)
target_link_libraries(zzlogg_storage_migrator_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_migrator_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.storage_migrator
         COMMAND zzlogg_storage_migrator_test)

add_executable(zzlogg_storage_bootstrap_test
  storagebootstraptest.cpp
  "${PROJECT_SOURCE_DIR}/src/app/storagebootstrap.cpp")
target_include_directories(zzlogg_storage_bootstrap_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(zzlogg_storage_bootstrap_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_bootstrap_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.storage_bootstrap
         COMMAND zzlogg_storage_bootstrap_test)

add_executable(zzlogg_storage_location_ui_test storagelocationuitest.cpp)
target_link_libraries(zzlogg_storage_location_ui_test PRIVATE
  klogg_ui klogg_storage Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_storage_location_ui_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.storage_location_ui
         COMMAND zzlogg_storage_location_ui_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui2.storage_location_ui PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui2.storage_location_ui APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

if(TARGET klogg_portable_folder)
  set(ZZLOGG_PORTABLE_DIR_CONTRACT
    "$<TARGET_GENEX_EVAL:klogg_portable_folder,$<TARGET_PROPERTY:klogg_portable_folder,ZZLOGG_PORTABLE_DIR>>")
else()
  set(ZZLOGG_PORTABLE_DIR_CONTRACT
    "${CMAKE_BINARY_DIR}/portable/$<CONFIG>/${ZZLOGG_PRODUCT_NAME}-portable")
endif()

add_test(NAME zzlogg_ui2.output_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DMAIN_APP=$<TARGET_FILE:klogg>"
    "-DPORTABLE_APP=$<TARGET_FILE:klogg_portable>"
    "-DUI2_APP=$<TARGET_FILE:zzlogg_ui2>"
    "-DGREP_APP=$<TARGET_FILE:klogg_grep>"
    "-DPORTABLE_DIR=${ZZLOGG_PORTABLE_DIR_CONTRACT}"
    "-DEXECUTABLE_SUFFIX=$<TARGET_FILE_SUFFIX:klogg>"
    "-DGREP_EXCLUDED_FROM_ALL=$<TARGET_PROPERTY:klogg_grep,EXCLUDE_FROM_ALL>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/outputcontracttest.cmake")

if(WIN32)
  find_program(ZZLOGG_POWERSHELL_EXECUTABLE NAMES powershell pwsh REQUIRED)
  add_test(NAME zzlogg_ui2.version_resource_contract
    COMMAND "${CMAKE_COMMAND}"
      "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
      "-DCONFIG=$<CONFIG>"
      "-DPOWERSHELL=${ZZLOGG_POWERSHELL_EXECUTABLE}"
      "-DCONTRACT_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/versionresourcecontracttest.ps1"
      "-DMAIN_APP=$<TARGET_FILE:klogg>"
      "-DPORTABLE_APP=$<TARGET_FILE:klogg_portable>"
      "-DGREP_APP=$<TARGET_FILE:klogg_grep>"
      "-DUI2_APP=$<TARGET_FILE:zzlogg_ui2>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/versionresourcecontracttest.cmake")
  add_test(NAME zzlogg_ui2.windows_installer_contract
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
      "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
      "-DCONFIG=$<CONFIG>"
      "-DPORTABLE_DIR=${ZZLOGG_PORTABLE_DIR_CONTRACT}"
      "-DPOWERSHELL=${ZZLOGG_POWERSHELL_EXECUTABLE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/windowsinstallercontracttest.cmake")
  set_tests_properties(zzlogg_ui2.windows_installer_contract PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120)
endif()

add_executable(zzlogg_icon_asset_test
  iconassettest.cpp "${PROJECT_SOURCE_DIR}/src/app/klogg.qrc")
target_link_libraries(zzlogg_icon_asset_test PRIVATE
  zzlogg_brand Qt6::Test Qt6::Gui Qt6::Svg)
set_target_properties(zzlogg_icon_asset_test PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_definitions(zzlogg_icon_asset_test PRIVATE
  ZZLOGG_HICOLOR_ROOT="${PROJECT_SOURCE_DIR}/src/app/images/hicolor"
  ZZLOGG_ICO_PATH="${PROJECT_SOURCE_DIR}/Resources/ZzLogg.ico"
  ZZLOGG_ICNS_PATH="${PROJECT_SOURCE_DIR}/Resources/ZzLogg.icns")
add_test(NAME zzlogg_ui2.icon_assets
  COMMAND zzlogg_icon_asset_test -platform offscreen)

add_test(NAME zzlogg_ui2.linux_icon_install_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/linux-icon-install-contract"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/linuxiconinstallcontracttest.cmake")
if(WIN32)
  set_property(TEST zzlogg_ui2.icon_assets PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui2.icon_assets APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

add_executable(zzlogg_theme_configuration_test themeconfigurationtest.cpp)
target_link_libraries(zzlogg_theme_configuration_test
  PRIVATE klogg_settings Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_theme_configuration_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.theme_configuration
         COMMAND zzlogg_theme_configuration_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui2.theme_configuration PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui2.theme_configuration APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

add_executable(zzlogg_settings_override_test settingsoverridetest.cpp)
target_link_libraries(zzlogg_settings_override_test
  PRIVATE klogg_settings Qt6::Test Qt6::Core)
set_target_properties(zzlogg_settings_override_test PROPERTIES AUTOMOC ON)
set_target_properties(zzlogg_settings_override_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/settings-override-contract")
add_test(NAME zzlogg_ui2.settings_override
         COMMAND zzlogg_settings_override_test)
if(WIN32)
  set_property(TEST zzlogg_ui2.settings_override APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

add_executable(zzlogg_brand_contract_test brandcontracttest.cpp)
target_link_libraries(zzlogg_brand_contract_test
  PRIVATE klogg_settings klogg_versioncheck klogg_crash_handler
          Qt6::Test Qt6::Core)
set_target_properties(zzlogg_brand_contract_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.brand_contract
         COMMAND zzlogg_brand_contract_test)
if(WIN32)
  set_property(TEST zzlogg_ui2.brand_contract APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_options_theme_test optionsthemetest.cpp)
target_link_libraries(zzlogg_options_theme_test
  PRIVATE klogg_ui Qt6::Test Qt6::Widgets)
target_include_directories(zzlogg_options_theme_test PRIVATE
  "${CMAKE_BINARY_DIR}/src/ui/klogg_ui_autogen/include_$<CONFIG>")
set_target_properties(zzlogg_options_theme_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.options_theme
         COMMAND zzlogg_options_theme_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui2.options_theme PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui2.options_theme APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_document_tab_close_test documenttabclosetest.cpp)
target_link_libraries(zzlogg_document_tab_close_test PRIVATE
  klogg_ui Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_document_tab_close_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.document_tab_close
         COMMAND zzlogg_document_tab_close_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui2.document_tab_close PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui2.document_tab_close APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_fluent_shell_test
  windowkittestmain.cpp fluentshelltest.cpp)
target_link_libraries(zzlogg_fluent_shell_test PRIVATE
  zzlogg_ui2_shell Qt6::Test Qt6::Widgets)
target_include_directories(zzlogg_fluent_shell_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/ui2/src")
set_target_properties(zzlogg_fluent_shell_test PROPERTIES AUTOMOC ON)
klogg_copy_ui2_runtime_dlls(zzlogg_fluent_shell_test)
add_test(NAME zzlogg_ui2.fluent_shell
         COMMAND zzlogg_fluent_shell_test)
if(WIN32)
  set_property(TEST zzlogg_ui2.fluent_shell PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/..")
endif()

qt6_wrap_cpp(ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h")
set_source_files_properties(
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h"
  PROPERTIES SKIP_AUTOMOC ON)
add_executable(zzlogg_runtime_contract_test runtimecontracttest.cpp
  ${ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES}
  "${PROJECT_SOURCE_DIR}/src/app/zzloggapplicationidentity.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/zzloggapplicationidentity.h"
  "${PROJECT_SOURCE_DIR}/src/app/klogg.qrc"
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h")
target_link_libraries(zzlogg_runtime_contract_test PRIVATE
  zzlogg_ui2_runtime klogg_ui klogg_logdata klogg_versioncheck
  klogg_crash_handler roaring kdsingleapp Qt6::Test Qt6::Widgets)
target_include_directories(zzlogg_runtime_contract_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
set_target_properties(zzlogg_runtime_contract_test PROPERTIES
  AUTOMOC ON
  AUTORCC ON
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime-contract")
add_test(NAME zzlogg_ui2.runtime_contract
         COMMAND zzlogg_runtime_contract_test)
if(WIN32)
  klogg_copy_ui2_runtime_dlls(zzlogg_runtime_contract_test)
  set_property(TEST zzlogg_ui2.runtime_contract PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/..")
endif()

add_test(NAME zzlogg_ui2.application_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:zzlogg_ui2>"
          "-DTEST_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/application-smoke-runtime"
          "-DTEST_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/application-smoke-config"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui2-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui2-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/applicationsmoke.cmake")
set_tests_properties(zzlogg_ui2.application_smoke PROPERTIES
  TIMEOUT 35)

if(WIN32)
  add_test(NAME zzlogg_ui2.windows_local_runtime_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DAPP=$<TARGET_FILE:zzlogg_ui2>"
            "-DCONFIG=$<CONFIG>"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/local-runtime-contract"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/localruntimecontracttest.cmake")
  set_tests_properties(zzlogg_ui2.windows_local_runtime_contract PROPERTIES
    TIMEOUT 20)
endif()

add_test(NAME zzlogg_ui2.session_restore_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:zzlogg_ui2>"
          "-DTEST_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/session-smoke-config"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/sessionrestoresmoke.cmake")
set_tests_properties(zzlogg_ui2.session_restore_smoke PROPERTIES TIMEOUT 45)

add_test(NAME zzlogg_ui2.smoke_setup_diagnostics
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:zzlogg_ui2>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/smoke-setup-diagnostics"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/smokesetupdiagnosticstest.cmake")
set_tests_properties(zzlogg_ui2.smoke_setup_diagnostics PROPERTIES TIMEOUT 15)

add_test(NAME zzlogg_ui2.async_object_lifetime_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:zzlogg_ui2>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/async-object-lifetime-smoke"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui2-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui2-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/asyncobjectlifetimesmoke.cmake")
set_tests_properties(zzlogg_ui2.async_object_lifetime_smoke PROPERTIES TIMEOUT 20)

add_test(NAME zzlogg_ui2.titlebar_lifetime_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:zzlogg_ui2>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/titlebar-lifetime-smoke"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui2-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui2-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/titlebarlifetimesmoke.cmake")
set_tests_properties(zzlogg_ui2.titlebar_lifetime_smoke PROPERTIES TIMEOUT 20)

if(WIN32)
  set_property(TEST
    zzlogg_ui2.application_smoke zzlogg_ui2.session_restore_smoke
    zzlogg_ui2.smoke_setup_diagnostics
    zzlogg_ui2.async_object_lifetime_smoke zzlogg_ui2.titlebar_lifetime_smoke
    PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/..")
  set_property(TEST
    zzlogg_ui2.application_smoke zzlogg_ui2.session_restore_smoke
    zzlogg_ui2.smoke_setup_diagnostics
    zzlogg_ui2.async_object_lifetime_smoke zzlogg_ui2.titlebar_lifetime_smoke
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:zzlogg_ui2>")
endif()
