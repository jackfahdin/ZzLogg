if(CMAKE_VERSION VERSION_LESS 3.23)
  message(FATAL_ERROR "KLOGG_BUILD_UI2_TESTS requires CMake 3.23 or newer")
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
