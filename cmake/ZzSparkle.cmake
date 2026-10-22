include_guard(GLOBAL)

# This macro runs after the ordinary bundle version defaults are set. Keeping
# metadata independent of downloading/linking makes the release contract testable.
macro(zzlogg_sparkle_bundle_metadata)
  set(ZZLOGG_SPARKLE_PLIST "")
  set(ZZLOGG_SPARKLE_FEED_PREFIX "")
  if(ZZLOGG_OFFICIAL_RELEASE)
    if(NOT ZZLOGG_RELEASE_CHANNEL MATCHES "^(stable|preview)$")
      message(FATAL_ERROR "Sparkle requires a stable or preview release channel")
    endif()
    set(_sparkle_arch "${CMAKE_OSX_ARCHITECTURES}")
    if(_sparkle_arch STREQUAL "")
      set(_sparkle_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(_sparkle_arch STREQUAL "x86_64")
      set(_sparkle_arch x64)
    elseif(NOT _sparkle_arch STREQUAL "arm64")
      message(FATAL_ERROR "Sparkle requires one x86_64 or arm64 target architecture")
    endif()
    set(_sparkle_sequence "${ZZLOGG_RELEASE_SEQUENCE}")
    if(_sparkle_sequence STREQUAL "")
      string(REPLACE "." "" _sparkle_sequence "${ZZLOGG_DISPLAY_VERSION}")
      string(REGEX REPLACE "^0+" "" _sparkle_sequence "${_sparkle_sequence}")
    endif()
    if(NOT _sparkle_sequence MATCHES "^[1-9][0-9]*$")
      message(FATAL_ERROR "Sparkle requires a positive release sequence")
    endif()
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${_sparkle_sequence}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${ZZLOGG_DISPLAY_VERSION}")
    # Same Ed25519 public key as packaging/update/github-public-key.json.
    # The app owns startup scheduling, so Sparkle's periodic checks and
    # automatic downloads are disabled. Interactive installation is unchanged.
    if(ZZLOGG_ENABLE_GITHUB_FEED)
    set(ZZLOGG_SPARKLE_FEED_PREFIX
      "https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-${_sparkle_arch}-")
    set(ZZLOGG_SPARKLE_PLIST "
    <key>SUFeedURL</key>
    <string>${ZZLOGG_SPARKLE_FEED_PREFIX}${ZZLOGG_RELEASE_CHANNEL}.xml</string>
    <key>SUPublicEDKey</key>
    <string>2FZCwpkTTEV6LI2D3KjrcWFMm4aNWE8zVYmSvIZFRE8=</string>
    <key>SUEnableAutomaticChecks</key>
    <false/>
    <key>SUAutomaticallyUpdate</key>
    <false/>
    <key>SUAllowsAutomaticUpdates</key>
    <false/>")
    endif()
  endif()
endmacro()

function(zzlogg_configure_sparkle target)
  if(NOT APPLE OR NOT ZZLOGG_OFFICIAL_RELEASE OR NOT ZZLOGG_ENABLE_GITHUB_FEED)
    return()
  endif()
  include(FetchContent)
  # Official upstream binary distribution, universal x86_64/arm64. Pin both the
  # release and bytes so an upstream replacement cannot silently enter a build.
  FetchContent_Declare(zzlogg_sparkle
    URL https://github.com/sparkle-project/Sparkle/releases/download/2.10.0/Sparkle-2.10.0.tar.xz
    URL_HASH SHA256=c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c)
  FetchContent_MakeAvailable(zzlogg_sparkle)
  set(framework "${zzlogg_sparkle_SOURCE_DIR}/Sparkle.framework")
  if(NOT EXISTS "${framework}/Sparkle")
    message(FATAL_ERROR "Pinned Sparkle distribution does not contain its framework")
  endif()
  add_library(zzlogg_sparkle_framework SHARED IMPORTED GLOBAL)
  set_target_properties(zzlogg_sparkle_framework PROPERTIES
    FRAMEWORK TRUE IMPORTED_LOCATION "${framework}/Sparkle")
  target_link_libraries(${target} PRIVATE zzlogg_sparkle_framework "-framework Cocoa")
  target_sources(${target} PRIVATE
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/app/macupdatecontroller.mm")
  target_compile_definitions(${target} PRIVATE
    ZZLOGG_SPARKLE_FEED_PREFIX="${ZZLOGG_SPARKLE_FEED_PREFIX}")
  set_property(TARGET ${target} PROPERTY ZZLOGG_SPARKLE_FEED_PREFIX
    "${ZZLOGG_SPARKLE_FEED_PREFIX}")
  set_source_files_properties(
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/app/macupdatecontroller.mm"
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc" SKIP_PRECOMPILE_HEADERS ON)
  set_target_properties(${target} PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
  target_compile_definitions(${target} PRIVATE ZZLOGG_HAS_SPARKLE)
  set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
  set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
  # ditto preserves Sparkle's versioned framework symlinks and nested helpers.
  # The entire bundle is signed inside-out by the packaging action afterwards.
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND /usr/bin/ditto "${framework}" "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks/Sparkle.framework"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${target}>/Contents/SharedSupport/licenses/Sparkle"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${zzlogg_sparkle_SOURCE_DIR}/LICENSE"
      "$<TARGET_BUNDLE_DIR:${target}>/Contents/SharedSupport/licenses/Sparkle/LICENSE"
    VERBATIM)
endfunction()
