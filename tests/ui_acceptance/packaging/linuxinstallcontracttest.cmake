file(READ "${APP_CMAKE}" content)
foreach(required IN ITEMS
    "set(KLOGG_ZZ_RUNTIME_TARGETS"
    "ZzCore" "ZzWindowKit" "ZzFluentFoundation" "ZzFluentUI" "ZzLog"
    [=[INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"]=]
    [=[INSTALL_RPATH "\$ORIGIN"]=]
    [=[LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}]=])
  string(FIND "${content}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Linux install rules are missing: ${required}")
  endif()
endforeach()
