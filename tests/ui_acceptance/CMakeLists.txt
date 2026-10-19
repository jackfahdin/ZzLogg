if(CMAKE_VERSION VERSION_LESS 3.23)
  message(FATAL_ERROR "KLOGG_BUILD_UI_TESTS requires CMake 3.23 or newer")
endif()

find_package(Qt6 REQUIRED COMPONENTS Gui Svg)

# Include in this directory scope to preserve test output paths and target discovery.
# Update checks precede translations; runtime MOC generation precedes handoff tests.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/update-checks.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ui.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/storage.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/app-translations.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packaging.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/app-runtime.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/app-smoke.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/update-handoff.cmake")

# Warning relaxation for test-only targets (see the function comment in the
# top-level CMakeLists.txt; -Werror arrives via linked interfaces).
zzlogg_relax_test_warnings_in_current_dir()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ui-syntax.cmake")
