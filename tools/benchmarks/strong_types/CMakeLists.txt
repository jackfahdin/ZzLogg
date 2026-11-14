cmake_minimum_required(VERSION 3.30)
project(strong_type_probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_library(probe_options INTERFACE)
target_include_directories(probe_options INTERFACE
  "${REPO_ROOT}/src/logdata/include" "${REPO_ROOT}/src/utils/include"
  "${REPO_ROOT}/src/logging/include" "${REPO_ROOT}/3rdparty/vendor/mimalloc/include"
  "${REPO_ROOT}/3rdparty/vendor/type_safe/include"
  "${REPO_ROOT}/3rdparty/vendor/type_safe/external/debug_assert")
# Match the production Release type_safe configuration, including active preconditions.
target_compile_definitions(probe_options INTERFACE
  TYPE_SAFE_ENABLE_ASSERTIONS=0 TYPE_SAFE_ENABLE_PRECONDITION_CHECKS=1
  TYPE_SAFE_ARITHMETIC_POLICY=1 TYPE_SAFE_ENABLE_WRAPPER=1)
target_link_libraries(probe_options INTERFACE Qt6::Core)
add_library(strong_type_codegen OBJECT codegen.cpp)
target_link_libraries(strong_type_codegen PRIVATE probe_options)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(strong_type_codegen PRIVATE -ffunction-sections -fno-ipa-icf)
endif()
add_executable(strong_type_checks checks.cpp)
target_link_libraries(strong_type_checks PRIVATE probe_options)
