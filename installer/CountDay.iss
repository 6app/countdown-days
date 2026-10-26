; 倒计日安装包（Inno Setup 6）
;
; 按用户安装到 %LOCALAPPDATA%\Programs\CountDay，不需要管理员权限。
; 安装：一次确认 → 自动启动并设为开机自启。
; 卸载：在「设置 → 应用」里卸载，会关闭程序、删除自启项；保留 %APPDATA%\CountDay 下的配置。
;
; 编译: iscc /DAppVersion=1.0.1 /DSourceExe=..\build\MinSizeRel\CountDay.exe installer\CountDay.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\build\MinSizeRel\CountDay.exe"
#endif

[Setup]
AppId={{8C3F6A52-4E1B-4F7D-9A2E-5B6C7D8E9F01}
AppName=倒计日
AppVersion={#AppVersion}
AppVerName=倒计日 {#AppVersion}
AppPublisher=6app
AppPublisherURL=https://github.com/6app/countdown-days
AppSupportURL=https://github.com/6app/countdown-days/issues
AppUpdatesURL=https://github.com/6app/countdown-days/releases
VersionInfoVersion={#AppVersion}
VersionInfoDescription=倒计日安装程序

; 按用户安装：{autopf} = %LOCALAPPDATA%\Programs
PrivilegesRequired=lowest
DefaultDirName={autopf}\CountDay
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; 尽量少的页面：一次确认即可
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
ShowLanguageDialog=no
WizardStyle=modern

; 升级时自动关闭正在运行的旧版本
CloseApplications=force
RestartApplications=no

SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\CountDay.exe
UninstallDisplayName=倒计日
OutputDir=..\dist
OutputBaseFilename=CountDay-Setup-v{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes

[Languages]
Name: "chs"; MessagesFile: "ChineseSimplified.isl"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\倒计日"; Filename: "{app}\CountDay.exe"

[Registry]
; 开机自启（与程序托盘菜单里的开关是同一个值，卸载时删除）
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "CountDay"; ValueData: """{app}\CountDay.exe"""; Flags: uninsdeletevalue

[Run]
Filename: "{app}\CountDay.exe"; Description: "立即启动倒计日"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM CountDay.exe"; Flags: runhidden; RunOnceId: "StopCountDay"
