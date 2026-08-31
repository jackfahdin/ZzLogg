# NSIS script creating the Windows installer for ZzLogg

# Is passed to the script using -DVERSION=$(git describe) on the command line
!ifndef VERSION
    !define VERSION 'dev-build'
!endif

!ifndef PLATFORM
    !define PLATFORM 'unknown'
!endif

!include "MUI2.nsh"
!include "${__FILEDIR__}\FileAssociation.nsh"

OutFile "ZzLogg-${VERSION}-${PLATFORM}-Qt6-setup.exe"
XpStyle on
SetCompressor /SOLID lzma

!ifdef ARCH32
  InstallDir "$PROGRAMFILES\ZzLogg"
!else
  InstallDir "$PROGRAMFILES64\ZzLogg"
!endif
InstallDirRegKey HKLM Software\ZzLogg ""

!define MUI_ICON "Resources\ZzLogg.ico"
RequestExecutionLevel admin

Name "ZzLogg"
Caption "ZzLogg ${VERSION} Setup"

!define MUI_WELCOMEPAGE_TITLE "Welcome to the ZzLogg ${VERSION} Setup Wizard"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of ZzLogg\
, a fast, advanced log explorer.$\r$\n$\r$\n\
ZzLogg and the Qt libraries are released under the GPL, see \
the COPYING and NOTICE files.$\r$\n$\r$\n$_CLICK"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Section "ZzLogg" zzlogg
    SectionIn RO

    SetOutPath $INSTDIR
    File release\ZzLogg.exe
    File release\klogg_crashpad_handler.exe
    File release\klogg_minidump_dump.exe
    File release\tbb12.dll

    File COPYING
    File NOTICE
    File README.md
    File docs\DOCUMENTATION.md
    File release\documentation.html

    SetShellVarContext current
    CreateShortCut "$SENDTO\ZzLogg.lnk" "$INSTDIR\ZzLogg.exe" "" "$INSTDIR\ZzLogg.exe" 0

    WriteRegStr HKCR "Applications\ZzLogg.exe" "" ""
    WriteRegStr HKCR "Applications\ZzLogg.exe\shell" "" "open"
    WriteRegStr HKCR "Applications\ZzLogg.exe\shell\open" "ZzLogg log viewer" "ZzLogg"
    WriteRegStr HKCR "Applications\ZzLogg.exe\shell\open\command" "" '"$INSTDIR\ZzLogg.exe" "%1"'
    WriteRegStr HKCR "*\OpenWithList\ZzLogg.exe" "" ""
    WriteRegStr HKCR ".txt\OpenWithList\ZzLogg.exe" "" ""
    WriteRegStr HKCR ".Log\OpenWithList\ZzLogg.exe" "" ""
    WriteRegStr HKCR ".cap\OpenWithList\ZzLogg.exe" "" ""

    WriteRegExpandStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"\
"UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegExpandStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"\
"InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "DisplayName" "ZzLogg"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "DisplayVersion" "${VERSION}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "NoModify" "1"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "NoRepair" "1"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Qt 6 Runtime libraries" qtlibs
    SetOutPath $INSTDIR
    File release\Qt6Core.dll
    File release\Qt6Gui.dll
    File release\Qt6Network.dll
    File release\Qt6Widgets.dll
    File release\Qt6Concurrent.dll
    File release\Qt6Xml.dll
    File release\Qt6Core5Compat.dll

    SetOutPath $INSTDIR\platforms
    File release\platforms\qwindows.dll
    SetOutPath $INSTDIR\styles
    File release\styles\qmodernwindowsstyle.dll
SectionEnd

Section "MSVC Runtime libraries" vcruntime
    SetOutPath $INSTDIR
    File release\msvcp140.dll
    File release\msvcp140_1.dll
    File release\vcruntime140.dll

!if ${PLATFORM} == "x64"
    File release\vcruntime140_1.dll
    File release\libcrypto-1_1-x64.dll
    File release\libssl-1_1-x64.dll
!else
    File release\libcrypto-1_1.dll
    File release\libssl-1_1.dll
!endif
SectionEnd

Section "Create Start menu shortcut" shortcut
    SetShellVarContext all
    CreateShortCut "$SMPROGRAMS\ZzLogg.lnk" "$INSTDIR\ZzLogg.exe" "" "$INSTDIR\ZzLogg.exe" 0
SectionEnd

Section /o "Associate with .log files" associate
    ${registerExtension} "$INSTDIR\ZzLogg.exe" ".log" "ZzLogg log file"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${zzlogg} "The core files required to use ZzLogg."
    !insertmacro MUI_DESCRIPTION_TEXT ${qtlibs} "Qt 6 libraries required by ZzLogg."
    !insertmacro MUI_DESCRIPTION_TEXT ${vcruntime} "Microsoft Visual C++ runtime libraries required by ZzLogg."
    !insertmacro MUI_DESCRIPTION_TEXT ${shortcut} "Create a shortcut in the Start menu for ZzLogg."
    !insertmacro MUI_DESCRIPTION_TEXT ${associate} "Make ZzLogg the default viewer for .log files."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    Delete "$INSTDIR\Uninstall.exe"
    Delete "$INSTDIR\ZzLogg.exe"
    Delete "$INSTDIR\klogg_crashpad_handler.exe"
    Delete "$INSTDIR\klogg_minidump_dump.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\COPYING"
    Delete "$INSTDIR\NOTICE"
    Delete "$INSTDIR\DOCUMENTATION.md"
    Delete "$INSTDIR\documentation.html"
    Delete "$INSTDIR\Qt6Widgets.dll"
    Delete "$INSTDIR\Qt6Core.dll"
    Delete "$INSTDIR\Qt6Gui.dll"
    Delete "$INSTDIR\Qt6Network.dll"
    Delete "$INSTDIR\Qt6Concurrent.dll"
    Delete "$INSTDIR\Qt6Xml.dll"
    Delete "$INSTDIR\Qt6Core5Compat.dll"
    Delete "$INSTDIR\platforms\qwindows.dll"
    Delete "$INSTDIR\styles\qmodernwindowsstyle.dll"
    Delete "$INSTDIR\msvcp140.dll"
    Delete "$INSTDIR\msvcp140_1.dll"
    Delete "$INSTDIR\vcruntime140.dll"
    Delete "$INSTDIR\vcruntime140_1.dll"
    Delete "$INSTDIR\tbb12.dll"
    Delete "$INSTDIR\libcrypto-1_1-x64.dll"
    Delete "$INSTDIR\libssl-1_1-x64.dll"
    Delete "$INSTDIR\libcrypto-1_1.dll"
    Delete "$INSTDIR\libssl-1_1.dll"
    RMDir "$INSTDIR\platforms"
    RMDir "$INSTDIR\styles"
    RMDir "$INSTDIR"

    Delete "$APPDATA\ZzLogg\ZzLogg.ini"
    Delete "$APPDATA\ZzLogg\ZzLogg_session.ini"
    RMDir "$APPDATA\ZzLogg"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"
    DeleteRegKey HKLM "Software\ZzLogg"
    ${unregisterExtension} ".log" "ZzLogg log file"

    DeleteRegKey HKCR "*\OpenWithList\ZzLogg.exe"
    DeleteRegKey HKCR ".txt\OpenWithList\ZzLogg.exe"
    DeleteRegKey HKCR ".Log\OpenWithList\ZzLogg.exe"
    DeleteRegKey HKCR ".cap\OpenWithList\ZzLogg.exe"
    DeleteRegKey HKCR "Applications\ZzLogg.exe\shell\open\command"
    DeleteRegKey HKCR "Applications\ZzLogg.exe\shell\open"
    DeleteRegKey HKCR "Applications\ZzLogg.exe\shell"
    DeleteRegKey HKCR "Applications\ZzLogg.exe"

    SetShellVarContext current
    Delete "$SENDTO\ZzLogg.lnk"
    SetShellVarContext all
    Delete "$SMPROGRAMS\ZzLogg.lnk"
SectionEnd
