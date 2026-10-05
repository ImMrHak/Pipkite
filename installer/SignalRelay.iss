; SignalRelay 1.0.0 per-user installer (Inno Setup 6). Built by installer\package.ps1.
; No administrator rights are needed for normal use: installs under %LOCALAPPDATA%\Programs.
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\SignalRelay-1.0.0"
#endif

[Setup]
AppId={{6F1C2A52-3A0E-4E1B-9D1E-5A9D6C0B7E11}
AppName=SignalRelay
AppVersion={#AppVersion}
AppPublisher=SignalRelay
AppVerName=SignalRelay {#AppVersion}
DefaultDirName={localappdata}\Programs\SignalRelay
DefaultGroupName=SignalRelay
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputBaseFilename=SignalRelay-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\SignalRelay.exe
CloseApplications=yes

[Tasks]
Name: "autostart"; Description: "Start SignalRelay when I sign in to Windows (opt-in)"; Flags: unchecked
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\SignalRelay"; Filename: "{app}\SignalRelay.Launcher.exe"; WorkingDir: "{app}"
Name: "{group}\SignalRelay User Guide"; Filename: "{app}\docs\USER-GUIDE.md"
Name: "{userdesktop}\SignalRelay"; Filename: "{app}\SignalRelay.Launcher.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "SignalRelay"; ValueData: """{app}\SignalRelay.Launcher.exe"" --minimized"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
; Trust SignalRelay's code-signing certificate for the current user (no admin needed) so Windows Security
; (SmartScreen / Smart App Control) recognizes the signed binaries instead of blocking them.
Filename: "{sys}\certutil.exe"; Parameters: "-user -addstore -f Root ""{app}\SignalRelay-publisher.cer"""; Flags: runhidden; StatusMsg: "Trusting the SignalRelay publisher certificate..."
Filename: "{sys}\certutil.exe"; Parameters: "-user -addstore -f TrustedPublisher ""{app}\SignalRelay-publisher.cer"""; Flags: runhidden
Filename: "{app}\SignalRelay.Launcher.exe"; Description: "Start SignalRelay"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Remove the publisher certificate we trusted at install time.
Filename: "{sys}\certutil.exe"; Parameters: "-user -delstore Root ""SignalRelay"""; Flags: runhidden; RunOnceId: "UntrustRoot"
Filename: "{sys}\certutil.exe"; Parameters: "-user -delstore TrustedPublisher ""SignalRelay"""; Flags: runhidden; RunOnceId: "UntrustPub"
; Orderly stop: the engine exits when its UI requests shutdown; as a fallback stop remaining processes.
Filename: "{sys}\taskkill.exe"; Parameters: "/IM SignalRelay.exe /F"; Flags: runhidden; RunOnceId: "StopUi"
Filename: "{sys}\taskkill.exe"; Parameters: "/IM SignalRelay.Engine.exe /F"; Flags: runhidden; RunOnceId: "StopEngine"

[Code]
// .NET Framework 4.8 prerequisite check (Release >= 528040).
function IsDotNet48Installed(): Boolean;
var
  release: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full', 'Release', release) and (release >= 528040);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if not IsDotNet48Installed() then
  begin
    MsgBox('SignalRelay requires the .NET Framework 4.8 (included in current Windows 10/11 updates). Please install it from Microsoft and run setup again.', mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Local history and journals are user data and are kept unless the user deletes them explicitly.
  if CurUninstallStep = usPostUninstall then
    MsgBox('Your local SignalRelay data (history, journals, settings) remains in %LOCALAPPDATA%\SignalRelay. Delete that folder yourself if you no longer need it.', mbInformation, MB_OK);
end;
