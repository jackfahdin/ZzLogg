file(READ "${SOURCE_ROOT}/.github/workflows/ci-build.yml" ci)
file(READ "${SOURCE_ROOT}/cmake/ZzPureTools.cmake" integration)
file(READ "${SOURCE_ROOT}/CMakeLists.txt" root_cmake)

# Exercise the host resolver's disabled path and its platform guard without
# requiring a Linux compiler on the machine running the contract suite.
execute_process(COMMAND "${CMAKE_COMMAND}" -DZZ_BUNDLE_GNU_RUNTIME=OFF
  -P "${SOURCE_ROOT}/cmake/ZzLoggGnuRuntime.cmake"
  RESULT_VARIABLE disabled_result)
if(NOT disabled_result EQUAL 0)
  message(FATAL_ERROR "Disabled runtime bundling must not require a compiler or licenses")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -DZZ_BUNDLE_GNU_RUNTIME=ON
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER_ID=GNU -DCMAKE_BUILD_TYPE=Release
  -P "${SOURCE_ROOT}/cmake/ZzLoggGnuRuntime.cmake"
  RESULT_VARIABLE invalid_result ERROR_VARIABLE invalid_error OUTPUT_QUIET)
if(invalid_result EQUAL 0 OR NOT invalid_error MATCHES "requires Linux, GNU, and Release")
  message(FATAL_ERROR "Runtime bundling must reject unsupported platforms")
endif()

string(FIND "${ci}" "  Linux:" linux_position)
string(FIND "${ci}" "  Mac:" mac_position)
if(linux_position EQUAL -1 OR mac_position EQUAL -1
   OR linux_position GREATER mac_position)
  message(FATAL_ERROR "Cannot isolate formal Linux CI job")
endif()
math(EXPR linux_length "${mac_position} - ${linux_position}")
string(SUBSTRING "${ci}" ${linux_position} ${linux_length} linux_job)

foreach(required IN ITEMS
    "-DZZ_BUNDLE_GNU_RUNTIME=ON"
    "-DZZ_GNU_RUNTIME_LICENSE_DIR=$ZZ_GNU_RUNTIME_LICENSE_DIR"
    "gcc-mirror/gcc/releases/gcc-13.3.0/COPYING3"
    "gcc-mirror/gcc/releases/gcc-13.3.0/COPYING.RUNTIME"
    "8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903"
    "9d6b43ce4d8de0c878bf16b54d8e7a10d9bd42b75178153e3af6a815bdc90f74"
    "/opt/ZzLogg/lib/libstdc++.so.6"
    "/opt/ZzLogg/lib/libgcc_s.so.1"
    "/opt/ZzLogg/share/ZzPureToolsFrame/licenses/gcc-runtime/COPYING3"
    "/opt/ZzLogg/share/ZzPureToolsFrame/licenses/gcc-runtime/COPYING.RUNTIME"
    "readlink -f"
    "GLIBCXX_"
    [=[dpkg -L "$package"]=]
    [=[rpm -ql "$package"]=])
  string(FIND "${linux_job}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Linux GNU runtime closure is missing: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "ZZLOGG_GNU_LIBSTDCXX_PATH"
    "ZZLOGG_GNU_LIBGCC_PATH"
    "ZZLOGG_GNU_RUNTIME_LICENSE_DIR")
  string(FIND "${integration}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "ZzPureTools integration does not expose: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    [=[if(ZZ_BUNDLE_GNU_RUNTIME)]=]
    [=[install(FILES "${ZZLOGG_GNU_LIBSTDCXX_PATH}"]=]
    [=[install(FILES "${ZZLOGG_GNU_LIBGCC_PATH}"]=]
    "libstdc++.so.6"
    "libgcc_s.so.1"
    "COPYING3"
    "COPYING.RUNTIME"
    [=[share/ZzPureToolsFrame/licenses/gcc-runtime]=])
  string(FIND "${root_cmake}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Root install does not own GNU runtime input: ${required}")
  endif()
endforeach()

string(REGEX MATCH
  [=[set\(CPACK_DEBIAN_PACKAGE_DEPENDS[^\)]*\)]=]
  debian_dependencies "${root_cmake}")
foreach(forbidden IN ITEMS "libstdc++6" "libgcc-s1")
  string(FIND "${debian_dependencies}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Bundled GNU runtime must not depend on host ${forbidden}")
  endif()
endforeach()
