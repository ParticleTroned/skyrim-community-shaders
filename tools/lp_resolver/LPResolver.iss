; Inno Setup template for LP Conflict Resolver.
; Update SourceDir before compiling this script in Inno Setup.

#define AppName "LP Conflict Resolver"
#define AppVersion "0.1.0"
#define Publisher "Community Shaders"
#define ExeName "LPConflictResolver.exe"
#define SourceDir "C:\path\to\skyrim-community-shaders\dist\lp_resolver_app\LPConflictResolver"

[Setup]
AppId={{E5A85A2D-1B73-4BF5-B8A9-2C90CB5B3F3A}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputBaseFilename=LPConflictResolver-Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#ExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#ExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

