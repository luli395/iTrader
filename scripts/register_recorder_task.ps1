param(
    [string]$TaskName = "iTrader CTP MD Recorder",
    [string]$LauncherPath = "scripts/start_recorder_release.ps1",
    [string]$ConfigPath = "configs/ctp_md_recorder.ini",
    [switch]$StartNow
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-RepoPath {
    param([string]$RelativePath)

    $repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
    if ([System.IO.Path]::IsPathRooted($RelativePath)) {
        return [System.IO.Path]::GetFullPath($RelativePath)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $RelativePath))
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$launcherFullPath = Resolve-RepoPath $LauncherPath
$configFullPath = Resolve-RepoPath $ConfigPath
$managedTaskNamePath = Resolve-RepoPath "runtime/ctp_md_recorder/managed_task_name.txt"

if (-not (Test-Path -LiteralPath $launcherFullPath)) {
    throw "Recorder launcher script not found: $launcherFullPath"
}

if (-not (Test-Path -LiteralPath $configFullPath)) {
    throw "Recorder config not found: $configFullPath"
}

$action = New-ScheduledTaskAction `
    -Execute "powershell.exe" `
    -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$launcherFullPath`" -ConfigPath `"$ConfigPath`""
$trigger = New-ScheduledTaskTrigger -AtStartup
$settings = New-ScheduledTaskSettingsSet `
    -StartWhenAvailable `
    -RestartCount 999 `
    -RestartInterval (New-TimeSpan -Minutes 1) `
    -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit (New-TimeSpan -Seconds 0) `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries
$principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest

Register-ScheduledTask `
    -TaskName $TaskName `
    -Action $action `
    -Trigger $trigger `
    -Settings $settings `
    -Principal $principal `
    -Description "Managed standalone iTrader CTP market data recorder" `
    -Force | Out-Null

New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($managedTaskNamePath)) -Force | Out-Null
Set-Content -LiteralPath $managedTaskNamePath -Value $TaskName -Encoding UTF8

Write-Host "[recorder-task] registered task '$TaskName'"
Write-Host "[recorder-task] launcher=$launcherFullPath"
Write-Host "[recorder-task] config=$configFullPath"
Write-Host "[recorder-task] marker=$managedTaskNamePath"

if ($StartNow) {
    Start-ScheduledTask -TaskName $TaskName
    Write-Host "[recorder-task] start requested."
}
