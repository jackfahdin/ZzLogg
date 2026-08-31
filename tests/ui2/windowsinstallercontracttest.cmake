if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED BUILD_DIRECTORY)
  message(FATAL_ERROR "BUILD_DIRECTORY is required")
endif()
if(NOT DEFINED CONFIG)
  message(FATAL_ERROR "CONFIG is required")
endif()
if(NOT DEFINED PORTABLE_DIR)
  message(FATAL_ERROR "PORTABLE_DIR is required")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIRECTORY}" --config "${CONFIG}"
          --target klogg_portable_folder
  RESULT_VARIABLE portable_build_result
  OUTPUT_VARIABLE portable_build_output
  ERROR_VARIABLE portable_build_error)
if(NOT portable_build_result EQUAL 0)
  message(FATAL_ERROR
    "Failed to build portable folder (${portable_build_result}):\n"
    "${portable_build_output}\n${portable_build_error}")
endif()

if(CONFIG STREQUAL "Debug")
  set(qt_debug_suffix d)
else()
  set(qt_debug_suffix "")
endif()

set(portable_sentinels
  "icuuc.dll"
  "platforms/qwindows${qt_debug_suffix}.dll"
  "iconengines/qsvgicon${qt_debug_suffix}.dll"
  "imageformats/qsvg${qt_debug_suffix}.dll")
foreach(portable_sentinel IN LISTS portable_sentinels)
  if(NOT EXISTS "${PORTABLE_DIR}/${portable_sentinel}")
    message(FATAL_ERROR
      "Portable deployment is missing expected dependency-closure sentinel: ${portable_sentinel}")
  endif()
endforeach()
if(NOT EXISTS "${PORTABLE_DIR}/ZzLogg_portable.exe")
  message(FATAL_ERROR "Portable deployment is missing ZzLogg_portable.exe")
endif()

file(GLOB_RECURSE portable_files RELATIVE "${PORTABLE_DIR}" "${PORTABLE_DIR}/*")
list(LENGTH portable_files portable_file_count)
if(portable_file_count LESS 20)
  message(FATAL_ERROR
    "Portable deployment is unexpectedly small: ${portable_file_count} files")
endif()

set(action_path "${SOURCE_ROOT}/.github/actions/agent-package-win/action.yml")
file(READ "${action_path}" action_content)
string(REPLACE "\r\n" "\n" action_content "${action_content}")

set(portable_stage_line
  [=[xcopy /e /i /y "%KLOGG_BUILD_ROOT%\portable\RelWithDebInfo\ZzLogg-portable" release]=])
set(portable_remove_line [=[del /q release\ZzLogg_portable.exe]=])
string(FIND "${action_content}" "${portable_stage_line}" portable_stage_position)
string(FIND "${action_content}" "${portable_remove_line}" portable_remove_position)
if(portable_stage_position EQUAL -1 OR portable_remove_position EQUAL -1
   OR portable_remove_position LESS portable_stage_position)
  message(FATAL_ERROR
    "Windows staging must copy the portable tree, then remove ZzLogg_portable.exe")
endif()

set(required_staging_lines
  [=[xcopy /y "%KLOGG_BUILD_ROOT%\output\ZzLogg.exe" release]=]
  [=[xcopy /y "%KLOGG_BUILD_ROOT%\output\ZzLogg_crashpad_handler.exe" release]=]
  [=[xcopy /y "%KLOGG_BUILD_ROOT%\output\ZzLogg_minidump_dump.exe" release]=]
  [=[xcopy /y "%KLOGG_BUILD_ROOT%\generated\documentation.html" release]=]
  [=[xcopy /y "%SSL_DIR%\libcrypto-1_1-x64.dll" release]=]
  [=[xcopy /y "%SSL_DIR%\libssl-1_1-x64.dll" release]=])
foreach(required_staging_line IN LISTS required_staging_lines)
  string(FIND "${action_content}" "${required_staging_line}" staging_line_position)
  if(staging_line_position EQUAL -1)
    message(FATAL_ERROR
      "Windows staging omits required installer addition: ${required_staging_line}")
  endif()
endforeach()

set(nsis_path "${SOURCE_ROOT}/packaging/windows/ZzLogg.nsi")
file(READ "${nsis_path}" nsis_content)
string(REPLACE "\r\n" "\n" nsis_content "${nsis_content}")

set(recursive_file_line [=[File /r "release\*.*"]=])
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
  [=[IfFileExists "$INSTDIR\.zzlogg-install-root" 0 unsafe_install_dir]=]
  [=[IfFileExists "$INSTDIR\ZzLogg.exe" 0 unsafe_install_dir]=]
  [=[StrCmp "$INSTDIR" "$PROGRAMFILES" unsafe_install_dir]=]
  [=[StrCmp "$INSTDIR" "$PROGRAMFILES64" unsafe_install_dir]=]
  [=[SetOutPath "$TEMP"]=]
  [=[RMDir /r "$INSTDIR"]=]
  [=[unsafe_install_dir:]=]
  [=[Abort]=])
foreach(required_uninstall_literal IN LISTS required_uninstall_literals)
  string(FIND "${nsis_content}" "${required_uninstall_literal}" uninstall_position)
  if(uninstall_position EQUAL -1)
    message(FATAL_ERROR
      "NSIS recursive uninstall safety contract is missing: ${required_uninstall_literal}")
  endif()
endforeach()

message(STATUS
  "NSIS recursively consumes the complete portable-based staging tree and removes it safely")
