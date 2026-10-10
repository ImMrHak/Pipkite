; Pipkite 1.0.0 per-user installer (Inno Setup 6). Built by installer\package.ps1.
; No administrator rights are needed for normal use: installs under %LOCALAPPDATA%\Programs.
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\Pipkite-1.0.0"
#endif

[Setup]
AppId={{6F1C2A52-3A0E-4E1B-9D1E-5A9D6C0B7E11}
AppName=Pipkite
AppVersion={#AppVersion}
AppPublisher=Pipkite
AppVerName=Pipkite {#AppVersion}
DefaultDirName={localappdata}\Programs\Pipkite
DefaultGroupName=Pipkite
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputBaseFilename=Pipkite-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Pipkite.exe
CloseApplications=yes

[Tasks]
Name: "autostart"; Description: "Start Pipkite when I sign in to Windows (opt-in)"; Flags: unchecked
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\Pipkite"; Filename: "{app}\Pipkite.Launcher.exe"; WorkingDir: "{app}"
Name: "{group}\Pipkite User Guide"; Filename: "{app}\docs\USER-GUIDE.md"
Name: "{userdesktop}\Pipkite"; Filename: "{app}\Pipkite.Launcher.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Pipkite"; ValueData: """{app}\Pipkite.Launcher.exe"" --minimized"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
; Trust Pipkite's code-signing certificate for the current user (no admin needed) so Windows Security
; (SmartScreen / Smart App Control) recognizes the signed binaries instead of blocking them.
Filename: "{sys}\certutil.exe"; Parameters: "-user -addstore -f Root ""{app}\Pipkite-publisher.cer"""; Flags: runhidden; StatusMsg: "Trusting the Pipkite publisher certificate..."
Filename: "{sys}\certutil.exe"; Parameters: "-user -addstore -f TrustedPublisher ""{app}\Pipkite-publisher.cer"""; Flags: runhidden
Filename: "{app}\Pipkite.Launcher.exe"; Description: "Start Pipkite"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Remove the publisher certificate we trusted at install time.
Filename: "{sys}\certutil.exe"; Parameters: "-user -delstore Root ""Pipkite"""; Flags: runhidden; RunOnceId: "UntrustRoot"
Filename: "{sys}\certutil.exe"; Parameters: "-user -delstore TrustedPublisher ""Pipkite"""; Flags: runhidden; RunOnceId: "UntrustPub"
; Orderly stop: the engine exits when its UI requests shutdown; as a fallback stop remaining processes.
Filename: "{sys}\taskkill.exe"; Parameters: "/IM Pipkite.exe /F"; Flags: runhidden; RunOnceId: "StopUi"
Filename: "{sys}\taskkill.exe"; Parameters: "/IM Pipkite.Engine.exe /F"; Flags: runhidden; RunOnceId: "StopEngine"

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
    MsgBox('Pipkite requires the .NET Framework 4.8 (included in current Windows 10/11 updates). Please install it from Microsoft and run setup again.', mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Local history and journals are user data and are kept unless the user deletes them explicitly.
  if CurUninstallStep = usPostUninstall then
    MsgBox('Your local Pipkite data (history, journals, settings) remains in %LOCALAPPDATA%\Pipkite. Delete that folder yourself if you no longer need it.', mbInformation, MB_OK);
end;
