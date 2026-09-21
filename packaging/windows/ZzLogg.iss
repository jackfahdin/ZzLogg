; Inno Setup 7.1.0. Compile from the repository root with /DVERSION and /DPLATFORM.
#ifndef VERSION
  #define VERSION "dev-build"
#endif
#ifndef PLATFORM
  #define PLATFORM "x64"
#endif
#if PLATFORM != "x64"
  #error This installer supports x64 only.
#endif

[Setup]
AppId=ZzLogg
AppName=ZzLogg
AppVersion={#VERSION}
AppPublisher=Jackfahdin
AppPublisherURL=https://github.com/jackfahdin/ZzLogg
DefaultDirName={code:DefaultInstallDir}
DefaultGroupName=ZzLogg
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=admin
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern dynamic windows11 hidebevels
WizardSizePercent=110
LanguageDetectionMethod=uilanguage
; A language picker precedes InitializeSetup, so it must not block a restricted entry.
ShowLanguageDialog=no
DisableStartupPrompt=yes
SourceDir=..\..
OutputDir=.
OutputBaseFilename=ZzLogg-{#VERSION}-{#PLATFORM}-Qt6-setup
SetupIconFile=Resources\ZzLogg.ico
UninstallDisplayIcon={app}\ZzLogg.exe
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
UsePreviousAppDir=no
CreateUninstallRegKey=no
; Reinstalls append by matching AppId in the existing unins???.dat file.
UninstallLogMode=append

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "chinesetraditional"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"

[CustomMessages]
english.StartShortcut=Create a Start menu shortcut
english.DesktopShortcut=Create a desktop shortcut
english.SendToShortcut=Add ZzLogg to the Send to menu
english.AssociateLog=Associate .log files with ZzLogg
english.LaunchProgram=Launch ZzLogg
chinesesimplified.StartShortcut=创建开始菜单快捷方式
chinesesimplified.DesktopShortcut=创建桌面快捷方式
chinesesimplified.SendToShortcut=将 ZzLogg 添加到“发送到”菜单
chinesesimplified.AssociateLog=将 .log 文件关联到 ZzLogg
chinesesimplified.LaunchProgram=启动 ZzLogg
chinesetraditional.StartShortcut=建立開始功能表捷徑
chinesetraditional.DesktopShortcut=建立桌面捷徑
chinesetraditional.SendToShortcut=將 ZzLogg 加入「傳送到」功能表
chinesetraditional.AssociateLog=將 .log 檔案關聯至 ZzLogg
chinesetraditional.LaunchProgram=啟動 ZzLogg

[Tasks]
Name: "startmenu"; Description: "{cm:StartShortcut}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "desktop"; Description: "{cm:DesktopShortcut}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "sendto"; Description: "{cm:SendToShortcut}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "associate"; Description: "{cm:AssociateLog}"; Flags: unchecked

[Files]
; Restricted entries extract only these two files. The helper is never installed.
Source: "txpayload\ZzLoggUpdateTx.exe"; Flags: dontcopy
Source: "release\.zzlogg-files.manifest"; DestName: "files.manifest"; Flags: dontcopy
Source: "release\*"; DestDir: "{app}"; Excludes: ".zzlogg-uninstall.nsh"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{commonprograms}\ZzLogg"; Filename: "{app}\ZzLogg.exe"; Tasks: startmenu
Name: "{commondesktop}\ZzLogg"; Filename: "{app}\ZzLogg.exe"; Tasks: desktop
Name: "{usersendto}\ZzLogg"; Filename: "{app}\ZzLogg.exe"; Tasks: sendto

[Registry]
; Keep the historical key as the only uninstall registration. Inno still
; creates its normal file-ownership uninstall log and uninstaller.
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: string; ValueName: "DisplayName"; ValueData: "ZzLogg"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: string; ValueName: "DisplayVersion"; ValueData: "{#VERSION}"
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: string; ValueName: "Publisher"; ValueData: "Jackfahdin"
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: string; ValueName: "InstallLocation"; ValueData: "{app}"
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: string; ValueName: "UninstallString"; ValueData: """{uninstallexe}"""
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: string; ValueName: "QuietUninstallString"; ValueData: """{uninstallexe}"" /VERYSILENT"
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: string; ValueName: "DisplayIcon"; ValueData: "{app}\ZzLogg.exe"
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: dword; ValueName: "UpdateIdentitySchema"; ValueData: "2"
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: dword; ValueName: "NoModify"; ValueData: "1"
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg"; ValueType: dword; ValueName: "NoRepair"; ValueData: "1"
Root: HKLM64; Subkey: "Software\Classes\Applications\ZzLogg.exe\shell\open\command"; ValueType: string; ValueData: """{app}\ZzLogg.exe"" ""%1"""; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Classes\*\OpenWithList\ZzLogg.exe"; ValueType: string; ValueData: ""; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Classes\.txt\OpenWithList\ZzLogg.exe"; ValueType: string; ValueData: ""; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Classes\.log\OpenWithList\ZzLogg.exe"; ValueType: string; ValueData: ""; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Classes\.cap\OpenWithList\ZzLogg.exe"; ValueType: string; ValueData: ""; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Classes\.log\OpenWithProgids"; ValueType: string; ValueName: "ZzLogg.LogFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "Software\Classes\ZzLogg.LogFile"; ValueType: string; ValueData: "ZzLogg log file"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Classes\ZzLogg.LogFile\DefaultIcon"; ValueType: string; ValueData: "{app}\ZzLogg.exe,0"
Root: HKLM64; Subkey: "Software\Classes\ZzLogg.LogFile\shell\open\command"; ValueType: string; ValueData: """{app}\ZzLogg.exe"" ""%1"""
Root: HKLM64; Subkey: "Software\Classes\.log"; ValueType: string; ValueData: "ZzLogg.LogFile"; Tasks: associate

[Run]
Filename: "{app}\ZzLogg.exe"; Description: "{cm:LaunchProgram}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallDelete]
; No recursive deletion: user-created files, portable data, and AppData survive.
Type: files; Name: "{app}\.zzlogg-install-root"

[Code]
const
  RegistrationKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg';
  InvalidAttributes = $FFFFFFFF;
  ReadOnlyMask = $001200A9;

type
  TFileInformation = record
    Attributes, CreatedLow, CreatedHigh, AccessLow, AccessHigh: Cardinal;
    WrittenLow, WrittenHigh, VolumeSerial, SizeHigh, SizeLow: Cardinal;
    Links, IndexHigh, IndexLow: Cardinal;
  end;
  TSecurityAttributes64 = record
    Size, Padding1: Cardinal;
    Descriptor: NativeInt;
    InheritHandle, Padding2: Cardinal;
  end;
  TPrefix = array[0..15] of Byte;

function WinCreateFile(Name: String; Access, Share: Cardinal; Security: NativeInt;
  Disposition, Flags: Cardinal; Template: NativeInt): NativeInt;
  external 'CreateFileW@kernel32.dll stdcall';
function WinCloseHandle(Handle: NativeInt): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';
function WinFileInformation(Handle: NativeInt; var Info: TFileInformation): Boolean;
  external 'GetFileInformationByHandle@kernel32.dll stdcall';
function WinFinalPath(Handle: NativeInt; Buffer: String; Size, Flags: Cardinal): Cardinal;
  external 'GetFinalPathNameByHandleW@kernel32.dll stdcall';
function WinReadFile(Handle: NativeInt; var Buffer: TPrefix; Count: Cardinal;
  var ReadCount: Cardinal; Overlapped: NativeInt): Boolean;
  external 'ReadFile@kernel32.dll stdcall';
function WinAttributes(Name: String): Cardinal;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function WinCreateDirectory(Name: String; var Security: TSecurityAttributes64): Boolean;
  external 'CreateDirectoryW@kernel32.dll stdcall';
function WinParseSecurity(Sddl: String; Revision: Cardinal; var Descriptor: NativeInt;
  Size: NativeInt): Boolean;
  external 'ConvertStringSecurityDescriptorToSecurityDescriptorW@advapi32.dll stdcall';
function WinGetSecurity(Name: String; ObjectType, Information: Cardinal;
  Owner, Group, Dacl, Sacl: NativeInt; var Descriptor: NativeInt): Cardinal;
  external 'GetNamedSecurityInfoW@advapi32.dll stdcall';
function WinSecurityString(Descriptor: NativeInt; Revision, Information: Cardinal;
  var Sddl: NativeInt; Size: NativeInt): Boolean;
  external 'ConvertSecurityDescriptorToStringSecurityDescriptorW@advapi32.dll stdcall';
function WinStringLength(Value: NativeInt): Integer;
  external 'lstrlenW@kernel32.dll stdcall';
function WinCopyString(Dest: String; Source: NativeInt; Count: Integer): NativeInt;
  external 'lstrcpynW@kernel32.dll stdcall';
function WinLocalFree(Value: NativeInt): NativeInt;
  external 'LocalFree@kernel32.dll stdcall';
procedure WinExitProcess(Code: Cardinal);
  external 'ExitProcess@kernel32.dll stdcall';

var
  Pins: array of NativeInt;
  Target, TargetIdentity, LegacyUninstaller, PreviousLogAssociation: String;
  PreviousLogHadValue: Boolean;

procedure ClosePins;
var I: Integer;
begin
  for I := 0 to GetArrayLength(Pins) - 1 do WinCloseHandle(Pins[I]);
  SetArrayLength(Pins, 0);
end;

procedure RestrictedExit(Code: Integer; MessageText: String);
begin
  if MessageText <> '' then Log(MessageText);
  ClosePins;
  { InitializeSetup=False maps to Inno's generic cancellation code. ExitProcess
    preserves the txcontract_win_p.h code, including engine codes 41..47. }
  WinExitProcess(Code);
end;

function StandardPath(Path: String): Boolean;
var I, Start: Integer; Part, Stem: String;
begin
  Result := False;
  if (Length(Path) < 4) or (Length(Path) >= 260) then Exit;
  if (Pos(Uppercase(Copy(Path, 1, 1)), 'ABCDEFGHIJKLMNOPQRSTUVWXYZ') = 0) or
     (Copy(Path, 2, 2) <> ':\') then Exit;
  Start := 4;
  for I := 4 to Length(Path) + 1 do begin
    if (I > Length(Path)) or (Copy(Path, I, 1) = '\') then begin
      Part := Copy(Path, Start, I - Start);
      if (Part = '') or (Copy(Part, Length(Part), 1) = '.') or
         (Copy(Part, Length(Part), 1) = ' ') then Exit;
      Stem := Uppercase(Part);
      if Pos('.', Stem) > 0 then Stem := Copy(Stem, 1, Pos('.', Stem) - 1);
      if (Stem = 'CON') or (Stem = 'PRN') or (Stem = 'AUX') or (Stem = 'NUL') or
         (Stem = 'CONIN$') or (Stem = 'CONOUT$') then Exit;
      if (Length(Stem) = 4) and ((Copy(Stem, 1, 3) = 'COM') or (Copy(Stem, 1, 3) = 'LPT')) and
         (Pos(Copy(Stem, 4, 1), '123456789¹²³') > 0) then Exit;
      Start := I + 1;
    end else if (Ord(Path[I]) < 32) or (Pos(Path[I], '/:*?"<>|') > 0) then Exit;
  end;
  Result := True;
end;

function PinDirectory(Path: String; var Identity: String): Boolean;
var Handle: NativeInt; Info: TFileInformation; FinalPath: String; Count: Cardinal; N: Integer;
begin
  Result := False;
  if (WinAttributes(Path) = InvalidAttributes) or
     ((WinAttributes(Path) and $450) <> $10) then Exit;
  { Match the engine lease: share READ only, excluding directory mutation
    and deletion; keep every ancestor pinned until the child exits. }
  Handle := WinCreateFile(Path, $80000000, 1, 0, 3, $02200000, 0);
  if (Handle = -1) or (Handle = 0) then Exit;
  if WinFileInformation(Handle, Info) and ((Info.Attributes and $450) = $10) then begin
    SetLength(FinalPath, 1024);
    Count := WinFinalPath(Handle, FinalPath, 1024, 0);
    if (Count > 0) and (Count < 1024) and
       (CompareText(Copy(FinalPath, 1, Count), '\\?\' + Path) = 0) then begin
      Identity := Format('%.8x:%.8x:%.8x', [Info.VolumeSerial, Info.IndexHigh, Info.IndexLow]);
      N := GetArrayLength(Pins); SetArrayLength(Pins, N + 1); Pins[N] := Handle;
      Result := True;
    end;
  end;
  if not Result then WinCloseHandle(Handle);
end;

function PinPath(Path: String): Boolean;
var I: Integer; Unused: String;
begin
  Result := False;
  if not StandardPath(Path) then Exit;
  if not PinDirectory(Copy(Path, 1, 3), Unused) then Exit;
  for I := 4 to Length(Path) do
    if (Path[I] = '\') and not PinDirectory(Copy(Path, 1, I - 1), Unused) then Exit;
  Result := PinDirectory(Path, TargetIdentity);
end;

function HasPrefix(Path, Expected: String): Boolean;
var Handle: NativeInt; Info: TFileInformation; Prefix: TPrefix; ReadCount: Cardinal; I: Integer;
begin
  Result := False;
  Handle := WinCreateFile(Path, $80000000, 1, 0, 3, $00200000, 0);
  if (Handle = -1) or (Handle = 0) then Exit;
  try
    if not WinFileInformation(Handle, Info) then Exit;
    if (Info.Attributes and $450) <> 0 then Exit;
    if not WinReadFile(Handle, Prefix, Length(Expected), ReadCount, 0) then Exit;
    if ReadCount <> Cardinal(Length(Expected)) then Exit;
    for I := 1 to Length(Expected) do if Prefix[I - 1] <> Ord(Expected[I]) then Exit;
    Result := True;
  finally
    WinCloseHandle(Handle);
  end;
end;

function VerifyTarget: Boolean;
var Schema: Cardinal;
begin
  Result := False;
  if not RegQueryStringValue(HKLM64, RegistrationKey, 'InstallLocation', Target) then Exit;
  if not RegQueryDWordValue(HKLM64, RegistrationKey, 'UpdateIdentitySchema', Schema) then Exit;
  if Schema <> 2 then Exit;
  if not PinPath(Target) then Exit;
  if not HasPrefix(Target + '\.zzlogg-install-root', 'ZzLogg ') then Exit;
  if not HasPrefix(Target + '\.zzlogg-files.manifest', 'ZZTXMAN1') then Exit;
  Result := True;
end;

function ProtectedAcl(Path: String): Boolean;
var Descriptor, SddlPointer: NativeInt; Sddl, Owner, Ace, Rights, Principal: String;
    I, P, D, Separator, Field, Count: Integer; Mask: Int64;
begin
  Result := False; Descriptor := 0; SddlPointer := 0;
  { OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION. }
  if WinGetSecurity(Path, 1, 5, 0, 0, 0, 0, Descriptor) <> 0 then Exit;
  try
    if not WinSecurityString(Descriptor, 1, 5, SddlPointer, 0) then Exit;
    Count := WinStringLength(SddlPointer);
    if (Count < 1) or (Count > 65536) then Exit;
    SetLength(Sddl, Count + 1); WinCopyString(Sddl, SddlPointer, Count + 1); SetLength(Sddl, Count);
    D := Pos('D:', Sddl);
    if (Copy(Sddl, 1, 2) <> 'O:') or (D = 0) then Exit;
    Owner := Copy(Sddl, 3, D - 3);
    if (Owner <> 'BA') and (Owner <> 'SY') then Exit;
    Sddl := Copy(Sddl, D + 2, MaxInt);
    { Require a protected DACL; no inherited writable principals can enter. }
    if Copy(Sddl, 1, 1) <> 'P' then Exit;
    P := Pos('(', Sddl);
    if P = 0 then Exit;
    Sddl := Copy(Sddl, P, MaxInt);
    while Sddl <> '' do begin
      P := Pos(')', Sddl);
      if (Copy(Sddl, 1, 1) <> '(') or (P = 0) then Exit;
      Ace := Copy(Sddl, 2, P - 2); Sddl := Copy(Sddl, P + 1, MaxInt);
      if Copy(Ace, 1, 2) <> 'A;' then Exit;
      Rights := ''; Principal := ''; Field := 0; Separator := 1;
      for I := 1 to Length(Ace) + 1 do
        if (I > Length(Ace)) or (Copy(Ace, I, 1) = ';') then begin
          if Field = 2 then Rights := Copy(Ace, Separator, I - Separator);
          if Field = 5 then Principal := Copy(Ace, Separator, I - Separator);
          Field := Field + 1; Separator := I + 1;
        end;
      if Field <> 6 then Exit;
      if (Principal <> 'BA') and (Principal <> 'SY') then begin
        if Copy(Rights, 1, 2) = '0x' then begin
          Mask := StrToInt64Def('$' + Copy(Rights, 3, MaxInt), -1);
          if (Mask < 0) or ((Mask and (not Int64(ReadOnlyMask))) <> 0) then Exit;
        end else begin
          while Rights <> '' do begin
            if (Copy(Rights, 1, 2) <> 'FR') and (Copy(Rights, 1, 2) <> 'FX') and
               (Copy(Rights, 1, 2) <> 'GR') and (Copy(Rights, 1, 2) <> 'GX') then Exit;
            Delete(Rights, 1, 2);
          end;
        end;
      end;
    end;
    Result := True;
  finally
    if SddlPointer <> 0 then WinLocalFree(SddlPointer);
    if Descriptor <> 0 then WinLocalFree(Descriptor);
  end;
end;

function CreateProtectedDirectory(Path: String; AllowExisting: Boolean): Boolean;
var Security: TSecurityAttributes64; Descriptor: NativeInt; Unused: String;
begin
  Result := False;
  if WinAttributes(Path) <> InvalidAttributes then begin
    if not AllowExisting then Exit;
    Result := PinDirectory(Path, Unused) and ProtectedAcl(Path); Exit;
  end;
  Descriptor := 0;
  { Set owner and ACL atomically, before the directory can receive any payload.
    The explicit padding matches SECURITY_ATTRIBUTES in this x64 Setup. }
  if not WinParseSecurity('O:BAG:BAD:P(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)(A;OICI;FR;;;AU)',
    1, Descriptor, 0) then Exit;
  try
    Security.Size := 24; Security.Padding1 := 0; Security.Descriptor := Descriptor;
    Security.InheritHandle := 0; Security.Padding2 := 0;
    if not WinCreateDirectory(Path, Security) then Exit;
    Result := PinDirectory(Path, Unused) and ProtectedAcl(Path);
  finally
    WinLocalFree(Descriptor);
  end;
end;

function ValidLocator(Value: String): Boolean;
var I: Integer;
begin
  Result := False;
  if (Length(Value) <> 16) or (Value = '0000000000000000') then Exit;
  for I := 1 to 16 do if Pos(Value[I], '0123456789abcdef') = 0 then Exit;
  Result := True;
end;

procedure RunRestricted(Mode, Locator: String);
var DataRoot, ProductRoot, TxRoot, TxDir, Stage, Parameters, Identity: String; ExitCode, Attempt: Integer;
begin
  if not VerifyTarget then RestrictedExit(42, 'Registered installation failed independent target validation.');
  Identity := TargetIdentity;
  DataRoot := ExpandConstant('{commonappdata}');
  if not PinPath(DataRoot) then RestrictedExit(48, 'Cannot pin ProgramData.');
  ProductRoot := DataRoot + '\ZzLogg';
  TxRoot := ProductRoot + '\UpdateTransactions'; TxDir := TxRoot + '\' + Locator;
  if Mode = 'upgrade' then begin
    { The shared NSIS parent may retain its inherited ProgramData ACLs. Pin
      all ancestors; independently require a protected transaction root. }
    if WinAttributes(ProductRoot) = InvalidAttributes then begin
      if not CreateProtectedDirectory(ProductRoot, False) then
        RestrictedExit(48, 'Cannot create protected product directory.');
    end else if not PinPath(ProductRoot) then
      RestrictedExit(48, 'Cannot pin existing product directory.');
    if not CreateProtectedDirectory(TxRoot, True) then
      RestrictedExit(48, 'Cannot create or validate protected transaction root.');
  end else begin
    { Recovery never creates a missing transaction tree. }
    if not PinPath(TxRoot) or not ProtectedAcl(TxRoot) then
      RestrictedExit(48, 'Protected recovery root missing.');
  end;
  if Mode = 'upgrade' then begin
    if WinAttributes(TxDir) <> InvalidAttributes then RestrictedExit(48, 'Transaction exists; use recovery.');
    Stage := TxRoot + '\staging-' + Locator;
  end else begin
    if not PinPath(TxDir) or not ProtectedAcl(TxDir) then RestrictedExit(48, 'Protected recovery transaction missing.');
    if not FileExists(TxDir + '\journal.log') or
       ((WinAttributes(TxDir + '\journal.log') and $450) <> 0) then
      RestrictedExit(48, 'Recovery journal missing or unsafe.');
    Stage := TxDir + '\recover';
    { A failed earlier recovery keeps its evidence. A retry uses a fresh
      protected sibling rather than overwriting the previous engine. }
    Attempt := 0;
    while WinAttributes(Stage) <> InvalidAttributes do begin
      Attempt := Attempt + 1;
      if Attempt > 10000 then RestrictedExit(48, 'Too many recovery attempts.');
      Stage := TxDir + '\recover-' + IntToStr(Attempt);
    end;
  end;
  { Never adopt or overwrite an existing staging/recovery directory. }
  if not CreateProtectedDirectory(Stage, False) then RestrictedExit(48, 'Cannot create fresh protected staging.');
  ExtractTemporaryFile('ZzLoggUpdateTx.exe');
  if not CopyFile(ExpandConstant('{tmp}\ZzLoggUpdateTx.exe'), Stage + '\ZzLoggUpdateTx.exe', True) then
    RestrictedExit(48, 'Cannot stage transaction engine.');
  if Mode = 'upgrade' then begin
    ExtractTemporaryFile('files.manifest');
    if not CopyFile(ExpandConstant('{tmp}\files.manifest'), Stage + '\files.manifest', True) then
      RestrictedExit(48, 'Cannot stage manifest.');
    SaveStringToFile(Stage + '\inno-entry.log', 'mode=upgrade locator=' + Locator +
      ' target=' + Target + ' identity=' + Identity + #13#10, False);
    Parameters := '--install "' + Target + '" --staging "' + Stage + '" --txroot "' + TxRoot +
      '" --txid "' + Locator + '" --version "{#VERSION}"';
  end else
    Parameters := '--recover --install "' + Target + '" --txroot "' + TxRoot + '" --txid "' + Locator + '"';
  if not Exec(Stage + '\ZzLoggUpdateTx.exe', Parameters, Stage, SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    RestrictedExit(48, 'Cannot start transaction engine.');
  RestrictedExit(ExitCode, '');
end;

function DefaultInstallDir(Param: String): String;
begin
  if not RegQueryStringValue(HKLM64, RegistrationKey, 'InstallLocation', Result) or
     not StandardPath(Result) then Result := ExpandConstant('{autopf}\ZzLogg');
end;

function InitializeSetup: Boolean;
var I, RestrictedCount: Integer; Argument, LowerArgument, Mode, Locator, Existing, Command: String;
    RejectArguments: Boolean;
begin
  Result := True; RestrictedCount := 0; RejectArguments := False;
  { Parse complete argument tokens, never a substring of another value. Any
    unknown argument in restricted mode rejects, including /D=, /DIR=,
    /LOADINF= and duplicates. Setup's private /SL5= handoff is harmless. }
  for I := 1 to ParamCount do begin
    Argument := ParamStr(I); LowerArgument := Lowercase(Argument);
    if (Pos('/zzloggupgrade=', LowerArgument) = 1) or (Pos('/zzloggrecover=', LowerArgument) = 1) then begin
      RestrictedCount := RestrictedCount + 1;
      if Pos('/zzloggupgrade=', LowerArgument) = 1 then Mode := 'upgrade' else Mode := 'recover';
      Locator := Copy(Argument, 16, MaxInt);
    end else if (LowerArgument <> '/silent') and (LowerArgument <> '/verysilent') and
                (LowerArgument <> '/s') and (LowerArgument <> '/suppressmsgboxes') and
                (LowerArgument <> '/norestart') and (LowerArgument <> '/sp-') and
                (Pos('/sl5=', LowerArgument) <> 1) then RejectArguments := True;
    if (Pos('/zzloggupgrade', LowerArgument) = 1) and (Pos('/zzloggupgrade=', LowerArgument) <> 1) then
      RestrictedExit(2, 'Malformed restricted upgrade switch.');
    if (Pos('/zzloggrecover', LowerArgument) = 1) and (Pos('/zzloggrecover=', LowerArgument) <> 1) then
      RestrictedExit(2, 'Malformed restricted recovery switch.');
  end;
  if RestrictedCount > 0 then begin
    if (RestrictedCount <> 1) or RejectArguments or not ValidLocator(Locator) then
      RestrictedExit(2, 'Restricted mode accepts a single nonzero 16-digit lowercase hexadecimal locator and no target override.');
    try
      RunRestricted(Mode, Locator);
    except
      RestrictedExit(48, GetExceptionMessage);
    end;
    Result := False;
  end else begin
    { Record only the exact old NSIS uninstaller. Never execute it: it would
      delete the newly installed files and registration. }
    if RegQueryStringValue(HKLM64, RegistrationKey, 'InstallLocation', Existing) and
       RegQueryStringValue(HKLM64, RegistrationKey, 'UninstallString', Command) then
      if StandardPath(Existing) and
         ((CompareText(Command, '"' + Existing + '\Uninstall.exe"') = 0) or
          (CompareText(Command, Existing + '\Uninstall.exe') = 0)) then
        LegacyUninstaller := Existing + '\Uninstall.exe';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Saved: Cardinal;
begin
  if CurStep = ssInstall then begin
    PreviousLogHadValue := RegQueryStringValue(HKLM64, 'Software\Classes\.log', '', PreviousLogAssociation);
    if PreviousLogHadValue and (PreviousLogAssociation = 'ZzLogg log file') then
      PreviousLogHadValue := RegQueryStringValue(HKLM64, 'Software\Classes\.log', 'backup_val', PreviousLogAssociation);
    { Keep the original backup over repeated Inno installs. }
    if (PreviousLogAssociation = 'ZzLogg.LogFile') and
       RegQueryDWordValue(HKLM64, RegistrationKey, 'PreviousLogHadValue', Saved) then begin
      PreviousLogHadValue := Saved <> 0;
      RegQueryStringValue(HKLM64, RegistrationKey, 'PreviousLogAssociation', PreviousLogAssociation);
    end;
  end;
  if CurStep = ssPostInstall then begin
    if WizardIsTaskSelected('associate') then begin
      if PreviousLogHadValue then Saved := 1 else Saved := 0;
      RegWriteDWordValue(HKLM64, RegistrationKey, 'PreviousLogHadValue', Saved);
      RegWriteStringValue(HKLM64, RegistrationKey, 'PreviousLogAssociation', PreviousLogAssociation);
    end;
    if not SaveStringToFile(ExpandConstant('{app}\.zzlogg-install-root'), 'ZzLogg {#VERSION}' + #13#10, False) then
      RaiseException('Cannot write installation identity marker.');
  end;
  if CurStep = ssDone then begin
    if (LegacyUninstaller <> '') and
       (CompareText(ExtractFileDir(LegacyUninstaller), ExpandConstant('{app}')) = 0) and
       ((WinAttributes(LegacyUninstaller) and $450) = 0) then
      DeleteFile(LegacyUninstaller);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Current, Previous, Command: String; HadValue: Cardinal; HasBackup: Boolean;
begin
  if CurUninstallStep = usUninstall then begin
    { Restore only while the default still belongs to this product. Respect
      a later choice by another application and migrate NSIS backup_val. }
    if RegQueryStringValue(HKLM64, 'Software\Classes\.log', '', Current) then begin
      HasBackup := False;
      if Current = 'ZzLogg.LogFile' then begin
        if RegQueryDWordValue(HKLM64, RegistrationKey, 'PreviousLogHadValue', HadValue) then begin
          HasBackup := HadValue <> 0;
          RegQueryStringValue(HKLM64, RegistrationKey, 'PreviousLogAssociation', Previous);
        end;
      end else if Current = 'ZzLogg log file' then
        HasBackup := RegQueryStringValue(HKLM64, 'Software\Classes\.log', 'backup_val', Previous);
      if (Current = 'ZzLogg.LogFile') or (Current = 'ZzLogg log file') then begin
        if HasBackup and (Previous <> 'ZzLogg.LogFile') and (Previous <> 'ZzLogg log file') then
          RegWriteStringValue(HKLM64, 'Software\Classes\.log', '', Previous)
        else RegDeleteValue(HKLM64, 'Software\Classes\.log', '');
        RegDeleteValue(HKLM64, 'Software\Classes\.log', 'backup_val');
      end;
    end;
    if RegQueryStringValue(HKLM64, 'Software\Classes\ZzLogg log file\shell\open\command', '', Command) and
       (CompareText(Command, '"' + ExpandConstant('{app}\ZzLogg.exe') + '" "%1"') = 0) then
      RegDeleteKeyIncludingSubkeys(HKLM64, 'Software\Classes\ZzLogg log file');
  end;
end;

procedure DeinitializeSetup;
begin
  ClosePins;
end;
