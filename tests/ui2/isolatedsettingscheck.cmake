function(assert_ui2_isolated_settings settings_root)
  set(app_settings "${settings_root}/klogg/klogg.ini")
  set(session_settings "${settings_root}/klogg/klogg_session.ini")
  if(NOT EXISTS "${app_settings}")
    message(FATAL_ERROR
      "UI2 smoke did not write exact app settings path ${app_settings}")
  endif()
  if(NOT EXISTS "${session_settings}")
    message(FATAL_ERROR
      "UI2 smoke did not write exact session settings path ${session_settings}")
  endif()
endfunction()
