function(zzlogg_capture_host_storage_state output_variable)
  if(WIN32)
    if(NOT DEFINED POWERSHELL OR NOT EXISTS "${POWERSHELL}")
      message(FATAL_ERROR "POWERSHELL is required for Windows smoke isolation snapshots")
    endif()
    set(snapshot_script [=[
$roaming = [Environment]::GetFolderPath('ApplicationData')
$local = [Environment]::GetFolderPath('LocalApplicationData')
$paths = @(
  (Join-Path $roaming 'ZzLogg\ZzLogg.ini'),
  (Join-Path $roaming 'ZzLogg\ZzLogg_session.ini'),
  (Join-Path $local 'ZzLogg\storage.ini')
)
foreach ($path in $paths) {
  if (Test-Path -LiteralPath $path) {
    $item = Get-Item -LiteralPath $path
    if ($item.PSIsContainer) {
      Write-Output "$path|directory"
    } else {
      $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
      Write-Output "$path|file|$hash|$($item.Length)|$($item.LastWriteTimeUtc.Ticks)"
    }
  } else {
    Write-Output "$path|missing"
  }
}
]=])
    execute_process(
      COMMAND "${POWERSHELL}" -NoProfile -NonInteractive -ExecutionPolicy Bypass
              -Command "${snapshot_script}"
      RESULT_VARIABLE snapshot_result
      OUTPUT_VARIABLE snapshot_state
      ERROR_VARIABLE snapshot_error
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT snapshot_result EQUAL 0)
      message(FATAL_ERROR "Failed to snapshot Windows host storage: ${snapshot_error}")
    endif()
  else()
    if(DEFINED ENV{XDG_CONFIG_HOME} AND NOT "$ENV{XDG_CONFIG_HOME}" STREQUAL "")
      set(host_config_home "$ENV{XDG_CONFIG_HOME}")
    else()
      set(host_config_home "$ENV{HOME}/.config")
    endif()
    set(host_paths
      "${host_config_home}/ZzLogg/ZzLogg.conf"
      "${host_config_home}/ZzLogg/ZzLogg_session.conf"
      "${host_config_home}/ZzLogg/storage.ini")
    set(snapshot_state "")
    foreach(path IN LISTS host_paths)
      if(IS_DIRECTORY "${path}")
        string(APPEND snapshot_state "${path}|directory\n")
      elseif(EXISTS "${path}")
        file(SHA256 "${path}" path_hash)
        file(SIZE "${path}" path_size)
        file(TIMESTAMP "${path}" path_mtime "%Y-%m-%dT%H:%M:%S.%fZ" UTC)
        string(APPEND snapshot_state "${path}|file|${path_hash}|${path_size}|${path_mtime}\n")
      else()
        string(APPEND snapshot_state "${path}|missing\n")
      endif()
    endforeach()
    string(REGEX REPLACE "\n$" "" snapshot_state "${snapshot_state}")
  endif()
  set(${output_variable} "${snapshot_state}" PARENT_SCOPE)
endfunction()

function(zzlogg_assert_host_storage_unchanged expected_state)
  zzlogg_capture_host_storage_state(actual_state)
  if(NOT "${actual_state}" STREQUAL "${expected_state}")
    message(FATAL_ERROR
      "Smoke changed real host ZzLogg settings or locator.\nBefore:\n${expected_state}\nAfter:\n${actual_state}")
  endif()
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
