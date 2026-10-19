add_test(NAME zzlogg_ui.version_display
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DBINARY_ROOT=${CMAKE_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/versiondisplaytest.cmake")

add_test(NAME zzlogg_ui.release_preset_contract
  COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/packaging/releasepresetcontracttest.cmake)

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
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/outputcontracttest.cmake")

add_test(NAME zzlogg_ui.output_platform_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DOUTPUT_CONTRACT=${CMAKE_CURRENT_SOURCE_DIR}/packaging/outputcontracttest.cmake"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/output-platform-contract"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/outputplatformcontracttest.cmake")

add_test(NAME zzlogg_ui.ci_toolchain_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/citoolchaincontracttest.cmake")

add_test(NAME zzlogg_ui.ci_compiler_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/cicompilercontracttest.cmake")

add_test(NAME zzlogg_ui.build_documentation_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/builddocumentationcontracttest.cmake")

add_test(NAME zzlogg_ui.linux_packaging_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linuxpackagingcontracttest.cmake")

add_test(NAME zzlogg_ui.linux_self_contained_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linuxselfcontainedcontracttest.cmake")

add_test(NAME zzlogg_ui.linux_package_validation_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linuxpackagevalidationcontracttest.cmake")

add_test(NAME zzlogg_ui.linux_package_integration_staging
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/linux-package-integration"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linuxpackageintegrationstagingtest.cmake")

add_test(NAME zzlogg_ui.linux_gnu_runtime_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linuxgnuruntimecontracttest.cmake")

add_test(NAME zzlogg_ui.staged_install_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
    "-DINSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/staged-install/$<CONFIG>"
    "-DCONFIG=$<CONFIG>"
    "-DWIN32_CONTRACT=${WIN32}"
    "-DUPDATER_DEPLOYED=$<TARGET_EXISTS:ZzLoggUpdate>"
    "-DSHARED_LIBRARY_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/stagedinstallcontracttest.cmake")
set_tests_properties(zzlogg_ui.staged_install_contract PROPERTIES
  RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME zzlogg_ui.linux_install_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP_CMAKE=${PROJECT_SOURCE_DIR}/src/app/CMakeLists.txt"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linuxinstallcontracttest.cmake")

if(TARGET zzlogg_runtime_folder)
  add_test(NAME zzlogg_ui.runtime_folder_setup
    COMMAND "${CMAKE_COMMAND}"
      "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
      "-DCONFIG=$<CONFIG>"
      "-DRUNTIME_DIR=${ZZLOGG_RUNTIME_DIR_CONTRACT}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/runtimefoldersetup.cmake")
  set_tests_properties(zzlogg_ui.runtime_folder_setup PROPERTIES
    FIXTURES_SETUP zzlogg_runtime_folder
    RUN_SERIAL TRUE
    TIMEOUT 120)
  set_tests_properties(zzlogg_ui.output_contract PROPERTIES
    FIXTURES_REQUIRED zzlogg_runtime_folder)
  add_test(NAME zzlogg_ui.lean_runtime_contract
    COMMAND "${CMAKE_COMMAND}" "-DRUNTIME_DIR=${ZZLOGG_RUNTIME_DIR_CONTRACT}"
      "-DCONFIG=$<CONFIG>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/leanruntimecontracttest.cmake")
  set_tests_properties(zzlogg_ui.lean_runtime_contract PROPERTIES
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
        -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/staticframeworktest.cmake")
  endif()
  add_test(NAME zzlogg_ui.version_resource_contract
    COMMAND "${CMAKE_COMMAND}"
      "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
      "-DCONFIG=$<CONFIG>"
      "-DPOWERSHELL=${ZZLOGG_POWERSHELL_EXECUTABLE}"
      "-DCONTRACT_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/packaging/versionresourcecontracttest.ps1"
      "-DMAIN_APP=$<TARGET_FILE:klogg>"
      "-DGREP_APP=$<TARGET_FILE:klogg_grep>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/versionresourcecontracttest.cmake")
  add_test(NAME zzlogg_ui.windows_installer_contract
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
      "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
      "-DCONFIG=$<CONFIG>"
      "-DRUNTIME_DIR=${ZZLOGG_RUNTIME_DIR_CONTRACT}"
      "-DPOWERSHELL=${ZZLOGG_POWERSHELL_EXECUTABLE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windowsinstallercontracttest.cmake")
  set_tests_properties(zzlogg_ui.windows_installer_contract PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120)
  add_test(NAME zzlogg_ui.upgrade_mode_contract
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
      "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
      "-DCONFIG=$<CONFIG>"
      "-DPOWERSHELL=${ZZLOGG_POWERSHELL_EXECUTABLE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/upgrademodecontracttest.cmake")
  set_tests_properties(zzlogg_ui.upgrade_mode_contract PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120)
endif()

add_executable(zzlogg_icon_asset_test
  packaging/iconassettest.cpp "${PROJECT_SOURCE_DIR}/src/app/klogg.qrc")
target_link_libraries(zzlogg_icon_asset_test PRIVATE
  zzlogg_brand Qt6::Test Qt6::Gui Qt6::Svg)
set_target_properties(zzlogg_icon_asset_test PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_definitions(zzlogg_icon_asset_test PRIVATE
  ZZLOGG_HICOLOR_ROOT="${PROJECT_SOURCE_DIR}/src/app/images/hicolor"
  ZZLOGG_ICO_PATH="${PROJECT_SOURCE_DIR}/packaging/assets/ZzLogg.ico"
  ZZLOGG_ICNS_PATH="${PROJECT_SOURCE_DIR}/packaging/assets/ZzLogg.icns")
add_test(NAME zzlogg_ui.icon_assets
  COMMAND zzlogg_icon_asset_test -platform offscreen)

add_test(NAME zzlogg_ui.linux_icon_install_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/linux-icon-install-contract"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linuxiconinstallcontracttest.cmake")
if(WIN32)
  set_property(TEST zzlogg_ui.icon_assets PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.icon_assets APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
