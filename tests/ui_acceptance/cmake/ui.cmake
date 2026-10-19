add_executable(zzlogg_text_cache_test ui/textcachetest.cpp)
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

add_executable(zzlogg_line_number_ui_test ui/linenumberuitest.cpp)
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
  ui/linenumberconfigurationtest.cpp)
target_link_libraries(zzlogg_line_number_configuration_test PRIVATE
  klogg_settings Qt6::Test Qt6::Core)
set_target_properties(zzlogg_line_number_configuration_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.line_number_configuration
  COMMAND zzlogg_line_number_configuration_test)

add_executable(zzlogg_theme_configuration_test ui/themeconfigurationtest.cpp)
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

add_executable(zzlogg_options_theme_test ui/optionsthemetest.cpp)
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

add_executable(zzlogg_search_panel_test ui/searchpaneltest.cpp)
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

add_executable(zzlogg_document_workspace_test ui/documentworkspacetest.cpp)
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

add_executable(zzlogg_document_tab_close_test ui/documenttabclosetest.cpp)
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
  ui/windowkittestmain.cpp ui/windowchromebehaviortest.cpp)
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

add_executable(zzlogg_window_chrome_test ui/windowchrometest.cpp)
target_link_libraries(zzlogg_window_chrome_test PRIVATE klogg_ui Zz::FluentUI Zz::WindowKit Qt6::Test)
set_target_properties(zzlogg_window_chrome_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.window_chrome COMMAND zzlogg_window_chrome_test -platform offscreen)
if(WIN32)
  klogg_copy_ui_runtime_dlls(zzlogg_window_chrome_test)
  set_property(TEST zzlogg_ui.window_chrome PROPERTY ENVIRONMENT "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.window_chrome PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()
