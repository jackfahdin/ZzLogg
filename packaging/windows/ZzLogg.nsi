# NSIS script creating the Windows installer for ZzLogg

# Is passed to the script using -DVERSION=$(git describe) on the command line
!ifndef VERSION
    !define VERSION 'dev-build'
!endif

!ifndef PLATFORM
    !define PLATFORM 'unknown'
!endif

!include "MUI2.nsh"
!include "StrFunc.nsh"
!include "${__FILEDIR__}\FileAssociation.nsh"

${StrStr}

OutFile "ZzLogg-${VERSION}-${PLATFORM}-Qt6-setup.exe"
XpStyle on
SetCompressor /SOLID lzma

!ifdef ARCH32
  InstallDir "$PROGRAMFILES\ZzLogg"
!else
  InstallDir "$PROGRAMFILES64\ZzLogg"
!endif
InstallDirRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "InstallLocation"

!define MUI_ICON "Resources\ZzLogg.ico"
RequestExecutionLevel admin

Name "ZzLogg"
Caption "ZzLogg ${VERSION} Setup"

!define MUI_WELCOMEPAGE_TITLE "Welcome to the ZzLogg ${VERSION} Setup Wizard"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of ZzLogg\
, a fast, advanced log explorer.$\r$\n$\r$\n\
ZzLogg and the Qt libraries are released under the GPL, see \
the COPYING and NOTICE files in licenses\ZzLogg.$\r$\n$\r$\n$_CLICK"

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

Function .onInit
!ifdef ARCH32
    SetRegView 32
!else
    SetRegView 64
!endif
    System::Call 'kernel32::GetCommandLine()t.r0'
    ${StrStr} $1 $0 " /D="
    StrCmp $1 "" 0 zzlogg_on_init_done
!ifdef ARCH32
    StrCpy $INSTDIR "$PROGRAMFILES\ZzLogg"
!else
    StrCpy $INSTDIR "$PROGRAMFILES64\ZzLogg"
!endif
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "InstallLocation"
    StrCmp $0 "" zzlogg_on_init_done
    StrCpy $INSTDIR "$0"
zzlogg_on_init_done:
FunctionEnd

Function un.onInit
!ifdef ARCH32
    SetRegView 32
!else
    SetRegView 64
!endif
FunctionEnd

Section "ZzLogg application and runtime" zzlogg
    SectionIn RO

    SetOutPath "$INSTDIR"
    File /r /x .zzlogg-uninstall.nsh "release\*.*"
    FileOpen $0 "$INSTDIR\.zzlogg-install-root" w
    FileWrite $0 "ZzLogg ${VERSION}$\r$\n"
    FileClose $0

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
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "UpdateIdentitySchema" 1
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "DisplayName" "ZzLogg"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "DisplayVersion" "${VERSION}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "NoModify" "1"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "NoRepair" "1"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Create Start menu shortcut" shortcut
    SetShellVarContext all
    CreateShortCut "$SMPROGRAMS\ZzLogg.lnk" "$INSTDIR\ZzLogg.exe" "" "$INSTDIR\ZzLogg.exe" 0
SectionEnd

Section /o "Associate with .log files" associate
    ${registerExtension} "$INSTDIR\ZzLogg.exe" ".log" "ZzLogg log file"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${zzlogg} "ZzLogg and all required runtime files."
    !insertmacro MUI_DESCRIPTION_TEXT ${shortcut} "Create a shortcut in the Start menu for ZzLogg."
    !insertmacro MUI_DESCRIPTION_TEXT ${associate} "Make ZzLogg the default viewer for .log files."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    !include "release\.zzlogg-uninstall.nsh"

    Delete "$APPDATA\ZzLogg\ZzLogg.ini"
    Delete "$APPDATA\ZzLogg\ZzLogg_session.ini"
    RMDir "$APPDATA\ZzLogg"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"
    SetRegView 32
    DeleteRegKey HKLM "Software\ZzLogg"
!ifdef ARCH32
    SetRegView 32
!else
    SetRegView 64
!endif
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
