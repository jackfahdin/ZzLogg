if(NOT DEFINED GENERATOR OR NOT DEFINED SOURCE OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "GENERATOR, SOURCE and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
set(hicolor_root "${TEST_ROOT}/hicolor")
set(ico_path "${TEST_ROOT}/ZzLogg.ico")
set(icns_path "${TEST_ROOT}/ZzLogg.icns")
set(asset_paths)

foreach(size IN ITEMS 16 32 48 64 128 256 512)
  set(asset "${hicolor_root}/${size}x${size}/ZzLogg.png")
  file(MAKE_DIRECTORY "${hicolor_root}/${size}x${size}")
  file(WRITE "${asset}" "sentinel-${size}")
  list(APPEND asset_paths "${asset}")
endforeach()
file(WRITE "${ico_path}" "sentinel-ico")
file(WRITE "${icns_path}" "sentinel-icns")
list(APPEND asset_paths "${ico_path}" "${icns_path}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    ZZLOGG_ICON_GENERATOR_TEST_FAIL_STAGED_READ_AT=3
    "${GENERATOR}"
    --source "${SOURCE}"
    --hicolor-root "${hicolor_root}"
    --ico "${ico_path}"
    --icns "${icns_path}"
  RESULT_VARIABLE injected_failure_result
  OUTPUT_VARIABLE injected_failure_output
  ERROR_VARIABLE injected_failure_error)
if(injected_failure_result EQUAL 0)
  message(FATAL_ERROR "Injected staged read failure unexpectedly succeeded")
endif()

foreach(size IN ITEMS 16 32 48 64 128 256 512)
  file(READ "${hicolor_root}/${size}x${size}/ZzLogg.png" sentinel)
  if(NOT sentinel STREQUAL "sentinel-${size}")
    message(FATAL_ERROR "Injected read failure changed ${size}px sentinel")
  endif()
endforeach()
file(READ "${ico_path}" ico_sentinel)
file(READ "${icns_path}" icns_sentinel)
if(NOT ico_sentinel STREQUAL "sentinel-ico" OR NOT icns_sentinel STREQUAL "sentinel-icns")
  message(FATAL_ERROR "Injected read failure changed container sentinels")
endif()

execute_process(
  COMMAND "${GENERATOR}"
    --source "${SOURCE}"
    --hicolor-root "${hicolor_root}"
    --ico "${ico_path}"
    --icns "${icns_path}"
  RESULT_VARIABLE first_result)
if(NOT first_result EQUAL 0)
  message(FATAL_ERROR "First deterministic generation failed: ${first_result}")
endif()

set(index 0)
foreach(asset IN LISTS asset_paths)
  file(SHA256 "${asset}" hash)
  set("hash_${index}" "${hash}")
  math(EXPR index "${index} + 1")
endforeach()

execute_process(
  COMMAND "${GENERATOR}"
    --source "${SOURCE}"
    --hicolor-root "${hicolor_root}"
    --ico "${ico_path}"
    --icns "${icns_path}"
  RESULT_VARIABLE second_result)
if(NOT second_result EQUAL 0)
  message(FATAL_ERROR "Second deterministic generation failed: ${second_result}")
endif()

set(index 0)
foreach(asset IN LISTS asset_paths)
  file(SHA256 "${asset}" hash)
  if(NOT hash STREQUAL "${hash_${index}}")
    message(FATAL_ERROR "Repeated generation changed ${asset}")
  endif()
  math(EXPR index "${index} + 1")
endforeach()

message(STATUS "Generator staged-read rollback and determinism contract passed")
