; ── Jot — Inno Setup installer script ─────────────────────────────────────────
; Per-user install (no admin needed): %LOCALAPPDATA%\Programs\Jot.
;
; Build (from the repository root, after `cmake --install build --prefix stage`):
;   ISCC.exe /DAppVersion=1.0.0 packaging\windows\jot.iss
; The GitHub Actions release workflow does this automatically on every v* tag.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#define AppName "Jot"
#define AppPublisher "JoshuaRVL"
#define StageDir "..\..\stage"

[Setup]
AppId={{31FD2953-14EE-4C60-8261-9D94C1A4B03E}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/JoshuaRVLS/jot
AppSupportURL=https://github.com/JoshuaRVLS/jot/issues
AppUpdatesURL=https://github.com/JoshuaRVLS/jot/releases

; Per-user, no elevation. Professional layout under LOCALAPPDATA.
DefaultDirName={localappdata}\Programs\Jot
DisableProgramGroupPage=yes
PrivilegesRequired=lowest

; The native Windows build is 64-bit MSVC only.
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

OutputDir=installer
OutputBaseFilename=jot-setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=jot.ico
UninstallDisplayIcon={app}\jot.ico
UninstallDisplayName={#AppName} {#AppVersion}
ChangesEnvironment=yes
CloseApplications=no
ShowLanguageDialog=no
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} - fast terminal code editor

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; The binary plus the data tree exactly as `cmake --install` produced it.
Source: "{#StageDir}\bin\jot.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#StageDir}\share\jot\configs\*"; DestDir: "{app}\share\jot\configs"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\share\jot\lua\*"; DestDir: "{app}\share\jot\lua"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "jot.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Jot"; Filename: "{app}\bin\jot.exe"; IconFilename: "{app}\jot.ico"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\Jot"; Filename: "{app}\bin\jot.exe"; IconFilename: "{app}\jot.ico"; WorkingDir: "{app}\bin"; Tasks: desktopicon

; ── PATH (per-user, appended once, removed cleanly on uninstall) ──────────────
[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}\bin"; Check: NeedsAddPath(ExpandConstant('{app}\bin'))

[Code]
function NeedsAddPath(Param: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    Exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure RemoveFromPath(InstalledPath: string);
var
  Path: string;
begin
  if RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then
  begin
    // StringChange replaces every occurrence (the appended entry is removed
    // with exact case, so flag-free replacement is correct here). Inno's
    // PascalScript has no rfReplaceAll/rfIgnoreCase constants.
    StringChange(Path, ';' + InstalledPath, '');
    StringChange(Path, InstalledPath + ';', '');
    StringChange(Path, InstalledPath, '');
    RegWriteExpandStringValue(HKCU, 'Environment', 'Path', Path);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveFromPath(ExpandConstant('{app}\bin'));
end;