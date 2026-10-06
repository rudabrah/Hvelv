; Installasjonsprogram for Hvelv (Inno Setup 6). Bygges med make_installer.bat.
;
; Installerer appen i Program Files, og ved behov:
;   - ODBC Driver 18 for SQL Server (hvis verken 17 eller 18 finnes)
;   - Visual C++ Redistributable (bare når appen er bygget med MSVC)
; SQL Server selv installeres ikke; den personlige databasen legges på en eksisterende instans.
;
; Stille installasjon (Intune/SCCM):  Hvelv-Setup.exe /VERYSILENT /NORESTART
; Brukerdata (hvelvfiler, databaser, innstillinger) røres ikke ved avinstallering.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef AppDir
  #define AppDir "..\build\Release\app"
#endif
#define Redist "redist"
#define MinGW FileExists(AppDir + "\libstdc++-6.dll")

#if !FileExists(AppDir + "\Hvelv.exe")
  #error Fant ikke Hvelv.exe. Kjør build.bat først.
#endif
#if !FileExists(Redist + "\msodbcsql.msi")
  #error Mangler filer i installer\redist. Kjør installer\fetch_redist.ps1 først.
#endif
#if !MinGW && !FileExists(Redist + "\VC_redist.x64.exe")
  #error MSVC-bygg: legg VC_redist.x64.exe i installer\redist.
#endif

[Setup]
AppId={{C312CD2F-2F17-4225-BA8A-C18DA28C8EED}
AppName=Hvelv
AppVersion={#AppVersion}
AppVerName=Hvelv {#AppVersion}
AppPublisher=Integrasjonspartner
DefaultDirName={autopf}\Hvelv
DefaultGroupName=Hvelv
DisableProgramGroupPage=yes
; ODBC-driveren installeres for hele maskinen.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=output
OutputBaseFilename=Hvelv-Setup-{#AppVersion}
SetupIconFile=..\assets\hvelv.ico
UninstallDisplayIcon={app}\Hvelv.exe
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; Lukker Hvelv (også i systemkurven) før filene byttes ved oppdatering.
CloseApplications=force
RestartApplications=no

[Languages]
Name: "nb"; MessagesFile: "compiler:Languages\Norwegian.isl"


[Tasks]
Name: "desktopicon"; Description: "Lag snarvei på skrivebordet"; Flags: unchecked
Name: "autostart"; Description: "Start Hvelv når brukere logger på Windows (påminnelser og hurtigtaster)"; Flags: unchecked

[Files]
Source: "{#AppDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Redist}\msodbcsql.msi"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: NeedsOdbc

#if !MinGW
Source: "{#Redist}\VC_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall
#endif

[Icons]
Name: "{autoprograms}\Hvelv"; Filename: "{app}\Hvelv.exe"
Name: "{autodesktop}\Hvelv"; Filename: "{app}\Hvelv.exe"; Tasks: desktopicon

[Registry]
; Autostart for alle brukere (installasjonen kjører som administrator), fjernes ved avinstallering.
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Hvelv"; \
    ValueData: """{app}\Hvelv.exe"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
#if !MinGW
Filename: "{tmp}\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "Installerer Visual C++ Redistributable…"; Flags: waituntilterminated
#endif
Filename: "msiexec.exe"; Parameters: "/i ""{tmp}\msodbcsql.msi"" /qn /norestart IACCEPTMSODBCSQLLICENSETERMS=YES"; \
    StatusMsg: "Installerer ODBC Driver 18 for SQL Server…"; Flags: waituntilterminated; Check: NeedsOdbc

Filename: "{app}\Hvelv.exe"; Description: "Start Hvelv"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
function OdbcDriverInstalled(const Name: String): Boolean;
var
  Value: String;
begin
  Result := RegQueryStringValue(HKLM64, 'SOFTWARE\ODBC\ODBCINST.INI\ODBC Drivers', Name, Value)
            and (Value = 'Installed');
end;

function NeedsOdbc: Boolean;
begin
  Result := not OdbcDriverInstalled('ODBC Driver 18 for SQL Server')
            and not OdbcDriverInstalled('ODBC Driver 17 for SQL Server');
end;
