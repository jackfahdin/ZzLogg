cmake_minimum_required(VERSION 3.23)
if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "Release sequence test requires SOURCE_ROOT")
endif()
include("${SOURCE_ROOT}/cmake/ZzReleaseIdentity.cmake")

# 与 scripts/ci/publish_update_feed.py 的 int(version.replace('.', '')) 等价，
# 包括前导零剥离。两侧不一致会让 selectUpdate 判为 ReleaseConflict。
function(expect_sequence version expected)
  _zzlogg_derive_release_sequence("${version}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${version}: expected ${expected}, got ${actual}")
  endif()
endfunction()

expect_sequence("26.09.03" "260903")
expect_sequence("26.12.00" "261200")
expect_sequence("09.01.00" "90100")
expect_sequence("00.01.00" "100")
