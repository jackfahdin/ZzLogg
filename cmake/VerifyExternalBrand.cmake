if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(current_entry_files
  "${SOURCE_ROOT}/CMakeLists.txt"
  "${SOURCE_ROOT}/packaging/osx/distribution.xml"
  "${SOURCE_ROOT}/README.md"
  "${SOURCE_ROOT}/docs/BUILD.md"
  "${SOURCE_ROOT}/docs/DOCUMENTATION.md"
  "${SOURCE_ROOT}/.github/actions/agent-package-win/action.yml"
  "${SOURCE_ROOT}/.github/actions/docker-package/action.yml"
  "${SOURCE_ROOT}/.github/actions/agent-package-mac/action.yml"
  "${SOURCE_ROOT}/.github/workflows/ci-build.yml")

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

set(violations)
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

  foreach(forbidden IN LISTS forbidden_literals)
    string(FIND "${entry_content}" "${forbidden}" forbidden_position)
    if(NOT forbidden_position EQUAL -1)
      file(RELATIVE_PATH relative_file "${SOURCE_ROOT}" "${entry_file}")
      list(APPEND violations "${relative_file}: forbidden literal '${forbidden}'")
    endif()
  endforeach()
endforeach()

set(required_entry_literals
  "README.md|https://gitcode.com/JackfahdinQt/ZzLogg"
  "docs/BUILD.md|https://gitcode.com/JackfahdinQt/ZzLogg"
  "packaging/osx/distribution.xml|com.gitcode.jackfahdinqt.zzlogg"
  "packaging/windows/ZzLogg.nsi|ZzLogg.exe"
  "packaging/windows/ZzLogg.nsi|Delete \"$APPDATA\\ZzLogg\\ZzLogg_session.ini\""
  "packaging/windows/ZzLogg.nsi|SetShellVarContext current\n    Delete \"$SENDTO\\ZzLogg.lnk\"\n    SetShellVarContext all\n    Delete \"$SMPROGRAMS\\ZzLogg.lnk\""
  "docs/DOCUMENTATION.md|do not configure an update manifest URL"
  ".github/actions/agent-package-win/action.yml|klogg_portable_folder"
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
