if(CMAKE_VERSION VERSION_LESS 3.23)
  message(FATAL_ERROR "KLOGG_BUILD_UI_TESTS requires CMake 3.23 or newer")
endif()

find_package(Qt6 REQUIRED COMPONENTS Gui Svg)

add_test(NAME zzlogg_ui.version_display
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DBINARY_ROOT=${CMAKE_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/versiondisplaytest.cmake")

add_executable(zzlogg_text_cache_test textcachetest.cpp)
target_link_libraries(zzlogg_text_cache_test PRIVATE
  klogg_ui klogg_storage Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_text_cache_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.text_cache COMMAND zzlogg_text_cache_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui.text_cache PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.text_cache PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_test(NAME zzlogg_ui.release_preset_contract
  COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/releasepresetcontracttest.cmake)

add_executable(zzlogg_line_number_ui_test linenumberuitest.cpp)
target_link_libraries(zzlogg_line_number_ui_test PRIVATE
  klogg_ui klogg_storage Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_line_number_ui_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.line_number_ui
  COMMAND zzlogg_line_number_ui_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui.line_number_ui PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.line_number_ui APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_line_number_configuration_test
  linenumberconfigurationtest.cpp)
target_link_libraries(zzlogg_line_number_configuration_test PRIVATE
  klogg_settings Qt6::Test Qt6::Core)
set_target_properties(zzlogg_line_number_configuration_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.line_number_configuration
  COMMAND zzlogg_line_number_configuration_test)

add_executable(zzlogg_storage_context_test storagecontexttest.cpp)
target_link_libraries(zzlogg_storage_context_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_context_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_context
         COMMAND zzlogg_storage_context_test)

add_executable(zzlogg_storage_validator_test storagevalidatortest.cpp)
target_link_libraries(zzlogg_storage_validator_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_validator_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_validator
         COMMAND zzlogg_storage_validator_test)

add_executable(zzlogg_storage_locator_test storagelocatortest.cpp)
target_link_libraries(zzlogg_storage_locator_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_locator_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_locator
         COMMAND zzlogg_storage_locator_test)

add_executable(zzlogg_legacy_storage_test legacystoragetest.cpp)
target_link_libraries(zzlogg_legacy_storage_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_legacy_storage_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.legacy_storage
         COMMAND zzlogg_legacy_storage_test)

add_executable(zzlogg_storage_migrator_test storagemigratortest.cpp)
target_link_libraries(zzlogg_storage_migrator_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_migrator_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_migrator
         COMMAND zzlogg_storage_migrator_test)

add_executable(zzlogg_storage_bootstrap_test
  storagebootstraptest.cpp
  "${PROJECT_SOURCE_DIR}/src/app/storagebootstrap.cpp")
target_include_directories(zzlogg_storage_bootstrap_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(zzlogg_storage_bootstrap_test PRIVATE
  klogg_storage zzlogg_brand Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_bootstrap_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_bootstrap
         COMMAND zzlogg_storage_bootstrap_test)

add_executable(zzlogg_storage_location_ui_test storagelocationuitest.cpp)
target_link_libraries(zzlogg_storage_location_ui_test PRIVATE
  klogg_ui klogg_storage Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_storage_location_ui_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_location_ui
         COMMAND zzlogg_storage_location_ui_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui.storage_location_ui PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.storage_location_ui APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
set(ZZLOGG_TRANSLATION_TEST_TS_FILES
  "${PROJECT_SOURCE_DIR}/src/app/i18n/en.ts"
  "${PROJECT_SOURCE_DIR}/src/app/i18n/zh_CN.ts"
  "${PROJECT_SOURCE_DIR}/src/app/i18n/zh_TW.ts")
qt6_add_translation(ZZLOGG_TRANSLATION_TEST_QM_FILES
  ${ZZLOGG_TRANSLATION_TEST_TS_FILES})
set(Qt_LCONVERT_EXECUTABLE Qt6::lconvert)
include(TranslationUtils)
add_app_translations_resource(ZZLOGG_TRANSLATION_TEST_APP_RES
  ${ZZLOGG_TRANSLATION_TEST_QM_FILES})
add_qt_translations_resource(ZZLOGG_TRANSLATION_TEST_QT_RES zh_CN zh_TW)
add_executable(zzlogg_application_translation_test
  applicationtranslationtest.cpp
  "${PROJECT_SOURCE_DIR}/src/app/klogg.qrc"
  "${PROJECT_SOURCE_DIR}/src/app/applicationlanguage.cpp"
  ${ZZLOGG_TRANSLATION_TEST_APP_RES}
  ${ZZLOGG_TRANSLATION_TEST_QT_RES})
target_link_libraries(zzlogg_application_translation_test PRIVATE
  klogg_ui Qt6::Test Qt6::Widgets)
target_include_directories(zzlogg_application_translation_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
set_target_properties(zzlogg_application_translation_test PROPERTIES
  AUTOMOC ON AUTORCC ON)
target_compile_definitions(zzlogg_application_translation_test PRIVATE
  ZZLOGG_UI_CAPTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}/ui-captures")
add_test(NAME zzlogg_ui.application_translation
  COMMAND zzlogg_application_translation_test -platform offscreen)
add_test(NAME zzlogg_ui.application_translation_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/applicationtranslationcontracttest.cmake")
add_executable(zzlogg_runtime_translation_catalog_test
  runtimecatalogcontracttest.cpp
  runtimecatalogvalidator.cpp)
target_link_libraries(zzlogg_runtime_translation_catalog_test PRIVATE
  Qt6::Test Qt6::Core)
target_compile_definitions(zzlogg_runtime_translation_catalog_test PRIVATE
  ZZLOGG_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
set_target_properties(zzlogg_runtime_translation_catalog_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.runtime_translation_catalog_contract
  COMMAND zzlogg_runtime_translation_catalog_test)
if(WIN32)
  set_property(TEST zzlogg_ui.runtime_translation_catalog_contract APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
if(WIN32)
  set_property(TEST zzlogg_ui.application_translation PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.application_translation APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_storage_consumers_test storageconsumerstest.cpp)
target_link_libraries(zzlogg_storage_consumers_test PRIVATE
  klogg_logging klogg_crash_handler klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_consumers_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_consumers
         COMMAND zzlogg_storage_consumers_test)
if(WIN32)
  set_property(TEST zzlogg_ui.storage_consumers APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

if(TARGET zzlogg_runtime_folder)
  set(ZZLOGG_RUNTIME_DIR_CONTRACT
    "$<TARGET_GENEX_EVAL:zzlogg_runtime_folder,$<TARGET_PROPERTY:zzlogg_runtime_folder,ZZLOGG_RUNTIME_DIR>>")
else()
  set(ZZLOGG_RUNTIME_DIR_CONTRACT
    "${CMAKE_BINARY_DIR}/runtime/$<CONFIG>/${ZZLOGG_PRODUCT_NAME}-runtime")
endif()

add_test(NAME zzlogg_ui.output_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DMAIN_APP=$<TARGET_FILE:klogg>"
    "-DRUNTIME_DIR=${ZZLOGG_RUNTIME_DIR_CONTRACT}"
    "-DEXECUTABLE_SUFFIX=$<TARGET_FILE_SUFFIX:klogg>"
    "-DTARGET_PORTABLE_EXISTS=$<TARGET_EXISTS:klogg_portable>"
    "-DLEGACY_GUI_TARGET_EXISTS=$<TARGET_EXISTS:zzlogg_ui2>"
    "-DGREP_EXCLUDED_FROM_ALL=$<TARGET_PROPERTY:klogg_grep,EXCLUDE_FROM_ALL>"
    "-DCHECK_RUNTIME_FOLDER=$<BOOL:$<TARGET_EXISTS:zzlogg_runtime_folder>>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/outputcontracttest.cmake")

add_test(NAME zzlogg_ui.output_platform_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DOUTPUT_CONTRACT=${CMAKE_CURRENT_SOURCE_DIR}/outputcontracttest.cmake"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/output-platform-contract"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/outputplatformcontracttest.cmake")

add_test(NAME zzlogg_ui.ci_toolchain_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/citoolchaincontracttest.cmake")

add_test(NAME zzlogg_ui.ci_compiler_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cicompilercontracttest.cmake")

add_test(NAME zzlogg_ui.build_documentation_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/builddocumentationcontracttest.cmake")

add_test(NAME zzlogg_ui.linux_packaging_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/linuxpackagingcontracttest.cmake")

add_test(NAME zzlogg_ui.linux_self_contained_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/linuxselfcontainedcontracttest.cmake")

add_test(NAME zzlogg_ui.linux_package_validation_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/linuxpackagevalidationcontracttest.cmake")

add_test(NAME zzlogg_ui.linux_package_integration_staging
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/linux-package-integration"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/linuxpackageintegrationstagingtest.cmake")

add_test(NAME zzlogg_ui.linux_gnu_runtime_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/linuxgnuruntimecontracttest.cmake")

add_test(NAME zzlogg_ui.staged_install_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
    "-DINSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/staged-install/$<CONFIG>"
    "-DCONFIG=$<CONFIG>"
    "-DWIN32_CONTRACT=${WIN32}"
    "-DSHARED_LIBRARY_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/stagedinstallcontracttest.cmake")
set_tests_properties(zzlogg_ui.staged_install_contract PROPERTIES
  RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME zzlogg_ui.linux_install_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP_CMAKE=${PROJECT_SOURCE_DIR}/src/app/CMakeLists.txt"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/linuxinstallcontracttest.cmake")

if(TARGET zzlogg_runtime_folder)
  add_test(NAME zzlogg_ui.runtime_folder_setup
    COMMAND "${CMAKE_COMMAND}"
      "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
      "-DCONFIG=$<CONFIG>"
      "-DRUNTIME_DIR=${ZZLOGG_RUNTIME_DIR_CONTRACT}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/runtimefoldersetup.cmake")
  set_tests_properties(zzlogg_ui.runtime_folder_setup PROPERTIES
    FIXTURES_SETUP zzlogg_runtime_folder
    RUN_SERIAL TRUE
    TIMEOUT 120)
  set_tests_properties(zzlogg_ui.output_contract PROPERTIES
    FIXTURES_REQUIRED zzlogg_runtime_folder)
endif()

if(WIN32)
  find_program(ZZLOGG_POWERSHELL_EXECUTABLE NAMES powershell pwsh REQUIRED)
  if(MSVC)
    get_filename_component(zzlogg_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(ZZLOGG_DUMPBIN_EXECUTABLE NAMES dumpbin
      HINTS "${zzlogg_compiler_directory}" REQUIRED)
    add_test(NAME zzlogg_ui.static_framework_imports
      COMMAND "${CMAKE_COMMAND}" "-DAPP=$<TARGET_FILE:klogg>"
        "-DDUMPBIN=${ZZLOGG_DUMPBIN_EXECUTABLE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/staticframeworktest.cmake")
  endif()
  add_test(NAME zzlogg_ui.version_resource_contract
    COMMAND "${CMAKE_COMMAND}"
      "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
      "-DCONFIG=$<CONFIG>"
      "-DPOWERSHELL=${ZZLOGG_POWERSHELL_EXECUTABLE}"
      "-DCONTRACT_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/versionresourcecontracttest.ps1"
      "-DMAIN_APP=$<TARGET_FILE:klogg>"
      "-DGREP_APP=$<TARGET_FILE:klogg_grep>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/versionresourcecontracttest.cmake")
  add_test(NAME zzlogg_ui.windows_installer_contract
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
      "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
      "-DCONFIG=$<CONFIG>"
      "-DRUNTIME_DIR=${ZZLOGG_RUNTIME_DIR_CONTRACT}"
      "-DPOWERSHELL=${ZZLOGG_POWERSHELL_EXECUTABLE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/windowsinstallercontracttest.cmake")
  set_tests_properties(zzlogg_ui.windows_installer_contract PROPERTIES
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
add_test(NAME zzlogg_ui.icon_assets
  COMMAND zzlogg_icon_asset_test -platform offscreen)

add_test(NAME zzlogg_ui.linux_icon_install_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/linux-icon-install-contract"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/linuxiconinstallcontracttest.cmake")
if(WIN32)
  set_property(TEST zzlogg_ui.icon_assets PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.icon_assets APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

add_executable(zzlogg_theme_configuration_test themeconfigurationtest.cpp)
target_link_libraries(zzlogg_theme_configuration_test
  PRIVATE klogg_settings Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_theme_configuration_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.theme_configuration
         COMMAND zzlogg_theme_configuration_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui.theme_configuration PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.theme_configuration APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

add_executable(zzlogg_settings_override_test settingsoverridetest.cpp)
target_link_libraries(zzlogg_settings_override_test
  PRIVATE klogg_settings Qt6::Test Qt6::Core)
set_target_properties(zzlogg_settings_override_test PROPERTIES AUTOMOC ON)
set_target_properties(zzlogg_settings_override_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/settings-override-contract")
add_test(NAME zzlogg_ui.settings_override
         COMMAND zzlogg_settings_override_test)
if(WIN32)
  set_property(TEST zzlogg_ui.settings_override APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

add_executable(zzlogg_brand_contract_test brandcontracttest.cpp)
target_link_libraries(zzlogg_brand_contract_test
  PRIVATE klogg_settings klogg_versioncheck klogg_crash_handler
          Qt6::Test Qt6::Core)
set_target_properties(zzlogg_brand_contract_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.brand_contract
         COMMAND zzlogg_brand_contract_test)
if(WIN32)
  set_property(TEST zzlogg_ui.brand_contract APPEND PROPERTY
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
add_test(NAME zzlogg_ui.options_theme
         COMMAND zzlogg_options_theme_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui.options_theme PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.options_theme APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_storage_settings_test storagesettingstest.cpp)
target_link_libraries(zzlogg_storage_settings_test PRIVATE
  klogg_ui klogg_storage Qt6::Test Qt6::Widgets)
target_include_directories(zzlogg_storage_settings_test PRIVATE
  "${CMAKE_BINARY_DIR}/src/ui/klogg_ui_autogen/include_$<CONFIG>")
set_target_properties(zzlogg_storage_settings_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_settings
  COMMAND zzlogg_storage_settings_test -platform offscreen)

add_executable(zzlogg_options_dialog_storage_paths_test
  optionsdialogstoragepathstest.cpp)
target_link_libraries(zzlogg_options_dialog_storage_paths_test PRIVATE
  klogg_ui Qt6::Test Qt6::Core)
set_target_properties(zzlogg_options_dialog_storage_paths_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.options_dialog_storage_paths
  COMMAND zzlogg_options_dialog_storage_paths_test)
if(WIN32)
  set_property(TEST zzlogg_ui.storage_settings PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.storage_settings APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
  set_property(TEST zzlogg_ui.options_dialog_storage_paths APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

add_executable(zzlogg_application_restart_path_test applicationrestartpathtest.cpp
  "${PROJECT_SOURCE_DIR}/src/app/applicationrestart.cpp")
target_link_libraries(zzlogg_application_restart_path_test PRIVATE Qt6::Test Qt6::Core)
target_include_directories(zzlogg_application_restart_path_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
set_target_properties(zzlogg_application_restart_path_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.application_restart_path
         COMMAND zzlogg_application_restart_path_test)

add_executable(zzlogg_application_smoke_paths_test
  applicationsmokepathstest.cpp
  "${PROJECT_SOURCE_DIR}/src/app/applicationsmokepaths.cpp")
target_include_directories(zzlogg_application_smoke_paths_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(zzlogg_application_smoke_paths_test PRIVATE Qt6::Test Qt6::Core)
set_target_properties(zzlogg_application_smoke_paths_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.application_smoke_paths
         COMMAND zzlogg_application_smoke_paths_test)

qt6_wrap_cpp(ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h")
set_source_files_properties(
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h"
  PROPERTIES SKIP_AUTOMOC ON)
add_executable(zzlogg_restart_contract_test restartcontracttest.cpp
  ${ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES}
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h")
target_link_libraries(zzlogg_restart_contract_test PRIVATE
  klogg_ui klogg_logdata klogg_versioncheck klogg_crash_handler
  klogg_storage roaring kdsingleapp Qt6::Test Qt6::Widgets)
target_include_directories(zzlogg_restart_contract_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app"
  "${CMAKE_BINARY_DIR}/src/ui/klogg_ui_autogen/include_$<CONFIG>")
set_target_properties(zzlogg_restart_contract_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.restart_contract
         COMMAND zzlogg_restart_contract_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui.restart_contract PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.restart_contract APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_search_panel_test searchpaneltest.cpp)
target_link_libraries(zzlogg_search_panel_test PRIVATE klogg_ui Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_search_panel_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.search_panel COMMAND zzlogg_search_panel_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui.search_panel PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.search_panel APPEND PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_document_workspace_test documentworkspacetest.cpp)
target_link_libraries(zzlogg_document_workspace_test PRIVATE klogg_ui Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_document_workspace_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.document_workspace
  COMMAND zzlogg_document_workspace_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui.document_workspace PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.document_workspace APPEND PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_document_tab_close_test documenttabclosetest.cpp)
target_link_libraries(zzlogg_document_tab_close_test PRIVATE
  klogg_ui Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_document_tab_close_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.document_tab_close
         COMMAND zzlogg_document_tab_close_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui.document_tab_close PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.document_tab_close APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_fluent_shell_test
  windowkittestmain.cpp windowchromebehaviortest.cpp)
target_link_libraries(zzlogg_fluent_shell_test PRIVATE
  klogg_ui Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_fluent_shell_test PROPERTIES AUTOMOC ON)
klogg_copy_ui_runtime_dlls(zzlogg_fluent_shell_test)
add_test(NAME zzlogg_ui.fluent_shell
         COMMAND zzlogg_fluent_shell_test)
if(WIN32)
  set_property(TEST zzlogg_ui.fluent_shell PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/..")
endif()

add_executable(zzlogg_runtime_contract_test runtimecontracttest.cpp
  ${ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES}
  "${PROJECT_SOURCE_DIR}/src/app/zzloggapplicationidentity.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/zzloggapplicationidentity.h"
  "${PROJECT_SOURCE_DIR}/src/app/klogg.qrc"
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h")
target_link_libraries(zzlogg_runtime_contract_test PRIVATE
  zzlogg_ui_runtime klogg_ui klogg_logdata klogg_versioncheck
  klogg_crash_handler roaring kdsingleapp Qt6::Test Qt6::Widgets)
target_include_directories(zzlogg_runtime_contract_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
set_target_properties(zzlogg_runtime_contract_test PROPERTIES
  AUTOMOC ON
  AUTORCC ON
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime-contract")
add_test(NAME zzlogg_ui.runtime_contract
         COMMAND zzlogg_runtime_contract_test)
if(WIN32)
  klogg_copy_ui_runtime_dlls(zzlogg_runtime_contract_test)
  set_property(TEST zzlogg_ui.runtime_contract PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/..")
endif()

add_test(NAME zzlogg_ui.application_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/application-smoke-runtime"
          "-DTEST_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/application-smoke-config"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/applicationsmoke.cmake")
set_tests_properties(zzlogg_ui.application_smoke PROPERTIES
  TIMEOUT 35)

add_test(NAME zzlogg_ui.smoke_isolation_contract
  COMMAND "${CMAKE_COMMAND}"
          "-DUI_TEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/smokeisolationcontracttest.cmake")
add_test(NAME zzlogg_ui.smoke_isolation_helper_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/smoke-isolation-helper-contract"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/smokeisolationhelpercontracttest.cmake")
add_test(NAME zzlogg_ui.storage_path_isolation_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/storagepathisolationcontracttest.cmake")
if(WIN32)
  add_test(NAME zzlogg_ui.manual_storage_acceptance_contract
    COMMAND "${ZZLOGG_POWERSHELL_EXECUTABLE}"
      -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/manualstorageacceptancecontracttest.ps1"
      -Launcher "${CMAKE_CURRENT_SOURCE_DIR}/manualstorageacceptance.ps1"
      -RepositoryRoot "${PROJECT_SOURCE_DIR}"
      -AllowedRoot "${PROJECT_SOURCE_DIR}/out/manual-storage-acceptance-contracts")
endif()

add_test(NAME zzlogg_ui.storage_application_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/storage-application-smoke"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/storageapplicationsmoke.cmake")
set_tests_properties(zzlogg_ui.storage_application_smoke PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 60)

if(WIN32)
  add_test(NAME zzlogg_ui.windows_local_runtime_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DAPP=$<TARGET_FILE:klogg>"
            "-DCONFIG=$<CONFIG>"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/local-runtime-contract"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/localruntimecontracttest.cmake")
  set_tests_properties(zzlogg_ui.windows_local_runtime_contract PROPERTIES
    TIMEOUT 20)
endif()

add_test(NAME zzlogg_ui.session_restore_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/session-smoke-config"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/sessionrestoresmoke.cmake")
set_tests_properties(zzlogg_ui.session_restore_smoke PROPERTIES TIMEOUT 45)

add_test(NAME zzlogg_ui.smoke_setup_diagnostics
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/smoke-setup-diagnostics"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/smokesetupdiagnosticstest.cmake")
set_tests_properties(zzlogg_ui.smoke_setup_diagnostics PROPERTIES TIMEOUT 15)

add_test(NAME zzlogg_ui.async_object_lifetime_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/async-object-lifetime-smoke"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/asyncobjectlifetimesmoke.cmake")
set_tests_properties(zzlogg_ui.async_object_lifetime_smoke PROPERTIES TIMEOUT 20)

add_test(NAME zzlogg_ui.titlebar_lifetime_smoke
  COMMAND "${CMAKE_COMMAND}"
          "-DAPP=$<TARGET_FILE:klogg>"
          "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/titlebar-lifetime-smoke"
          "-DFIRST_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-first.log"
          "-DSECOND_LOG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui-second.log"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/titlebarlifetimesmoke.cmake")
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
add_executable(zzlogg_window_chrome_test windowchrometest.cpp)
target_link_libraries(zzlogg_window_chrome_test PRIVATE klogg_ui Zz::FluentUI Zz::WindowKit Qt6::Test)
set_target_properties(zzlogg_window_chrome_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.window_chrome COMMAND zzlogg_window_chrome_test -platform offscreen)
if(WIN32)
  klogg_copy_ui_runtime_dlls(zzlogg_window_chrome_test)
  set_property(TEST zzlogg_ui.window_chrome PROPERTY ENVIRONMENT "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.window_chrome PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()
