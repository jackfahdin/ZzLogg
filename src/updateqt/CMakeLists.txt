add_library(zzlogg_update_qt STATIC src/updatestate.cpp src/manifestfetcher.cpp src/installationidentity.cpp
  src/installedrelease.cpp include/zzlogg/updateqt/installedrelease.h
  include/zzlogg/updateqt/manifestfetcher.h
  include/zzlogg/updateqt/installationidentity.h
  src/packagecache.cpp include/zzlogg/updateqt/packagecache.h)
target_sources(zzlogg_update_qt PRIVATE src/packagedownloader.cpp include/zzlogg/updateqt/packagedownloader.h)
set_target_properties(zzlogg_update_qt PROPERTIES AUTOMOC ON)
target_sources(zzlogg_update_qt PRIVATE src/updateservice.cpp include/zzlogg/updateqt/updateservice.h)
target_sources(zzlogg_update_qt PRIVATE src/updateconfiguration.cpp)
target_sources(zzlogg_update_qt PRIVATE src/updatedownloadservice.cpp src/updatecachepaths.cpp
  include/zzlogg/updateqt/updatedownloadservice.h include/zzlogg/updateqt/updatecachepaths.h)
target_link_libraries(zzlogg_update_qt PRIVATE zzlogg_brand)
target_sources(zzlogg_update_qt PRIVATE src/installationprobe.cpp src/installationprobe_p.h)
if(WIN32)
  target_link_libraries(zzlogg_update_qt PRIVATE Advapi32)
endif()
target_include_directories(zzlogg_update_qt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(zzlogg_update_qt PRIVATE "../update/src")
target_compile_features(zzlogg_update_qt PUBLIC cxx_std_17)
target_link_libraries(zzlogg_update_qt PUBLIC zzlogg_update_core Qt6::Core Qt6::Network PRIVATE zzlogg_update_json)
