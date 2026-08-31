get_filename_component(main_name "${MAIN_APP}" NAME)
get_filename_component(portable_name "${PORTABLE_APP}" NAME)
get_filename_component(ui2_name "${UI2_APP}" NAME)
get_filename_component(grep_name "${GREP_APP}" NAME)
get_filename_component(portable_dir_name "${PORTABLE_DIR}" NAME)

if(WIN32)
  set(executable_suffix ".exe")
else()
  set(executable_suffix "")
endif()

foreach(expected_name IN ITEMS
    "ZzLogg${executable_suffix}"
    "ZzLogg_portable${executable_suffix}"
    "ZzLogg_ui2${executable_suffix}"
    "ZzLogg_grep${executable_suffix}")
  list(APPEND expected_names "${expected_name}")
endforeach()

set(actual_names "${main_name}" "${portable_name}" "${ui2_name}" "${grep_name}")
foreach(index RANGE 0 3)
  list(GET expected_names ${index} expected_name)
  list(GET actual_names ${index} actual_name)
  if(NOT actual_name STREQUAL expected_name)
    message(FATAL_ERROR "Expected ${expected_name}, got ${actual_name}")
  endif()
endforeach()

if(NOT portable_dir_name STREQUAL "ZzLogg-portable")
  message(FATAL_ERROR "Expected portable directory ZzLogg-portable, got ${portable_dir_name}")
endif()

if(NOT GREP_EXCLUDED_FROM_ALL)
  message(FATAL_ERROR "klogg_grep must remain EXCLUDE_FROM_ALL")
endif()
