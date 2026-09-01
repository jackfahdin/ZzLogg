if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(current_entry_files
  "${SOURCE_ROOT}/CMakeLists.txt"
  "${SOURCE_ROOT}/packaging/osx/distribution.xml"
  "${SOURCE_ROOT}/README.md"
  "${SOURCE_ROOT}/docs/BUILD.md"
  "${SOURCE_ROOT}/docs/DOCUMENTATION.md"
  "${SOURCE_ROOT}/.github/CONTRIBUTING.md"
  "${SOURCE_ROOT}/src/app/CMakeLists.txt"
  "${SOURCE_ROOT}/src/crash_handler/src/crashhandler.cpp"
  "${SOURCE_ROOT}/cmake/ZzLoggBrand.cmake"
  "${SOURCE_ROOT}/cmake/ZzLoggIconInstall.cmake"
  "${SOURCE_ROOT}/packaging/osx/dmg_setup.scpt"
  "${SOURCE_ROOT}/.github/actions/agent-package-win/action.yml"
  "${SOURCE_ROOT}/.github/actions/docker-package/action.yml"
  "${SOURCE_ROOT}/.github/actions/agent-package-mac/action.yml"
  "${SOURCE_ROOT}/.github/workflows/ci-build.yml")

set(expected_scanned_entries
  "${SOURCE_ROOT}/src/app/CMakeLists.txt"
  "${SOURCE_ROOT}/cmake/ZzLoggBrand.cmake"
  "${SOURCE_ROOT}/cmake/ZzLoggIconInstall.cmake"
  "${SOURCE_ROOT}/packaging/osx/dmg_setup.scpt")

if(EXISTS "${SOURCE_ROOT}/packaging/windows/ZzLogg.nsi")
  list(APPEND current_entry_files "${SOURCE_ROOT}/packaging/windows/ZzLogg.nsi")
else()
  list(APPEND current_entry_files "${SOURCE_ROOT}/packaging/windows/klogg.nsi")
endif()

if(EXISTS "${SOURCE_ROOT}/packaging/linux/ZzLogg.desktop")
  list(APPEND current_entry_files "${SOURCE_ROOT}/packaging/linux/ZzLogg.desktop")
else()
  list(APPEND current_entry_files "${SOURCE_ROOT}/packaging/linux/klogg.desktop")
endif()

set(forbidden_literals
  "https://github.com/variar/klogg"
  "https://raw.githubusercontent.com/variar/klogg"
  "com.github.variar.klogg"
  "Applications\\klogg.exe"
  "klogg-portable"
  "packaging\\windows\\prepare_release.cmd"
  "packaging/linux/appimage/generate_appimage.sh"
  "packaging/windows/klogg.nsi"
  "packages/klogg-"
  "output/klogg.app"
  "ubuntu_appimage"
  "windows-x86-qt5")

list(APPEND forbidden_literals
  "klogg_crashpad_handler"
  "klogg_minidump_dump")

set(violations)
foreach(expected_entry IN LISTS expected_scanned_entries)
  list(FIND current_entry_files "${expected_entry}" expected_entry_index)
  if(expected_entry_index EQUAL -1)
    file(RELATIVE_PATH relative_entry "${SOURCE_ROOT}" "${expected_entry}")
    list(APPEND violations "external entry is not covered by forbidden-literal scan: ${relative_entry}")
  endif()
endforeach()

foreach(entry_file IN LISTS current_entry_files)
  if(NOT EXISTS "${entry_file}")
    list(APPEND violations "missing current entry: ${entry_file}")
    continue()
  endif()

  file(READ "${entry_file}" entry_content)
  if(entry_file MATCHES "README\\.md$")
    string(REGEX REPLACE
      "## 来源与许可[^#]*(#[^#][^#][^\n]*\n)?"
      ""
      entry_content
      "${entry_content}")
  endif()
  if(entry_file MATCHES "CONTRIBUTING\\.md$")
    string(FIND "${entry_content}" "## Upstream provenance" upstream_section_position)
    if(NOT upstream_section_position EQUAL -1)
      string(SUBSTRING "${entry_content}" 0 ${upstream_section_position} entry_content)
    endif()
  endif()

  foreach(forbidden IN LISTS forbidden_literals)
    string(FIND "${entry_content}" "${forbidden}" forbidden_position)
    if(NOT forbidden_position EQUAL -1)
      file(RELATIVE_PATH relative_file "${SOURCE_ROOT}" "${entry_file}")
      list(APPEND violations "${relative_file}: forbidden literal '${forbidden}'")
    endif()
  endforeach()
endforeach()

function(require_entry_literal relative_file description expected_literal)
  set(required_file "${SOURCE_ROOT}/${relative_file}")
  if(NOT EXISTS "${required_file}")
    list(APPEND violations "${relative_file}: required current entry is missing")
    set(violations "${violations}" PARENT_SCOPE)
    return()
  endif()

  file(READ "${required_file}" required_content)
  string(FIND "${required_content}" "${expected_literal}" required_position)
  if(required_position EQUAL -1)
    list(APPEND violations "${relative_file}: ${description}, expected '${expected_literal}'")
    set(violations "${violations}" PARENT_SCOPE)
  endif()
endfunction()

function(require_entry_line relative_file description expected_line)
  set(required_file "${SOURCE_ROOT}/${relative_file}")
  if(NOT EXISTS "${required_file}")
    list(APPEND violations "${relative_file}: required current entry is missing")
    set(violations "${violations}" PARENT_SCOPE)
    return()
  endif()

  file(READ "${required_file}" required_content)
  string(REPLACE "\r\n" "\n" required_content "${required_content}")
  set(padded_content "\n${required_content}\n")
  string(FIND "${padded_content}" "\n${expected_line}\n" required_position)
  if(required_position EQUAL -1)
    list(APPEND violations "${relative_file}: ${description}, expected exact line '${expected_line}'")
    set(violations "${violations}" PARENT_SCOPE)
  endif()
endfunction()

require_entry_line(
  "packaging/linux/ZzLogg.desktop" "desktop product name is not exact" "Name=ZzLogg")
require_entry_line(
  "packaging/linux/ZzLogg.desktop" "desktop main command is not exact" "Exec=ZzLogg %F")
require_entry_line(
  "packaging/linux/ZzLogg.desktop" "desktop icon is not exact" "Icon=ZzLogg")
require_entry_line(
  "packaging/linux/ZzLogg.desktop" "desktop actions list is not exact" "Actions=Session;NewInstance;")
require_entry_line(
  "packaging/linux/ZzLogg.desktop" "desktop session action is not exact" "Exec=ZzLogg --load-session %F")
require_entry_line(
  "packaging/linux/ZzLogg.desktop" "desktop new-instance action is not exact" "Exec=ZzLogg --multi %F")

require_entry_line(
  "cmake/ZzLoggBrand.cmake" "central product name is not exact" "set(ZZLOGG_PRODUCT_NAME \"ZzLogg\")")
require_entry_line(
  "cmake/ZzLoggBrand.cmake" "central product description is not exact" "set(ZZLOGG_PRODUCT_DESCRIPTION \"ZzLogg log viewer\")")
require_entry_line(
  "cmake/ZzLoggBrand.cmake" "central vendor is not exact" "set(ZZLOGG_VENDOR \"JackfahdinQt\")")
require_entry_line(
  "cmake/ZzLoggBrand.cmake" "central homepage is not exact" "set(ZZLOGG_HOMEPAGE_URL \"https://gitcode.com/JackfahdinQt/ZzLogg\")")
require_entry_line(
  "cmake/ZzLoggBrand.cmake" "central identifier is not exact" "set(ZZLOGG_IDENTIFIER \"com.gitcode.jackfahdinqt.zzlogg\")")
require_entry_line(
  "cmake/ZzLoggBrand.cmake" "crashpad helper name is not centralized" "set(ZZLOGG_CRASHPAD_HANDLER_NAME \"ZzLogg_crashpad_handler\")")
require_entry_line(
  "cmake/ZzLoggBrand.cmake" "minidump helper name is not centralized" "set(ZZLOGG_MINIDUMP_DUMP_NAME \"ZzLogg_minidump_dump\")")

require_entry_literal(
  "cmake/zzlogg_brand.h.in" "runtime crashpad helper name is not centralized" [=[inline constexpr char CrashpadHandlerName[] = "@ZZLOGG_CRASHPAD_HANDLER_NAME@";]=])
require_entry_literal(
  "cmake/zzlogg_brand.h.in" "runtime minidump helper name is not centralized" [=[inline constexpr char MinidumpDumpName[] = "@ZZLOGG_MINIDUMP_DUMP_NAME@";]=])

require_entry_literal(
  "CMakeLists.txt" "CPack name is not centralized" [=[set(CPACK_PACKAGE_NAME "${ZZLOGG_PRODUCT_NAME}")]=])
require_entry_literal(
  "CMakeLists.txt" "CPack description is not centralized" [=[set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${ZZLOGG_PRODUCT_DESCRIPTION}")]=])
require_entry_literal(
  "CMakeLists.txt" "CPack vendor is not centralized" [=[set(CPACK_PACKAGE_VENDOR "${ZZLOGG_VENDOR}")]=])
require_entry_literal(
  "CMakeLists.txt" "CPack homepage is not centralized" [=[set(CPACK_PACKAGE_HOMEPAGE_URL "${ZZLOGG_HOMEPAGE_URL}")]=])
require_entry_literal(
  "CMakeLists.txt" "CPack icon does not use the selected platform icon" [=[set(CPACK_PACKAGE_ICON "${ICON_FILE}")]=])
require_entry_literal(
  "CMakeLists.txt" "CPack strip executable is not ZzLogg" "set(CPACK_STRIP_FILES \"bin/ZzLogg\")")

require_entry_literal(
  "src/app/CMakeLists.txt" "macOS display name is not centralized" [=[set(MACOSX_BUNDLE_BUNDLE_DISPLAY_NAME "${ZZLOGG_PRODUCT_NAME}")]=])
require_entry_literal(
  "src/app/CMakeLists.txt" "macOS bundle name is not centralized" [=[set(MACOSX_BUNDLE_BUNDLE_NAME "${ZZLOGG_PRODUCT_NAME}")]=])
require_entry_literal(
  "src/app/CMakeLists.txt" "macOS identifier is not centralized" [=[set(MACOSX_BUNDLE_GUI_IDENTIFIER "${ZZLOGG_IDENTIFIER}")]=])
require_entry_literal(
  "src/app/CMakeLists.txt" "macOS icon name is not exact" "set(MACOSX_BUNDLE_ICON_FILE \"ZzLogg.icns\")")
require_entry_literal(
  "packaging/osx/dmg_setup.scpt" "DMG layout does not position the ZzLogg bundle" "set position of item \"ZzLogg.app\"")

require_entry_literal(
  "cmake/ZzLoggIconInstall.cmake" "Linux PNG install sizes are incomplete" "foreach(size IN ITEMS 16 32 48 64 128 256 512)")
require_entry_literal(
  "cmake/ZzLoggIconInstall.cmake" "Linux PNG external name is not ZzLogg.png" "RENAME ZzLogg.png")
require_entry_literal(
  "cmake/ZzLoggIconInstall.cmake" "Linux scalable icon source is missing" "hicolor/scalable/ZzLogg.svg")
require_entry_literal(
  "cmake/ZzLoggIconInstall.cmake" "Linux SVG external name is not ZzLogg.svg" "RENAME ZzLogg.svg")

require_entry_literal(
  "packaging/windows/ZzLogg.nsi" "custom NSIS include depends on the compiler working directory" [=[!include "${__FILEDIR__}\FileAssociation.nsh"]=])
require_entry_literal(
  "packaging/windows/ZzLogg.nsi" "installer does not consume the complete staged tree" [=[File /r /x .zzlogg-uninstall.nsh "release\*.*"]=])
require_entry_literal(
  "src/app/CMakeLists.txt" "build does not publish the centralized crashpad helper name" [=[${ZZLOGG_CRASHPAD_HANDLER_NAME}]=])
require_entry_literal(
  "src/app/CMakeLists.txt" "build does not publish the centralized minidump helper name" [=[${ZZLOGG_MINIDUMP_DUMP_NAME}]=])
require_entry_literal(
  "src/crash_handler/src/crashhandler.cpp" "runtime does not locate the centralized crashpad helper" [=[zzlogg::brand::CrashpadHandlerName]=])
require_entry_literal(
  "src/crash_handler/src/crashhandler.cpp" "runtime does not locate the centralized minidump helper" [=[zzlogg::brand::MinidumpDumpName]=])
require_entry_literal(
  ".github/actions/agent-package-win/action.yml" "Windows staging omits the OpenSSL crypto DLL" "xcopy /y \"%SSL_DIR%\\libcrypto-1_1-x64.dll\" release\\")
require_entry_literal(
  ".github/actions/agent-package-win/action.yml" "Windows staging omits the OpenSSL SSL DLL" "xcopy /y \"%SSL_DIR%\\libssl-1_1-x64.dll\" release\\")
require_entry_literal(
  ".github/actions/agent-package-win/action.yml" "Windows staging does not reject a missing SSL_DIR" "if not defined SSL_DIR (")
require_entry_literal(
  ".github/actions/agent-package-win/action.yml" "Windows staging does not check the OpenSSL crypto source" "if not exist \"%SSL_DIR%\\libcrypto-1_1-x64.dll\" (")
require_entry_literal(
  ".github/actions/agent-package-win/action.yml" "Windows staging does not check the OpenSSL SSL source" "if not exist \"%SSL_DIR%\\libssl-1_1-x64.dll\" (")
require_entry_literal(
  ".github/actions/agent-package-win/action.yml" "makensis does not preserve the repository-root working directory" "arguments: \"/NOCD -DVERSION=%KLOGG_VERSION% -DPLATFORM=%KLOGG_ARCH%\"")
require_entry_literal(
  ".github/actions/agent-package-win/action.yml" "Windows staging omits the branded crashpad helper" "xcopy /y \"%KLOGG_BUILD_ROOT%\\output\\ZzLogg_crashpad_handler.exe\" release\\")
require_entry_literal(
  ".github/actions/agent-package-win/action.yml" "Windows staging omits the branded minidump helper" "xcopy /y \"%KLOGG_BUILD_ROOT%\\output\\ZzLogg_minidump_dump.exe\" release\\")
require_entry_literal(
  ".github/actions/agent-package-win/action.yml" "Windows staging omits the unified runtime folder" "xcopy /e /i /y \"%KLOGG_BUILD_ROOT%\\runtime\\RelWithDebInfo\\ZzLogg-runtime\" release")
require_entry_literal(
  ".github/workflows/ci-build.yml" "Windows workflow does not publish the OpenSSL source directory" [=[echo "SSL_DIR=${{ github.workspace }}\openssl-1.1\${{ matrix.config.arch }}\bin" >> $GITHUB_ENV]=])
require_entry_literal(
  "docs/BUILD.md" "standard Qt 6 dependency list omits Core5Compat" "Qt 6 Core, Core5Compat, Gui, Widgets, Concurrent, Network, Xml, and Tools modules")
require_entry_literal(
  ".github/CONTRIBUTING.md" "contribution guide does not identify the current project" "contributing to ZzLogg")
require_entry_literal(
  ".github/CONTRIBUTING.md" "contribution guide does not use the current issue tracker" "https://gitcode.com/JackfahdinQt/ZzLogg/issues")

set(nsis_custom_include "${SOURCE_ROOT}/packaging/windows/FileAssociation.nsh")
if(NOT EXISTS "${nsis_custom_include}")
  list(APPEND violations "packaging/windows/ZzLogg.nsi: resolved custom include does not exist: ${nsis_custom_include}")
endif()

set(required_entry_literals
  "README.md|https://gitcode.com/JackfahdinQt/ZzLogg"
  "docs/BUILD.md|https://gitcode.com/JackfahdinQt/ZzLogg"
  "packaging/osx/distribution.xml|com.gitcode.jackfahdinqt.zzlogg"
  "packaging/windows/ZzLogg.nsi|ZzLogg.exe"
  "packaging/windows/ZzLogg.nsi|Delete \"$APPDATA\\ZzLogg\\ZzLogg_session.ini\""
  "packaging/windows/ZzLogg.nsi|SetShellVarContext current\n    Delete \"$SENDTO\\ZzLogg.lnk\"\n    SetShellVarContext all\n    Delete \"$SMPROGRAMS\\ZzLogg.lnk\""
  "docs/DOCUMENTATION.md|do not configure an update manifest URL"
  ".github/actions/agent-package-win/action.yml|zzlogg_runtime_folder"
  ".github/actions/agent-package-win/action.yml|packaging/windows/ZzLogg.nsi"
  ".github/actions/docker-package/action.yml|packages/ZzLogg-"
  ".github/actions/agent-package-mac/action.yml|output/ZzLogg.app"
  ".github/workflows/ci-build.yml|/usr/local/ZzLogg"
  "CMakeLists.txt|ZzLogg.desktop")

foreach(requirement IN LISTS required_entry_literals)
  string(REPLACE "|" ";" fields "${requirement}")
  list(GET fields 0 relative_file)
  list(GET fields 1 required_literal)
  set(required_file "${SOURCE_ROOT}/${relative_file}")
  if(NOT EXISTS "${required_file}")
    list(APPEND violations "${relative_file}: required current entry is missing")
    continue()
  endif()
  file(READ "${required_file}" required_content)
  if(relative_file STREQUAL "README.md")
    string(REGEX REPLACE
      "## 来源与许可[^#]*(#[^#][^#][^\n]*\n)?"
      ""
      required_content
      "${required_content}")
  endif()
  string(FIND "${required_content}" "${required_literal}" required_position)
  if(required_position EQUAL -1)
    list(APPEND violations "${relative_file}: missing required literal '${required_literal}'")
  endif()
endforeach()

set(removed_release_entries
  "AppImageBuilder.yml"
  "packaging/linux/appimage/generate_appimage.sh"
  "packaging/linux/arch/PKGBUILD"
  "packaging/linux/arch/.SRCINFO"
  "packaging/linux/gentoo/klogg-22.06.0.1289.ebuild"
  "packaging/linux/deb"
  "packaging/linux/rpm"
  "packaging/windows/chocolatey/klogg.nuspec"
  "packaging/windows/chocolatey/tools/chocolateyInstall.ps1"
  "packaging/windows/chocolatey/.gitignore"
  "packaging/windows/scoop/klogg.json"
  "packaging/windows/prepare_release.cmd"
  "packaging/windows/7z_klogg_listfile.txt"
  "packaging/windows/7z_pdb_listfile.txt"
  ".github/workflows/ci-release.yml")

foreach(removed_entry IN LISTS removed_release_entries)
  if(EXISTS "${SOURCE_ROOT}/${removed_entry}")
    list(APPEND violations "${removed_entry}: removed release entry still exists")
  endif()
endforeach()

if(violations)
  list(JOIN violations "\n  - " violation_report)
  message(FATAL_ERROR "External brand verification failed:\n  - ${violation_report}")
endif()

message(STATUS "External brand verification passed for current packaging and public documentation")
