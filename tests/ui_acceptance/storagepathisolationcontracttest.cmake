if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(READ "${SOURCE_ROOT}/src/app/storagebootstrap.cpp" bootstrap_contents)
file(READ "${SOURCE_ROOT}/src/app/applicationrunner.cpp" runner_contents)
file(READ "${SOURCE_ROOT}/src/app/applicationsmokepaths.cpp" smoke_paths_contents)
file(READ "${SOURCE_ROOT}/src/ui/src/optionsdialog.cpp" options_contents)
set(manual_script
  "${SOURCE_ROOT}/tests/ui_acceptance/manualstorageacceptance.ps1")
if(NOT EXISTS "${manual_script}")
  message(FATAL_ERROR "Task 10 manual acceptance launcher is missing")
endif()
file(READ "${manual_script}" manual_script_contents)

string(REGEX MATCH
  "if[ \t\r\n]*\\([^\\)]*options\\.createUiRuntime"
  ui_runtime_gate "${runner_contents}")
if(NOT ui_runtime_gate STREQUAL "")
  message(FATAL_ERROR
    "storage isolation must not be disabled when Fluent UI runtime preparation fails")
endif()

foreach(required_startup_plan_fragment IN ITEMS
    "planApplicationSmokeStartup("
    "static_cast<bool>( options.createUiRuntime )"
    "const ApplicationSmokeRequest& uiSmoke = startupPlan.smokeRequest"
    "if ( startupPlan.createUiRuntime )")
  string(FIND "${runner_contents}" "${required_startup_plan_fragment}" required_index)
  if(required_index EQUAL -1)
    message(FATAL_ERROR
      "applicationrunner.cpp lost the testable runtime-independent smoke plan: ${required_startup_plan_fragment}")
  endif()
endforeach()

foreach(forbidden_bootstrap_fragment IN ITEMS
    "QSettings::UserScope"
    "legacyUserSettingsDirectory()")
  string(FIND "${bootstrap_contents}" "${forbidden_bootstrap_fragment}" forbidden_index)
  if(NOT forbidden_index EQUAL -1)
    message(FATAL_ERROR
      "storagebootstrap.cpp must not query a production legacy KnownFolder: ${forbidden_bootstrap_fragment}")
  endif()
endforeach()

foreach(required_runner_fragment IN ITEMS
    "legacyUserSettingsDirectory"
    "bootstrapStorage("
    "legacyUserSettingsDirectory,")
  string(FIND "${runner_contents}" "${required_runner_fragment}" required_index)
  if(required_index EQUAL -1)
    message(FATAL_ERROR
      "applicationrunner.cpp lost isolated legacy-path wiring: ${required_runner_fragment}")
  endif()
endforeach()

string(FIND "${smoke_paths_contents}"
  "QDir{ appConfigDirectory }.filePath("
  isolated_legacy_path_index)
if(isolated_legacy_path_index EQUAL -1)
  message(FATAL_ERROR
    "valid smoke overrides must derive an isolated legacy user-settings directory")
endif()

foreach(required_manual_fragment IN ITEMS
    "isManualIsolationSmokeMode( uiSmoke.mode )"
    "uiSmoke.requested && !isManualIsolationSmokeMode( uiSmoke.mode )"
    "startUiManualIsolationDeadline( app, uiSmoke.deadlineMs )")
  string(FIND "${runner_contents}" "${required_manual_fragment}" required_index)
  if(required_index EQUAL -1)
    message(FATAL_ERROR
      "applicationrunner.cpp lost non-automating manual isolation mode: ${required_manual_fragment}")
  endif()
endforeach()

foreach(required_options_fragment IN ITEMS
    "resolveOptionsDialogStoragePaths("
    "storage.runtimePaths()"
    "storagePaths_.appConfigDirectory"
    "storagePaths_.userDataDirectory")
  string(FIND "${options_contents}" "${required_options_fragment}" required_index)
  if(required_index EQUAL -1)
    message(FATAL_ERROR
      "optionsdialog.cpp lost installed storage-path wiring: ${required_options_fragment}")
  endif()
endforeach()

string(FIND "${options_contents}"
  "QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) ) )"
  eager_app_data_index)
string(FIND "${options_contents}"
  "QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) )"
  eager_app_config_index)
if(NOT eager_app_data_index EQUAL -1 OR NOT eager_app_config_index EQUAL -1)
  message(FATAL_ERROR "OptionsDialog still eagerly evaluates a QStandardPaths fallback")
endif()

foreach(required_manual_safety_fragment IN ITEMS
    "[System.IO.Path]::IsPathFullyQualified"
    "$environmentNames = @("
    "$savedEnvironment = @{}"
    "[System.Environment]::SetEnvironmentVariable"
    "try {"
    "finally {")
  string(FIND "${manual_script_contents}" "${required_manual_safety_fragment}" required_index)
  if(required_index EQUAL -1)
    message(FATAL_ERROR
      "manual acceptance launcher must restore the caller environment: ${required_manual_safety_fragment}")
  endif()
endforeach()

string(FIND "${manual_script_contents}" "[System.IO.Path]::IsPathRooted" rooted_only_index)
if(NOT rooted_only_index EQUAL -1)
  message(FATAL_ERROR
    "manual acceptance launcher must not accept drive-relative or root-relative paths")
endif()

string(FIND "${manual_script_contents}" "Get-Process -Name 'ZzLogg'" process_check_index)
string(FIND "${manual_script_contents}"
  "Set-ProcessEnvironment -Name 'APPDATA'"
  first_environment_mutation_index)
if(process_check_index EQUAL -1 OR first_environment_mutation_index EQUAL -1
    OR process_check_index GREATER first_environment_mutation_index)
  message(FATAL_ERROR
    "manual acceptance launcher must reject existing ZzLogg processes before mutating its environment")
endif()

string(FIND "${manual_script_contents}" "New-Item -ItemType Directory" first_create_index)
string(FIND "${manual_script_contents}" "Remove-Item -LiteralPath" first_remove_index)
if(first_create_index EQUAL -1 OR first_remove_index EQUAL -1
    OR process_check_index GREATER first_create_index
    OR process_check_index GREATER first_remove_index)
  message(FATAL_ERROR
    "manual acceptance launcher must reject existing ZzLogg processes before mutating scenario files")
endif()
