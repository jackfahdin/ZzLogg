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

set(nsis_path "${SOURCE_ROOT}/packaging/windows/ZzLogg.nsi")
file(READ "${nsis_path}" nsis_content)
string(REPLACE "\r\n" "\n" nsis_content "${nsis_content}")

string(FIND "${nsis_content}" "Section \"Qt 6 Runtime libraries\"" qt_section_start)
if(qt_section_start EQUAL -1)
  message(FATAL_ERROR "NSIS Qt 6 runtime section is missing")
endif()
string(SUBSTRING "${nsis_content}" ${qt_section_start} -1 qt_section_tail)
string(FIND "${qt_section_tail}" "SectionEnd" qt_section_length)
if(qt_section_length EQUAL -1)
  message(FATAL_ERROR "NSIS Qt 6 runtime section is unterminated")
endif()
string(SUBSTRING "${qt_section_tail}" 0 ${qt_section_length} qt_section)

string(REPLACE "\n" ";" qt_section_lines "${qt_section}")
set(actual_runtime_entries)
set(runtime_file_prefix [=[File release\]=])
string(LENGTH "${runtime_file_prefix}" runtime_file_prefix_length)
foreach(file_line IN LISTS qt_section_lines)
  string(STRIP "${file_line}" file_line)
  string(FIND "${file_line}" "${runtime_file_prefix}" runtime_file_position)
  if(NOT runtime_file_position EQUAL 0)
    continue()
  endif()
  string(SUBSTRING "${file_line}" ${runtime_file_prefix_length} -1 runtime_entry)
  string(REPLACE "\\" "/" runtime_entry "${runtime_entry}")
  list(APPEND actual_runtime_entries "${runtime_entry}")
endforeach()

set(expected_runtime_entries
  Qt6Core.dll
  Qt6Gui.dll
  Qt6Network.dll
  Qt6Widgets.dll
  Qt6Xml.dll
  Qt6Core5Compat.dll
  Qt6Svg.dll
  platforms/qwindows.dll
  styles/qmodernwindowsstyle.dll
  iconengines/qsvgicon.dll)

if(NOT actual_runtime_entries STREQUAL expected_runtime_entries)
  list(JOIN expected_runtime_entries ", " expected_report)
  list(JOIN actual_runtime_entries ", " actual_report)
  message(FATAL_ERROR
    "NSIS Qt runtime manifest differs from the portable deployment contract.\n"
    "Expected: ${expected_report}\nActual: ${actual_report}")
endif()

foreach(directory_entry IN ITEMS
    "platforms|qwindows.dll"
    "styles|qmodernwindowsstyle.dll"
    "iconengines|qsvgicon.dll")
  string(REPLACE "|" ";" directory_fields "${directory_entry}")
  list(GET directory_fields 0 runtime_directory)
  list(GET directory_fields 1 runtime_file)
  set(expected_install_block
    "SetOutPath $INSTDIR\\${runtime_directory}\n    File release\\${runtime_directory}\\${runtime_file}")
  string(FIND "${qt_section}" "${expected_install_block}" install_block_position)
  if(install_block_position EQUAL -1)
    message(FATAL_ERROR
      "NSIS runtime entry is not installed into ${runtime_directory}: ${runtime_file}")
  endif()
endforeach()

foreach(runtime_entry IN LISTS actual_runtime_entries)
  if(NOT CONFIG STREQUAL "Debug" AND NOT EXISTS "${PORTABLE_DIR}/${runtime_entry}")
    message(FATAL_ERROR
      "NSIS runtime entry is absent from the portable deployment: ${runtime_entry}")
  endif()
  string(REPLACE "/" "\\" nsis_entry "${runtime_entry}")
  set(delete_literal [=[Delete "$INSTDIR\]=])
  string(APPEND delete_literal "${nsis_entry}\"")
  string(FIND "${nsis_content}" "${delete_literal}" delete_position)
  if(delete_position EQUAL -1)
    message(FATAL_ERROR "NSIS uninstall omits runtime entry: ${runtime_entry}")
  endif()
endforeach()

foreach(runtime_dir IN ITEMS platforms styles iconengines)
  set(rmdir_literal [=[RMDir "$INSTDIR\]=])
  string(APPEND rmdir_literal "${runtime_dir}\"")
  string(FIND "${nsis_content}" "${rmdir_literal}" rmdir_position)
  if(rmdir_position EQUAL -1)
    message(FATAL_ERROR "NSIS uninstall omits runtime directory: ${runtime_dir}")
  endif()
endforeach()

message(STATUS "NSIS Qt runtime manifest matches the portable deployment contract")
