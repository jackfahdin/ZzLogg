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
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/zh_TW.ts
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
add_qt_translations_resource(KLOGG_QT_TRANSLATION_RES zh_CN zh_TW)

set(MAIN_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cli.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/klogg.qrc 
                 ${KLOGG_I18N_RES}
                 ${KLOGG_QT_TRANSLATION_RES})

set(KLOGG_UI_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/messagereceiver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/kloggapp.h
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationlanguage.h
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationlanguage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationsmokepaths.h
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationsmokepaths.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationrunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationrunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationrestart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/storagebootstrap.h
    ${CMAKE_CURRENT_SOURCE_DIR}/storagebootstrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/zzloggapplicationidentity.h
    ${CMAKE_CURRENT_SOURCE_DIR}/zzloggapplicationidentity.cpp
    ${DOCUMENTATION_RESOURCE}
    ${ICON_FILE}
)

set(KLOGG_GREP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/klogg_grep.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/storagebootstrap.h
    ${CMAKE_CURRENT_SOURCE_DIR}/storagebootstrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/zzloggapplicationidentity.h
    ${CMAKE_CURRENT_SOURCE_DIR}/zzloggapplicationidentity.cpp)

set(MAIN_LIBS
    klogg_logdata
    klogg_storage
    klogg_support
    roaring
    kdsingleapp
)

foreach(DOC_LANGUAGE IN ITEMS en zh_TW)
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/documentation-${DOC_LANGUAGE}.html
    COMMAND $<TARGET_FILE:maddy>
      ${CMAKE_SOURCE_DIR}/docs/i18n/${DOC_LANGUAGE}/DOCUMENTATION.md
      ${CMAKE_BINARY_DIR}/generated/documentation-${DOC_LANGUAGE}.html
    DEPENDS maddy ${CMAKE_SOURCE_DIR}/docs/i18n/${DOC_LANGUAGE}/DOCUMENTATION.md
    VERBATIM)
endforeach()
add_custom_target(zzlogg_documentation DEPENDS ${DOCUMENTATION_HTML}
  ${CMAKE_BINARY_DIR}/generated/documentation-en.html
  ${CMAKE_BINARY_DIR}/generated/documentation-zh_TW.html)

add_executable(klogg ${OS_BUNDLE} ${MAIN_SOURCES}
  ${KLOGG_UI_COMMON_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
# Keep the experimental command-line frontend available for explicit builds,
# but do not include it in the default build until its runtime issues are fixed.
add_executable(klogg_grep EXCLUDE_FROM_ALL ${MAIN_SOURCES} ${KLOGG_GREP_SOURCES})

add_dependencies(ci_build klogg)
add_dependencies(klogg zzlogg_documentation)

set_target_properties(klogg PROPERTIES
  OUTPUT_NAME "${ZZLOGG_PRODUCT_NAME}"
  AUTORCC ON
  AUTOMOC ON)
set_target_properties(klogg_grep PROPERTIES
  OUTPUT_NAME "${ZZLOGG_PRODUCT_NAME}_grep"
  AUTORCC ON
  AUTOMOC ON)

if(KLOGG_USE_LTO)
  set_property(TARGET klogg PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET klogg_grep PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_compile_features(klogg PRIVATE cxx_std_20)
target_link_libraries(klogg PUBLIC ${MAIN_LIBS} klogg_ui zzlogg_ui_runtime)
klogg_copy_ui_runtime_dlls(klogg)
target_link_libraries(klogg_grep PUBLIC ${MAIN_LIBS})

if(WIN32)

  target_sources(klogg PRIVATE ${ProductVersionResourceFiles})
  target_sources(klogg_grep PRIVATE ${GrepProductVersionResourceFiles})

elseif(APPLE)
  set_source_files_properties(${ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties(${NOTICE_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${COPYING_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)

  set_source_files_properties(${README_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${DOCUMENTATION_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${DOCUMENTATION_HTML} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)

  set(MACOSX_BUNDLE_BUNDLE_DISPLAY_NAME "${ZZLOGG_PRODUCT_NAME}")
  set(MACOSX_BUNDLE_BUNDLE_NAME "${ZZLOGG_PRODUCT_NAME}")
  set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
  set(MACOSX_BUNDLE_COPYRIGHT ${COPYRIGHT})
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "${ZZLOGG_IDENTIFIER}")
  set(MACOSX_BUNDLE_ICON_FILE "ZzLogg.icns")

endif()

install(
  TARGETS klogg
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)

set(KLOGG_ZZ_RUNTIME_TARGETS
  ZzCore
  ZzWindowKit
  ZzFluentFoundation
  ZzFluentUI
  ZzLog)
foreach(zz_runtime_target IN LISTS KLOGG_ZZ_RUNTIME_TARGETS)
  if(NOT TARGET ${zz_runtime_target})
    message(FATAL_ERROR "Required ZzLogg runtime target is unavailable: ${zz_runtime_target}")
  endif()
endforeach()
install(TARGETS ${KLOGG_ZZ_RUNTIME_TARGETS}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(UNIX AND NOT APPLE)
  set_target_properties(klogg PROPERTIES
    INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  set_target_properties(${KLOGG_ZZ_RUNTIME_TARGETS} PROPERTIES
    INSTALL_RPATH "\$ORIGIN")

  qt_generate_deploy_app_script(
    TARGET klogg
    OUTPUT_SCRIPT KLOGG_LINUX_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR)
  install(SCRIPT ${KLOGG_LINUX_DEPLOY_SCRIPT})
endif()

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
    set(KLOGG_RUNTIME_ROOT "${CMAKE_BINARY_DIR}/runtime/$<CONFIG>")
    set(KLOGG_RUNTIME_DIR "${KLOGG_RUNTIME_ROOT}/${ZZLOGG_PRODUCT_NAME}-runtime")

    set(KLOGG_RUNTIME_DEPENDENCIES klogg)
    set(KLOGG_RUNTIME_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:klogg> "${KLOGG_RUNTIME_DIR}")

    foreach(runtime_target tbb tbbmalloc tbbmalloc_proxy)
      if(TARGET ${runtime_target})
        list(APPEND KLOGG_RUNTIME_DEPENDENCIES ${runtime_target})
        list(APPEND KLOGG_RUNTIME_COMMANDS
             COMMAND ${CMAKE_COMMAND} -E copy_if_different
                     $<TARGET_FILE:${runtime_target}> "${KLOGG_RUNTIME_DIR}"
        )
      endif()
    endforeach()

    foreach(runtime_file IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
      if(EXISTS "${runtime_file}")
        list(APPEND KLOGG_RUNTIME_COMMANDS
             COMMAND ${CMAKE_COMMAND} -E copy_if_different
                     "${runtime_file}" "${KLOGG_RUNTIME_DIR}"
        )
      endif()
    endforeach()

    add_custom_target(
      zzlogg_runtime_folder
      COMMAND
        ${CMAKE_COMMAND}
        "-DAPP_EXECUTABLE=$<TARGET_FILE:klogg>"
        "-DDEPLOY_DIR=${KLOGG_RUNTIME_DIR}"
        "-DWINDEPLOYQT=${KLOGG_WINDEPLOYQT_EXECUTABLE}"
        "-DCONFIG=$<CONFIG>"
        -P "${CMAKE_SOURCE_DIR}/cmake/DeployPortable.cmake"
      COMMAND
        ${CMAKE_COMMAND}
        "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        "-DDEPLOY_DIR=${KLOGG_RUNTIME_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/StageRuntimeLicenses.cmake"
      ${KLOGG_RUNTIME_COMMANDS}
      DEPENDS ${KLOGG_RUNTIME_DEPENDENCIES}
      COMMENT "Creating ${ZZLOGG_PRODUCT_NAME} runtime folder"
      COMMAND_EXPAND_LISTS
      VERBATIM
    )
    set_property(TARGET zzlogg_runtime_folder PROPERTY
      ZZLOGG_RUNTIME_DIR "${KLOGG_RUNTIME_DIR}")

  else()
    message(WARNING "windeployqt was not found; runtime folder target is unavailable")
  endif()
endif()
