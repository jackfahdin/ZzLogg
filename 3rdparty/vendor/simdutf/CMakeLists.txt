# simdutf 本地 amalgamation 构建
# 通过 singleheader 形式提供，避免在受限网络环境下从 GitHub 下载

option(SIMDUTF_BUILD_DEMO "Build simdutf amalgamation demo" OFF)

add_library(
  simdutf STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/simdutf.h
  ${CMAKE_CURRENT_SOURCE_DIR}/simdutf.cpp
)

target_include_directories(
  simdutf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(
  simdutf PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
)

if(SIMDUTF_BUILD_DEMO)
  add_executable(
    simdutf_amalgamation_demo
    ${CMAKE_CURRENT_SOURCE_DIR}/amalgamation_demo.cpp
  )
  target_link_libraries(
    simdutf_amalgamation_demo PRIVATE simdutf
  )
endif()
