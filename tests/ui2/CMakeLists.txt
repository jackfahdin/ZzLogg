add_executable(zzlogg_theme_configuration_test themeconfigurationtest.cpp)
target_link_libraries(zzlogg_theme_configuration_test
  PRIVATE klogg_settings Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_theme_configuration_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.theme_configuration
         COMMAND zzlogg_theme_configuration_test -platform offscreen)
