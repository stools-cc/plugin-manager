#ifndef MyAppVersion
  #define MyAppVersion "dev"
#endif

#define MyAppName "stools Plugin Manager"
#define MyAppPublisher "stools.cc"
#define MyAppURL "https://stools.cc"

[Setup]
AppId={{F7A2C8E1-9B34-4D56-A1E3-7C8F5D2B0A91}
AppName={#MyAppName} (OBS Plugin)
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={code:GetOBSDir}
DirExistsWarning=no
DisableProgramGroupPage=yes
OutputDir=release
OutputBaseFilename=st-pluginmanager-{#MyAppVersion}-windows-installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName} (OBS Plugin)
SourceDir=..

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Files]
Source: "build\st-pluginmanager.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion

[UninstallDelete]
Type: files; Name: "{app}\obs-plugins\64bit\st-pluginmanager.dll"
Type: files; Name: "{app}\obs-plugins\64bit\.st-pluginmanager.version"

[Messages]
english.WelcomeLabel2=This will install the {#MyAppName} plugin for OBS Studio.%n%nPlease close OBS Studio before continuing.
german.WelcomeLabel2=Dies installiert das {#MyAppName} Plugin f%C3%BCr OBS Studio.%n%nBitte schlie%C3%9Fe OBS Studio vor der Installation.

[Code]
function GetOBSDir(Param: String): String;
var
  Path: String;
begin
  if RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', Path) then
    Result := Path
  else
    Result := ExpandConstant('{autopf}\obs-studio');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    if FindWindowByClassName('OBSMainWindow') <> 0 then
    begin
      MsgBox('OBS Studio is currently running. Please close it before continuing.', mbError, MB_OK);
      Abort;
    end;
  end;
end;
