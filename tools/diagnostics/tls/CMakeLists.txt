# Explicit diagnostic only: not installed, deployed, or included in ci_build.
add_executable(zzlogg_tls_probe tlsprobe.cpp)
target_link_libraries(zzlogg_tls_probe PRIVATE Qt6::Core Qt6::Network)
target_compile_features(zzlogg_tls_probe PRIVATE cxx_std_17)
