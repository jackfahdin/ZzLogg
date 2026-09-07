if(NOT DEFINED COMPILER_VERSION OR COMPILER_VERSION STREQUAL "")
  message(FATAL_ERROR "COMPILER_VERSION is required")
endif()
if(COMPILER_VERSION VERSION_LESS 13.1)
  message(FATAL_ERROR
    "Official Linux builds require GCC 13.1 or newer; got ${COMPILER_VERSION}")
endif()
