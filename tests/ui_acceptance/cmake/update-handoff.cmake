# Qt acceptance of the restricted update handoff controller: real KloggApp,
# real handoff fixture child process, real directory reservation. Built only
# where the dynamic-CRT handoff closure of the updater sources exists
# (Windows x64); other platforms keep the no-update compile boundary.
if(WIN32 AND TARGET zzlogg_update_handoff)
  add_executable(zzlogg_update_handoff_test update/updatehandofftest.cpp
    ${ZZLOGG_RUNTIME_CONTRACT_MOC_SOURCES}
    "${PROJECT_SOURCE_DIR}/src/app/kloggapp.h"
    "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.h"
    "${PROJECT_SOURCE_DIR}/src/app/applicationupdateguard.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/applicationupdatehandoff.h"
    "${PROJECT_SOURCE_DIR}/src/app/applicationupdatehandoff.cpp"
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatesession.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/messagereceiver.h")
  add_dependencies(zzlogg_update_handoff_test
    zzlogg_runtime_contract_moc zzlogg_updater_handofffixture_test)
  target_link_libraries(zzlogg_update_handoff_test PRIVATE
    klogg_ui klogg_logdata klogg_support klogg_storage roaring kdsingleapp
    zzlogg_update_handoff Qt6::Test Qt6::Widgets)
  target_include_directories(zzlogg_update_handoff_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app"
    "${PROJECT_SOURCE_DIR}/src/updater"
    "${CMAKE_BINARY_DIR}/src/ui/klogg_ui_autogen/include_$<CONFIG>")
  target_compile_definitions(zzlogg_update_handoff_test PRIVATE
    ZZLOGG_HAS_INSTALLATION_ACTIVITY)
  set_target_properties(zzlogg_update_handoff_test PROPERTIES AUTOMOC ON)
  add_test(NAME zzlogg_ui.update_handoff
           COMMAND zzlogg_update_handoff_test -platform offscreen
             -o "${CMAKE_CURRENT_BINARY_DIR}/update-handoff-$<CONFIG>.txt,txt")
  set_property(TEST zzlogg_ui.update_handoff PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/.."
    "ZZLOGG_HANDOFF_FIXTURE_EXE=$<TARGET_FILE:zzlogg_updater_handofffixture_test>")
  set_property(TEST zzlogg_ui.update_handoff APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
  set_tests_properties(zzlogg_ui.update_handoff PROPERTIES TIMEOUT 180)
endif()

# Deployed-update-helper artifact contract on the runtime folder: the helper
# is deployed by explicit target name, keeps its static-CRT Qt-free closure,
# rejects arbitrary arguments, and no test fixture is deployed.
if(WIN32 AND MSVC AND TARGET ZzLoggUpdate AND TARGET zzlogg_runtime_folder)
  add_test(NAME zzlogg_ui.runtime_updater_contract
    COMMAND "${CMAKE_COMMAND}"
      "-DRUNTIME_DIR=${ZZLOGG_RUNTIME_DIR_CONTRACT}"
      "-DDUMPBIN=${ZZLOGG_DUMPBIN_EXECUTABLE}"
      "-DUPDATER_NAME=$<TARGET_FILE_NAME:ZzLoggUpdate>"
      "-DFIXTURE_NAME=$<TARGET_FILE_NAME:zzlogg_updater_handofffixture_test>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/update/runtimeupdatercontracttest.cmake")
  set_tests_properties(zzlogg_ui.runtime_updater_contract PROPERTIES
    FIXTURES_REQUIRED zzlogg_runtime_folder
    RUN_SERIAL TRUE
    TIMEOUT 30)
endif()
