#define AppVersion GetEnv("FIFSCREEN_VERSION")
#define StageDir GetEnv("FIFSCREEN_STAGE_DIR")
#define OutputDir GetEnv("FIFSCREEN_OUTPUT_DIR")
#define OutputBaseName GetEnv("FIFSCREEN_OUTPUT_BASENAME")
#define DriverFlavor GetEnv("FIFSCREEN_DRIVER_FLAVOR")
#define EditionLabel GetEnv("FIFSCREEN_EDITION_LABEL")
#define UpdateChannel GetEnv("FIFSCREEN_UPDATE_CHANNEL")
#define UpdateManifestUrl GetEnv("FIFSCREEN_UPDATE_MANIFEST_URL")

[Setup]
AppId={{E451D924-FA9E-4A43-9B4D-46A4B52DC040}
AppName=FifScreen
AppVersion={#AppVersion}
AppVerName=FifScreen {#AppVersion} {#EditionLabel}
AppPublisher=FifScreen
DefaultDirName={autopf}\FifScreen
DefaultGroupName=FifScreen
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
Uninstallable=yes
UninstallDisplayIcon={app}\runtime\bin\fif-host.exe
VersionInfoVersion={#AppVersion}.0
VersionInfoProductVersion={#AppVersion}.0
VersionInfoCompany=FifScreen
VersionInfoDescription=FifScreen Setup
VersionInfoProductName=FifScreen
VersionInfoProductTextVersion={#AppVersion}
VersionInfoTextVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\FifScreen\FifScreen Control"; Filename: "{app}\FifScreen Control.cmd"; WorkingDir: "{app}"
Name: "{autoprograms}\FifScreen\Check for Updates"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\maintenance\check-update.ps1"" -InstallDir ""{app}"""; WorkingDir: "{app}"
Name: "{autoprograms}\FifScreen\Uninstall FifScreen"; Filename: "{uninstallexe}"
Name: "{autodesktop}\FifScreen Control"; Filename: "{app}\FifScreen Control.cmd"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKLM; Subkey: "Software\FifScreen"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\FifScreen"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\FifScreen"; ValueType: string; ValueName: "UpdateChannel"; ValueData: "{#UpdateChannel}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\FifScreen"; ValueType: string; ValueName: "UpdateManifestUrl"; ValueData: "{#UpdateManifestUrl}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\FifScreen"; Flags: uninsdeletekeyifempty

[Run]
Filename: "{app}\FifScreen Control.cmd"; Description: "Launch FifScreen Control"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\maintenance\uninstall-cleanup.ps1"" -InstallDir ""{app}"""; Flags: runhidden waituntilterminated; RunOnceId: "FifScreenCleanupV1"

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\FifScreen"
Type: filesandordirs; Name: "{commonappdata}\FifScreen"

[Code]
var
  AllowTestDriver: Boolean;

function PowerShellPath: String;
begin
  Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

function IsDevelopmentDriver: Boolean;
begin
  Result := CompareText('{#DriverFlavor}', 'Development') = 0;
end;

function InitializeSetup: Boolean;
var
  Answer: Integer;
begin
  Result := True;
  AllowTestDriver := CompareText(ExpandConstant('{param:ALLOWTESTDRIVER|0}'), '1') = 0;

  if IsDevelopmentDriver and not AllowTestDriver then
  begin
    if WizardSilent then
    begin
      MsgBox('This developer package requires /ALLOWTESTDRIVER=1 in silent mode.', mbError, MB_OK);
      Result := False;
      exit;
    end;

    Answer := MsgBox(
      'This Developer Preview contains a test-signed display driver.' + #13#10 + #13#10 +
      'It works only when Windows is already running in Test Signing mode and Secure Boot does not block it. ' +
      'Setup will trust the bundled public test certificate, but it will NOT change BCD, Secure Boot, or reboot settings.' + #13#10 + #13#10 +
      'Continue with the development driver?',
      mbConfirmation, MB_YESNO);
    if Answer <> IDYES then
    begin
      Result := False;
      exit;
    end;
    AllowTestDriver := True;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  StopScript: String;
  Params: String;
begin
  Result := '';
  StopScript := ExpandConstant('{app}\maintenance\stop-runtime.ps1');
  if FileExists(StopScript) then
  begin
    Params := '-NoProfile -ExecutionPolicy Bypass -File "' + StopScript + '" -InstallDir "' + ExpandConstant('{app}') + '"';
    if (not Exec(PowerShellPath, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or
       (ResultCode <> 0) then
      Result := 'Could not stop the existing FifScreen runtime before update.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  Params: String;
begin
  if CurStep = ssPostInstall then
  begin
    Params := '-NoProfile -ExecutionPolicy Bypass -File "' +
      ExpandConstant('{app}\maintenance\install-driver.ps1') + '" -InstallDir "' +
      ExpandConstant('{app}') + '"';
    if AllowTestDriver then
      Params := Params + ' -AllowTestDriver';

    if (not Exec(PowerShellPath, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or
       (ResultCode <> 0) then
      RaiseException('FifScreen driver installation failed. See ' +
        ExpandConstant('{commonappdata}\FifScreen\logs\driver-install.log'));
  end;
end;
