#Requires -Version 5.1
<#
.SYNOPSIS
    把 build\dist 打成一个可安装的 setup.exe（Inno Setup）。

.DESCRIPTION
    唯一的输入是 build\dist —— 也就是 `build.bat package` 的产物
    （disktest.exe + Qt DLL + 平台插件 + fio + 许可证 + README）。
    本脚本不编译程序、不改源码，只做「检查 dist 是否完整 → 调 ISCC 编译安装脚本」。

    产物：build\installer\disktest-<版本>-setup.exe

.PARAMETER Version
    安装包的版本号。默认从 CMakeLists.txt 的 project(... VERSION x.y.z) 读，
    那是全工程唯一的版本来源，别在别处写死。

.PARAMETER DistDir
    默认 build\dist。

.PARAMETER OutDir
    默认 build\installer。

.PARAMETER Iscc
    ISCC.exe 的完整路径。默认依次找 PATH、%LOCALAPPDATA%\Programs\Inno Setup 6、
    Program Files (x86)、Program Files。

.PARAMETER PortableZip
    顺手打一个绿色版 zip（build\installer\disktest-<版本>-portable.zip），
    内容就是 build\dist。

.EXAMPLE
    pwsh tools\make-installer.ps1
    pwsh tools\make-installer.ps1 -PortableZip
    build.bat installer          # 全量重建 + package + 安装包，一条命令
#>
[CmdletBinding()]
param(
    [string]$Version,
    [string]$DistDir,
    [string]$OutDir,
    [string]$Iscc,
    [switch]$PortableZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DistDir) { $DistDir = Join-Path $RepoRoot 'build\dist' }
if (-not $OutDir)  { $OutDir  = Join-Path $RepoRoot 'build\installer' }
$IssPath = Join-Path $RepoRoot 'installer\disktest.iss'

function Write-Step([string]$Text) { Write-Host "[installer] $Text" }

# ---------------------------------------------------------------- 版本号
if (-not $Version) {
    $cmakeLists = Join-Path $RepoRoot 'CMakeLists.txt'
    if (-not (Test-Path $cmakeLists)) { throw "找不到 $cmakeLists" }
    # 不能用 Select-String：它按行匹配，而 project(...) 的 VERSION 写在下一行。
    $cmakeText = Get-Content -Path $cmakeLists -Raw -Encoding UTF8
    $match = [regex]::Match($cmakeText, 'project\s*\(\s*disktest\s+VERSION\s+([0-9]+(?:\.[0-9]+)+)')
    if (-not $match.Success) {
        throw "没能从 CMakeLists.txt 的 project(... VERSION) 里读出 disktest 的版本号。"
    }
    $Version = $match.Groups[1].Value
}
if ($Version -notmatch '^[0-9]+(\.[0-9]+)+$') {
    throw "版本号 '$Version' 不是 x.y[.z] 形式。"
}
Write-Step "版本 $Version"

# ---------------------------------------------------------------- dist 完整性
# 这些少了任何一个，装出来的程序都起不来（或者法律上不能发）。宁可在编译安装包
# 之前报错，也不要打出一个装完打不开的 setup.exe。
$required = @(
    'disktest.exe'
    'Qt6Core.dll'
    'Qt6Widgets.dll'
    'Qt6Charts.dll'
    'platforms\qwindows.dll'
    'vcruntime140.dll'
    'msvcp140.dll'
    'fio\fio.exe'
    'fio\COPYING.txt'
    'licenses\LICENSE'
    'licenses\THIRD-PARTY-NOTICES.md'
    'licenses\Qt-LGPLv3.txt'
    'licenses\fio-GPLv2.txt'
)
if (-not (Test-Path $DistDir)) {
    throw "找不到 $DistDir —— 先运行 build.bat package（或 cmake --build build --target package）。"
}
$missing = @()
foreach ($rel in $required) {
    if (-not (Test-Path (Join-Path $DistDir $rel))) { $missing += $rel }
}
if ($missing.Count -gt 0) {
    $list = ($missing | ForEach-Object { "    $_" }) -join [Environment]::NewLine
    throw ("$DistDir 不完整，缺这些文件：`n$list`n" +
           "重新跑 build.bat package —— windeployqt 或 MSVC 运行库那一步没成功时就会这样。")
}
Write-Step "dist 完整（$( (Get-ChildItem $DistDir -Recurse -File).Count ) 个文件，$([math]::Round((Get-ChildItem $DistDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)) MB）"
# dist 里的 exe 自己也必须带版本资源，而且版本要跟 CMakeLists 对得上 ——
# 用户看到的是它的「属性 → 详细信息」（那一屏由 CMake 生成的 app.rc 提供），
# 版本对不上就说明 dist 里躺的是上一次编译的 exe。
$exeVi = (Get-Item (Join-Path $DistDir 'disktest.exe')).VersionInfo
$exeProductVersion = "$($exeVi.ProductVersion)".Trim()
if (-not $exeProductVersion) {
    throw ("$DistDir\disktest.exe 没有版本资源 —— 它应该由 CMake 生成的 app.rc 编进去。`n" +
           "重新跑 build.bat package。")
}
if ($exeProductVersion -ne $Version) {
    throw ("dist 里 disktest.exe 的产品版本是 '$exeProductVersion'，与 CMakeLists 的 $Version 不一致。`n" +
           "dist 是旧的，重新跑 build.bat package。")
}
Write-Step "exe 版本资源 = $exeProductVersion（$($exeVi.ProductName) / $($exeVi.CompanyName)）"

# ---------------------------------------------------------------- 找 ISCC
function Find-Iscc {
    param([string]$ExplicitPath)
    if ($ExplicitPath) {
        if (-not (Test-Path $ExplicitPath)) { throw "指定的 ISCC 不存在：$ExplicitPath" }
        return (Resolve-Path $ExplicitPath).Path
    }
    $cmd = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    # winget 默认是「仅为当前用户」安装，落在 LOCALAPPDATA\Programs 下。
    $candidates = @()
    if (${env:LOCALAPPDATA}) { $candidates += (Join-Path ${env:LOCALAPPDATA} 'Programs\Inno Setup 6\ISCC.exe') }
    if (${env:ProgramFiles(x86)}) { $candidates += (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe') }
    if ($env:ProgramFiles) { $candidates += (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe') }
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    throw ("找不到 ISCC.exe（Inno Setup 的编译器）。装一下：`n" +
           "    winget install --id JRSoftware.InnoSetup --exact`n" +
           "或者用 -Iscc <ISCC.exe 的完整路径> 指定。")
}

$IsccExe = Find-Iscc -ExplicitPath $Iscc
# ISCC.exe 自己没有版本资源（FileVersion 是 0.0.0.0），只报路径。
Write-Step "ISCC: $IsccExe"

# ---------------------------------------------------------------- 编译安装包
if (-not (Test-Path $IssPath)) { throw "找不到安装脚本 $IssPath" }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

Write-Step "编译 $IssPath ..."
Push-Location $RepoRoot
try {
    & $IsccExe "/DMyAppVersion=$Version" '/Qp' $IssPath
    $isccExit = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($isccExit -ne 0) {
    throw "ISCC 失败（退出码 $isccExit）。上面的输出里有具体是哪一行。"
}

$SetupPath = Join-Path $OutDir "disktest-$Version-setup.exe"
if (-not (Test-Path $SetupPath)) {
    throw "ISCC 报告成功，但没找到 $SetupPath —— 检查 disktest.iss 的 OutputDir/OutputBaseFilename。"
}

# 光有文件还不够：确认它真的是个 PE，版本资源也对得上。
# Inno 把版本字符串按固定宽度补了空格，比较前必须 Trim。
$setupItem = Get-Item $SetupPath
$pe = $setupItem.VersionInfo
$productVersion = "$($pe.ProductVersion)".Trim()
if (-not $productVersion) { throw "$SetupPath 没有版本资源，编译出来的可能不是安装包。" }
if ($productVersion -ne $Version) {
    throw "安装包的版本资源是 '$productVersion'，与 /DMyAppVersion=$Version 不一致。"
}
Write-Step "安装包版本资源 = $productVersion"

$hash = (Get-FileHash $SetupPath -Algorithm SHA256).Hash
Write-Host ''
Write-Host '  安装包: ' -NoNewline
Write-Host $SetupPath -ForegroundColor Green
Write-Host ("  大小  : {0:N1} MB" -f ($setupItem.Length / 1MB))
Write-Host "  SHA256: $hash"

# ---------------------------------------------------------------- 绿色版 zip（可选）
if ($PortableZip) {
    $zipPath = Join-Path $OutDir "disktest-$Version-portable.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Write-Step "打绿色版 zip ..."
    Compress-Archive -Path (Join-Path $DistDir '*') -DestinationPath $zipPath -CompressionLevel Optimal
    $zipItem = Get-Item $zipPath
    Write-Host ''
    Write-Host '  绿色版: ' -NoNewline
    Write-Host $zipPath -ForegroundColor Green
    Write-Host ("  大小  : {0:N1} MB" -f ($zipItem.Length / 1MB))
}
