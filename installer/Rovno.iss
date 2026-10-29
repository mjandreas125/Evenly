; Inno Setup script for Evenly (internal file names keep the project's old name, Rovno).
; Build: ISCC.exe /DBuildDir=C:\dev\build\Rovno Rovno.iss
#define AppName "Evenly"
#define AppVersion "0.9.0"
#ifndef BuildDir
  #define BuildDir "..\build"
#endif

[Setup]
AppId={{6C2B9E41-0D2A-4B57-9A1E-52F3C8D0A7B4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName=Evenly {#AppVersion}
AppPublisher=Evenly contributors
AppPublisherURL=https://github.com/mjandreas125/Evenly
DefaultDirName={autopf}\Rovno
DefaultGroupName=Evenly
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Evenly-{#AppVersion}-setup
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
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "pt"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "nl"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "sv"; MessagesFile: "compiler:Languages\Swedish.isl"
Name: "fi"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "tr"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[CustomMessages]
ru.Autostart=Запускать вместе с Windows (в трее)
ru.Desktop=Значок на рабочем столе
ru.RunNow=Открыть «Ровно»
en.Autostart=Start with Windows (in the tray)
en.Desktop=Desktop icon
en.RunNow=Open Evenly
de.Autostart=Mit Windows starten (im Infobereich)
de.Desktop=Symbol auf dem Desktop
de.RunNow=Evenly öffnen
fr.Autostart=Démarrer avec Windows (dans la zone de notification)
fr.Desktop=Icône sur le Bureau
fr.RunNow=Ouvrir Evenly
es.Autostart=Iniciar con Windows (en la bandeja)
es.Desktop=Icono en el escritorio
es.RunNow=Abrir Evenly
it.Autostart=Avvia con Windows (nell'area di notifica)
it.Desktop=Icona sul desktop
it.RunNow=Apri Evenly
pt.Autostart=Iniciar com o Windows (na bandeja)
pt.Desktop=Ícone na área de trabalho
pt.RunNow=Abrir o Evenly
pl.Autostart=Uruchamiaj z Windows (w zasobniku)
pl.Desktop=Ikona na pulpicie
pl.RunNow=Otwórz Evenly
nl.Autostart=Starten met Windows (in het systeemvak)
nl.Desktop=Pictogram op het bureaublad
nl.RunNow=Evenly openen
sv.Autostart=Starta med Windows (i systemfältet)
sv.Desktop=Ikon på skrivbordet
sv.RunNow=Öppna Evenly
fi.Autostart=Käynnistä Windowsin mukana (ilmaisinalueella)
fi.Desktop=Kuvake työpöydälle
fi.RunNow=Avaa Evenly
tr.Autostart=Windows ile başlat (sistem tepsisinde)
tr.Desktop=Masaüstü simgesi
tr.RunNow=Evenly'yi aç
uk.Autostart=Запускати разом із Windows (у треї)
uk.Desktop=Значок на робочому столі
uk.RunNow=Відкрити Evenly

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

[InstallDelete]
; shortcuts of the early builds, before the program was renamed
Type: files; Name: "{group}\Rovno.lnk"
Type: files; Name: "{autodesktop}\Rovno.lnk"

[Icons]
Name: "{group}\Evenly"; Filename: "{app}\Rovno.exe"
Name: "{autodesktop}\Evenly"; Filename: "{app}\Rovno.exe"; Tasks: desktopicon

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
