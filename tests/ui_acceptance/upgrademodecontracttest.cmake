if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED BUILD_DIRECTORY)
  message(FATAL_ERROR "BUILD_DIRECTORY is required")
endif()
if(NOT DEFINED CONFIG)
  message(FATAL_ERROR "CONFIG is required")
endif()
if(NOT DEFINED POWERSHELL OR NOT EXISTS "${POWERSHELL}")
  message(FATAL_ERROR "POWERSHELL is required")
endif()

file(READ "${SOURCE_ROOT}/packaging/windows/ZzLogg.iss" inno_content)
string(REPLACE "\r\n" "\n" inno_content "${inno_content}")
string(REGEX REPLACE "(^|\n)[ \t]*(;|//)[^\n]*" "\\1" inno_active_content "${inno_content}")
# Strip standalone Pascal comment blocks as well; preserve Inno constants
# inside string literals, such as '{commonappdata}' and '{#VERSION}'.
string(REGEX REPLACE "(^|\n)[ \t]*\\{[^}]*\\}" "\\1" inno_active_content "${inno_active_content}")

# The coordinator passes only its private switch. Inno shows its language
# dialog before InitializeSetup, so early UI must be disabled explicitly.
foreach(required IN ITEMS "ShowLanguageDialog=no" "DisableStartupPrompt=yes")
  string(FIND "${inno_active_content}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Restricted entry may show UI before InitializeSetup: ${required}")
  endif()
endforeach()

function(inno_block name output_variable)
  string(REGEX MATCH "(function|procedure) ${name}[:;(]" declaration "${inno_active_content}")
  if(declaration STREQUAL "")
    message(FATAL_ERROR "Inno restricted entry is missing routine ${name}")
  endif()
  string(FIND "${inno_active_content}" "${declaration}" start)
  string(SUBSTRING "${inno_active_content}" ${start} -1 tail)
  string(REGEX MATCH "\n(function|procedure) " next_declaration "${tail}")
  if(NOT next_declaration STREQUAL "")
    string(FIND "${tail}" "${next_declaration}" finish)
    string(SUBSTRING "${tail}" 0 ${finish} tail)
  endif()
  set(${output_variable} "${tail}" PARENT_SCOPE)
endfunction()

function(require_order block_variable)
  set(offset 0)
  foreach(required IN LISTS ARGN)
    string(SUBSTRING "${${block_variable}}" ${offset} -1 remaining)
    string(FIND "${remaining}" "${required}" position)
    if(position EQUAL -1)
      message(FATAL_ERROR "Inno ${block_variable} is missing ordered contract: ${required}")
    endif()
    string(LENGTH "${required}" length)
    math(EXPR offset "${offset} + ${position} + ${length}")
  endforeach()
endfunction()

foreach(routine IN ITEMS InitializeSetup ValidLocator VerifyTarget PinDirectory PinPath
    ProtectedAcl CreateProtectedDirectory RunRestricted RestrictedExit)
  inno_block("${routine}" "${routine}")
endforeach()

# Both private switches are parsed from complete arguments before any wizard
# work. Unknown options (including /DIR and /LOADINF), duplicate modes and
# malformed locators reject with the established usage code.
require_order(InitializeSetup "ParamStr(I)" "'/zzloggupgrade='" "'/zzloggrecover='"
  "RejectArguments := True" "if RestrictedCount > 0"
  "RestrictedCount <> 1" "RejectArguments" "not ValidLocator(Locator)"
  "RestrictedExit(2," "RunRestricted(Mode, Locator)" "RestrictedExit(48," "Result := False")
require_order(ValidLocator "Length(Value) <> 16" "Value = '0000000000000000'"
  "'0123456789abcdef'" "Result := True")
require_order(RestrictedExit "ClosePins" "WinExitProcess(Code)")

# The updater and installed-release probe require the machine 64-bit identity
# and schema 2. An independent read-only target check precedes every write.
require_order(VerifyTarget "RegQueryStringValue(HKLM64, RegistrationKey, 'InstallLocation'"
  "RegQueryDWordValue(HKLM64, RegistrationKey, 'UpdateIdentitySchema'"
  "Schema <> 2" "PinPath(Target)" "'\\.zzlogg-install-root'"
  "'\\.zzlogg-files.manifest'" "'ZZTXMAN1'")
foreach(forbidden IN ITEMS "RegWrite" "CreateProtectedDirectory" "ExtractTemporaryFile" "Exec(")
  string(FIND "${VerifyTarget}" "${forbidden}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "Target verification must remain read-only: ${forbidden}")
  endif()
endforeach()
# Refuse reparse/device directories; open existing directories with read-only
# sharing (no write/delete sharing). Keep every ancestor pinned through engine exit.
require_order(PinDirectory "WinAttributes(Path)" "and $450" "$10"
  "WinCreateFile(Path, $80000000, 1, 0, 3, $02200000, 0)"
  "WinFileInformation" "WinFinalPath" "Info.VolumeSerial" "Pins[N] := Handle")
require_order(PinPath "StandardPath(Path)" "PinDirectory(Copy(Path, 1, 3)"
  "for I := 4 to Length(Path)" "PinDirectory(Copy(Path, 1, I - 1)"
  "PinDirectory(Path, TargetIdentity)")

# Create ACLs atomically and validate existing directories before extraction.
# BA/SY retain full control; AU may read. Writable inherited/non-admin ACEs
# are refused, and existing staging directories are never adopted.
require_order(CreateProtectedDirectory "if not AllowExisting then Exit"
  "ProtectedAcl(Path)" "WinParseSecurity('O:BAG:BAD:P(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)(A;OICI;FR;;;AU)'"
  "WinCreateDirectory(Path, Security)" "ProtectedAcl(Path)")
require_order(ProtectedAcl "WinGetSecurity" "WinSecurityString"
  "Owner <> 'BA'" "Owner <> 'SY'" "<> 'P'" "ReadOnlyMask")
require_order(RunRestricted "VerifyTarget" "RestrictedExit(42," "'{commonappdata}'"
  "'\\UpdateTransactions'" "CreateProtectedDirectory(Stage, False)"
  "ExtractTemporaryFile('ZzLoggUpdateTx.exe')")
require_order(RunRestricted "if Mode = 'upgrade'" "CreateProtectedDirectory(TxRoot, True)"
  "else begin" "PinPath(TxRoot)" "ProtectedAcl(TxRoot)" "if Mode = 'upgrade'")
foreach(required IN ITEMS "WinAttributes(TxDir)" "'\\journal.log'" "'\\staging-'"
    "ExtractTemporaryFile('files.manifest')" "--install" "--staging" "--txroot"
    "--txid" "--version" "--recover --install")
  require_order(RunRestricted "${required}")
endforeach()
require_order(RunRestricted "ewWaitUntilTerminated, ExitCode)"
  "RestrictedExit(48," "RestrictedExit(ExitCode,")
foreach(forbidden IN ITEMS "CreateProtectedDirectory(TxDir," "ForceDirectories(TxDir"
    "RegWrite" "CreateShellLink" "WizardForm" "MsgBox(")
  string(FIND "${RunRestricted}" "${forbidden}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "Restricted entry violates transaction ownership/UI isolation: ${forbidden}")
  endif()
endforeach()


# --- Landing-manifest generator: real PowerShell execution ---

set(manifest_generator "${SOURCE_ROOT}/packaging/windows/GenerateInstallerManifest.ps1")
if(NOT EXISTS "${manifest_generator}")
  message(FATAL_ERROR "Landing manifest generator is missing: ${manifest_generator}")
endif()

set(test_root "${BUILD_DIRECTORY}/tests/ui_acceptance/upgrade-mode-contract/${CONFIG}")
set(staging "${test_root}/release")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${staging}/plugins/nested")
file(WRITE "${staging}/zzlogg.exe" "main")
file(WRITE "${staging}/documentation.html" "docs")
file(WRITE "${staging}/libcrypto-1_1-x64.dll" "crypto")
file(WRITE "${staging}/libssl-1_1-x64.dll" "ssl")
file(WRITE "${staging}/plugins/cost$plugin.dll" "dollar")
file(WRITE "${staging}/plugins/nested/sample.dll" "nested")

# Generated files and legacy staging leftovers must not become payload entries.
file(WRITE "${staging}/.zzlogg-uninstall.nsh" "stale")
file(WRITE "${staging}/.zzlogg-files.manifest" "stale")

execute_process(
  COMMAND "${POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
          -File "${manifest_generator}"
          -StagingDirectory "${staging}"
  RESULT_VARIABLE manifest_result
  OUTPUT_VARIABLE manifest_output
  ERROR_VARIABLE manifest_error)
if(NOT manifest_result EQUAL 0)
  message(FATAL_ERROR
    "Landing manifest generation failed (${manifest_result}):\n"
    "${manifest_output}\n${manifest_error}")
endif()

# OrdinalIgnoreCase order over the lowercase fixtures is reproducible here.
set(expected_entries
  "documentation.html"
  "libcrypto-1_1-x64.dll"
  "libssl-1_1-x64.dll"
  "plugins/cost$plugin.dll"
  "plugins/nested/sample.dll"
  "zzlogg.exe")

function(le_hex value byte_count output_variable)
  set(result "")
  set(remaining ${value})
  foreach(byte_index RANGE 1 ${byte_count})
    math(EXPR byte_value "${remaining} % 256")
    math(EXPR remaining "${remaining} / 256")
    math(EXPR byte_hex "${byte_value}" OUTPUT_FORMAT HEXADECIMAL)
    string(REGEX REPLACE "^0x" "" byte_hex "${byte_hex}")
    if(byte_hex STREQUAL "")
      set(byte_hex "0")
    endif()
    string(LENGTH "${byte_hex}" byte_hex_length)
    if(byte_hex_length EQUAL 1)
      set(byte_hex "0${byte_hex}")
    endif()
    string(APPEND result "${byte_hex}")
  endforeach()
  if(NOT remaining EQUAL 0)
    message(FATAL_ERROR "le_hex value ${value} does not fit in ${byte_count} bytes")
  endif()
  set(${output_variable} "${result}" PARENT_SCOPE)
endfunction()

# Binary format shared with the transaction engine: "ZZTXMAN1", u32le version
# (1), u32le count, then per entry u32le utf8 path bytes, u64le size, 32 raw
# SHA-256 bytes, utf8 path with '/' separators.
set(expected_hex "5a5a54584d414e31")
le_hex(1 4 version_hex)
string(APPEND expected_hex "${version_hex}")
list(LENGTH expected_entries entry_count)
le_hex(${entry_count} 4 count_hex)
string(APPEND expected_hex "${count_hex}")
foreach(entry_path IN LISTS expected_entries)
  string(LENGTH "${entry_path}" entry_path_length)
  le_hex(${entry_path_length} 4 path_length_hex)
  file(READ "${staging}/${entry_path}" entry_content)
  string(LENGTH "${entry_content}" entry_size)
  le_hex(${entry_size} 8 entry_size_hex)
  file(SHA256 "${staging}/${entry_path}" entry_sha256)
  string(HEX "${entry_path}" entry_path_hex)
  string(APPEND expected_hex
    "${path_length_hex}${entry_size_hex}${entry_sha256}${entry_path_hex}")
endforeach()

set(landing_manifest "${staging}/.zzlogg-files.manifest")
if(NOT EXISTS "${landing_manifest}")
  message(FATAL_ERROR "Landing manifest was not generated")
endif()
file(READ "${landing_manifest}" actual_hex HEX)
if(NOT actual_hex STREQUAL expected_hex)
  string(LENGTH "${actual_hex}" actual_hex_length)
  string(LENGTH "${expected_hex}" expected_hex_length)
  message(FATAL_ERROR
    "Landing manifest bytes diverge from the staged tree "
    "(${actual_hex_length} vs ${expected_hex_length} hex digits):\n"
    "actual:   ${actual_hex}\nexpected: ${expected_hex}")
endif()

# --- CI packaging order: manifest and payload precede Inno compilation ---

set(action_path "${SOURCE_ROOT}/.github/actions/agent-package-win/action.yml")
file(READ "${action_path}" action_content)
string(REPLACE "\r\n" "\n" action_content "${action_content}")

set(runtime_stage_line
  [=[xcopy /e /i /y "%KLOGG_BUILD_ROOT%\runtime\RelWithDebInfo\ZzLogg-runtime" release]=])
set(payload_stage_line
  [=[xcopy /y "%KLOGG_BUILD_ROOT%\output\ZzLoggUpdateTx.exe" txpayload\]=])
set(landing_manifest_line
  [=[powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\GenerateInstallerManifest.ps1 -StagingDirectory release]=])
set(installer_step "Build-InnoInstaller.ps1")
set(last_position -1)
foreach(required_action_line IN ITEMS
    "${runtime_stage_line}"
    "${payload_stage_line}"
    "${landing_manifest_line}"
    "${installer_step}")
  string(FIND "${action_content}" "${required_action_line}" action_position)
  if(action_position EQUAL -1)
    message(FATAL_ERROR
      "Windows packaging action is missing: ${required_action_line}")
  endif()
  if(NOT action_position GREATER last_position)
    message(FATAL_ERROR
      "Windows packaging action order violation at: ${required_action_line}")
  endif()
  set(last_position ${action_position})
endforeach()
string(FIND "${action_content}" [=[ZzLoggUpdateTx.exe" release]=] engine_release_position)
if(NOT engine_release_position EQUAL -1)
  message(FATAL_ERROR
    "The transaction engine must stay payload-only, never staged into release")
endif()

message(STATUS
  "Inno restricted upgrade/recovery entry and the landing manifest satisfy the contract")
