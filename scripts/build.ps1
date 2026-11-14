[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}

if (-not $vsPath) {
    throw '缺少 MSVC 或 Windows SDK。请先以管理员身份运行 scripts\bootstrap-build-tools.ps1，再重新打开终端构建。'
}

$devShell = Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1'
& $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmake) {
    $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path $candidate) {
        $cmake = $candidate
    }
}
if (-not $cmake) {
    throw '缺少 CMake。请先以管理员身份运行 scripts\bootstrap-build-tools.ps1，再重新打开终端构建。'
}

$buildDir = Join-Path $root 'build'
& $cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) {
    throw "CMake 配置失败，退出码 $LASTEXITCODE"
}

& $cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    throw "编译失败，退出码 $LASTEXITCODE"
}

$dll = Join-Path $buildDir "bin\$Configuration\TV1TSF.dll"
if (-not (Test-Path $dll)) {
    throw "构建命令完成，但未找到 $dll"
}
Write-Host "构建完成：$dll" -ForegroundColor Green
