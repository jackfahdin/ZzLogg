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

Var ZzLoggMode
Var ZzLoggLocator
Var ZzLoggTarget
Var ZzLoggTxDir
Var ZzLoggTxRoot
Var ZzLoggStaging
Var ZzLoggTargetPin
Var ZzLoggIdentity

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
    ; Restricted upgrade/recovery entry (design spec section 4). Parsed from the
    ; raw command line; both modes forbid /D= because a restricted run never
    ; changes the registered target, and both Quit before any page can display.
    ; Every failure branch SetErrorLevel before Abort (a bare Abort exits 0) and
    ; every MessageBox carries /SD IDOK so a silent run never blocks on a dialog;
    ; the codes are the engine process contract (txcontract_win_p.h).
    StrCpy $ZzLoggMode ""
    ${StrStr} $2 $0 "/ZzLoggUpgrade="
    StrCmp $2 "" zzlogg_init_recover_check
        StrCmp $1 "" zzlogg_upgrade_switch_ok
            MessageBox MB_ICONSTOP "Upgrade mode does not accept /D=: the registered installation directory cannot change." /SD IDOK
            ; txcontract_win_p.h detail::UsageRejected
            SetErrorLevel 2
            Abort
        zzlogg_upgrade_switch_ok:
        StrCpy $ZzLoggMode "upgrade"
        StrCpy $ZzLoggLocator $2 "" 15
        Call ZzLoggValidateLocator
        Pop $3
        StrCmp $3 "ok" 0 zzlogg_switch_bad
        Call ZzLoggRestrictedUpgrade
        Quit
    zzlogg_init_recover_check:
    ${StrStr} $2 $0 "/ZzLoggRecover="
    StrCmp $2 "" zzlogg_init_done
        StrCmp $1 "" zzlogg_recover_switch_ok
            MessageBox MB_ICONSTOP "Recovery mode does not accept /D=: the registered installation directory cannot change." /SD IDOK
            ; txcontract_win_p.h detail::UsageRejected
            SetErrorLevel 2
            Abort
        zzlogg_recover_switch_ok:
        StrCpy $ZzLoggMode "recover"
        StrCpy $ZzLoggLocator $2 "" 15
        Call ZzLoggValidateLocator
        Pop $3
        StrCmp $3 "ok" 0 zzlogg_switch_bad
        Call ZzLoggRestrictedRecover
        Quit
    zzlogg_switch_bad:
        MessageBox MB_ICONSTOP "Malformed restricted-mode locator." /SD IDOK
        ; txcontract_win_p.h detail::UsageRejected
        SetErrorLevel 2
        Abort
    zzlogg_init_done:
FunctionEnd

Function ZzLoggValidateLocator
    ; The locator is byte-identical to the engine's parseTxid acceptance:
    ; exactly 16 lowercase hexadecimal digits, nonzero. IntCmp label order is
    ; (equal, less, greater): only an exactly-16 length falls into the scan;
    ; shorter and longer both reject. The per-character scan requires each
    ; character inside the lowercase hex alphabet (StrStr yields "" for any
    ; other character) and tracks whether any non-zero digit was seen.
    StrLen $3 $ZzLoggLocator
    IntCmp $3 16 zzlogg_locator_scan_init zzlogg_locator_bad zzlogg_locator_bad
    zzlogg_locator_scan_init:
    StrCpy $5 0
    StrCpy $6 0
    zzlogg_locator_scan:
        IntCmp $5 16 zzlogg_locator_scanned 0 zzlogg_locator_scanned
        StrCpy $4 $ZzLoggLocator 1 $5
        ${StrStr} $7 "0123456789abcdef" $4
        StrCmp $7 "" zzlogg_locator_bad
        StrCmp $4 "0" zzlogg_locator_scan_next
        StrCpy $6 1
        zzlogg_locator_scan_next:
        IntOp $5 $5 + 1
        Goto zzlogg_locator_scan
    zzlogg_locator_scanned:
    StrCmp $6 1 zzlogg_locator_ok zzlogg_locator_bad
    zzlogg_locator_ok:
    Push "ok"
    Return
    zzlogg_locator_bad:
    Push "bad"
FunctionEnd

Function ZzLoggCheckRealDirectory
    ; $R8 in: path. "ok" only for an existing directory that is neither a
    ; reparse point nor a device; every component of the target is gated here.
    System::Call 'kernel32::GetFileAttributesW(w $R8) i .R9'
    IntCmp $R9 -1 zzlogg_component_bad 0 0
    IntOp $R7 $R9 & 0x440
    StrCmp $R7 0 0 zzlogg_component_bad
    IntOp $R7 $R9 & 0x10
    StrCmp $R7 0 zzlogg_component_bad 0
    Push "ok"
    Return
    zzlogg_component_bad:
    Push "bad"
FunctionEnd

Function ZzLoggVerifyTarget
    ; Independent recheck of the registration, marker, landing manifest and
    ; target path before any write; the verified directory is then pinned and
    ; its identity (volume serial + file index) captured for the entry log.
    ReadRegStr $ZzLoggTarget HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "InstallLocation"
    StrCmp $ZzLoggTarget "" zzlogg_verify_fail
    ReadRegDWORD $3 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "UpdateIdentitySchema"
    StrCmp $3 2 0 zzlogg_verify_fail
    StrCpy $4 $ZzLoggTarget 2
    StrCmp $4 '\\' zzlogg_verify_fail 0
    StrCpy $4 $ZzLoggTarget 1 1
    StrCmp $4 ":" 0 zzlogg_verify_fail
    StrCpy $4 $ZzLoggTarget 1 2
    StrCmp $4 '\' 0 zzlogg_verify_fail
    ${StrStr} $4 $ZzLoggTarget ".."
    StrCmp $4 "" 0 zzlogg_verify_fail
    IfFileExists "$ZzLoggTarget\.zzlogg-install-root" 0 zzlogg_verify_fail
    IfFileExists "$ZzLoggTarget\.zzlogg-files.manifest" 0 zzlogg_verify_fail
    ; Reparse points are rejected level by level: drive root, every
    ; intermediate component and the full target must be real directories.
    StrCpy $R8 $ZzLoggTarget 3
    Call ZzLoggCheckRealDirectory
    Pop $R9
    StrCmp $R9 "ok" 0 zzlogg_verify_fail
    StrLen $R5 $ZzLoggTarget
    StrCpy $R6 3
    zzlogg_verify_walk:
        IntCmp $R6 $R5 zzlogg_verify_walk_done 0 zzlogg_verify_walk_done
        StrCpy $R7 $ZzLoggTarget 1 $R6
        StrCmp $R7 '\' 0 zzlogg_verify_walk_next
        StrCpy $R8 $ZzLoggTarget $R6
        Call ZzLoggCheckRealDirectory
        Pop $R9
        StrCmp $R9 "ok" 0 zzlogg_verify_fail
        zzlogg_verify_walk_next:
        IntOp $R6 $R6 + 1
        Goto zzlogg_verify_walk
    zzlogg_verify_walk_done:
    StrCpy $R8 $ZzLoggTarget
    Call ZzLoggCheckRealDirectory
    Pop $R9
    StrCmp $R9 "ok" 0 zzlogg_verify_fail
    System::Call 'kernel32::CreateFileW(w $ZzLoggTarget, i 0x80000000, i 3, i 0, i 3, i 0x02200000, i 0) p .R0'
    IntCmp $R0 -1 zzlogg_verify_fail 0 0
    StrCmp $R0 0 zzlogg_verify_fail
    ; BY_HANDLE_FILE_INFORMATION: 13 DWORD members; R2 = volume serial (8),
    ; R3 = file index high (12), R4 = file index low (13).
    System::Call 'kernel32::GetFileInformationByHandle(p R0, *(i.R1, i, i, i, i, i, i, i.R2, i, i, i, i.R3, i.R4)) i .R5'
    StrCmp $R5 1 0 zzlogg_verify_fail_pin
    IntOp $R6 $R1 & 0x10
    StrCmp $R6 0 zzlogg_verify_fail_pin
    IntOp $R6 $R1 & 0x400
    StrCmp $R6 0 0 zzlogg_verify_fail_pin
    StrCpy $ZzLoggTargetPin $R0
    IntFmt $R2 "0x%08X" $R2
    IntFmt $R3 "0x%08X" $R3
    IntFmt $R4 "0x%08X" $R4
    StrCpy $ZzLoggIdentity "$R2:$R3:$R4"
    Return
    zzlogg_verify_fail_pin:
        System::Call 'kernel32::CloseHandle(p R0)'
    zzlogg_verify_fail:
        MessageBox MB_ICONSTOP "The registered ZzLogg installation failed the independent target recheck." /SD IDOK
        ; txcontract_win_p.h detail::exitForOutcome(TxOutcome::Rejected)
        SetErrorLevel 42
        Abort
FunctionEnd

Function ZzLoggCloseTargetPin
    StrCmp $ZzLoggTargetPin "" 0 zzlogg_close_pin
    Return
    zzlogg_close_pin:
    StrCmp $ZzLoggTargetPin "0" 0 zzlogg_close_pin_do
    Return
    zzlogg_close_pin_do:
    System::Call 'kernel32::CloseHandle(p $ZzLoggTargetPin)'
    StrCpy $ZzLoggTargetPin ""
FunctionEnd

Function ZzLoggRestrictedUpgrade
    Call ZzLoggVerifyTarget
    SetShellVarContext all
    StrCpy $ZzLoggTxRoot "$APPDATA\ZzLogg\UpdateTransactions"
    StrCpy $ZzLoggTxDir "$ZzLoggTxRoot\$ZzLoggLocator"
    StrCpy $ZzLoggStaging "$ZzLoggTxRoot\staging-$ZzLoggLocator"
    ; The journal directory belongs to the engine alone: TxJournal::open
    ; creates it exclusively, so an existing one is an interrupted
    ; transaction (busy), never a directory to adopt or pre-create.
    IfFileExists "$ZzLoggTxDir" zzlogg_upgrade_txdir_busy 0
    IfFileExists "$ZzLoggTxRoot" zzlogg_upgrade_root_ready 0
    CreateDirectory "$ZzLoggTxRoot"
    IfErrors zzlogg_upgrade_txdir_fail 0
    ; The protected root is hardened exactly once at creation: admins/SYSTEM
    ; full, authenticated users read, nothing inherited. The engine's
    ; protected-image check accepts only this exact ACL shape on the root.
    nsExec::ExecToLog 'icacls "$ZzLoggTxRoot" /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-18:(OI)(CI)F" "*S-1-5-11:(OI)(CI)R"'
    Pop $3
    StrCmp $3 "0" 0 zzlogg_upgrade_txdir_fail
    zzlogg_upgrade_root_ready:
    ; Staging is a hardened sibling of the journal directory, never inside it.
    CreateDirectory "$ZzLoggStaging"
    IfErrors zzlogg_upgrade_txdir_fail 0
    ; Administrators/SYSTEM full, users read, nothing inherited.
    nsExec::ExecToLog 'icacls "$ZzLoggStaging" /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-545:(OI)(CI)R"'
    Pop $3
    StrCmp $3 "0" 0 zzlogg_upgrade_txdir_fail
    SetOutPath "$ZzLoggStaging"
    File "/oname=$ZzLoggStaging\ZzLoggUpdateTx.exe" "txpayload\ZzLoggUpdateTx.exe"
    File "/oname=$ZzLoggStaging\files.manifest" "release\.zzlogg-files.manifest"
    FileOpen $4 "$ZzLoggStaging\nsis-entry.log" w
    IfErrors zzlogg_upgrade_entrylog_done 0
    FileWrite $4 "mode=upgrade locator=$ZzLoggLocator target=$ZzLoggTarget identity=$ZzLoggIdentity$\r$\n"
    FileClose $4
    zzlogg_upgrade_entrylog_done:
    ; Finalized engine argv (3C task 5): only non-secret parameters as
    ; flag/value pairs; transaction credentials travel solely through the
    ; current-user-private credential file named by the locator.
    ExecWait '"$ZzLoggStaging\ZzLoggUpdateTx.exe" --install "$ZzLoggTarget" --staging "$ZzLoggStaging" --txroot "$ZzLoggTxRoot" --txid "$ZzLoggLocator" --version "${VERSION}"' $3
    Call ZzLoggCloseTargetPin
    SetErrorLevel $3
    Quit
    zzlogg_upgrade_txdir_busy:
        MessageBox MB_ICONSTOP "A transaction with this locator already exists. Use recovery mode." /SD IDOK
        ; txcontract_win_p.h detail::InstallerRuntimeFailure
        SetErrorLevel 48
        Abort
    zzlogg_upgrade_txdir_fail:
        MessageBox MB_ICONSTOP "Cannot create the protected transaction directory." /SD IDOK
        ; txcontract_win_p.h detail::InstallerRuntimeFailure
        SetErrorLevel 48
        Abort
FunctionEnd

Function ZzLoggRestrictedRecover
    Call ZzLoggVerifyTarget
    SetShellVarContext all
    StrCpy $ZzLoggTxDir "$APPDATA\ZzLogg\UpdateTransactions\$ZzLoggLocator"
    ; Recovery targets only an existing protected transaction with a journal.
    IfFileExists "$ZzLoggTxDir\journal.log" 0 zzlogg_recover_missing
    CreateDirectory "$ZzLoggTxDir\recover"
    IfErrors zzlogg_recover_fail 0
    SetOutPath "$ZzLoggTxDir\recover"
    File "/oname=$ZzLoggTxDir\recover\ZzLoggUpdateTx.exe" "txpayload\ZzLoggUpdateTx.exe"
    ExecWait '"$ZzLoggTxDir\recover\ZzLoggUpdateTx.exe" --recover --install "$ZzLoggTarget" --txroot "$APPDATA\ZzLogg\UpdateTransactions" --txid "$ZzLoggLocator"' $3
    Call ZzLoggCloseTargetPin
    SetErrorLevel $3
    Quit
    zzlogg_recover_missing:
        MessageBox MB_ICONSTOP "No interrupted transaction with this name exists." /SD IDOK
        ; txcontract_win_p.h detail::InstallerRuntimeFailure
        SetErrorLevel 48
        Abort
    zzlogg_recover_fail:
        MessageBox MB_ICONSTOP "Cannot stage the recovery engine." /SD IDOK
        ; txcontract_win_p.h detail::InstallerRuntimeFailure
        SetErrorLevel 48
        Abort
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
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "UpdateIdentitySchema" 2
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
