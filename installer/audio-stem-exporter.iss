#define MyAppName      "Audio Stem Exporter for OBS"
#define MyAppVersion   "1.0.1"
#define MyAppPublisher "1134.digital"
#define MyAppURL       "https://1134.digital/tools"
#define MyPluginID     "obs-mp3-writer"
#define MyDLL          "..\build_x64\RelWithDebInfo\obs-mp3-writer.dll"
#define MyLocale       "..\data\locale\en-US.ini"

[Setup]
AppId={{8F3A2C1D-4B7E-4F9A-B2D6-1C8E5F3A9D2B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\obs-studio
DirExistsWarning=no
DisableDirPage=no
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=.
OutputBaseFilename=AudioStemExporter-{#MyAppVersion}-Windows-x64-Setup
SetupIconFile=
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\obs-plugins\64bit\{#MyPluginID}.dll
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nRecord any OBS audio source directly to MP3, WAV, or AIFF — no conversion needed. Perfect for DJ mixes and multi-source stem recording.%n%nClick Next to continue.

[Files]
Source: {#MyDLL};    DestDir: "{app}\obs-plugins\64bit";                        Flags: ignoreversion
Source: {#MyLocale}; DestDir: "{app}\data\obs-plugins\{#MyPluginID}\locale";   Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data\obs-plugins\{#MyPluginID}"

[Run]
Filename: "{#MyAppURL}"; Description: "Check out more tools at 1134.digital/tools"; Flags: postinstall shellexec skipifsilent unchecked
Filename: "https://ko-fi.com/catch22"; Description: "Buy me a Chai ☕🍵☕ — support development"; Flags: postinstall shellexec skipifsilent unchecked
Filename: "https://www.paypal.biz/1134digital"; Description: "Support via PayPal 💸 — support development"; Flags: postinstall shellexec skipifsilent unchecked

[Code]

// ── Auto-detect OBS install directory ────────────────────────────────────────
function GetOBSDir: String;
var
  RegPath: String;
begin
  RegPath := '';
  if RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio',
                         'InstallLocation', RegPath) then
  begin
    Result := RegPath;
    Exit;
  end;
  Result := ExpandConstant('{autopf}\obs-studio');
end;

procedure InitializeWizard();
begin
  WizardForm.DirEdit.Text := GetOBSDir();
end;

// ── Warn if OBS is running ────────────────────────────────────────────────────
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  Exec(ExpandConstant('{sys}\cmd.exe'),
       '/C tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe" > nul 2>&1',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  if ResultCode = 0 then
  begin
    if MsgBox('OBS Studio is currently running.' + #13#10 +
              'Please close OBS before continuing.' + #13#10#13#10 +
              'Click OK once OBS is closed, or Cancel to abort.',
              mbConfirmation, MB_OKCANCEL) = IDCANCEL then
    begin
      Result := 'Please close OBS Studio and run the installer again.';
    end;
  end;
end;
