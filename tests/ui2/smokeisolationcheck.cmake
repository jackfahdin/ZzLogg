function(zzlogg_require_safe_test_root test_root)
  if(NOT IS_ABSOLUTE "${test_root}")
    message(FATAL_ERROR "Smoke test root must be absolute: ${test_root}")
  endif()

  cmake_path(SET normalized_root NORMALIZE "${test_root}")
  cmake_path(GET normalized_root ROOT_PATH filesystem_root)
  if(normalized_root STREQUAL filesystem_root)
    message(FATAL_ERROR "Smoke test root must not be a filesystem root: ${test_root}")
  endif()
endfunction()

function(zzlogg_assert_path_in_test_root label test_root candidate)
  zzlogg_require_safe_test_root("${test_root}")
  cmake_path(SET normalized_root NORMALIZE "${test_root}")
  cmake_path(SET normalized_candidate NORMALIZE "${candidate}")
  cmake_path(IS_PREFIX normalized_root "${normalized_candidate}" NORMALIZE is_contained)
  if(NOT is_contained)
    message(FATAL_ERROR
      "${label} escaped the isolated smoke root: ${candidate} (root: ${test_root})")
  endif()
endfunction()

function(zzlogg_assert_output_isolated label test_root stdout stderr)
  string(CONCAT combined_output "${stdout}" "\n" "${stderr}")
  file(TO_CMAKE_PATH "${combined_output}" normalized_output)
  string(TOLOWER "${normalized_output}" normalized_output)

  set(forbidden_storage_paths)
  if(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}" user_profile)
    list(APPEND forbidden_storage_paths
      "${user_profile}/AppData/Roaming/ZzLogg"
      "${user_profile}/AppData/Local/ZzLogg")
  endif()
  if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{HOME}" home_directory)
    list(APPEND forbidden_storage_paths "${home_directory}/.config/ZzLogg")
  endif()

  foreach(forbidden_path IN LISTS forbidden_storage_paths)
    string(TOLOWER "${forbidden_path}" forbidden_path_lower)
    string(FIND "${normalized_output}" "${forbidden_path_lower}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
      message(FATAL_ERROR
        "${label} exposed a real user storage path in process output: ${forbidden_path}")
    endif()
  endforeach()

  zzlogg_assert_path_in_test_root("${label}" "${test_root}" "${test_root}")
endfunction()

function(zzlogg_assert_no_locator_or_probe runtime_directory config_root data_scope)
  if(EXISTS "${runtime_directory}/ZzLogg.storage.ini")
    message(FATAL_ERROR
      "CLI smoke wrote a program locator: ${runtime_directory}/ZzLogg.storage.ini")
  endif()

  set(scan_roots "${runtime_directory}" "${config_root}" "${data_scope}")
  foreach(scan_root IN LISTS scan_roots)
    if(NOT EXISTS "${scan_root}")
      continue()
    endif()
    file(GLOB_RECURSE forbidden_entries LIST_DIRECTORIES FALSE
      "${scan_root}/storage.ini"
      "${scan_root}/ZzLogg.storage.ini"
      "${scan_root}/.zzlogg-write-test-*"
      "${scan_root}/.zzlogg-locator-probe-*")
    if(forbidden_entries)
      list(JOIN forbidden_entries ", " forbidden_text)
      message(FATAL_ERROR "CLI smoke left locator or probe files: ${forbidden_text}")
    endif()
  endforeach()
endfunction()
