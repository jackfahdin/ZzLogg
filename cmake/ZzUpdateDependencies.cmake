include_guard(GLOBAL)
set(_zz_update_vendor "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/vendor")
add_library(zzlogg_update_monocypher STATIC
  "${_zz_update_vendor}/monocypher/src/monocypher.c"
  "${_zz_update_vendor}/monocypher/src/optional/monocypher-ed25519.c")
target_include_directories(zzlogg_update_monocypher PUBLIC
  "${_zz_update_vendor}/monocypher/src"
  "${_zz_update_vendor}/monocypher/src/optional")
set_target_properties(zzlogg_update_monocypher PROPERTIES
  C_STANDARD 99 C_STANDARD_REQUIRED YES POSITION_INDEPENDENT_CODE ON)
