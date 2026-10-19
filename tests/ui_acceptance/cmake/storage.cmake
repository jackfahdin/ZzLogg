add_executable(zzlogg_storage_context_test storage/storagecontexttest.cpp)
target_link_libraries(zzlogg_storage_context_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_context_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_context
         COMMAND zzlogg_storage_context_test)

add_executable(zzlogg_storage_validator_test storage/storagevalidatortest.cpp)
target_link_libraries(zzlogg_storage_validator_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_validator_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_validator
         COMMAND zzlogg_storage_validator_test)

add_executable(zzlogg_storage_locator_test storage/storagelocatortest.cpp)
target_link_libraries(zzlogg_storage_locator_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_locator_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_locator
         COMMAND zzlogg_storage_locator_test)

add_executable(zzlogg_legacy_storage_test storage/legacystoragetest.cpp)
target_link_libraries(zzlogg_legacy_storage_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_legacy_storage_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.legacy_storage
         COMMAND zzlogg_legacy_storage_test)

add_executable(zzlogg_storage_migrator_test storage/storagemigratortest.cpp)
target_link_libraries(zzlogg_storage_migrator_test PRIVATE
  klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_migrator_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_migrator
         COMMAND zzlogg_storage_migrator_test
           -o "${CMAKE_CURRENT_BINARY_DIR}/storage-migrator-$<CONFIG>.txt,txt")

add_executable(zzlogg_storage_bootstrap_test
  storage/storagebootstraptest.cpp
  "${PROJECT_SOURCE_DIR}/src/app/storagebootstrap.cpp")
target_include_directories(zzlogg_storage_bootstrap_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(zzlogg_storage_bootstrap_test PRIVATE
  klogg_storage zzlogg_brand Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_bootstrap_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_bootstrap
         COMMAND zzlogg_storage_bootstrap_test)

add_executable(zzlogg_storage_location_ui_test storage/storagelocationuitest.cpp)
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

add_executable(zzlogg_storage_consumers_test storage/storageconsumerstest.cpp)
target_link_libraries(zzlogg_storage_consumers_test PRIVATE
  klogg_logging klogg_support klogg_storage Qt6::Test Qt6::Core)
set_target_properties(zzlogg_storage_consumers_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_consumers
         COMMAND zzlogg_storage_consumers_test)
if(WIN32)
  set_property(TEST zzlogg_ui.storage_consumers APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()

add_executable(zzlogg_settings_override_test storage/settingsoverridetest.cpp)
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

add_executable(zzlogg_storage_settings_test storage/storagesettingstest.cpp)
target_link_libraries(zzlogg_storage_settings_test PRIVATE
  klogg_ui klogg_storage Qt6::Test Qt6::Widgets)
target_include_directories(zzlogg_storage_settings_test PRIVATE
  "${CMAKE_BINARY_DIR}/src/ui/klogg_ui_autogen/include_$<CONFIG>")
set_target_properties(zzlogg_storage_settings_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.storage_settings
  COMMAND zzlogg_storage_settings_test -platform offscreen)

add_executable(zzlogg_options_dialog_storage_paths_test
  storage/optionsdialogstoragepathstest.cpp)
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
