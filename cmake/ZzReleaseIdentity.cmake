option(ZZLOGG_OFFICIAL_RELEASE "Build with an official ZzLogg release identity" OFF)
set(ZZLOGG_RELEASE_SEQUENCE "" CACHE STRING "Official release sequence")
set(ZZLOGG_RELEASE_CHANNEL "" CACHE STRING "Official release channel")
set(ZZLOGG_RELEASE_DATA_SCHEMA "" CACHE STRING "Official release data schema")
set(_ZZLOGG_RELEASE_IDENTITY_TEMPLATE
  "${CMAKE_CURRENT_LIST_DIR}/zzlogg_release_identity.h.in")

function(_zzlogg_validate_release_uint name value maximum)
  if("${value}" STREQUAL "")
    message(FATAL_ERROR "Official release identity: ${name} is required")
  endif()
  if(NOT "${value}" MATCHES "^(0|[1-9][0-9]*)$")
    message(FATAL_ERROR
      "Official release identity: ${name} must be canonical unsigned decimal")
  endif()

  string(LENGTH "${value}" value_length)
  string(LENGTH "${maximum}" maximum_length)
  if(value_length GREATER maximum_length
      OR (value_length EQUAL maximum_length
          AND "${value}" STRGREATER "${maximum}"))
    message(FATAL_ERROR "Official release identity: ${name} exceeds ${maximum}")
  endif()
endfunction()

function(zzlogg_configure_release_identity output)
  set(ZZLOGG_RELEASE_IDENTITY_AVAILABLE 0)
  set(ZZLOGG_RELEASE_IDENTITY_VERSION "")
  set(ZZLOGG_RELEASE_IDENTITY_SEQUENCE 0ULL)
  set(ZZLOGG_RELEASE_IDENTITY_CHANNEL "")
  set(ZZLOGG_RELEASE_IDENTITY_OS "")
  set(ZZLOGG_RELEASE_IDENTITY_ARCH "")
  set(ZZLOGG_RELEASE_IDENTITY_DATA_SCHEMA 0U)
  set(ZZLOGG_RELEASE_IDENTITY_UPDATER_PROTOCOL 1U)

  if(ZZLOGG_OFFICIAL_RELEASE)
    _zzlogg_validate_release_uint(
      "ZZLOGG_RELEASE_SEQUENCE" "${ZZLOGG_RELEASE_SEQUENCE}"
      "18446744073709551615")
    if(ZZLOGG_RELEASE_SEQUENCE STREQUAL "0")
      message(FATAL_ERROR
        "Official release identity: ZZLOGG_RELEASE_SEQUENCE must be nonzero")
    endif()
    _zzlogg_validate_release_uint(
      "ZZLOGG_RELEASE_DATA_SCHEMA" "${ZZLOGG_RELEASE_DATA_SCHEMA}"
      "4294967295")

    if(NOT ZZLOGG_RELEASE_CHANNEL STREQUAL "stable"
        AND NOT ZZLOGG_RELEASE_CHANNEL STREQUAL "preview")
      message(FATAL_ERROR
        "Official release identity: ZZLOGG_RELEASE_CHANNEL must be stable or preview")
    endif()
    if(NOT "${ZZLOGG_DISPLAY_VERSION}" MATCHES
        "^[0-9][0-9]\\.(0[1-9]|1[0-2])\\.[0-9][0-9]$")
      message(FATAL_ERROR
        "Official release identity: ZZLOGG_DISPLAY_VERSION must be YY.MM.PP")
    endif()
    if(DEFINED PROJECT_VERSION_TWEAK
        AND NOT "${PROJECT_VERSION_TWEAK}" STREQUAL ""
        AND NOT "${PROJECT_VERSION_TWEAK}" STREQUAL "0")
      message(FATAL_ERROR
        "Official release identity: PROJECT_VERSION_TWEAK must be zero")
    endif()

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
      message(FATAL_ERROR
        "Official release identity: the target operating system must be Windows")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
      message(FATAL_ERROR
        "Official release identity: the target pointer width must be 64 bits")
    endif()

    set(target_architecture "")
    if(NOT "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
      set(target_architecture "${CMAKE_GENERATOR_PLATFORM}")
    elseif(NOT "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "")
      set(target_architecture "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
    elseif(NOT "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "")
      set(target_architecture "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    string(TOLOWER "${target_architecture}" target_architecture)
    if(NOT target_architecture MATCHES "^(x64|amd64|x86_64)$")
      message(FATAL_ERROR
        "Official release identity: the compiler target architecture must be x64")
    endif()

    set(ZZLOGG_RELEASE_IDENTITY_AVAILABLE 1)
    set(ZZLOGG_RELEASE_IDENTITY_VERSION "${ZZLOGG_DISPLAY_VERSION}")
    set(ZZLOGG_RELEASE_IDENTITY_SEQUENCE "${ZZLOGG_RELEASE_SEQUENCE}ULL")
    set(ZZLOGG_RELEASE_IDENTITY_CHANNEL "${ZZLOGG_RELEASE_CHANNEL}")
    set(ZZLOGG_RELEASE_IDENTITY_OS "windows")
    set(ZZLOGG_RELEASE_IDENTITY_ARCH "x64")
    set(ZZLOGG_RELEASE_IDENTITY_DATA_SCHEMA "${ZZLOGG_RELEASE_DATA_SCHEMA}U")
  endif()

  get_filename_component(output_directory "${output}" DIRECTORY)
  file(MAKE_DIRECTORY "${output_directory}")
  configure_file(
    "${_ZZLOGG_RELEASE_IDENTITY_TEMPLATE}" "${output}" @ONLY)
endfunction()
