# Proves the two same-source closures keep their CRT separation: the updater
# closure stays static CRT while the application closure is dynamic CRT, and
# the application executable still imports the dynamic CRT after linking the
# application-side handoff closure.
if(NOT EXISTS "${DUMPBIN}" OR NOT EXISTS "${MD_LIBRARY}" OR NOT EXISTS "${MT_LIBRARY}")
  message(FATAL_ERROR "CRT closure inspection requires dumpbin and both handoff archives")
endif()
foreach(pair IN ITEMS "MD_LIBRARY:msvcrt" "MT_LIBRARY:libcmt")
  string(REPLACE ":" ";" parts "${pair}")
  list(GET parts 0 variable)
  list(GET parts 1 expected)
  execute_process(COMMAND "${DUMPBIN}" /DIRECTIVES "${${variable}}"
    RESULT_VARIABLE result OUTPUT_VARIABLE directives ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect archive ${${variable}}: ${error}")
  endif()
  string(TOLOWER "${directives}" directives)
  if(NOT directives MATCHES "defaultlib:[\"]?${expected}")
    message(FATAL_ERROR "Expected CRT evidence ${expected} missing for ${${variable}}")
  endif()
endforeach()
execute_process(COMMAND "${DUMPBIN}" /DIRECTIVES "${MD_LIBRARY}"
  RESULT_VARIABLE result OUTPUT_VARIABLE md_directives ERROR_VARIABLE error)
string(TOLOWER "${md_directives}" md_directives)
if(md_directives MATCHES "defaultlib:[\"]?libcmt")
  message(FATAL_ERROR "Static CRT leaked into the application handoff closure")
endif()
if(DEFINED APP)
  execute_process(COMMAND "${DUMPBIN}" /IMPORTS "${APP}"
    RESULT_VARIABLE result OUTPUT_VARIABLE imports ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect application executable: ${error}")
  endif()
  string(TOLOWER "${imports}" imports)
  if(NOT imports MATCHES "ucrtbase|msvcp[0-9]")
    message(FATAL_ERROR "Application executable lost its dynamic CRT imports")
  endif()
endif()
message(STATUS "Application and updater closures keep their CRT separation")
