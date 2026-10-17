; ============================================================================
;  disktest / 硬盘可靠性测试工具 —— Inno Setup 6 安装脚本
;
;  用法（通常由 tools\make-installer.ps1 或 build.bat installer 调用）：
;      ISCC.exe /DMyAppVersion=<版本> installer\disktest.iss
;
;  输入  ..\build\dist        build.bat package 的产物
;                             （disktest.exe + Qt DLL + 平台插件 + fio + 许可证）
;  输出  ..\build\installer\disktest-<版本>-setup.exe
;
;  安装范围由向导里的「为谁安装」页选择：
;      所有用户    → {autopf}\Disktest                       需要一次 UAC 提权
;      仅当前用户  → {localappdata}\Programs\Disktest        不弹 UAC
;
;  注意：本文件必须存成 UTF-8 with BOM。里面是中文，不带 BOM 时 ISCC
;        会按 ANSI 代码页读，安装界面上全是乱码。
; ============================================================================

#define MyAppName "磁盘测试By MOONLIGHT"
#define MyAppNameTw "磁碟測試By MOONLIGHT"
#define MyAppNameEn "Disk Tester By MOONLIGHT"
#ifndef MyAppVersion
  ; 只是个兜底：make-installer.ps1 从 CMakeLists.txt 读版本并通过 /D 传进来。
  #define MyAppVersion "3.0.7"
#endif
#define MyAppPublisher "MOONLIGHT"
#define MyAppExeName "disktest.exe"
; AppId 一旦发布就不能再改：Windows 靠它认出「这是同一个程序的另一个版本」，
; 改了会变成两个程序、旧版卸不掉。{{ 是 Inno 里表示字面量 { 的转义。
#define MyAppId "{{9F2C6B14-7A3D-4E58-B0C9-1D6E8F4A2B73}"
#define DistDir "..\build\dist"

; 编译期就把「dist 不在」挡下来，否则打出来的是个只有许可证的空包。
#if !FileExists(AddBackslash(SourcePath) + DistDir + "\" + MyAppExeName)
  #error 找不到 build\dist\disktest.exe —— 先运行 build.bat package 生成 dist
#endif
#if !FileExists(AddBackslash(SourcePath) + DistDir + "\platforms\qwindows.dll")
  #error build\dist 里没有 platforms\qwindows.dll —— dist 不完整，重新跑 build.bat package
#endif

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppCopyright=Copyright (C) 2026 {#MyAppPublisher}. GNU GPL v3 or later.
AppComments={#MyAppNameEn} {#MyAppVersion} - fio based disk reliability tester
DefaultDirName={autopf}\Disktest
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
; GPL 第 4/6 条要求随二进制提供许可证文本；发行包里 licenses\ 还有 Qt 与 fio 的。
LicenseFile=..\LICENSE
; 开测前必须先看到的警告页（这个程序会写满分区）。
; InfoBeforeFile 在编译期只能写一个文件，而三种界面语言各有一份；
; 所以这里先放简体那份把页面建出来，运行时在 [Code] 里按实际语言换成对应的。
InfoBeforeFile=preinstall.zh-CN.txt
OutputDir=..\build\installer
OutputBaseFilename=disktest-{#MyAppVersion}-setup
SetupIconFile=..\resources\logo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={cm:AppShortcutName} {#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; 程序是 x64 的 Qt 6.8 构建，最低 Windows 10 1809（17763）—— Qt 6.8 的下限。
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
; 默认不要求管理员：向导会询问「为所有用户」还是「仅为我」，
; 选了所有用户才重启一次并走 UAC。命令行用 /ALLUSERS 或 /CURRENTUSER 直接指定。
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
SetupMutex=disktest-setup-mutex
; 升级时如果程序正开着：请它关掉再继续，而不是让文件拷贝失败。
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppNameEn} Setup
VersionInfoProductName={#MyAppNameEn}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoCopyright=Copyright (C) 2026 {#MyAppPublisher}

[Languages]
; 官方 Inno 的 Languages\ 目录里没有中文。这两份是 jrsoftware 的 istrans 列表上
; 的非官方翻译（简体：Zhenghan Yang；繁体：GoneTone），放在仓库里随工程维护，
; 免得每台打包机都要自己装一遍。
;
; 次序就是回退次序：系统语言匹配不上任何一份时用第一个（简体）。
; 简体 = LanguageID $0804（zh-CN），繁体 = $0404（zh-TW）；繁体系统会自己选到繁体。
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "chinesetraditional"; MessagesFile: "ChineseTraditional.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
; 标题栏里的品牌名也要跟着界面语言走：英文界面摆一个中文品牌名很怪，
; 繁体界面里更不该出现简体字（程序自己的窗口标题叫「磁碟測試By MOONLIGHT」，
; 见 resources/i18n/zh_TW.tsv 的 kAppName）。
chinesesimplified.SetupWindowTitle={#MyAppName} {#MyAppVersion} 安装程序
chinesetraditional.SetupWindowTitle={#MyAppNameTw} {#MyAppVersion} 安裝程式
english.SetupWindowTitle={#MyAppNameEn} {#MyAppVersion} Setup

[CustomMessages]
; 开始菜单 / 桌面快捷方式的名字，以及卸载项显示的名字（同一份程序，三种叫法）
AppShortcutName={#MyAppName}
chinesetraditional.AppShortcutName={#MyAppNameTw}
english.AppShortcutName={#MyAppNameEn}

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; 整个 dist 原样搬进去：exe、Qt DLL、plugins（platforms/styles/imageformats/...）、
; fio\、licenses\、README.md 与它引用的 docs\images\。
;
; 排除 vc_redist.x64.exe：windeployqt 在加载了 vcvars 的环境里会顺手把 18 MB 的
; VC++ 运行库*安装程序*也拷进 dist，而 CMakeLists 已经把 msvcp140/vcruntime140 等
; 10 个 CRT DLL 拷在 {app} 里了 —— 再带一个安装程序纯属重复，且它自身是压缩包、
; 装进 setup.exe 后几乎不缩小（占了安装包一半体积）。
Source: "{#DistDir}\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe"; Flags: ignoreversion recursesubdirs createallsubdirs

; 「安装前必读」三份译文：dontcopy = 不安装，只在运行时按语言提取到 {tmp}
; （提取与替换见 [Code] 的 CurPageChanged）。
Source: "preinstall.*.txt"; DestDir: "{tmp}"; Flags: dontcopy

[Dirs]
; 程序默认把测试结果 CSV 存到自己所在的目录（mainwindow.cpp 里保存对话框的默认路径），
; 界面语言也存在这里的 disktest.ini。装进 Program Files 之后普通用户对 {app} 没有写权限，
; 这两件事都会失败，所以给 Users 加可修改权限。仅为当前用户安装时 Inno 会忽略这个参数
; （%LOCALAPPDATA%\Programs 本来就可写）。
Name: "{app}"; Permissions: users-modify

[UninstallDelete]
; 语言设置是程序运行时自己建的（不属于 [Files] 装的，Inno 不会自动删），卸载时顺手清掉。
; 同一目录下的测试结果 CSV **不删**：那是用户的数据，别在卸载时替用户做决定。
Type: files; Name: "{app}\disktest.ini"

[Icons]
Name: "{autoprograms}\{cm:AppShortcutName}"; Filename: "{app}\{#MyAppExeName}"; Comment: "{#MyAppNameEn}"
Name: "{autodesktop}\{cm:AppShortcutName}"; Filename: "{app}\{#MyAppExeName}"; Comment: "{#MyAppNameEn}"; Tasks: desktopicon

[Registry]
; App Paths：让 Win+R 里直接敲 disktest 也能起来。按安装范围落到 HKLM 或 HKCU。
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\{#MyAppExeName}"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey; Check: IsAdminInstallMode
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\{#MyAppExeName}"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey; Check: not IsAdminInstallMode

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
// ----------------------------------------------------------------------------
//  「安装前必读」按界面语言换文件
//
//  InfoBeforeFile 是编译期指令，只能指定一个文件；而这句话三种语言得各说一遍。
//  做法：页面还是由简体那份建出来（见 [Setup] 的 InfoBeforeFile），三份都以
//  dontcopy 打进 setup，翻到「信息」页时把 memo 的内容换成当前语言那一份。
//
//  放在 CurPageChanged 而不是 InitializeWizard，是因为 memo 的正文在页面显示前
//  才定稿；在初始化里改会被随后的加载覆盖掉。
// ----------------------------------------------------------------------------
var
  InfoBeforeFileLoaded: Boolean;

procedure LoadInfoBeforePage;
var
  FileName: String;
begin
  case ActiveLanguage of
    'chinesetraditional': FileName := 'preinstall.zh-TW.txt';
    'english':            FileName := 'preinstall.en.txt';
  else
    FileName := 'preinstall.zh-CN.txt';
  end;

  if not InfoBeforeFileLoaded then
  begin
    ExtractTemporaryFile(FileName);
    InfoBeforeFileLoaded := True;
  end;

  WizardForm.InfoBeforeMemo.Lines.LoadFromFile(ExpandConstant('{tmp}\') + FileName);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpInfoBefore then
    LoadInfoBeforePage;
end;
