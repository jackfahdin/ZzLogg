add_library(zzlogg_update_core STATIC src/version.cpp)
target_include_directories(zzlogg_update_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(zzlogg_update_core PUBLIC cxx_std_17)
