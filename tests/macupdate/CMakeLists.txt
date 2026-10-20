# Sparkle is downloaded and made available by the official application build.
if(APPLE AND TARGET zzlogg_sparkle_framework)
  add_executable(zzlogg_mac_update_exit_test
    macupdateexittest.mm ../../src/app/macupdatecontroller.mm)
  set_target_properties(zzlogg_mac_update_exit_test PROPERTIES
    OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
  target_compile_options(zzlogg_mac_update_exit_test PRIVATE -fobjc-arc)
  target_include_directories(zzlogg_mac_update_exit_test PRIVATE ../../src/app)
  target_link_libraries(zzlogg_mac_update_exit_test PRIVATE
    Qt6::Widgets zzlogg_sparkle_framework "-framework Cocoa")
  add_test(NAME zzlogg_update.mac_exit COMMAND zzlogg_mac_update_exit_test -platform offscreen)
  add_dependencies(ci_build zzlogg_mac_update_exit_test)
endif()
