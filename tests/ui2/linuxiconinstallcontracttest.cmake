if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
set(contract_source "${TEST_ROOT}/source")
set(contract_build "${TEST_ROOT}/build")
set(contract_prefix "${TEST_ROOT}/prefix")
file(MAKE_DIRECTORY "${contract_source}")

file(WRITE "${contract_source}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.23)\n"
"project(zzlogg_linux_icon_install_contract LANGUAGES NONE)\n"
"list(APPEND CMAKE_MODULE_PATH \"${SOURCE_ROOT}/cmake\")\n"
"include(ZzLoggIconInstall)\n"
"zzlogg_install_legacy_linux_icons(\"${SOURCE_ROOT}\")\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${contract_source}" -B "${contract_build}"
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
    "Linux icon install contract configure failed (${configure_result})\n"
    "${configure_output}\n${configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${contract_build}" --prefix "${contract_prefix}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR
    "Linux icon install contract failed (${install_result})\n"
    "${install_output}\n${install_error}")
endif()

foreach(size IN ITEMS 16 32 48 64 128 256 512)
  set(source "${SOURCE_ROOT}/src/app/images/hicolor/${size}x${size}/ZzLogg.png")
  set(installed "${contract_prefix}/share/icons/hicolor/${size}x${size}/apps/ZzLogg.png")
  if(NOT EXISTS "${installed}")
    message(FATAL_ERROR "Missing installed ZzLogg icon: ${installed}")
  endif()
  file(SHA256 "${source}" source_hash)
  file(SHA256 "${installed}" installed_hash)
  if(NOT source_hash STREQUAL installed_hash)
    message(FATAL_ERROR "Installed ${size}px ZzLogg icon differs from source")
  endif()
endforeach()

set(source_svg "${SOURCE_ROOT}/src/app/images/hicolor/scalable/ZzLogg.svg")
set(installed_svg "${contract_prefix}/share/icons/hicolor/scalable/apps/ZzLogg.svg")
if(NOT EXISTS "${installed_svg}")
  message(FATAL_ERROR "Missing installed ZzLogg icon: ${installed_svg}")
endif()
file(SHA256 "${source_svg}" source_svg_hash)
file(SHA256 "${installed_svg}" installed_svg_hash)
if(NOT source_svg_hash STREQUAL installed_svg_hash)
  message(FATAL_ERROR "Installed scalable ZzLogg icon differs from source")
endif()

message(STATUS "Linux ZzLogg icon install contract passed")
