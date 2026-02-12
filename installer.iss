; ============================================
; AI Meeting Assistant - Inno Setup Installer
; ============================================
; 
; To build this installer:
;   1. Install Inno Setup (https://jrsoftware.org/isinfo.php)
;   2. Run build_release.bat first to create the exe
;   3. Open this file in Inno Setup Compiler and click Build
;
; OR from command line:
;   iscc installer.iss
;

#define MyAppName "AI Meeting Assistant"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Invisible Overlay Project"
#define MyAppExeName "InvisibleOverlay.exe"
#define MyAppURL "https://console.groq.com"

[Setup]
AppId={{7A3D9F8E-4B2C-4E1A-A6D5-8F3C2E1B9A0D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=installer_output
OutputBaseFilename=AIMeetingAssistant_Setup_{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
UninstallDisplayName={#MyAppName}
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"
Name: "autostart"; Description: "Start automatically with Windows"; GroupDescription: "Startup:"

[Files]
; Main executable (must build with build_release.bat first)
Source: "build\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Helper scripts
Source: "deploy\InvisibleOverlay\SetApiKey.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "deploy\InvisibleOverlay\README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Set API Key"; Filename: "{app}\SetApiKey.bat"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Autostart entry (optional, user-selected during install)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "AIMeetingAssistant"; \
  ValueData: """{app}\{#MyAppExeName}"""; \
  Flags: uninsdeletevalue; Tasks: autostart

[Run]
; Ask user to set API key after installation
Filename: "{app}\SetApiKey.bat"; Description: "Set up Groq API key (required for AI features)"; \
  Flags: nowait postinstall skipifsilent shellexec
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; \
  Flags: nowait postinstall skipifsilent

[UninstallRun]
; Clean up on uninstall
Filename: "cmd.exe"; Parameters: "/c reg delete ""HKCU\Software\Microsoft\Windows\CurrentVersion\Run"" /v ""AIMeetingAssistant"" /f"; \
  Flags: runhidden

[Code]
// Check Windows version
function InitializeSetup(): Boolean;
begin
  Result := True;
  if not IsWin64 then
  begin
    MsgBox('This application requires 64-bit Windows 10 version 2004 or later.', mbCriticalError, MB_OK);
    Result := False;
  end;
end;

// Kill running instance before install/uninstall
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssInstall then
  begin
    Exec('taskkill.exe', '/F /IM {#MyAppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    Exec('taskkill.exe', '/F /IM {#MyAppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
