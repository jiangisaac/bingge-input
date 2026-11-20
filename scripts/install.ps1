[CmdletBinding()]
param(
    [string]$DllPath,
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
if (-not $DllPath) {
    $packagedDll = Join-Path $PSScriptRoot 'TV1TSF.dll'
    $DllPath = if (Test-Path $packagedDll) {
        $packagedDll
    }
    else {
        Join-Path $root 'build\bin\Release\TV1TSF.dll'
    }
}
$DllPath = (Resolve-Path $DllPath).Path
if (-not $Version) {
    $fileVersion = (Get-Item -LiteralPath $DllPath).VersionInfo.FileVersion
    if ([string]::IsNullOrWhiteSpace($fileVersion)) {
        throw "无法从 $DllPath 读取文件版本，请显式指定 -Version。"
    }
    $major = $fileVersion.Split('.')[0]
    if ($major -notmatch '^\d+$') {
        throw "DLL 文件版本 $fileVersion 无法推导安装版本，请显式指定 -Version。"
    }
    $Version = "v$major"
}
Write-Verbose "安装版本：$Version"

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '安装和系统级 TSF 注册需要管理员 PowerShell。'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'TV1 仅提供 Windows 10/11 x64 原型。'
}

$installDir = Join-Path $env:ProgramFiles 'TV1TSF'
$target = Join-Path $installDir "TV1TSF.$Version.dll"
New-Item -ItemType Directory -Force $installDir | Out-Null

$shouldCopy = $true
if (Test-Path -LiteralPath $target) {
    $sourceHash = (Get-FileHash -LiteralPath $DllPath -Algorithm SHA256).Hash
    $targetHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    $shouldCopy = $sourceHash -ne $targetHash
}
if ($shouldCopy) {
    try {
        Copy-Item -LiteralPath $DllPath -Destination $target -Force -ErrorAction Stop
    }
    catch {
        throw "无法写入 $target。新版 DLL 必须使用新的版本名，例如：.\install.ps1 -Version v14。详细信息：$($_.Exception.Message)"
    }
}

$regsvr32 = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
$registration = Start-Process -FilePath $regsvr32 -ArgumentList '/s', ('"{0}"' -f $target) -WindowStyle Hidden -Wait -PassThru
if ($registration.ExitCode -ne 0) {
    throw "TSF/COM 注册失败，regsvr32 退出码 $($registration.ExitCode)"
}

$lockedOldFiles = @()
Get-ChildItem -LiteralPath $installDir -Filter 'TV1TSF*.dll' -File |
    Where-Object { $_.FullName -ne $target } |
    ForEach-Object {
        $oldFile = $_
        try {
            Remove-Item -LiteralPath $oldFile.FullName -Force -ErrorAction Stop
        }
        catch {
            $lockedOldFiles += $oldFile.Name
        }
    }

Write-Host "安装完成：$target" -ForegroundColor Green
Write-Host '新版已注册，无需先卸载或重启 Windows。请关闭并重新打开测试应用，使其加载新版；已打开的应用不会强制切换。'
if ($lockedOldFiles.Count -gt 0) {
    Write-Host "旧版本仍被占用，已保留：$($lockedOldFiles -join '、')"
}
Write-Host '首次使用时，请在 Windows 设置的“时间和语言 > 语言和区域 > 中文(简体，中国) > 语言选项 > 键盘”中添加“炳哥输入”。'
Write-Host '切换到此输入法后，在记事本输入 wen 验证拼音，按 F8 后输入 wq 验证五笔86，按 Ctrl+. 切换全半角标点。'
