if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED BUILD_DIRECTORY)
  message(FATAL_ERROR "BUILD_DIRECTORY is required")
endif()
if(NOT DEFINED CONFIG)
  message(FATAL_ERROR "CONFIG is required")
endif()
if(NOT DEFINED RUNTIME_DIR)
  message(FATAL_ERROR "RUNTIME_DIR is required")
endif()
if(NOT DEFINED POWERSHELL OR NOT EXISTS "${POWERSHELL}")
  message(FATAL_ERROR "POWERSHELL is required")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIRECTORY}" --config "${CONFIG}"
          --target zzlogg_runtime_folder
  RESULT_VARIABLE runtime_build_result
  OUTPUT_VARIABLE runtime_build_output
  ERROR_VARIABLE runtime_build_error)
if(NOT runtime_build_result EQUAL 0)
  message(FATAL_ERROR
    "Failed to build runtime folder (${runtime_build_result}):\n"
    "${runtime_build_output}\n${runtime_build_error}")
endif()

if(CONFIG STREQUAL "Debug")
  set(qt_debug_suffix d)
else()
  set(qt_debug_suffix "")
endif()

set(runtime_sentinels
  "licenses/ZzLogg/COPYING"
  "licenses/ZzLogg/NOTICE"
  "licenses/ZzPureTools/LICENSE"
  "licenses/ZzPureTools/THIRD_PARTY_NOTICES.md"
  "licenses/ZzPureTools/release-evidence.json"
  "licenses/ZzPureTools/ZzLog/LICENSE"
  "licenses/ZzPureTools/ZzLog/spdlog/LICENSE.txt"
  "licenses/ZzPureTools/ZzLog/fmt/LICENSE.txt"
  "licenses/ZzPureTools/qwindowkit/LICENSE"
  "icuuc.dll"
  "tbb12.dll"
  "platforms/qwindows${qt_debug_suffix}.dll"
  "iconengines/qsvgicon${qt_debug_suffix}.dll"
  "imageformats/qsvg${qt_debug_suffix}.dll")
foreach(runtime_sentinel IN LISTS runtime_sentinels)
  if(NOT EXISTS "${RUNTIME_DIR}/${runtime_sentinel}")
    message(FATAL_ERROR
      "Runtime deployment is missing expected dependency-closure sentinel: ${runtime_sentinel}")
  endif()
endforeach()
if(NOT EXISTS "${RUNTIME_DIR}/ZzLogg.exe")
  message(FATAL_ERROR "Runtime deployment is missing ZzLogg.exe")
endif()
foreach(forbidden_executable IN ITEMS ZzLogg_portable.exe ZzLogg_ui2.exe)
  if(EXISTS "${RUNTIME_DIR}/${forbidden_executable}")
    message(FATAL_ERROR "Runtime deployment contains legacy GUI: ${forbidden_executable}")
  endif()
endforeach()

file(GLOB_RECURSE runtime_files RELATIVE "${RUNTIME_DIR}" "${RUNTIME_DIR}/*")
foreach(runtime_file IN LISTS runtime_files)
  get_filename_component(runtime_name "${runtime_file}" NAME)
  string(TOLOWER "${runtime_name}" runtime_name)
  if(runtime_name MATCHES "^(zz[a-z0-9_]*|qwk[a-z0-9_]*)\\.dll$")
    message(FATAL_ERROR "Static framework deployment contains a DLL: ${runtime_file}")
  endif()
endforeach()
list(LENGTH runtime_files runtime_file_count)
if(runtime_file_count LESS 20)
  message(FATAL_ERROR
    "Runtime deployment is unexpectedly small: ${runtime_file_count} files")
endif()

set(action_path "${SOURCE_ROOT}/.github/actions/agent-package-win/action.yml")
file(READ "${action_path}" action_content)
string(REPLACE "\r\n" "\n" action_content "${action_content}")

set(runtime_stage_line
  [=[xcopy /e /i /y "%KLOGG_BUILD_ROOT%\runtime\RelWithDebInfo\ZzLogg-runtime" release]=])
string(FIND "${action_content}" "${runtime_stage_line}" runtime_stage_position)
if(runtime_stage_position EQUAL -1)
  message(FATAL_ERROR "Windows staging must copy the unified runtime tree")
endif()
foreach(forbidden_staging_line IN ITEMS
    [=[del /q release\ZzLogg_portable.exe]=]
    [=[xcopy /y "%KLOGG_BUILD_ROOT%\output\ZzLogg.exe" release]=])
  string(FIND "${action_content}" "${forbidden_staging_line}" forbidden_staging_position)
  if(NOT forbidden_staging_position EQUAL -1)
    message(FATAL_ERROR
      "Windows staging retains a legacy multi-executable step: ${forbidden_staging_line}")
  endif()
endforeach()

set(required_staging_lines
  [=[xcopy /y "%KLOGG_BUILD_ROOT%\generated\documentation.html" release]=]
  [=[xcopy /y "%SSL_DIR%\libcrypto-1_1-x64.dll" release]=]
  [=[xcopy /y "%SSL_DIR%\libssl-1_1-x64.dll" release]=])
set(last_staging_position ${runtime_stage_position})
foreach(required_staging_line IN LISTS required_staging_lines)
  string(FIND "${action_content}" "${required_staging_line}" staging_line_position)
  if(staging_line_position EQUAL -1)
    message(FATAL_ERROR
      "Windows staging omits required installer addition: ${required_staging_line}")
  endif()
  if(staging_line_position GREATER last_staging_position)
    set(last_staging_position ${staging_line_position})
  endif()
endforeach()

# Inno consumes this complete tree and records installed paths in its own
# uninstall log; the updater's landing manifest remains an independent ABI.
set(manifest_generator
  "${SOURCE_ROOT}/packaging/windows/GenerateInstallerManifest.ps1")
set(manifest_test_root
  "${BUILD_DIRECTORY}/tests/ui_acceptance/windows-installer-manifest-contract/${CONFIG}")
set(manifest_staging "${manifest_test_root}/release")
file(REMOVE_RECURSE "${manifest_test_root}")
file(MAKE_DIRECTORY "${manifest_staging}")
file(COPY "${RUNTIME_DIR}/" DESTINATION "${manifest_staging}")
file(WRITE "${manifest_staging}/documentation.html" "docs")
file(WRITE "${manifest_staging}/libcrypto-1_1-x64.dll" "crypto")
file(WRITE "${manifest_staging}/libssl-1_1-x64.dll" "ssl")
execute_process(
  COMMAND "${POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
          -File "${manifest_generator}" -StagingDirectory "${manifest_staging}"
  RESULT_VARIABLE manifest_result
  OUTPUT_VARIABLE manifest_output
  ERROR_VARIABLE manifest_error)
if(NOT manifest_result EQUAL 0)
  message(FATAL_ERROR
    "Runtime landing manifest generation failed (${manifest_result}):\n"
    "${manifest_output}\n${manifest_error}")
endif()
file(READ "${manifest_staging}/.zzlogg-files.manifest" manifest_header LIMIT 16 HEX)
if(NOT manifest_header MATCHES "^5a5a54584d414e3101000000[0-9a-f]+$")
  message(FATAL_ERROR "Runtime landing manifest has an invalid header: ${manifest_header}")
endif()

set(manifest_action_line "GenerateInstallerManifest.ps1 -StagingDirectory release")
string(FIND "${action_content}" "${manifest_action_line}" manifest_action_position)
string(FIND "${action_content}" "Build-InnoInstaller.ps1" installer_position)
if(manifest_action_position EQUAL -1 OR installer_position EQUAL -1
   OR manifest_action_position LESS last_staging_position
   OR manifest_action_position GREATER installer_position)
  message(FATAL_ERROR
    "Windows action must generate the landing manifest after staging and before Inno compilation")
endif()

file(READ "${SOURCE_ROOT}/packaging/windows/ZzLogg.iss" inno_content)
string(REPLACE "\r\n" "\n" inno_content "${inno_content}")
# Strip full-line comments. Semicolons terminate Pascal statements and divide
# Inno entry fields, so treating them as trailing comments would hide code.
string(REGEX REPLACE "(^|\n)[ \t]*(;|//)[^\n]*" "\\1"
  inno_active_content "${inno_content}")

function(require_inno_literal required_literal)
  string(FIND "${inno_active_content}" "${required_literal}" literal_position)
  if(literal_position EQUAL -1)
    message(FATAL_ERROR "Inno installer contract is missing: ${required_literal}")
  endif()
endfunction()

foreach(required_literal IN ITEMS
    "ArchitecturesInstallIn64BitMode=x64compatible"
    "PrivilegesRequired=admin"
    "CreateUninstallRegKey=no"
    [=[Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg]=]
    "InstallLocation" "UpdateIdentitySchema" "DisplayVersion"
    ".zzlogg-install-root" ".zzlogg-files.manifest"
    "recursesubdirs" "createallsubdirs"
    "uninsdeletekey" "HKLM64")
  require_inno_literal("${required_literal}")
endforeach()

require_inno_literal([=[ValueType: dword; ValueName: "UpdateIdentitySchema"; ValueData: "2"]=])
require_inno_literal([=[ValueName: "InstallLocation"; ValueData: "{app}"]=])
require_inno_literal([=[ValueName: "UninstallString"; ValueData: """{uninstallexe}"""]=])
require_inno_literal("DefaultDirName={code:DefaultInstallDir}")
require_inno_literal("RegQueryStringValue(HKLM64, RegistrationKey, 'InstallLocation', Result)")
require_inno_literal("'ZzLogg {#VERSION}' + #13#10")

# Exactly one recursive payload tree plus the two restricted-entry resources:
# no separate Qt/runtime components and no transaction helper in {app}.
string(REGEX MATCHALL "(^|\n)Source:" file_entries "${inno_active_content}")
list(LENGTH file_entries file_entry_count)
if(NOT file_entry_count EQUAL 3)
  message(FATAL_ERROR "Inno must embed one runtime tree and two restricted-entry resources")
endif()
require_inno_literal([=[Source: "release\*"; DestDir: "{app}"]=])
require_inno_literal([=[Source: "txpayload\ZzLoggUpdateTx.exe"; Flags: dontcopy]=])
require_inno_literal([=[Source: "release\.zzlogg-files.manifest"; DestName: "files.manifest"; Flags: dontcopy]=])
require_inno_literal([=[Excludes: ".zzlogg-uninstall.nsh"]=])

# The association task is opt-in and restores the previous default only while
# still owned by ZzLogg, including migration of the old NSIS backup value.
require_inno_literal([=[Name: "associate"; Description: "{cm:AssociateLog}"; Flags: unchecked]=])
require_inno_literal("'PreviousLogHadValue'")
require_inno_literal("'PreviousLogAssociation'")
require_inno_literal("'backup_val'")
require_inno_literal("(Current = 'ZzLogg.LogFile') or (Current = 'ZzLogg log file')")
require_inno_literal("RegWriteStringValue(HKLM64, 'Software\\Classes\\.log', '', Previous)")

# Refuse installation-root recursive deletion and user-data cleanup. Inno's
# uninstall log removes only owned paths; custom roots/settings survive.
foreach(forbidden_pattern IN ITEMS
    "Type:[ \t]*filesandordirs"
    "DelTree[ \t]*\\("
    "\\{userappdata\\}.*ZzLogg.ini"
    "\\{userappdata\\}.*ZzLogg.session")
  if(inno_active_content MATCHES "${forbidden_pattern}")
    message(FATAL_ERROR "Inno uninstall violates owned-path cleanup: ${forbidden_pattern}")
  endif()
endforeach()
# A legacy uninstaller may be removed as a file, but must never execute: its
# cleanup would delete settings and files now owned by the Inno installation.
if(inno_active_content MATCHES "Exec[^\n]*[Uu]ninstall\\.exe")
  message(FATAL_ERROR "Migration must not run the legacy NSIS uninstaller")
endif()

message(STATUS
  "Inno consumes the complete runtime tree and preserves installer identity and user data")
