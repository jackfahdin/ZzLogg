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

set(manifest_generator
  "${SOURCE_ROOT}/packaging/windows/GenerateNsisUninstallManifest.ps1")
if(NOT EXISTS "${manifest_generator}")
  message(FATAL_ERROR "NSIS uninstall manifest generator is missing: ${manifest_generator}")
endif()

set(manifest_test_root
  "${BUILD_DIRECTORY}/tests/ui_acceptance/windows-installer-manifest-contract/${CONFIG}")
set(manifest_staging "${manifest_test_root}/release")
file(REMOVE_RECURSE "${manifest_test_root}")
file(MAKE_DIRECTORY "${manifest_staging}")
file(COPY "${RUNTIME_DIR}/" DESTINATION "${manifest_staging}")
file(MAKE_DIRECTORY "${manifest_staging}/plugins/nested")
file(WRITE "${manifest_staging}/ZzLogg.exe" "main")
file(WRITE "${manifest_staging}/documentation.html" "docs")
file(WRITE "${manifest_staging}/libcrypto-1_1-x64.dll" "crypto")
file(WRITE "${manifest_staging}/libssl-1_1-x64.dll" "ssl")
file(WRITE "${manifest_staging}/plugins/cost$plugin.dll" "dollar")
file(WRITE "${manifest_staging}/plugins/nested/sample.dll" "nested")

execute_process(
  COMMAND "${POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
          -File "${manifest_generator}"
          -StagingDirectory "${manifest_staging}"
  RESULT_VARIABLE manifest_result
  OUTPUT_VARIABLE manifest_output
  ERROR_VARIABLE manifest_error)
if(NOT manifest_result EQUAL 0)
  message(FATAL_ERROR
    "NSIS uninstall manifest generation failed (${manifest_result}):\n"
    "${manifest_output}\n${manifest_error}")
endif()

set(uninstall_manifest "${manifest_staging}/.zzlogg-uninstall.nsh")
if(NOT EXISTS "${uninstall_manifest}")
  message(FATAL_ERROR "NSIS uninstall manifest was not generated")
endif()
file(READ "${uninstall_manifest}" manifest_content)
string(REPLACE "\r\n" "\n" manifest_content "${manifest_content}")

file(GLOB_RECURSE staged_files RELATIVE "${manifest_staging}" "${manifest_staging}/*")
set(staged_directories)
foreach(staged_file IN LISTS staged_files)
  if(staged_file STREQUAL ".zzlogg-uninstall.nsh")
    continue()
  endif()
  string(REPLACE "/" "\\" nsis_file "${staged_file}")
  string(REPLACE "$" "$$" nsis_file "${nsis_file}")
  set(expected_delete [=[Delete "$INSTDIR\]=])
  string(APPEND expected_delete "${nsis_file}\"")
  string(FIND "${manifest_content}" "${expected_delete}" delete_position)
  if(delete_position EQUAL -1)
    message(FATAL_ERROR
      "Generated uninstall manifest omits staged file: ${staged_file}")
  endif()
  get_filename_component(staged_directory "${staged_file}" DIRECTORY)
  while(NOT staged_directory STREQUAL "")
    list(APPEND staged_directories "${staged_directory}")
    get_filename_component(staged_directory "${staged_directory}" DIRECTORY)
  endwhile()
endforeach()
list(REMOVE_DUPLICATES staged_directories)
foreach(staged_directory IN LISTS staged_directories)
  string(REPLACE "/" "\\" nsis_directory "${staged_directory}")
  string(REPLACE "$" "$$" nsis_directory "${nsis_directory}")
  set(expected_rmdir [=[RMDir "$INSTDIR\]=])
  string(APPEND expected_rmdir "${nsis_directory}\"")
  string(FIND "${manifest_content}" "${expected_rmdir}" rmdir_position)
  if(rmdir_position EQUAL -1)
    message(FATAL_ERROR
      "Generated uninstall manifest omits staged directory: ${staged_directory}")
  endif()
endforeach()

set(required_manifest_literals
  [=[Delete "$INSTDIR\plugins\cost$$plugin.dll"]=]
  [=[Delete "$INSTDIR\Uninstall.exe"]=]
  [=[Delete "$INSTDIR\.zzlogg-install-root"]=]
  [=[RMDir "$INSTDIR\plugins\nested"]=]
  [=[RMDir "$INSTDIR\plugins"]=]
  [=[RMDir "$INSTDIR"]=])
foreach(required_manifest_literal IN LISTS required_manifest_literals)
  string(FIND "${manifest_content}" "${required_manifest_literal}" manifest_literal_position)
  if(manifest_literal_position EQUAL -1)
    message(FATAL_ERROR
      "Generated uninstall manifest is missing: ${required_manifest_literal}")
  endif()
endforeach()
string(FIND "${manifest_content}" [=[RMDir "$INSTDIR\plugins\nested"]=]
  nested_directory_position)
string(FIND "${manifest_content}" [=[RMDir "$INSTDIR\plugins"]=]
  parent_directory_position)
if(nested_directory_position GREATER parent_directory_position)
  message(FATAL_ERROR "Generated uninstall directories are not deepest-first")
endif()
string(FIND "${manifest_content}" "RMDir /r" recursive_manifest_position)
if(NOT recursive_manifest_position EQUAL -1)
  message(FATAL_ERROR "Generated uninstall manifest contains recursive deletion")
endif()

set(manifest_action_line
  [=[powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\GenerateNsisUninstallManifest.ps1 -StagingDirectory release]=])
string(FIND "${action_content}" "${manifest_action_line}" manifest_action_position)
string(FIND "${action_content}" "- name: Win installer" installer_step_position)
if(manifest_action_position EQUAL -1 OR installer_step_position EQUAL -1
   OR manifest_action_position LESS last_staging_position
   OR manifest_action_position GREATER installer_step_position)
  message(FATAL_ERROR
    "Windows action must generate the uninstall manifest after staging and before makensis")
endif()

set(nsis_path "${SOURCE_ROOT}/packaging/windows/ZzLogg.nsi")
file(READ "${nsis_path}" nsis_content)
string(REPLACE "\r\n" "\n" nsis_content "${nsis_content}")

# Static NSIS identity assertions are intentionally scoped to active code blocks.
# Strip full-line and trailing comments so contract words in commentary cannot pass.
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
      "NSIS ${block_name} identity contract is missing: ${required_literal}")
  endif()
endfunction()

foreach(required_literal IN ITEMS
    "InstallDirRegKey HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZzLogg\" \"InstallLocation\""
    "SetRegView 64"
    "\"UpdateIdentitySchema\" 1")
  string(FIND "${nsis_active_content}" "${required_literal}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing installer identity contract: ${required_literal}")
  endif()
endforeach()

extract_nsis_block(nsis_active_content "Function .onInit" "FunctionEnd"
  "installer .onInit" installer_on_init)
extract_nsis_block(nsis_active_content "Function un.onInit" "FunctionEnd"
  "uninstaller un.onInit" uninstaller_on_init)
extract_nsis_block(nsis_active_content
  [=[Section "ZzLogg application and runtime" zzlogg]=] "SectionEnd"
  "application install section" application_install_section)
extract_nsis_block(nsis_active_content [=[Section "Uninstall"]=] "SectionEnd"
  "uninstall section" uninstall_section)

set(expected_reg_view_selection [=[!ifdef ARCH32
    SetRegView 32
!else
    SetRegView 64
!endif]=])
require_nsis_block_literal(installer_on_init "${expected_reg_view_selection}"
  "installer .onInit")
require_nsis_block_literal(uninstaller_on_init "${expected_reg_view_selection}"
  "uninstaller un.onInit")

set(uninstall_identity_key_delete
  [=[DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"]=])
set(legacy_identity_key_delete [=[DeleteRegKey HKLM "Software\ZzLogg"]=])
set(file_association_cleanup
  [=[${unregisterExtension} ".log" "ZzLogg log file"]=])
foreach(uninstall_identity_literal IN ITEMS
    "${uninstall_identity_key_delete}"
    "SetRegView 32"
    "${legacy_identity_key_delete}"
    "${expected_reg_view_selection}"
    "${file_association_cleanup}")
  require_nsis_block_literal(uninstall_section "${uninstall_identity_literal}"
    "uninstall section")
endforeach()
string(FIND "${uninstall_section}" "${uninstall_identity_key_delete}"
  uninstall_identity_delete_position)
string(FIND "${uninstall_section}" "SetRegView 32" legacy_view_position)
string(FIND "${uninstall_section}" "${legacy_identity_key_delete}"
  legacy_identity_delete_position)
string(FIND "${uninstall_section}" "${expected_reg_view_selection}"
  restored_view_position)
string(FIND "${uninstall_section}" "${file_association_cleanup}"
  file_association_cleanup_position)
if(NOT uninstall_identity_delete_position LESS legacy_view_position
   OR NOT legacy_view_position LESS legacy_identity_delete_position
   OR NOT legacy_identity_delete_position LESS restored_view_position
   OR NOT restored_view_position LESS file_association_cleanup_position)
  message(FATAL_ERROR
    "NSIS uninstall must delete the new identity in the architecture view, "
    "clean the legacy key in the 32-bit view, then restore the architecture view")
endif()

set(installer_directory_read
  [=[ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "InstallLocation"]=])
foreach(installer_init_literal IN ITEMS
    [=[System::Call 'kernel32::GetCommandLine()t.r0']=]
    [=[${StrStr} $1 $0 " /D="]=]
    [=[StrCmp $1 "" 0 zzlogg_on_init_done]=]
    "${installer_directory_read}"
    [=[StrCpy $INSTDIR "$0"]=])
  require_nsis_block_literal(installer_on_init "${installer_init_literal}"
    "installer .onInit")
endforeach()
string(FIND "${installer_on_init}" "SetRegView 64" installer_view_position)
string(FIND "${installer_on_init}" [=[${StrStr} $1 $0 " /D="]=]
  installer_override_check_position)
string(FIND "${installer_on_init}" "${installer_directory_read}"
  installer_directory_read_position)
if(installer_view_position GREATER installer_directory_read_position
   OR installer_override_check_position GREATER installer_directory_read_position)
  message(FATAL_ERROR
    "NSIS installer must select its registry view and preserve /D= before directory readback")
endif()

foreach(application_identity_literal IN ITEMS
    [=["InstallLocation" "$INSTDIR"]=]
    [=[WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "UpdateIdentitySchema" 1]=])
  require_nsis_block_literal(application_install_section
    "${application_identity_literal}" "application install section")
endforeach()

set(recursive_file_line [=[File /r /x .zzlogg-uninstall.nsh "release\*.*"]=])
string(REGEX MATCHALL "\n[ \t]+File[ \t][^\n]*" nsis_file_lines "${nsis_content}")
list(LENGTH nsis_file_lines nsis_file_line_count)
if(NOT nsis_file_line_count EQUAL 1)
  message(FATAL_ERROR
    "NSIS must consume the complete release staging tree with one recursive File command")
endif()
list(GET nsis_file_lines 0 actual_file_line)
string(STRIP "${actual_file_line}" actual_file_line)
if(NOT actual_file_line STREQUAL recursive_file_line)
  message(FATAL_ERROR
    "NSIS does not recursively consume the release staging tree: ${actual_file_line}")
endif()

foreach(forbidden_section IN ITEMS
    [=[Section "Qt 6 Runtime libraries"]=]
    [=[Section "MSVC Runtime libraries"]=])
  string(FIND "${nsis_content}" "${forbidden_section}" forbidden_section_position)
  if(NOT forbidden_section_position EQUAL -1)
    message(FATAL_ERROR "NSIS retains a split runtime section: ${forbidden_section}")
  endif()
endforeach()

set(required_uninstall_literals
  [=[FileOpen $0 "$INSTDIR\.zzlogg-install-root" w]=]
  [=[FileWrite $0 "ZzLogg ${VERSION}$\r$\n"]=]
  [=[FileClose $0]=]
  [=[!include "release\.zzlogg-uninstall.nsh"]=])
foreach(required_uninstall_literal IN LISTS required_uninstall_literals)
  string(FIND "${nsis_content}" "${required_uninstall_literal}" uninstall_position)
  if(uninstall_position EQUAL -1)
    message(FATAL_ERROR
      "NSIS recursive uninstall safety contract is missing: ${required_uninstall_literal}")
  endif()
endforeach()

string(FIND "${nsis_content}" "RMDir /r" recursive_delete_position)
if(NOT recursive_delete_position EQUAL -1)
  message(FATAL_ERROR "NSIS must never recursively delete the installation directory")
endif()

message(STATUS
  "NSIS installs the complete staging tree and uninstalls only installer-owned paths")
