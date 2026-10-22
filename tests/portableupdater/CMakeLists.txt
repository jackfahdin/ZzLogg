zzlogg_update_clean_legacy_crt_flags()
add_executable(zzlogg_portable_transaction_test transactiontest.cpp ../../src/portableupdater/transaction.cpp)
target_include_directories(zzlogg_portable_transaction_test PRIVATE ../../src/portableupdater)
target_compile_features(zzlogg_portable_transaction_test PRIVATE cxx_std_17)
add_test(NAME zzlogg_portable.transaction COMMAND zzlogg_portable_transaction_test)
add_executable(zzlogg_portable_package_test packagetest.cpp)
target_link_libraries(zzlogg_portable_package_test PRIVATE zzlogg_portableupdater klogg_karchive Qt6::Test)
set_target_properties(zzlogg_portable_package_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_portable.package COMMAND zzlogg_portable_package_test)
if(WIN32)
  set_property(TEST zzlogg_portable.package PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
if(WIN32)
  add_executable(zzlogg_portable_installer_test installertest_win.cpp)
  target_link_libraries(zzlogg_portable_installer_test PRIVATE
    zzlogg_portableupdater zzlogg_update_handoff klogg_karchive klogg_storage)
  zzlogg_update_runtime(zzlogg_portable_installer_test FALSE)
  add_dependencies(zzlogg_portable_installer_test ZzLoggPortableUpdate)
  add_test(NAME zzlogg_portable.installer COMMAND zzlogg_portable_installer_test
    "$<TARGET_FILE:ZzLoggPortableUpdate>" "$<TARGET_FILE:zzlogg_portable_restart_fixture>")
  set_tests_properties(zzlogg_portable.installer PROPERTIES TIMEOUT 120
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  add_executable(zzlogg_portable_restart_fixture restartfixture_win.cpp)
  add_dependencies(zzlogg_portable_installer_test zzlogg_portable_restart_fixture)
  add_executable(zzlogg_portable_helper_test helpertest_win.cpp)
  target_include_directories(zzlogg_portable_helper_test PRIVATE ../../src/portableupdater)
  target_link_libraries(zzlogg_portable_helper_test PRIVATE zzlogg_updater_handoff bcrypt)
  zzlogg_update_runtime(zzlogg_portable_restart_fixture TRUE)
  zzlogg_update_runtime(zzlogg_portable_helper_test TRUE)
  add_dependencies(zzlogg_portable_helper_test ZzLoggPortableUpdate zzlogg_portable_restart_fixture)
  add_test(NAME zzlogg_portable.helper COMMAND zzlogg_portable_helper_test
    "$<TARGET_FILE:ZzLoggPortableUpdate>" "$<TARGET_FILE:zzlogg_portable_restart_fixture>")
  set_tests_properties(zzlogg_portable.helper PROPERTIES TIMEOUT 100)
endif()

if(MSVC)
  get_filename_component(portable_tool_directory "${CMAKE_LINKER}" DIRECTORY)
  find_program(portable_dumpbin NAMES dumpbin HINTS "${portable_tool_directory}" REQUIRED)
  add_test(NAME zzlogg_portable.helper_dependencies
    COMMAND "${CMAKE_COMMAND}" "-DDUMPBIN=${portable_dumpbin}"
      "-DEXECUTABLE=$<TARGET_FILE:ZzLoggPortableUpdate>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../updater/dependencytest.cmake")
endif()
