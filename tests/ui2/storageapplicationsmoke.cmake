if(NOT DEFINED APP OR NOT DEFINED TEST_ROOT OR NOT DEFINED FIRST_LOG
   OR NOT DEFINED SECOND_LOG)
  message(FATAL_ERROR "APP, TEST_ROOT, FIRST_LOG and SECOND_LOG are required")
endif()

if(NOT IS_ABSOLUTE "${TEST_ROOT}"
   OR "${TEST_ROOT}" MATCHES "^[/\\\\]?$"
   OR "${TEST_ROOT}" MATCHES "^[A-Za-z]:[/\\\\]?$" )
  message(FATAL_ERROR "TEST_ROOT must be a non-root absolute path: ${TEST_ROOT}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/smokeisolationcheck.cmake")
zzlogg_require_safe_test_root("${TEST_ROOT}")

function(zzlogg_record_failure failure)
  set_property(GLOBAL APPEND PROPERTY ZZLOGG_STORAGE_APPLICATION_SMOKE_FAILURES
    "${failure}")
endfunction()

function(zzlogg_prepare_case case_name output_runtime output_config output_env)
  set(case_root "${TEST_ROOT}/${case_name}")
  set(runtime_dir "${case_root}/runtime")
  set(config_root "${case_root}/environment")
  set(appdata_dir "${config_root}/AppData/Roaming")
  set(localappdata_dir "${config_root}/AppData/Local")
  set(xdg_config_dir "${config_root}/xdg/config")
  set(xdg_data_dir "${config_root}/xdg/data")
  set(xdg_cache_dir "${config_root}/xdg/cache")
  set(smoke_app_config_dir "${config_root}/smoke/app-config")
  set(smoke_user_data_dir "${config_root}/smoke/user-data")
  zzlogg_assert_path_in_test_root("${case_name} runtime" "${TEST_ROOT}" "${runtime_dir}")
  zzlogg_assert_path_in_test_root(
    "${case_name} config environment" "${TEST_ROOT}" "${config_root}")
  file(MAKE_DIRECTORY
    "${runtime_dir}"
    "${appdata_dir}"
    "${localappdata_dir}"
    "${xdg_config_dir}"
    "${xdg_data_dir}"
    "${xdg_cache_dir}"
    "${smoke_app_config_dir}"
    "${smoke_user_data_dir}")

  set(case_env
    "APPDATA=${appdata_dir}"
    "LOCALAPPDATA=${localappdata_dir}"
    "XDG_CONFIG_HOME=${xdg_config_dir}"
    "XDG_DATA_HOME=${xdg_data_dir}"
    "XDG_CACHE_HOME=${xdg_cache_dir}"
    "ZZLOGG_UI2_SMOKE_APP_CONFIG_DIR=${smoke_app_config_dir}"
    "ZZLOGG_UI2_SMOKE_USER_DATA_DIR=${smoke_user_data_dir}")
  if(NOT WIN32)
    list(APPEND case_env "QT_QPA_PLATFORM=offscreen")
  endif()

  set(${output_runtime} "${runtime_dir}" PARENT_SCOPE)
  set(${output_config} "${config_root}" PARENT_SCOPE)
  set(${output_env} "${case_env}" PARENT_SCOPE)
endfunction()

function(zzlogg_copy_application runtime_dir output_app)
  get_filename_component(app_name "${APP}" NAME)
  get_filename_component(app_directory "${APP}" DIRECTORY)
  set(runtime_app "${runtime_dir}/${app_name}")
  file(COPY "${app_directory}/" DESTINATION "${runtime_dir}"
    PATTERN "ZzLogg.storage.ini" EXCLUDE
    PATTERN "data" EXCLUDE)
  set(${output_app} "${runtime_app}" PARENT_SCOPE)
endfunction()

function(zzlogg_write_manifest data_root)
  file(MAKE_DIRECTORY "${data_root}")
  file(WRITE "${data_root}/storage-manifest.ini"
    "[Storage]\nlayoutVersion=1\nproduct=ZzLogg\n")
endfunction()

function(zzlogg_check_data_root case_name data_root require_log)
  zzlogg_assert_path_in_test_root("${case_name} data" "${TEST_ROOT}" "${data_root}")
  foreach(relative_path IN ITEMS
      config/ZzLogg.ini
      session/ZzLogg_session.ini
      logs
      crashes
      storage-manifest.ini)
    if(NOT EXISTS "${data_root}/${relative_path}")
      zzlogg_record_failure(
        "${case_name}: missing persistent data ${data_root}/${relative_path}")
    endif()
  endforeach()

  if(require_log)
    file(GLOB log_files LIST_DIRECTORIES FALSE "${data_root}/logs/*.log")
    if(NOT log_files)
      zzlogg_record_failure("${case_name}: no log file was written under ${data_root}/logs")
    endif()
  endif()
endfunction()

function(zzlogg_check_no_business_settings case_name config_root)
  set(business_settings_files)
  foreach(file_name IN ITEMS ZzLogg.ini ZzLogg_session.ini)
    file(GLOB_RECURSE found_files LIST_DIRECTORIES FALSE
      "${config_root}/${file_name}")
    list(APPEND business_settings_files ${found_files})
  endforeach()
  if(business_settings_files)
    list(REMOVE_DUPLICATES business_settings_files)
    list(JOIN business_settings_files ", " business_settings_text)
    zzlogg_record_failure(
      "${case_name}: isolated environment contains business settings outside the selected root: ${business_settings_text}")
  endif()
endfunction()

function(zzlogg_check_no_business_ini case_name config_root)
  zzlogg_check_no_business_settings("${case_name}" "${config_root}")
  set(business_ini_files)
  file(GLOB_RECURSE business_ini_files LIST_DIRECTORIES FALSE
    "${config_root}/storage.ini")
  if(business_ini_files)
    list(REMOVE_DUPLICATES business_ini_files)
    list(JOIN business_ini_files ", " business_ini_text)
    zzlogg_record_failure(
      "${case_name}: isolated APPDATA/config contains business INI files: ${business_ini_text}")
  endif()
endfunction()

get_property(previous_failures GLOBAL
  PROPERTY ZZLOGG_STORAGE_APPLICATION_SMOKE_FAILURES)
set_property(GLOBAL PROPERTY ZZLOGG_STORAGE_APPLICATION_SMOKE_FAILURES "")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

# Case 1: an explicit --data-dir must contain every persistent artifact and logs.
zzlogg_prepare_case(custom custom_runtime custom_config custom_env)
zzlogg_copy_application("${custom_runtime}" custom_app)
set(custom_data_root "${TEST_ROOT}/custom/storage")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${custom_env}
          ZZLOGG_UI2_SMOKE_MS=1800
          "${custom_app}" --multi --new-session --log
          --data-dir "${custom_data_root}" "${FIRST_LOG}" "${SECOND_LOG}"
  RESULT_VARIABLE custom_result
  OUTPUT_VARIABLE custom_stdout
  ERROR_VARIABLE custom_stderr
  TIMEOUT 15)
if(NOT custom_result EQUAL 0)
  zzlogg_record_failure(
    "custom --data-dir launch failed (${custom_result}): ${custom_stdout} ${custom_stderr}")
endif()
zzlogg_assert_output_isolated(
  "custom --data-dir" "${TEST_ROOT}" "${custom_stdout}" "${custom_stderr}")
zzlogg_check_data_root("custom --data-dir" "${custom_data_root}" TRUE)
zzlogg_check_no_business_ini("custom --data-dir" "${custom_config}")

# Case 2: the adjacent locator is relative to the copied executable's runtime.
zzlogg_prepare_case(program program_runtime program_config program_env)
zzlogg_copy_application("${program_runtime}" program_app)
set(program_data_root "${program_runtime}/data")
zzlogg_write_manifest("${program_data_root}")
file(WRITE "${program_runtime}/ZzLogg.storage.ini"
  "[Storage]\nformatVersion=1\nmode=program\ndataRoot=data\nverified=true\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${program_env}
          ZZLOGG_UI2_SMOKE_MS=1800
          "${program_app}" --multi --new-session "${FIRST_LOG}" "${SECOND_LOG}"
  RESULT_VARIABLE program_result
  OUTPUT_VARIABLE program_stdout
  ERROR_VARIABLE program_stderr
  TIMEOUT 15)
if(NOT program_result EQUAL 0)
  zzlogg_record_failure(
    "program locator launch failed (${program_result}): ${program_stdout} ${program_stderr}")
endif()
zzlogg_assert_output_isolated(
  "program locator" "${TEST_ROOT}" "${program_stdout}" "${program_stderr}")
zzlogg_check_data_root("program locator" "${program_data_root}" FALSE)
zzlogg_check_no_business_ini("program locator" "${program_config}")

# Case 3: the user locator must seed and then restore a real multi-window session.
zzlogg_prepare_case(user user_runtime user_config user_env)
zzlogg_copy_application("${user_runtime}" user_app)
set(user_data_root "${TEST_ROOT}/user/storage")
zzlogg_write_manifest("${user_data_root}")
set(user_locator "${user_config}/smoke/app-config/storage.ini")
file(TO_CMAKE_PATH "${user_data_root}" serialized_user_data_root)
get_filename_component(user_locator_directory "${user_locator}" DIRECTORY)
file(MAKE_DIRECTORY "${user_locator_directory}")
file(WRITE "${user_locator}"
  "[Storage]\nformatVersion=1\nmode=user\ndataRoot=${serialized_user_data_root}\nverified=true\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${user_env}
          ZZLOGG_UI2_SMOKE_MODE=seed-session ZZLOGG_UI2_SMOKE_MS=3000
          "${user_app}" --multi --new-session
  RESULT_VARIABLE user_seed_result
  OUTPUT_VARIABLE user_seed_stdout
  ERROR_VARIABLE user_seed_stderr
  TIMEOUT 15)
if(NOT user_seed_result EQUAL 0)
  zzlogg_record_failure(
    "user locator session seed failed (${user_seed_result}): ${user_seed_stdout} ${user_seed_stderr}")
endif()
zzlogg_assert_output_isolated(
  "user locator seed" "${TEST_ROOT}" "${user_seed_stdout}" "${user_seed_stderr}")
zzlogg_check_data_root("user locator seed" "${user_data_root}" FALSE)
zzlogg_check_no_business_settings("user locator seed" "${user_config}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${user_env}
          ZZLOGG_UI2_SMOKE_MODE=verify-restored ZZLOGG_UI2_SMOKE_MS=3000
          "${user_app}" --multi --load-session
  RESULT_VARIABLE user_restore_result
  OUTPUT_VARIABLE user_restore_stdout
  ERROR_VARIABLE user_restore_stderr
  TIMEOUT 15)
if(NOT user_restore_result EQUAL 0)
  zzlogg_record_failure(
    "user locator session restore failed (${user_restore_result}): ${user_restore_stdout} ${user_restore_stderr}")
endif()
zzlogg_assert_output_isolated(
  "user locator restore" "${TEST_ROOT}" "${user_restore_stdout}" "${user_restore_stderr}")
zzlogg_check_data_root("user locator restore" "${user_data_root}" FALSE)
zzlogg_check_no_business_settings("user locator restore" "${user_config}")

# A saved custom locator whose root disappears must report an error, not create a new profile.
file(WRITE "${user_locator}"
  "[Storage]\nformatVersion=1\nmode=custom\ndataRoot=${serialized_user_data_root}\nverified=true\n")
set(hidden_user_data_root "${TEST_ROOT}/user/storage-hidden")
file(RENAME "${user_data_root}" "${hidden_user_data_root}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${user_env}
          ZZLOGG_UI2_SMOKE_MS=500
          "${user_app}" --multi --new-session
  RESULT_VARIABLE missing_root_result
  OUTPUT_VARIABLE missing_root_stdout
  ERROR_VARIABLE missing_root_stderr
  TIMEOUT 10)
if(missing_root_result EQUAL 0)
  zzlogg_record_failure(
    "missing custom storage root launched with a blank default profile")
endif()
zzlogg_assert_output_isolated(
  "missing custom storage root" "${TEST_ROOT}" "${missing_root_stdout}" "${missing_root_stderr}")
string(FIND "${missing_root_stderr}"
  "storage directory has no compatible manifest" missing_root_error_index)
if(missing_root_error_index EQUAL -1)
  zzlogg_record_failure(
    "missing custom storage root did not report the recovery error: ${missing_root_stdout} ${missing_root_stderr}")
endif()
if(EXISTS "${user_data_root}/config/ZzLogg.ini"
   OR EXISTS "${user_data_root}/session/ZzLogg_session.ini"
   OR EXISTS "${user_data_root}/storage-manifest.ini")
  zzlogg_record_failure(
    "missing custom storage root created a blank persistent profile: ${user_data_root}")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")

get_property(smoke_failures GLOBAL
  PROPERTY ZZLOGG_STORAGE_APPLICATION_SMOKE_FAILURES)
set_property(GLOBAL PROPERTY ZZLOGG_STORAGE_APPLICATION_SMOKE_FAILURES
  "${previous_failures}")
if(smoke_failures)
  list(JOIN smoke_failures "\n- " smoke_failure_text)
  message(FATAL_ERROR "Storage application smoke failures:\n- ${smoke_failure_text}")
endif()
