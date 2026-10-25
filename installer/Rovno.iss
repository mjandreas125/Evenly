; Inno Setup script for Rovno.  Build: ISCC.exe /DBuildDir=C:\dev\build\Rovno Rovno.iss
#define AppName "Rovno"
#define AppVersion "0.9.0"
#ifndef BuildDir
  #define BuildDir "..\build"
#endif

[Setup]
AppId={{6C2B9E41-0D2A-4B57-9A1E-52F3C8D0A7B4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName=Rovno {#AppVersion}
AppPublisher=Rovno contributors
AppPublisherURL=https://github.com/mjandreas125/rovno
DefaultDirName={autopf}\Rovno
DefaultGroupName=Rovno
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Rovno-{#AppVersion}-setup
SetupIconFile=..\Resources\icon.ico
UninstallDisplayIcon={app}\Rovno.exe
LicenseFile=..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ru.Autostart=Запускать вместе с Windows (в трее)
en.Autostart=Start with Windows (in the tray)
ru.Desktop=Значок на рабочем столе
en.Desktop=Desktop icon
ru.RunNow=Открыть «Ровно»
en.RunNow=Open Rovno

[Tasks]
Name: "autostart"; Description: "{cm:Autostart}"
Name: "desktopicon"; Description: "{cm:Desktop}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\Rovno_artefacts\Release\Rovno.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\RovnoApo.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\rovno_ctl.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Resources\fonts\OFL.txt"; DestDir: "{app}"; DestName: "Onest-OFL.txt"; Flags: ignoreversion

[Dirs]
; Settings for the effect: the app (user) writes them, the audio service (LOCAL SERVICE) reads them.
Name: "{commonappdata}\Rovno"

[Icons]
Name: "{group}\Rovno"; Filename: "{app}\Rovno.exe"
Name: "{autodesktop}\Rovno"; Filename: "{app}\Rovno.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Rovno"; ValueData: """{app}\Rovno.exe"" --tray"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{sys}\icacls.exe"; Parameters: """{commonappdata}\Rovno"" /grant *S-1-5-19:(OI)(CI)M *S-1-5-32-545:(OI)(CI)M"; Flags: runhidden
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\RovnoApo.dll"""; Flags: runhidden
Filename: "{app}\rovno_ctl.exe"; Parameters: "restart-audio"; Flags: runhidden; Check: AudioWasStopped
Filename: "{app}\Rovno.exe"; Description: "{cm:RunNow}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\rovno_ctl.exe"; Parameters: "disable-all"; Flags: runhidden; RunOnceId: "RovnoDisableAll"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\RovnoApo.dll"""; Flags: runhidden; RunOnceId: "RovnoUnregister"

[UninstallDelete]
Type: filesandordirs; Name: "{commonappdata}\Rovno"

[Code]
var
  StoppedAudio: Boolean;

// An update has to replace RovnoApo.dll, which the Windows audio service keeps loaded while
// Rovno is connected to a device: stop the service for the copy, start it again afterwards.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  StoppedAudio := False;
  if FileExists(ExpandConstant('{app}\RovnoApo.dll')) then
  begin
    Exec(ExpandConstant('{sys}\net.exe'), 'stop audiosrv /y', '', SW_HIDE, ewWaitUntilTerminated, Code);
    StoppedAudio := True;
  end;
  Result := '';
end;

function AudioWasStopped(): Boolean;
begin
  Result := StoppedAudio;
end;
