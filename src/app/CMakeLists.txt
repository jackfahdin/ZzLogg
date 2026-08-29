if(APPLE)
  set(OS_BUNDLE MACOSX_BUNDLE)
elseif(WIN32)
  set(OS_BUNDLE WIN32)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/documentation.qrc.in ${DOCUMENTATION_RESOURCE} @ONLY)

add_custom_command(
  OUTPUT ${DOCUMENTATION_HTML}
  COMMAND $<TARGET_FILE:maddy> ${DOCUMENTATION_FILE} ${DOCUMENTATION_HTML}
  DEPENDS maddy ${DOCUMENTATION_FILE}
)

set(TS_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/en.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/zh_CN.ts
)

find_package(Qt${QT_VERSION_MAJOR}LinguistTools REQUIRED)
qt6_add_translation(QM_FILES ${TS_FILES})

if (NOT TARGET Qt${QT_VERSION_MAJOR}::lupdate)
    message(FATAL_ERROR "The package \"Qt${QT_VERSION_MAJOR}LinguistTools\" is required.")
endif()
set(Qt_LUPDATE_EXECUTABLE Qt${QT_VERSION_MAJOR}::lupdate)
set(Qt_LCONVERT_EXECUTABLE Qt${QT_VERSION_MAJOR}::lconvert)
add_custom_target(lupdate
    COMMAND ${Qt_LUPDATE_EXECUTABLE} -recursive -no-obsolete ${CMAKE_SOURCE_DIR}/src -ts ${TS_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src
    COMMENT "Updating translations"
)

include(TranslationUtils)
add_app_translations_resource(KLOGG_I18N_RES ${QM_FILES})
add_qt_translations_resource(KLOGG_QT_TRANSLATION_RES zh_CN)

set(MAIN_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cli.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/klogg.qrc 
                 ${KLOGG_I18N_RES}
                 ${KLOGG_QT_TRANSLATION_RES})

set(KLOGG_UI_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/messagereceiver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/kloggapp.h
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationrunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationrunner.cpp
    ${DOCUMENTATION_RESOURCE}
    ${ICON_FILE}
)

set(KLOGG_GREP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/klogg_grep.cpp)

set(MAIN_LIBS
    klogg_logdata
    klogg_crash_handler
    roaring
    kdsingleapp
)

add_executable(klogg ${OS_BUNDLE} ${MAIN_SOURCES}
  ${KLOGG_UI_COMMON_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
add_executable(klogg_portable ${OS_BUNDLE} ${MAIN_SOURCES}
  ${KLOGG_UI_COMMON_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
# Keep the experimental command-line frontend available for explicit builds,
# but do not include it in the default build until its runtime issues are fixed.
add_executable(klogg_grep EXCLUDE_FROM_ALL ${MAIN_SOURCES} ${KLOGG_GREP_SOURCES})

add_dependencies(ci_build klogg)

if(WIN32)
  add_dependencies(ci_build klogg_portable)
endif()

set_target_properties(klogg PROPERTIES AUTORCC ON)
set_target_properties(klogg PROPERTIES AUTOMOC ON)
set_target_properties(klogg_portable PROPERTIES AUTORCC ON)
set_target_properties(klogg_portable PROPERTIES AUTOMOC ON)
set_target_properties(klogg_grep PROPERTIES AUTORCC ON)
set_target_properties(klogg_grep PROPERTIES AUTOMOC ON)

if(KLOGG_USE_LTO)
  set_property(TARGET klogg PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET klogg_portable PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET klogg_grep PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_link_libraries(klogg PUBLIC ${MAIN_LIBS} klogg_ui)
target_link_libraries(klogg_portable PUBLIC ${MAIN_LIBS} klogg_ui)
target_link_libraries(klogg_grep PUBLIC ${MAIN_LIBS})

target_compile_definitions(klogg_portable PUBLIC -DKLOGG_PORTABLE)

if(WIN32)

  target_sources(klogg PRIVATE ${ProductVersionResourceFiles})
  target_sources(klogg_portable PRIVATE ${ProductVersionResourceFiles})
  target_sources(klogg_grep PRIVATE ${ProductVersionResourceFiles})

elseif(APPLE)
  set_source_files_properties(${ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties(${NOTICE_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${COPYING_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)

  set_source_files_properties(${README_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${DOCUMENTATION_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${DOCUMENTATION_HTML} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)

  set(MACOSX_BUNDLE_BUNDLE_DISPLAY_NAME ${PROJECT_DESCRIPTION})
  set(MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME})
  set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
  set(MACOSX_BUNDLE_COPYRIGHT ${COPYRIGHT})
  set(MACOSX_BUNDLE_GUI_IDENTIFIER ${IDENTIFIER})
  set(MACOSX_BUNDLE_ICON_FILE ${ICON_NAME})

endif()

install(
  TARGETS klogg
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)

if(WIN32)
  if(TARGET tbb)
    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:tbb> $<TARGET_FILE_DIR:klogg>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy tbb"
    )
  endif()
  if(TARGET tbbmalloc_proxy)
    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:tbbmalloc> $<TARGET_FILE_DIR:klogg>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy tbbmalloc"
    )
    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:tbbmalloc_proxy> $<TARGET_FILE_DIR:klogg>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy tbbmalloc_proxy"
    )
  endif()
endif()

if(WIN32 AND TARGET Qt6::qmake)
  include(InstallRequiredSystemLibraries)

  get_target_property(KLOGG_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(KLOGG_QT_BIN_DIR "${KLOGG_QMAKE_EXECUTABLE}" DIRECTORY)
  find_program(KLOGG_WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${KLOGG_QT_BIN_DIR}")

  if(KLOGG_WINDEPLOYQT_EXECUTABLE)
    set(KLOGG_PORTABLE_ROOT "${CMAKE_BINARY_DIR}/portable/$<CONFIG>")
    set(KLOGG_PORTABLE_DIR "${KLOGG_PORTABLE_ROOT}/klogg-portable")
    set(KLOGG_PORTABLE_DOCUMENTS
        "${README_FILE};${COPYING_FILE};${NOTICE_FILE};${DOCUMENTATION_FILE}"
    )

    set(KLOGG_PORTABLE_DEPENDENCIES klogg_portable)
    set(KLOGG_PORTABLE_RUNTIME_COMMANDS)

    foreach(runtime_target tbb tbbmalloc tbbmalloc_proxy)
      if(TARGET ${runtime_target})
        list(APPEND KLOGG_PORTABLE_DEPENDENCIES ${runtime_target})
        list(APPEND KLOGG_PORTABLE_RUNTIME_COMMANDS
             COMMAND ${CMAKE_COMMAND} -E copy_if_different
                     $<TARGET_FILE:${runtime_target}> "${KLOGG_PORTABLE_DIR}"
        )
      endif()
    endforeach()

    foreach(runtime_file IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
      if(EXISTS "${runtime_file}")
        list(APPEND KLOGG_PORTABLE_RUNTIME_COMMANDS
             COMMAND ${CMAKE_COMMAND} -E copy_if_different
                     "${runtime_file}" "${KLOGG_PORTABLE_DIR}"
        )
      endif()
    endforeach()

    add_custom_target(
      klogg_portable_folder
      COMMAND
        ${CMAKE_COMMAND}
        "-DAPP_EXECUTABLE=$<TARGET_FILE:klogg_portable>"
        "-DDEPLOY_DIR=${KLOGG_PORTABLE_DIR}"
        "-DWINDEPLOYQT=${KLOGG_WINDEPLOYQT_EXECUTABLE}"
        "-DCONFIG=$<CONFIG>"
        "-DPORTABLE_DOCUMENTS=${KLOGG_PORTABLE_DOCUMENTS}"
        -P "${CMAKE_SOURCE_DIR}/cmake/DeployPortable.cmake"
      ${KLOGG_PORTABLE_RUNTIME_COMMANDS}
      DEPENDS ${KLOGG_PORTABLE_DEPENDENCIES}
      COMMENT "Creating portable klogg folder"
      VERBATIM
    )

  else()
    message(WARNING "windeployqt was not found; portable package targets are unavailable")
  endif()
endif()

if(KLOGG_USE_SENTRY)
  if(NOT WIN32)
    if(APPLE)
      find_program(DSYMUTIL dsymutil)
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND ${DSYMUTIL} --out=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/klogg.dSym $<TARGET_FILE:klogg>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:klogg>"
      )

      add_custom_command(
        TARGET klogg_portable
        POST_BUILD
        COMMAND ${DSYMUTIL} --out=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/klogg_portable.dSym $<TARGET_FILE:klogg_portable>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:klogg_portable>"
      )
    else()
      find_program(OBJCOPY objcopy)
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND ${OBJCOPY} --only-keep-debug --compress-debug-sections=zlib $<TARGET_FILE:klogg>
                ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/klogg.debug
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:klogg>"
      )
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND ${OBJCOPY} --strip-debug --strip-unneeded $<TARGET_FILE:klogg>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Stripping $<TARGET_FILE:klogg>"
      )
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND ${OBJCOPY} --add-gnu-debuglink=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/klogg.debug $<TARGET_FILE:klogg>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Linking symbols for $<TARGET_FILE:klogg>"
      )

      add_custom_command(
        TARGET klogg_portable
        POST_BUILD
        COMMAND ${OBJCOPY} --only-keep-debug $<TARGET_FILE:klogg_portable>
                ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/klogg_portable.debug
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:klogg>"
      )
      add_custom_command(
        TARGET klogg_portable
        POST_BUILD
        COMMAND ${OBJCOPY} --strip-debug --strip-unneeded $<TARGET_FILE:klogg_portable>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Stripping $<TARGET_FILE:klogg_portable>"
      )
      add_custom_command(
        TARGET klogg_portable
        POST_BUILD
        COMMAND ${OBJCOPY} --add-gnu-debuglink=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/klogg_portable.debug
                $<TARGET_FILE:klogg_portable>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Linking symbols for $<TARGET_FILE:klogg_portable>"
      )
    endif()

    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:crashpad_handler> $<TARGET_FILE_DIR:klogg>/klogg_crashpad_handler
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy crash_handler"
    )

    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy ${SYMS_TOOLS}/minidump_dump $<TARGET_FILE_DIR:klogg>/klogg_minidump_dump
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy stackwalk"
    )
  else()
    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:crashpad_handler> $<TARGET_FILE_DIR:klogg>/klogg_crashpad_handler.exe
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy crash_handler"
    )

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND cmake -E copy ${SYMS_TOOLS}/minidump_dump_x64.exe $<TARGET_FILE_DIR:klogg>/klogg_minidump_dump.exe
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Copy stackwalk"
      )
    else()
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND cmake -E copy ${SYMS_TOOLS}/minidump_dump_x86.exe $<TARGET_FILE_DIR:klogg>/klogg_minidump_dump.exe
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Copy stackwalk"
      )
    endif()
  endif()

  if(APPLE)
    install(PROGRAMS $<TARGET_FILE_DIR:klogg>/klogg_minidump_dump $<TARGET_FILE_DIR:klogg>/klogg_crashpad_handler
            DESTINATION $<TARGET_BUNDLE_CONTENT_DIR:klogg>/MacOS
    )
  elseif(NOT WIN32)
    install(PROGRAMS $<TARGET_FILE_DIR:klogg>/klogg_minidump_dump $<TARGET_FILE_DIR:klogg>/klogg_crashpad_handler
            DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
endif(KLOGG_USE_SENTRY)
