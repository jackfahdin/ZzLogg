if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(READ "${SOURCE_ROOT}/src/app/applicationrunner.cpp" runner_contents)
file(READ "${SOURCE_ROOT}/src/app/CMakeLists.txt" app_cmake_contents)
file(READ "${SOURCE_ROOT}/src/ui/src/mainwindow.cpp" main_window_contents)

string(FIND "${runner_contents}"
  "preBootstrapLanguage( QLocale::system() )" pre_bootstrap_language_index)
string(FIND "${runner_contents}"
  "MainWindow::installLanguage( bootstrapLanguage )" pre_bootstrap_install_index)
string(FIND "${runner_contents}"
  "const auto storageResult = bootstrapStorage(" bootstrap_index)
string(FIND "${runner_contents}"
  "const auto& config = Configuration::getSynced();" configuration_index)
string(FIND "${runner_contents}"
  "MainWindow::installLanguage( config.language() )" configured_install_index)

foreach(required_index IN ITEMS
    pre_bootstrap_language_index pre_bootstrap_install_index bootstrap_index
    configuration_index configured_install_index)
  if(${required_index} EQUAL -1)
    message(FATAL_ERROR "Missing startup translation contract: ${required_index}")
  endif()
endforeach()

if(NOT pre_bootstrap_language_index LESS pre_bootstrap_install_index
   OR NOT pre_bootstrap_install_index LESS bootstrap_index)
  message(FATAL_ERROR
    "The system-locale application translator must be installed before storage bootstrap UI")
endif()
if(NOT bootstrap_index LESS configuration_index
   OR NOT configuration_index LESS configured_install_index)
  message(FATAL_ERROR
    "Configuration language must be read and installed only after storage bootstrap succeeds")
endif()

string(FIND "${app_cmake_contents}"
  "add_qt_translations_resource(KLOGG_QT_TRANSLATION_RES zh_CN zh_TW)"
  qt_traditional_resource_index)
if(qt_traditional_resource_index EQUAL -1)
  message(FATAL_ERROR "The application must bundle the zh_TW Qt translator")
endif()

string(FIND "${main_window_contents}"
  "if ( qtTranslations.isValid() )" optional_qt_translator_index)
string(FIND "${main_window_contents}"
  "if ( !appTranslations.isValid() || !mTranslator.load( appPath ) )"
  required_app_translator_index)
if(optional_qt_translator_index EQUAL -1 OR required_app_translator_index EQUAL -1)
  message(FATAL_ERROR
    "Application translation must tolerate a missing Qt catalog but reject a missing app catalog")
endif()
