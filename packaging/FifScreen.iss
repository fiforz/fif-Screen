#define AppVersion GetEnv("FIFSCREEN_VERSION")
#define StageDir GetEnv("FIFSCREEN_STAGE_DIR")
#define OutputDir GetEnv("FIFSCREEN_OUTPUT_DIR")
#define OutputBaseName GetEnv("FIFSCREEN_OUTPUT_BASENAME")
#define DriverFlavor GetEnv("FIFSCREEN_DRIVER_FLAVOR")
#define EditionLabel GetEnv("FIFSCREEN_EDITION_LABEL")
#define UpdateChannel GetEnv("FIFSCREEN_UPDATE_CHANNEL")
#define ReleaseApiUrl GetEnv("FIFSCREEN_RELEASE_API_URL")

[Setup]
AppId={{E451D924-FA9E-4A43-9B4D-46A4B52DC040}
AppName=FifScreen
AppVersion={#AppVersion}
AppVerName=FifScreen {#AppVersion} {#EditionLabel}
AppPublisher=FifScreen
AppPublisherURL=https://github.com/fiforz/fif-Screen
AppSupportURL=https://github.com/fiforz/fif-Screen/issues
AppUpdatesURL=https://github.com/fiforz/fif-Screen/releases
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
Name: "{autoprograms}\FifScreen\FifScreen 控制中心"; Filename: "{app}\FifScreen Control.cmd"; WorkingDir: "{app}"
Name: "{autoprograms}\FifScreen\检查更新"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\maintenance\check-update.ps1"" -InstallDir ""{app}"""; WorkingDir: "{app}"
Name: "{autoprograms}\FifScreen\卸载 FifScreen"; Filename: "{uninstallexe}"
Name: "{autodesktop}\FifScreen 控制中心"; Filename: "{app}\FifScreen Control.cmd"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKLM; Subkey: "Software\FifScreen"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\FifScreen"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\FifScreen"; ValueType: string; ValueName: "UpdateChannel"; ValueData: "{#UpdateChannel}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\FifScreen"; ValueType: string; ValueName: "ReleaseApiUrl"; ValueData: "{#ReleaseApiUrl}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\FifScreen"; Flags: uninsdeletekeyifempty

[Run]
Filename: "{app}\FifScreen Control.cmd"; Description: "启动 FifScreen 控制中心"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent

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
      MsgBox('开发版静默安装必须添加 /ALLOWTESTDRIVER=1 参数。', mbError, MB_OK);
      Result := False;
      exit;
    end;

    Answer := MsgBox(
      '此开发预览版包含测试签名的显示驱动。' + #13#10 + #13#10 +
      'Windows 必须已经启用测试签名模式，且安全启动不能阻止该驱动。安装程序会信任随附的公开测试证书，' +
      '但不会修改 BCD、安全启动或重启设置。' + #13#10 + #13#10 +
      '是否继续安装开发版驱动？',
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
      Result := '更新前无法关闭现有的 FifScreen 运行进程。';
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
      RaiseException('FifScreen 驱动安装失败，请查看日志：' +
        ExpandConstant('{commonappdata}\FifScreen\logs\driver-install.log'));
  end;
end;
