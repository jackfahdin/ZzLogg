zzlogg_update_clean_legacy_crt_flags()
# One source set, two native closures: the updater closure is static CRT and
# Qt-free; the application closure is dynamic CRT against the MD update core.
# Never link the MT closure into the application.
function(zzlogg_add_updater_handoff prefix static_crt)
  add_library(${prefix}_handoff STATIC handoffprotocol.cpp installlock_win.cpp
    installationactivity_win.cpp runtimecopy_win.cpp processidentity_win.cpp
    localchannel_win.cpp bootstrap_win.cpp coordinator.cpp txjournal_win.cpp
    txengine_win.cpp)
  target_include_directories(${prefix}_handoff PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(${prefix}_handoff PUBLIC cxx_std_17)
  target_link_libraries(${prefix}_handoff PUBLIC ${prefix}_execution PRIVATE bcrypt advapi32)
  zzlogg_update_runtime(${prefix}_handoff ${static_crt})
  target_include_directories(${prefix}_handoff PRIVATE ../update/src)
endfunction()
zzlogg_add_updater_handoff(zzlogg_updater TRUE)
zzlogg_add_updater_handoff(zzlogg_update FALSE)
add_executable(ZzLoggUpdate main.cpp)
target_link_libraries(ZzLoggUpdate PRIVATE zzlogg_updater_handoff)
zzlogg_update_runtime(ZzLoggUpdate TRUE)
# Formal deployment of the standalone update helper: the full Inno Setup package
# consumes the staged runtime tree, and the staged install carries the helper
# next to the application. Execution itself stays closed this phase.
install(TARGETS ZzLoggUpdate RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Payload-embedded transaction engine. Build-only this phase: the Inno Setup package
# extracts it into the protected staging (task 4 wiring); it is never installed
# next to the application and never joins the staged runtime tree.
add_executable(ZzLoggUpdateTx txengine_main.cpp)
target_link_libraries(ZzLoggUpdateTx PRIVATE zzlogg_updater_handoff)
zzlogg_update_runtime(ZzLoggUpdateTx TRUE)
# The Inno Setup staging step xcopies ZzLoggUpdateTx.exe from the build output, so
# the CI build target must produce it (ci_build otherwise builds klogg only).
add_dependencies(ci_build ZzLoggUpdateTx)
