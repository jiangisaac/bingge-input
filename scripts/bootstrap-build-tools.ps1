[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请在管理员 PowerShell 中运行本脚本。该脚本会安装 Visual Studio 2022 Build Tools、MSVC、Windows 10 SDK 和 CMake。'
}

$downloadDir = Join-Path $env:TEMP 'TV1TSF-build-tools'
New-Item -ItemType Directory -Force $downloadDir | Out-Null
$installer = Join-Path $downloadDir 'vs_BuildTools.exe'
$url = 'https://aka.ms/vs/17/release/vs_BuildTools.exe'

Write-Host '下载 Microsoft Visual Studio Build Tools 官方引导程序...'
& curl.exe -L --fail --output $installer $url
if ($LASTEXITCODE -ne 0) {
    throw "下载失败，退出码 $LASTEXITCODE；URL：$url"
}

$arguments = @(
    '--quiet', '--wait', '--norestart', '--installWhileDownloading', '--includeOnlyRequested',
    '--add', 'Microsoft.VisualStudio.Workload.VCTools',
    '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    '--add', 'Microsoft.VisualStudio.Component.Windows10SDK.19041',
    '--add', 'Microsoft.VisualStudio.Component.VC.CMake.Project'
)
Write-Host '开始安装原生构建依赖；安装器退出前请勿关闭窗口...'
& $installer @arguments
$exitCode = $LASTEXITCODE
if ($exitCode -notin @(0, 3010)) {
    throw "Build Tools 安装失败或未完成，退出码 $exitCode。请检查 $env:TEMP 下以 dd_ 开头的 Visual Studio 安装日志。"
}

Write-Host '构建依赖安装完成。请重新打开 PowerShell，再运行 scripts\build.ps1。' -ForegroundColor Green
if ($exitCode -eq 3010) {
    Write-Warning '安装器要求重启系统。'
}
