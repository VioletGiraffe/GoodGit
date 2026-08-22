#define MyAppName "GoodGit"
#define MyAppPublisher "VioletGiraffe"
#define MyAppExeName "gg.exe"
#define LauncherDirName "launcher"
#define VCRedistExeName "vc_redist.x64.exe"
; Version is read from the built exe (which gets it from VERSION in app/app.pro) - single source of truth
#define MyAppVersion GetVersionNumbersString(AddBackslash(SourcePath) + "dist\" + MyAppExeName)

[Setup]
; Fixed install identity: must never change, or upgrades stop finding existing installs
AppId={{5CAAFEAD-8D18-40E0-8D15-D674168996C4}
AppName={#MyAppName}
AppPublisher={#MyAppPublisher}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=.
OutputBaseFilename={#MyAppName}

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=app\res\goodgit.ico

; The launcher dir is added to the system PATH (gg is meant to be launched from a terminal in a repo);
; this makes the installer broadcast the environment change so new shells pick it up without a reboot
ChangesEnvironment=yes

SolidCompression=true
LZMANumBlockThreads=4
Compression=lzma2/ultra64
LZMAUseSeparateProcess=yes
LZMABlockSize=8192

[Files]
; Both exes have their own entry so ignoreversion forces overwrite on same-version rebuilds. Being non-wildcard
; Sources, they also make a missing exe (e.g. a failed build) a hard compile error instead of a silent broken installer.
; The wildcard's Excludes matches by file name at any depth, so it covers the launcher copy too.
Source: "{#SourcePath}\dist\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\dist\{#LauncherDirName}\{#MyAppExeName}"; DestDir: "{app}\{#LauncherDirName}"; Flags: ignoreversion
Source: "{#SourcePath}\dist\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs; Excludes: "{#VCRedistExeName},{#MyAppExeName}"
Source: "{#SourcePath}\dist\{#VCRedistExeName}";  DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#SourcePath}\LICENSE"; DestDir: "{app}"

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autoprograms}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: {cm:CreateDesktopIcon}; GroupDescription: {cm:AdditionalIcons};

[Registry]
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
	ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}\{#LauncherDirName}"; \
	Check: NeedsAddToPath(ExpandConstant('{app}\{#LauncherDirName}'))

[Run]
Filename: "{tmp}\{#VCRedistExeName}"; Parameters: "/install /quiet /norestart"; StatusMsg: Installing Microsoft C++ Runtime...; Flags: runhidden waituntilterminated skipifdoesntexist

[UninstallDelete]
Type: dirifempty; Name: "{app}\{#LauncherDirName}"
Type: dirifempty; Name: "{app}"

[Code]
const
	EnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

function NeedsAddToPath(Dir: string): Boolean;
var
	Path: string;
begin
	if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Path) then
	begin
		Result := True;
		exit;
	end;
	// Semicolon-delimited exact segment match, so e.g. an existing "...\GoodGit2" does not count as present
	Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Path) + ';') = 0;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
	Path, Dir: string;
	Position: Integer;
begin
	if CurUninstallStep <> usPostUninstall then
		exit;
	if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Path) then
		exit;

	Dir := ExpandConstant('{app}\{#LauncherDirName}');
	Position := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Path) + ';');
	if Position = 0 then
		exit;

	// Position is within the ';'-wrapped copy, one ahead of the raw value; deleting Length(Dir)+1
	// characters removes the segment together with one of its two delimiters. When the segment is
	// the first entry the raw value has no leading ';', so the deletion starts at 1 and eats the
	// trailing delimiter instead.
	if Position > 1 then
		Delete(Path, Position - 1, Length(Dir) + 1)
	else
		Delete(Path, 1, Length(Dir) + 1);
	RegWriteExpandStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Path);
end;
