include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ZzUpdateDependencies.cmake")
include(ZzReleaseIdentity)

set(zzlogg_release_identity_header
  "${CMAKE_CURRENT_BINARY_DIR}/generated/zzlogg_release_identity.h")
zzlogg_configure_release_identity("${zzlogg_release_identity_header}")

function(zzlogg_add_update_core prefix static_crt)
  add_library(${prefix}_core STATIC
    src/version.cpp src/releaseidentity.cpp src/signature.cpp src/strictjson.cpp
    src/manifest.cpp src/payload.cpp src/policy.cpp src/urlpolicy.cpp src/executionselection.cpp)
  target_include_directories(${prefix}_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_include_directories(${prefix}_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_features(${prefix}_core PUBLIC cxx_std_17)
  target_link_libraries(${prefix}_core PRIVATE ${prefix}_monocypher zzlogg_update_json)
  zzlogg_update_runtime(${prefix}_core ${static_crt})
  if(WIN32)
    add_library(${prefix}_execution STATIC src/stablepackage_win.cpp
      src/authenticode_policy.cpp src/authenticode_win.cpp src/packageverification.cpp)
    target_link_libraries(${prefix}_execution PUBLIC ${prefix}_core PRIVATE bcrypt wintrust crypt32)
    target_compile_features(${prefix}_execution PUBLIC cxx_std_17)
    zzlogg_update_runtime(${prefix}_execution ${static_crt})
  endif()
endfunction()
zzlogg_add_update_core(zzlogg_update FALSE)

set(updater_arch "${CMAKE_GENERATOR_PLATFORM}")
if(NOT updater_arch)
  set(updater_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
string(TOLOWER "${updater_arch}" updater_arch)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND updater_arch MATCHES "^(x64|amd64|x86_64)$")
  zzlogg_add_update_monocypher(zzlogg_updater_monocypher TRUE)
  zzlogg_add_update_core(zzlogg_updater TRUE)
endif()
