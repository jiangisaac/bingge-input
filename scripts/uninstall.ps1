[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '卸载和取消系统级 TSF 注册需要管理员 PowerShell。'
}

$installDir = Join-Path $env:ProgramFiles 'TV1TSF'
$registeredTarget = $null
$inprocKey = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey(
    'CLSID\{7F3C21A4-6D0B-4A8E-9C20-11E2A7B3C501}\InprocServer32')
if ($null -ne $inprocKey) {
    try {
        $registeredTarget = [string]$inprocKey.GetValue('')
    }
    finally {
        $inprocKey.Close()
    }
}

$installedFiles = @()
if (Test-Path -LiteralPath $installDir) {
    $installedFiles = @(Get-ChildItem -LiteralPath $installDir -Filter 'TV1TSF*.dll' -File)
}
$target = $registeredTarget
if (-not $target -or -not (Test-Path -LiteralPath $target)) {
    $target = $installedFiles | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}

if ($target) {
    $regsvr32 = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
    $unregistration = Start-Process -FilePath $regsvr32 -ArgumentList '/u', '/s', ('"{0}"' -f $target) -WindowStyle Hidden -Wait -PassThru
    if ($unregistration.ExitCode -ne 0) {
        throw "取消 TSF/COM 注册失败，regsvr32 退出码 $($unregistration.ExitCode)"
    }
}

$lockedFiles = @()
foreach ($file in $installedFiles) {
    try {
        Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop
    }
    catch {
        $lockedFiles += $file.FullName
    }
}
if ($lockedFiles.Count -eq 0) {
    Remove-Item -LiteralPath $installDir -Force -ErrorAction SilentlyContinue
    Write-Host '卸载完成。' -ForegroundColor Green
}
else {
    Write-Warning "注册已取消；以下 DLL 仍被已登录的应用占用，已保留，无需为了安装新版而重启：$($lockedFiles -join '、')"
}
