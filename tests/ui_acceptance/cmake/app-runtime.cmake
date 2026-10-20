add_executable(zzlogg_brand_contract_test app/brandcontracttest.cpp)
target_link_libraries(zzlogg_brand_contract_test
  PRIVATE klogg_settings zzlogg_update_qt klogg_support
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

add_executable(zzlogg_application_restart_path_test app/applicationrestartpathtest.cpp
  "${PROJECT_SOURCE_DIR}/src/app/applicationrestart.cpp")
target_link_libraries(zzlogg_application_restart_path_test PRIVATE Qt6::Test Qt6::Core)
target_include_directories(zzlogg_application_restart_path_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
set_target_properties(zzlogg_application_restart_path_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.application_restart_path
         COMMAND zzlogg_application_restart_path_test)

add_executable(zzlogg_application_smoke_paths_test
  app/applicationsmokepathstest.cpp
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
# Both executables consume these generated files. Give generation one owner so
# parallel MSBuild projects cannot run moc against the same output concurrently.
add_custom_target(zzlogg_runtime_contract_moc
  DEPENDS ${ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES})
set_source_files_properties(
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h"
  PROPERTIES SKIP_AUTOMOC ON)
add_executable(zzlogg_restart_contract_test app/restartcontracttest.cpp
  ${ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES}
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.h"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatehandoff.h"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatehandoff.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatesession.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h")
target_link_libraries(zzlogg_restart_contract_test PRIVATE
  klogg_ui klogg_logdata klogg_support
  klogg_storage roaring kdsingleapp Qt6::Test Qt6::Widgets)
if(WIN32 AND TARGET zzlogg_update_handoff)
  target_link_libraries(zzlogg_restart_contract_test PRIVATE zzlogg_update_handoff)
endif()
target_include_directories(zzlogg_restart_contract_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app"
  "${PROJECT_SOURCE_DIR}/src/updater"
  "${CMAKE_BINARY_DIR}/src/ui/klogg_ui_autogen/include_$<CONFIG>")
set_target_properties(zzlogg_restart_contract_test PROPERTIES AUTOMOC ON)
add_dependencies(zzlogg_restart_contract_test zzlogg_runtime_contract_moc)
add_executable(zzlogg_update_integration_test
  ../updateqt/updatecheckintegrationsmoke.cpp ../updateqt/unconfiguredfeed.cpp
  ${ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES}
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.h"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatehandoff.h"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatehandoff.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatesession.cpp")
target_link_libraries(zzlogg_update_integration_test PRIVATE
  klogg_ui klogg_support kdsingleapp Qt6::Test Qt6::Widgets)
if(WIN32 AND TARGET zzlogg_update_handoff)
  target_link_libraries(zzlogg_update_integration_test PRIVATE zzlogg_update_handoff)
endif()
target_include_directories(zzlogg_update_integration_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app" "${PROJECT_SOURCE_DIR}/src/updater")
set_target_properties(zzlogg_update_integration_test PROPERTIES AUTOMOC ON)
add_dependencies(zzlogg_update_integration_test zzlogg_runtime_contract_moc)
add_test(NAME zzlogg_update.integration COMMAND zzlogg_update_integration_test -platform offscreen
  -o "${CMAKE_CURRENT_BINARY_DIR}/update-integration-$<CONFIG>.txt,txt" -o "-,txt")
set_property(TEST zzlogg_update.integration PROPERTY ENVIRONMENT
  "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
if(WIN32)
  set_property(TEST zzlogg_update.integration PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()
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

add_executable(zzlogg_runtime_contract_test app/runtimecontracttest.cpp
  ${ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES}
  "${PROJECT_SOURCE_DIR}/src/app/zzloggapplicationidentity.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/zzloggapplicationidentity.h"
  "${PROJECT_SOURCE_DIR}/src/app/klogg.qrc"
  "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.h"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatehandoff.h"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatehandoff.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatesession.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h")
add_dependencies(zzlogg_runtime_contract_test zzlogg_runtime_contract_moc)
foreach(update_app_test zzlogg_restart_contract_test zzlogg_update_integration_test zzlogg_runtime_contract_test)
  target_link_libraries(${update_app_test} PRIVATE zzlogg_portableupdater)
endforeach()
target_link_libraries(zzlogg_runtime_contract_test PRIVATE
  zzlogg_ui_runtime klogg_ui klogg_logdata
  klogg_support roaring kdsingleapp Qt6::Test Qt6::Widgets)
if(WIN32 AND TARGET zzlogg_update_handoff)
  target_link_libraries(zzlogg_runtime_contract_test PRIVATE zzlogg_update_handoff)
endif()
target_include_directories(zzlogg_runtime_contract_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app"
  "${PROJECT_SOURCE_DIR}/src/updater")
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

# Real-process verification that single-instance forwarding is scoped to the
# installation directory. Only built where the dynamic-CRT handoff closure of
# the updater sources exists (Windows x64); other platforms keep the legacy
# single-instance name and are not covered by this contract.
if(WIN32 AND TARGET zzlogg_update_handoff)
  add_executable(zzlogg_single_instance_scope_test app/singleinstancescopetest.cpp
    "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.h"
    "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.cpp")
  target_include_directories(zzlogg_single_instance_scope_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app" "${PROJECT_SOURCE_DIR}/src/updater")
  target_compile_definitions(zzlogg_single_instance_scope_test PRIVATE
    ZZLOGG_HAS_INSTALLATION_ACTIVITY)
  target_link_libraries(zzlogg_single_instance_scope_test PRIVATE
    kdsingleapp zzlogg_update_handoff Qt6::Test Qt6::Core Qt6::Network)
  set_target_properties(zzlogg_single_instance_scope_test PROPERTIES AUTOMOC ON)
  add_test(NAME zzlogg_ui.single_instance_scope
           COMMAND zzlogg_single_instance_scope_test)
  set_property(TEST zzlogg_ui.single_instance_scope APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
