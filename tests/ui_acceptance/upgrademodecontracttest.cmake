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

set(nsis_path "${SOURCE_ROOT}/packaging/windows/ZzLogg.nsi")
file(READ "${nsis_path}" nsis_content)
string(REPLACE "\r\n" "\n" nsis_content "${nsis_content}")

# Static NSIS assertions are scoped to active code: strip full-line and
# trailing comments so contract words in commentary cannot pass.
string(REGEX REPLACE "(^|\n)[ \t]*[#;][^\n]*" "\\1" nsis_active_content
  "${nsis_content}")
string(REGEX REPLACE "[ \t]+[#;][^\n]*" "" nsis_active_content
  "${nsis_active_content}")

function(extract_nsis_block content_variable start_marker end_marker block_name output_variable)
  string(FIND "${${content_variable}}" "${start_marker}" block_start)
  if(block_start EQUAL -1)
    message(FATAL_ERROR "NSIS ${block_name} block is missing")
  endif()
  string(SUBSTRING "${${content_variable}}" ${block_start} -1 block_tail)
  string(FIND "${block_tail}" "${end_marker}" block_end)
  if(block_end EQUAL -1)
    message(FATAL_ERROR "NSIS ${block_name} block is unterminated")
  endif()
  string(LENGTH "${end_marker}" end_marker_length)
  math(EXPR block_length "${block_end} + ${end_marker_length}")
  string(SUBSTRING "${block_tail}" 0 ${block_length} block_content)
  set(${output_variable} "${block_content}" PARENT_SCOPE)
endfunction()

function(require_nsis_block_literal block_variable required_literal block_name)
  string(FIND "${${block_variable}}" "${required_literal}" literal_position)
  if(literal_position EQUAL -1)
    message(FATAL_ERROR
      "NSIS ${block_name} restricted-mode contract is missing: ${required_literal}")
  endif()
endfunction()

function(require_nsis_block_order block_variable block_name)
  set(search_offset 0)
  foreach(required_literal IN LISTS ARGN)
    string(SUBSTRING "${${block_variable}}" ${search_offset} -1 search_window)
    string(FIND "${search_window}" "${required_literal}" literal_position)
    if(literal_position EQUAL -1)
      message(FATAL_ERROR
        "NSIS ${block_name} restricted-mode contract is missing: ${required_literal}")
    endif()
    string(LENGTH "${required_literal}" literal_length)
    math(EXPR search_offset "${search_offset} + ${literal_position} + ${literal_length}")
  endforeach()
endfunction()

function(forbid_nsis_block_literal block_variable forbidden_literal block_name)
  string(FIND "${${block_variable}}" "${forbidden_literal}" literal_position)
  if(NOT literal_position EQUAL -1)
    message(FATAL_ERROR
      "NSIS ${block_name} restricted-mode contract forbids: ${forbidden_literal}")
  endif()
endfunction()

extract_nsis_block(nsis_active_content "Function .onInit" "FunctionEnd"
  "installer .onInit" installer_on_init)
extract_nsis_block(nsis_active_content "Function ZzLoggValidateLocator" "FunctionEnd"
  "locator validation" validate_locator)
extract_nsis_block(nsis_active_content "Function ZzLoggVerifyTarget" "FunctionEnd"
  "target recheck" verify_target)
extract_nsis_block(nsis_active_content "Function ZzLoggCheckRealDirectory" "FunctionEnd"
  "per-component reparse check" check_real_directory)
extract_nsis_block(nsis_active_content "Function ZzLoggRestrictedUpgrade" "FunctionEnd"
  "restricted upgrade" restricted_upgrade)
extract_nsis_block(nsis_active_content "Function ZzLoggRestrictedRecover" "FunctionEnd"
  "restricted recover" restricted_recover)

# Both restricted switches are parsed in .onInit and dispatched to handlers
# that Quit before any interactive page can display.
require_nsis_block_order(installer_on_init "installer .onInit"
  [=[${StrStr} $1 $0 " /D="]=]
  [=[${StrStr} $2 $0 "/ZzLoggUpgrade="]=]
  [=[StrCmp $1 "" zzlogg_upgrade_switch_ok]=]
  "Abort"
  [=[StrCpy $ZzLoggMode "upgrade"]=]
  [=[Call ZzLoggValidateLocator]=]
  [=[Call ZzLoggRestrictedUpgrade]=]
  "Quit"
  [=[${StrStr} $2 $0 "/ZzLoggRecover="]=]
  [=[StrCmp $1 "" zzlogg_recover_switch_ok]=]
  "Abort"
  [=[StrCpy $ZzLoggMode "recover"]=]
  [=[Call ZzLoggRestrictedRecover]=]
  "Quit")

# The bounded locator rejects separators, dot segments and quotes before use.
require_nsis_block_literal(validate_locator "StrLen $3 $ZzLoggLocator"
  "locator validation")
require_nsis_block_literal(validate_locator "IntCmp $3 8" "locator validation")
require_nsis_block_literal(validate_locator "IntCmp $3 65" "locator validation")
foreach(locator_bad_char IN ITEMS
    [=[${StrStr} $4 $ZzLoggLocator " "]=]
    [=[${StrStr} $4 $ZzLoggLocator '\']=]
    [=[${StrStr} $4 $ZzLoggLocator "/"]=]
    [=[${StrStr} $4 $ZzLoggLocator "."]=]
    [=[${StrStr} $4 $ZzLoggLocator '"']=])
  require_nsis_block_literal(validate_locator "${locator_bad_char}" "locator validation")
endforeach()

# The per-component reparse gate: GetFileAttributesW must refuse missing
# paths, reparse points and devices, and demand a real directory.
require_nsis_block_order(check_real_directory "per-component reparse check"
  [=[System::Call 'kernel32::GetFileAttributesW(w $R8) i .R9']=]
  [=[IntCmp $R9 -1 zzlogg_component_bad 0 zzlogg_component_bad]=]
  [=[IntOp $R7 $R9 & 0x440]=]
  [=[IntOp $R7 $R9 & 0x10]=])

# The independent recheck reads the registration in the 64-bit view, gates on
# schema 2, demands the marker and the landing manifest, rejects reparse
# points level by level, then pins the target directory capturing its
# identity. It never writes anything.
# The System::Call shapes are pinned verbatim: CreateFileW must open with
# GENERIC_READ, share READ|WRITE (delete denied), OPEN_EXISTING and
# FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT (0x02200000), and the
# BY_HANDLE_FILE_INFORMATION struct must carry all 13 DWORD members with the
# outputs on volume serial (8), file index high (12) and low (13).
require_nsis_block_order(verify_target "target recheck"
  [=[ReadRegStr $ZzLoggTarget HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "InstallLocation"]=]
  [=[ReadRegDWORD $3 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "UpdateIdentitySchema"]=]
  [=[StrCmp $3 2 0 zzlogg_verify_fail]=]
  [=[IfFileExists "$ZzLoggTarget\.zzlogg-install-root" 0 zzlogg_verify_fail]=]
  [=[IfFileExists "$ZzLoggTarget\.zzlogg-files.manifest" 0 zzlogg_verify_fail]=]
  [=[StrCpy $R8 $ZzLoggTarget 3]=]
  [=[Call ZzLoggCheckRealDirectory]=]
  [=[System::Call 'kernel32::CreateFileW(w $ZzLoggTarget, i 0x80000000, i 3, i 0, i 3, i 0x02200000, i 0) p .R0']=]
  [=[System::Call 'kernel32::GetFileInformationByHandle(p R0, *(i.R1, i, i, i, i, i, i, i.R2, i, i, i, i.R3, i.R4)) i .R5']=])
# Level-by-level walk: the drive root, every intermediate component and the
# full target each pass the reparse gate before the pin.
string(REGEX MATCHALL "Call ZzLoggCheckRealDirectory" walk_calls "${verify_target}")
list(LENGTH walk_calls walk_call_count)
if(NOT walk_call_count EQUAL 3)
  message(FATAL_ERROR
    "NSIS target recheck must apply the reparse gate to the drive root, every "
    "intermediate component and the full target (found ${walk_call_count} calls)")
endif()
foreach(verify_forbidden IN ITEMS
    "WriteRegStr" "WriteRegDWORD" "WriteRegExpandStr" "CreateDirectory"
    "CreateShortCut" "FileOpen" "ExecWait" "INSTDIR")
  forbid_nsis_block_literal(verify_target "${verify_forbidden}" "target recheck")
endforeach()

# Upgrade mode: the registration recheck precedes every write; the protected
# transaction directory is created exclusively, ACL-hardened, then the payload
# engine and the fresh manifest are extracted and the engine runs with its
# exit code propagated.
require_nsis_block_order(restricted_upgrade "restricted upgrade"
  [=[Call ZzLoggVerifyTarget]=]
  [=[IfFileExists "$ZzLoggTxDir" zzlogg_upgrade_txdir_busy 0]=]
  [=[CreateDirectory "$ZzLoggTxDir"]=]
  [=[nsExec::ExecToLog 'icacls "$ZzLoggTxDir" /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-545:(OI)(CI)R"']=]
  [=[CreateDirectory "$ZzLoggTxDir\staging"]=]
  [=[SetOutPath "$ZzLoggTxDir\staging"]=]
  [=[File "/oname=$ZzLoggTxDir\staging\ZzLoggUpdateTx.exe" "txpayload\ZzLoggUpdateTx.exe"]=]
  [=[File "/oname=$ZzLoggTxDir\staging\files.manifest" "release\.zzlogg-files.manifest"]=]
  [=[ExecWait '"$ZzLoggTxDir\staging\ZzLoggUpdateTx.exe" "$ZzLoggLocator"' $3]=]
  [=[SetErrorLevel $3]=]
  "Quit")
foreach(upgrade_forbidden IN ITEMS
    "StrCpy $INSTDIR" "MUI_PAGE" "CreateShortCut" "WriteRegStr" "WriteRegDWORD")
  forbid_nsis_block_literal(restricted_upgrade "${upgrade_forbidden}" "restricted upgrade")
endforeach()

# Recovery mode only ever targets an existing protected transaction directory
# with a journal; it never creates the transaction root itself.
require_nsis_block_order(restricted_recover "restricted recover"
  [=[Call ZzLoggVerifyTarget]=]
  [=[IfFileExists "$ZzLoggTxDir\journal.log" 0 zzlogg_recover_missing]=]
  [=[CreateDirectory "$ZzLoggTxDir\recover"]=]
  [=[File "/oname=$ZzLoggTxDir\recover\ZzLoggUpdateTx.exe" "txpayload\ZzLoggUpdateTx.exe"]=]
  [=[ExecWait '"$ZzLoggTxDir\recover\ZzLoggUpdateTx.exe" --recover --install "$ZzLoggTarget" --txroot "$APPDATA\ZzLogg\UpdateTransactions" --txid "$ZzLoggLocator"' $3]=]
  [=[SetErrorLevel $3]=]
  "Quit")
forbid_nsis_block_literal(restricted_recover [=[CreateDirectory "$ZzLoggTxDir"]=]
  "restricted recover")
foreach(recover_forbidden IN ITEMS
    "StrCpy $INSTDIR" "MUI_PAGE" "CreateShortCut" "WriteRegStr" "WriteRegDWORD")
  forbid_nsis_block_literal(restricted_recover "${recover_forbidden}" "restricted recover")
endforeach()

# --- Landing-manifest generator: real PowerShell execution ---

set(manifest_generator "${SOURCE_ROOT}/packaging/windows/GenerateNsisManifest.ps1")
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

# Stray leftovers of both generated names must be replaced, never enumerated.
file(WRITE "${staging}/.zzlogg-uninstall.nsh" "stale")
file(WRITE "${staging}/.zzlogg-files.manifest" "stale")

execute_process(
  COMMAND "${POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
          -File "${SOURCE_ROOT}/packaging/windows/GenerateNsisUninstallManifest.ps1"
          -StagingDirectory "${staging}"
  RESULT_VARIABLE uninstall_result
  OUTPUT_VARIABLE uninstall_output
  ERROR_VARIABLE uninstall_error)
if(NOT uninstall_result EQUAL 0)
  message(FATAL_ERROR
    "Uninstall manifest generation failed (${uninstall_result}):\n"
    "${uninstall_output}\n${uninstall_error}")
endif()
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

# Same-source consistency: the uninstall manifest deletes exactly the staged
# set plus the installer-owned extras, with NSIS escaping for '$'.
file(READ "${staging}/.zzlogg-uninstall.nsh" uninstall_content)
string(REPLACE "\r\n" "\n" uninstall_content "${uninstall_content}")
foreach(entry_path IN LISTS expected_entries)
  string(REPLACE "/" "\\" nsis_entry "${entry_path}")
  string(REPLACE "$" "$$" nsis_entry "${nsis_entry}")
  set(expected_delete [=[Delete "$INSTDIR\]=])
  string(APPEND expected_delete "${nsis_entry}\"")
  string(FIND "${uninstall_content}" "${expected_delete}" delete_position)
  if(delete_position EQUAL -1)
    message(FATAL_ERROR
      "Uninstall manifest omits a file the landing manifest owns: ${entry_path}")
  endif()
endforeach()
foreach(required_uninstall_literal IN ITEMS
    [=[Delete "$INSTDIR\.zzlogg-files.manifest"]=]
    [=[Delete "$INSTDIR\Uninstall.exe"]=]
    [=[Delete "$INSTDIR\.zzlogg-install-root"]=])
  string(FIND "${uninstall_content}" "${required_uninstall_literal}" literal_position)
  if(literal_position EQUAL -1)
    message(FATAL_ERROR
      "Uninstall manifest is missing installer-owned cleanup: ${required_uninstall_literal}")
  endif()
endforeach()
# The generated manifests are excluded from enumeration, so the landing
# manifest appears exactly once (the explicit cleanup line) and the NSIS
# uninstall include never deletes itself.
string(REGEX MATCHALL "zzlogg-files\\.manifest" manifest_mentions "${uninstall_content}")
list(LENGTH manifest_mentions manifest_mention_count)
if(NOT manifest_mention_count EQUAL 1)
  message(FATAL_ERROR
    "Uninstall manifest references the landing manifest ${manifest_mention_count} times")
endif()
string(FIND "${uninstall_content}" "zzlogg-uninstall.nsh" self_delete_position)
if(NOT self_delete_position EQUAL -1)
  message(FATAL_ERROR "Uninstall manifest enumerates itself")
endif()

# --- CI packaging order: manifests and payload precede makensis ---

set(action_path "${SOURCE_ROOT}/.github/actions/agent-package-win/action.yml")
file(READ "${action_path}" action_content)
string(REPLACE "\r\n" "\n" action_content "${action_content}")

set(runtime_stage_line
  [=[xcopy /e /i /y "%KLOGG_BUILD_ROOT%\runtime\RelWithDebInfo\ZzLogg-runtime" release]=])
set(payload_stage_line
  [=[xcopy /y "%KLOGG_BUILD_ROOT%\output\ZzLoggUpdateTx.exe" txpayload\]=])
set(uninstall_manifest_line
  [=[powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\GenerateNsisUninstallManifest.ps1 -StagingDirectory release]=])
set(landing_manifest_line
  [=[powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\GenerateNsisManifest.ps1 -StagingDirectory release]=])
set(installer_step "- name: Win installer")
set(last_position -1)
foreach(required_action_line IN ITEMS
    "${runtime_stage_line}"
    "${payload_stage_line}"
    "${uninstall_manifest_line}"
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
  "NSIS restricted upgrade/recovery entry and the landing manifest satisfy the contract")
