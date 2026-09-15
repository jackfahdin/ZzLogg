add_library(zzlogg_update_qt STATIC src/updatestate.cpp src/manifestfetcher.cpp
  include/zzlogg/updateqt/manifestfetcher.h)
set_target_properties(zzlogg_update_qt PROPERTIES AUTOMOC ON)
target_include_directories(zzlogg_update_qt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(zzlogg_update_qt PRIVATE "../update/src")
target_compile_features(zzlogg_update_qt PUBLIC cxx_std_17)
target_link_libraries(zzlogg_update_qt PUBLIC zzlogg_update_core Qt6::Core Qt6::Network PRIVATE zzlogg_update_json)
