param(
    [string]$ConfigPath = "configs/ctp_md_recorder.ini",
    [switch]$ValidateOnly
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

function Get-DotEnvMap {
    param([string]$Path)

    $values = @{}
    if (-not (Test-Path -LiteralPath $Path)) {
        return $values
    }

    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith("#") -or $trimmed.StartsWith(";")) {
            continue
        }

        $candidate = $trimmed
        if ($candidate.StartsWith("export ")) {
            $candidate = $candidate.Substring(7).Trim()
        }

        $delimiter = $candidate.IndexOf("=")
        if ($delimiter -lt 0) {
            continue
        }

        $key = $candidate.Substring(0, $delimiter).Trim()
        $value = $candidate.Substring($delimiter + 1).Trim()
        if ($value.Length -ge 2) {
            if (($value.StartsWith('"') -and $value.EndsWith('"')) -or ($value.StartsWith("'") -and $value.EndsWith("'"))) {
                $value = $value.Substring(1, $value.Length - 2)
            }
        }

        if (-not [string]::IsNullOrWhiteSpace($key)) {
            $values[$key] = $value
        }
    }

    return $values
}

function Get-ResolvedConfigValue {
    param(
        [hashtable]$DotEnv,
        [string]$Key
    )

    $processValue = [Environment]::GetEnvironmentVariable($Key)
    if (-not [string]::IsNullOrWhiteSpace($processValue)) {
        return $processValue
    }

    if ($DotEnv.ContainsKey($Key) -and -not [string]::IsNullOrWhiteSpace([string]$DotEnv[$Key])) {
        return [string]$DotEnv[$Key]
    }

    return $null
}

function Parse-IniFile {
    param([string]$Path)

    $sections = @{}
    $currentSection = $null

    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith("#") -or $trimmed.StartsWith(";")) {
            continue
        }

        if ($trimmed.StartsWith("[") -and $trimmed.EndsWith("]")) {
            $currentSection = $trimmed.Substring(1, $trimmed.Length - 2).Trim()
            if (-not $sections.ContainsKey($currentSection)) {
                $sections[$currentSection] = @{}
            }
            continue
        }

        if ($null -eq $currentSection) {
            continue
        }

        $delimiter = $trimmed.IndexOf("=")
        if ($delimiter -lt 0) {
            continue
        }

        $key = $trimmed.Substring(0, $delimiter).Trim()
        $value = $trimmed.Substring($delimiter + 1).Trim()
        if (-not [string]::IsNullOrWhiteSpace($key)) {
            $sections[$currentSection][$key] = $value
        }
    }

    return $sections
}

function Test-ConfiguredSecretValue {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $false
    }

    return $Value -notmatch '^\$\{.+\}$|^(PLEASE_SET_|CHANGEME|TODO)'
}

function Get-ExistingRecorderProcess {
    param(
        [string]$ExePath,
        [string]$ConfigPath
    )

    $normalizedExePath = [System.IO.Path]::GetFullPath($ExePath).ToLowerInvariant()
    $normalizedConfigPath = [System.IO.Path]::GetFullPath($ConfigPath).ToLowerInvariant()

    return Get-CimInstance Win32_Process |
        Where-Object {
            if ($_.Name -ne 'itrader_ctp_md_recorder.exe') {
                return $false
            }

            $processExePath = [string]$_.ExecutablePath
            if ([string]::IsNullOrWhiteSpace($processExePath)) {
                return $false
            }

            $normalizedProcessExePath = [System.IO.Path]::GetFullPath($processExePath).ToLowerInvariant()
            if ($normalizedProcessExePath -ne $normalizedExePath) {
                return $false
            }

            $commandLine = [string]$_.CommandLine
            if ([string]::IsNullOrWhiteSpace($commandLine)) {
                return $false
            }

            return $commandLine.ToLowerInvariant().Contains($normalizedConfigPath)
        } |
        Select-Object -First 1
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$exePath = Resolve-RepoPath "build/Release/itrader_ctp_md_recorder.exe"
$configFullPath = Resolve-RepoPath $ConfigPath
$dotEnvPath = Resolve-RepoPath ".env"
$dotEnv = Get-DotEnvMap -Path $dotEnvPath

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Release recorder executable not found: $exePath"
}

if (-not (Test-Path -LiteralPath $configFullPath)) {
    throw "Recorder config not found: $configFullPath"
}

$ini = Parse-IniFile -Path $configFullPath
$accountSectionName = $ini.Keys | Where-Object { $_ -like 'account.*' } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($accountSectionName)) {
    throw "Recorder config must contain an [account.*] section: $configFullPath"
}

$accountSection = $ini[$accountSectionName]

$requiredKeys = @(
    @{
        EnvKey = "ITRADER_RECORDER_BROKER_ID"
        ConfigKeys = @("md_broker_id", "broker_id")
    },
    @{
        EnvKey = "ITRADER_RECORDER_USER_ID"
        ConfigKeys = @("md_user_id", "user_id")
    },
    @{
        EnvKey = "ITRADER_RECORDER_PASSWORD"
        ConfigKeys = @("md_password", "password")
    }
)

$missingKeys = @()
foreach ($requirement in $requiredKeys) {
    $configValue = $null
    foreach ($configKey in $requirement.ConfigKeys) {
        if ($accountSection.ContainsKey($configKey)) {
            $configValue = [string]$accountSection[$configKey]
            if (-not [string]::IsNullOrWhiteSpace($configValue)) {
                break
            }
        }
    }

    if (Test-ConfiguredSecretValue -Value $configValue) {
        continue
    }

    $resolvedValue = Get-ResolvedConfigValue -DotEnv $dotEnv -Key $requirement.EnvKey
    if (-not (Test-ConfiguredSecretValue -Value $resolvedValue)) {
        $missingKeys += $requirement.EnvKey
    }
}

Write-Host "[recorder-launcher] repo_root=$repoRoot"
Write-Host "[recorder-launcher] exe=$exePath"
Write-Host "[recorder-launcher] config=$configFullPath"

if ($missingKeys.Count -gt 0) {
    throw "Recorder prerequisites missing in process env or .env: $($missingKeys -join ', ')"
}

Write-Host "[recorder-launcher] prerequisite check passed."

if ($ValidateOnly) {
    Write-Host "[recorder-launcher] validation only mode; recorder not started."
    exit 0
}

$existingProcess = Get-ExistingRecorderProcess -ExePath $exePath -ConfigPath $configFullPath
if ($null -ne $existingProcess) {
    Write-Host "[recorder-launcher] recorder is already running; pid=$($existingProcess.ProcessId)"
    exit 0
}

Push-Location $repoRoot
try {
    & $exePath --config $configFullPath
} finally {
    Pop-Location
}
