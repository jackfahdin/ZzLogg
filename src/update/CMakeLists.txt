include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ZzUpdateDependencies.cmake")
include(ZzReleaseIdentity)

set(zzlogg_release_identity_header
  "${CMAKE_CURRENT_BINARY_DIR}/generated/zzlogg_release_identity.h")
zzlogg_configure_release_identity("${zzlogg_release_identity_header}")

add_library(zzlogg_update_core STATIC
  src/version.cpp
  src/releaseidentity.cpp
  src/signature.cpp
  src/strictjson.cpp
  src/manifest.cpp)
target_sources(zzlogg_update_core PRIVATE src/payload.cpp src/policy.cpp src/urlpolicy.cpp)
target_sources(zzlogg_update_core PRIVATE src/executionselection.cpp)
target_include_directories(zzlogg_update_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(zzlogg_update_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(zzlogg_update_core PUBLIC cxx_std_17)
target_link_libraries(zzlogg_update_core PRIVATE zzlogg_update_monocypher zzlogg_update_json)

if(WIN32)
  add_library(zzlogg_update_execution STATIC src/stablepackage_win.cpp
    src/authenticode_policy.cpp src/authenticode_win.cpp src/packageverification.cpp)
  target_link_libraries(zzlogg_update_execution PUBLIC zzlogg_update_core PRIVATE bcrypt wintrust crypt32)
  target_compile_features(zzlogg_update_execution PUBLIC cxx_std_17)
endif()
