foreach(workflow IN ITEMS
    "${SOURCE_ROOT}/.github/workflows/ci-build.yml"
    "${SOURCE_ROOT}/.github/workflows/codeql-analysis.yml")
  file(READ "${workflow}" content)
  if(content MATCHES "qt_version:[ \t]*['\"]?6\\.[0-7](\\.|['\"\n])")
    message(FATAL_ERROR "${workflow} still selects Qt older than 6.8")
  endif()
  string(FIND "${content}" "6.8.3" qt_683)
  if(qt_683 EQUAL -1)
    message(FATAL_ERROR "${workflow} must pin Qt 6.8.3")
  endif()
endforeach()

file(READ "${SOURCE_ROOT}/.github/workflows/ci-build.yml" ci)
file(READ "${SOURCE_ROOT}/.github/actions/agent-setup/action.yml" agent_setup)
string(APPEND ci "\n${agent_setup}")
foreach(required IN ITEMS
    "jurplel/install-qt-action@v4"
    "ubuntu-22.04"
    "cmake --install"
    "ldd"
    "KLOGG_OSX_DEPLOYMENT_TARGET=13.3"
    "win64_msvc2022_64")
  string(FIND "${ci}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Official CI is missing toolchain gate: ${required}")
  endif()
endforeach()
foreach(forbidden IN ITEMS "docker-build" "6.7.3" "MACOSX_DEPLOYMENT_TARGET: 13.0")
  string(FIND "${ci}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "Official CI retains unsupported configuration: ${forbidden}")
  endif()
endforeach()
