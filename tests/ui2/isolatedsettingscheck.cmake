function(assert_ui2_isolated_settings settings_root settings_suffix)
  set(app_settings "${settings_root}/klogg/klogg${settings_suffix}")
  set(session_settings "${settings_root}/klogg/klogg_session${settings_suffix}")
  if(NOT EXISTS "${app_settings}")
    message(FATAL_ERROR
      "UI2 smoke did not write exact app settings path ${app_settings}")
  endif()
  if(NOT EXISTS "${session_settings}")
    message(FATAL_ERROR
      "UI2 smoke did not write exact session settings path ${session_settings}")
  endif()
endfunction()
