#ifndef APP_VERSION
  #define APP_VERSION "0.1.0"
#endif

#ifndef STAGE_DIR
  #error STAGE_DIR is required. Example: /DSTAGE_DIR=B:\glassNote\build-release\stage
#endif

#ifndef OUTPUT_DIR
  #define OUTPUT_DIR "dist"
#endif

#define APP_NAME "glassNote"
#define APP_PUBLISHER "glassNote"

[Setup]
AppId={{3F8A1B59-ED95-4713-86D1-35DF294A7FA9}
AppName={#APP_NAME}
AppVersion={#APP_VERSION}
AppPublisher={#APP_PUBLISHER}
DefaultDirName={autopf}\{#APP_NAME}
DefaultGroupName={#APP_NAME}
OutputDir={#OUTPUT_DIR}
OutputBaseFilename=glassNote-{#APP_VERSION}-win64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
UninstallDisplayIcon={app}\glassNote.exe
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#STAGE_DIR}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#APP_NAME}"; Filename: "{app}\glassNote.exe"
Name: "{autodesktop}\{#APP_NAME}"; Filename: "{app}\glassNote.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\glassNote.exe"; Description: "Launch {#APP_NAME}"; Flags: nowait postinstall skipifsilent
