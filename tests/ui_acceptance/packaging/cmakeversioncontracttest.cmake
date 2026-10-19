if(CMAKE_VERSION VERSION_LESS 3.23)
  message(FATAL_ERROR "Official Linux CI requires CMake 3.23 or newer; got ${CMAKE_VERSION}")
endif()
