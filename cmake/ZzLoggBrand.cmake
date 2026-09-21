set(ZZLOGG_PRODUCT_NAME "ZzLogg")
set(ZZLOGG_PRODUCT_DESCRIPTION "ZzLogg log viewer")
set(ZZLOGG_VENDOR "Jackfahdin")
# Stable Qt path namespace: a company display rename must not move user data.
set(ZZLOGG_PATH_ORGANIZATION "JackfahdinQt")
set(ZZLOGG_HOMEPAGE_URL "https://github.com/jackfahdin/ZzLogg")
set(ZZLOGG_IDENTIFIER "com.gitcode.jackfahdinqt.zzlogg")
set(ZZLOGG_SETTINGS_ORGANIZATION "ZzLogg")
set(ZZLOGG_SETTINGS_APPLICATION "ZzLogg")
set(ZZLOGG_SESSION_SETTINGS_APPLICATION "ZzLogg_session")
set(ZZLOGG_PORTABLE_CONFIG_BASENAME "ZzLogg")
set(ZZLOGG_ICON_RESOURCE ":/zzlogg/icons/ZzLogg.svg")
include("${CMAKE_CURRENT_LIST_DIR}/ZzUpdateFeedConfiguration.cmake")

function(zzlogg_configure_brand)
  zzlogg_configure_update_feed("${CMAKE_BINARY_DIR}/generated/zzlogg_update_feed.h")
  configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/zzlogg_brand.h.in"
    "${CMAKE_BINARY_DIR}/generated/zzlogg_brand.h"
    @ONLY)
  add_library(zzlogg_brand INTERFACE)
  target_include_directories(zzlogg_brand INTERFACE "${CMAKE_BINARY_DIR}/generated")
endfunction()
