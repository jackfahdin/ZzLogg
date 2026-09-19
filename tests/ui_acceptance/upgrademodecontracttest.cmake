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
# that Quit before any interactive page can display. The raw command line is
# stashed in $ZzLoggCmdLine immediately after GetCommandLine because the
# INSTDIR block clobbers $0 via ReadRegStr (regression: restricted entries
# silently never matched when parsing the clobbered register).
require_nsis_block_order(installer_on_init "installer .onInit"
  [=[System::Call 'kernel32::GetCommandLine()t.r0']=]
  [=[StrCpy $ZzLoggCmdLine $0]=]
  [=[${StrStr} $1 $0 " /D="]=]
  [=[${StrStr} $2 $ZzLoggCmdLine "/ZzLoggUpgrade="]=]
  [=[StrCmp $1 "" zzlogg_upgrade_switch_ok]=]
  "Abort"
  [=[StrCpy $ZzLoggMode "upgrade"]=]
  [=[Call ZzLoggValidateLocator]=]
  [=[Call ZzLoggRestrictedUpgrade]=]
  "Quit"
  [=[${StrStr} $2 $ZzLoggCmdLine "/ZzLoggRecover="]=]
  [=[StrCmp $1 "" zzlogg_recover_switch_ok]=]
  "Abort"
  [=[StrCpy $ZzLoggMode "recover"]=]
  [=[Call ZzLoggRestrictedRecover]=]
  "Quit")
forbid_nsis_block_literal(installer_on_init
  [=[${StrStr} $2 $0 "/ZzLogg]=]
  "installer .onInit")

# The locator is byte-identical to the engine's parseTxid acceptance: exactly
# 16 lowercase hexadecimal digits, nonzero. IntCmp label order is (equal,
# less, greater): only an exactly-16 length falls into the scan init, both
# shorter and longer reject. The per-character scan extracts one character,
# requires it inside the lowercase hex alphabet (StrStr yields "" for any
# other character), and tracks whether any non-zero digit was seen; the final
# StrCmp rejects an all-zero locator.
require_nsis_block_literal(validate_locator "StrLen $3 $ZzLoggLocator"
  "locator validation")
require_nsis_block_literal(validate_locator
  "IntCmp $3 16 zzlogg_locator_scan_init zzlogg_locator_bad zzlogg_locator_bad"
  "locator validation")
require_nsis_block_order(validate_locator "locator validation"
  [=[StrCpy $4 $ZzLoggLocator 1 $5]=]
  [=[${StrStr} $7 "0123456789abcdef" $4]=]
  [=[StrCmp $7 "" zzlogg_locator_bad]=]
  [=[StrCmp $4 "0" zzlogg_locator_scan_next]=]
  [=[StrCpy $6 1]=])
require_nsis_block_literal(validate_locator
  "StrCmp $6 1 zzlogg_locator_ok zzlogg_locator_bad" "locator validation")

# The per-component reparse gate: GetFileAttributesW must refuse missing
# paths, reparse points and devices, and demand a real directory.
# Branch semantics: GetFileAttributesW failure returns exactly -1, so ONLY the
# equal label may reject — every existing directory yields a small positive
# attribute value (signed > -1) and must fall through both remaining labels.
# System::Call string inputs (t/w) take bare register names only: the $-form
# is passed as a literal string and never dereferenced (proven empirically:
# `w $R8` yields INVALID_FILE_ATTRIBUTES for an existing path, `w R8` works).
require_nsis_block_order(check_real_directory "per-component reparse check"
  [=[System::Call 'kernel32::GetFileAttributesW(w R8) i .R9']=]
  [=[IntCmp $R9 -1 zzlogg_component_bad 0 0]=]
  [=[IntOp $R7 $R9 & 0x440]=]
  [=[IntOp $R7 $R9 & 0x10]=])

# The independent recheck reads the registration in the 64-bit view, gates on
# schema 2, demands the marker and the landing manifest, rejects reparse
# points level by level, then pins the target directory capturing its
# identity. It never writes anything.
# The System::Call shapes are pinned verbatim: CreateFileW must open with
# GENERIC_READ, share READ|WRITE (delete denied), OPEN_EXISTING and
# FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT (0x02200000), and the
# BY_HANDLE_FILE_INFORMATION readback must use Alloc+deref (the inline struct
# literal form silently fails under NSIS 3.11).
# Branch semantics: CreateFileW failure returns exactly -1
# (INVALID_HANDLE_VALUE); only the equal label may reject, because every valid
# handle is a positive value (signed > -1) and must fall through.
require_nsis_block_order(verify_target "target recheck"
  [=[ReadRegStr $ZzLoggTarget HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "InstallLocation"]=]
  [=[ReadRegDWORD $3 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "UpdateIdentitySchema"]=]
  [=[StrCmp $3 2 0 zzlogg_verify_fail]=]
  [=[IfFileExists "$ZzLoggTarget\.zzlogg-install-root" 0 zzlogg_verify_fail]=]
  [=[IfFileExists "$ZzLoggTarget\.zzlogg-files.manifest" 0 zzlogg_verify_fail]=]
  [=[StrCpy $R8 $ZzLoggTarget 3]=]
  [=[Call ZzLoggCheckRealDirectory]=]
  [=[StrCpy $R8 $ZzLoggTarget]=]
  [=[System::Call 'kernel32::CreateFileW(w R8, i 0x80000000, i 3, i 0, i 3, i 0x02200000, i 0) p .R0']=]
  [=[IntCmp $R0 -1 zzlogg_verify_fail 0 0]=]
  [=[System::Alloc 52]=]
  [=[System::Call 'kernel32::GetFileInformationByHandle(p R0, p R7) i .R5']=]
  [=[System::Free $R7]=]
  [=[StrCmp $R5 1 0 zzlogg_verify_fail_pin]=])
# String inputs to System::Call must never use the $-form (literal-string trap).
string(REGEX MATCHALL "System::Call[^
]*" system_calls "${nsis_active_content}")
foreach(system_call IN LISTS system_calls)
  if(system_call MATCHES "\\([tw] \\$")
    message(FATAL_ERROR
      "System::Call string input uses $-form (passed as literal, never "
      "dereferenced): ${system_call}")
  endif()
endforeach()
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

# Upgrade mode: the registration recheck precedes every write. The journal
# directory belongs to the engine alone (TxJournal::open creates it
# exclusively), so the installer only probes it for the busy case and never
# creates it; the protected root is created and ACL-hardened once; staging is
# a hardened sibling directory that receives the payload engine, the fresh
# manifest and the entry log. The engine argv is the finalized 3C contract:
# only flag/value pairs with the non-secret parameters (target, staging, tx
# root, 16-hex locator as txid, payload version); credentials travel solely
# through the current-user-private credential file named by the locator.
# Branch semantics: IfFileExists takes (jump-if-exists, jump-if-not); the
# 0 second operand falls through. The root-exists probe jumps FORWARD over
# creation+hardening, so an existing root is never re-hardened, while a
# missing root is created and hardened exactly once before staging.
require_nsis_block_order(restricted_upgrade "restricted upgrade"
  [=[Call ZzLoggVerifyTarget]=]
  [=[IfFileExists "$ZzLoggTxDir" zzlogg_upgrade_txdir_busy 0]=]
  [=[IfFileExists "$ZzLoggTxRoot" zzlogg_upgrade_root_ready 0]=]
  [=[CreateDirectory "$ZzLoggTxRoot"]=]
  [=[nsExec::ExecToLog 'icacls "$ZzLoggTxRoot" /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-18:(OI)(CI)F" "*S-1-5-11:(OI)(CI)R"']=]
  [=[CreateDirectory "$ZzLoggStaging"]=]
  [=[nsExec::ExecToLog 'icacls "$ZzLoggStaging" /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-545:(OI)(CI)R"']=]
  [=[SetOutPath "$ZzLoggStaging"]=]
  [=[File "/oname=$ZzLoggStaging\ZzLoggUpdateTx.exe" "txpayload\ZzLoggUpdateTx.exe"]=]
  [=[File "/oname=$ZzLoggStaging\files.manifest" "release\.zzlogg-files.manifest"]=]
  [=[FileOpen $4 "$ZzLoggStaging\nsis-entry.log" w]=]
  [=[ExecWait '"$ZzLoggStaging\ZzLoggUpdateTx.exe" --install "$ZzLoggTarget" --staging "$ZzLoggStaging" --txroot "$ZzLoggTxRoot" --txid "$ZzLoggLocator" --version "${VERSION}"' $3]=]
  [=[SetErrorLevel $3]=]
  "Quit")
# The engine creates the journal directory exclusively; an installer-created
# one would be an adopted directory and defeat the Exists anti-replay
# semantics. The pre-task-5 flat-locator argv form stays forbidden.
forbid_nsis_block_literal(restricted_upgrade [=[CreateDirectory "$ZzLoggTxDir"]=]
  "restricted upgrade")
forbid_nsis_block_literal(restricted_upgrade [=[ZzLoggUpdateTx.exe" "$ZzLoggLocator"]=]
  "restricted upgrade")
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

# Restricted-entry failure exits: a bare Abort leaves the process exit code at
# 0, so every failure branch must SetErrorLevel first, and every MessageBox
# must carry /SD IDOK so silent mode (/S) takes the default instead of
# blocking on a dialog. The codes link the engine process contract
# (src/updater/txcontract_win_p.h): UsageRejected=2 for usage rejections
# (/D= in a restricted mode, malformed locator),
# exitForOutcome(TxOutcome::Rejected)=42 for the independent target recheck,
# InstallerRuntimeFailure=48 for installer-side runtime failures (busy
# transaction, protected-root/staging creation, missing journal).
function(require_guarded_aborts block_variable block_name expected_count expected_code)
  string(REGEX MATCHALL "SetErrorLevel ${expected_code}\n[ \t]*Abort"
    guarded_aborts "${${block_variable}}")
  list(LENGTH guarded_aborts guarded_count)
  string(REGEX MATCHALL "\n[ \t]*Abort" all_aborts "${${block_variable}}")
  list(LENGTH all_aborts abort_count)
  if(NOT abort_count EQUAL expected_count OR NOT guarded_count EQUAL expected_count)
    message(FATAL_ERROR
      "NSIS ${block_name}: expected ${expected_count} Abort(s) each guarded by "
      "SetErrorLevel ${expected_code} (found ${abort_count} Abort(s), "
      "${guarded_count} guarded)")
  endif()
endfunction()

function(require_messagebox_silent_default block_variable block_name)
  string(REGEX MATCHALL "MessageBox [^\n]*" boxes "${${block_variable}}")
  foreach(box IN LISTS boxes)
    string(FIND "${box}" "/SD IDOK" sd_position)
    if(sd_position EQUAL -1)
      message(FATAL_ERROR
        "NSIS ${block_name}: MessageBox without /SD IDOK blocks silent mode: ${box}")
    endif()
  endforeach()
endfunction()

require_guarded_aborts(installer_on_init "installer .onInit" 3 2)
require_guarded_aborts(verify_target "target recheck" 1 42)
require_guarded_aborts(restricted_upgrade "restricted upgrade" 2 48)
require_guarded_aborts(restricted_recover "restricted recover" 2 48)
foreach(sd_block IN ITEMS
    installer_on_init verify_target restricted_upgrade restricted_recover)
  require_messagebox_silent_default(${sd_block} "restricted entry")
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
