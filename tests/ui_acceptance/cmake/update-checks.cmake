add_executable(zzlogg_update_session_factory_test update/updatesessionfactorytest.cpp
  "${PROJECT_SOURCE_DIR}/src/app/applicationupdatesession.cpp")
target_link_libraries(zzlogg_update_session_factory_test PRIVATE klogg_logging klogg_storage Qt6::Test)
target_include_directories(zzlogg_update_session_factory_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app"
  "${PROJECT_SOURCE_DIR}/src/updater")
if(WIN32 AND TARGET zzlogg_update_handoff)
  target_link_libraries(zzlogg_update_session_factory_test PRIVATE
    zzlogg_update_handoff)
endif()
set_target_properties(zzlogg_update_session_factory_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.update_session_factory
  COMMAND zzlogg_update_session_factory_test
    -o "${CMAKE_CURRENT_BINARY_DIR}/update-session-factory-$<CONFIG>.txt,txt" -o "-,txt")

add_executable(zzlogg_update_check_ui_test update/updatecheckuitest.cpp)
target_link_libraries(zzlogg_update_check_ui_test PRIVATE klogg_ui Qt6::Test)
target_include_directories(zzlogg_update_check_ui_test PRIVATE "${PROJECT_SOURCE_DIR}/src/update/src")
target_link_libraries(zzlogg_update_check_ui_test PRIVATE zzlogg_update_json zzlogg_update_monocypher)
target_compile_definitions(zzlogg_update_check_ui_test PRIVATE ZZLOGG_UI_QM_DIR="${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(zzlogg_update_check_ui_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.update_check COMMAND zzlogg_update_check_ui_test -platform offscreen
  -o "${CMAKE_CURRENT_BINARY_DIR}/update-check-$<CONFIG>.txt,txt" -o "-,txt")
set_property(TEST zzlogg_ui.update_check PROPERTY ENVIRONMENT
  "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
if(WIN32)
  set_property(TEST zzlogg_ui.update_check PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()
