find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
set(ZZLOGG_TRANSLATION_TEST_TS_FILES
  "${PROJECT_SOURCE_DIR}/src/app/i18n/en.ts"
  "${PROJECT_SOURCE_DIR}/src/app/i18n/zh_CN.ts"
  "${PROJECT_SOURCE_DIR}/src/app/i18n/zh_TW.ts")
qt6_add_translation(ZZLOGG_TRANSLATION_TEST_QM_FILES
  ${ZZLOGG_TRANSLATION_TEST_TS_FILES})
set(Qt_LCONVERT_EXECUTABLE Qt6::lconvert)
include(TranslationUtils)
add_app_translations_resource(ZZLOGG_TRANSLATION_TEST_APP_RES
  ${ZZLOGG_TRANSLATION_TEST_QM_FILES})
add_qt_translations_resource(ZZLOGG_TRANSLATION_TEST_QT_RES zh_CN zh_TW)
add_executable(zzlogg_application_translation_test
  app/applicationtranslationtest.cpp
  ${DOCUMENTATION_RESOURCE}
  "${PROJECT_SOURCE_DIR}/src/app/klogg.qrc"
  "${PROJECT_SOURCE_DIR}/src/app/applicationlanguage.cpp"
  ${ZZLOGG_TRANSLATION_TEST_APP_RES}
  ${ZZLOGG_TRANSLATION_TEST_QT_RES})
target_link_libraries(zzlogg_application_translation_test PRIVATE
  klogg_ui Qt6::Test Qt6::Widgets)
add_dependencies(zzlogg_application_translation_test zzlogg_documentation)
add_dependencies(zzlogg_update_check_ui_test zzlogg_application_translation_test)
target_include_directories(zzlogg_application_translation_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/app")
set_target_properties(zzlogg_application_translation_test PROPERTIES
  AUTOMOC ON AUTORCC ON)
target_compile_definitions(zzlogg_application_translation_test PRIVATE
  ZZLOGG_UI_CAPTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}/ui-captures")
add_test(NAME zzlogg_ui.application_translation
  COMMAND zzlogg_application_translation_test -platform offscreen)
add_test(NAME zzlogg_ui.application_translation_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/app/applicationtranslationcontracttest.cmake")
add_executable(zzlogg_runtime_translation_catalog_test
  app/runtimecatalogcontracttest.cpp
  app/runtimecatalogvalidator.cpp)
target_link_libraries(zzlogg_runtime_translation_catalog_test PRIVATE
  Qt6::Test Qt6::Core)
target_compile_definitions(zzlogg_runtime_translation_catalog_test PRIVATE
  ZZLOGG_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
set_target_properties(zzlogg_runtime_translation_catalog_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.runtime_translation_catalog_contract
  COMMAND zzlogg_runtime_translation_catalog_test)
if(WIN32)
  set_property(TEST zzlogg_ui.runtime_translation_catalog_contract APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
if(WIN32)
  set_property(TEST zzlogg_ui.application_translation PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui.application_translation APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()
