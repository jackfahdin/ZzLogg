get_filename_component(main_name "${MAIN_APP}" NAME)
get_filename_component(portable_name "${PORTABLE_APP}" NAME)
get_filename_component(ui2_name "${UI2_APP}" NAME)
get_filename_component(grep_name "${GREP_APP}" NAME)
get_filename_component(portable_dir_name "${PORTABLE_DIR}" NAME)

if(NOT DEFINED EXECUTABLE_SUFFIX)
  message(FATAL_ERROR "EXECUTABLE_SUFFIX is required")
endif()

foreach(expected_name IN ITEMS
    "ZzLogg${EXECUTABLE_SUFFIX}"
    "ZzLogg_portable${EXECUTABLE_SUFFIX}"
    "ZzLogg_ui2${EXECUTABLE_SUFFIX}"
    "ZzLogg_grep${EXECUTABLE_SUFFIX}")
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
