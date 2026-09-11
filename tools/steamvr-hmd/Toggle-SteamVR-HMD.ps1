#Requires -Version 7.5
<#
.SYNOPSIS
Switch SteamVR between Valve's null display and the normal headset route.
.EXAMPLE
./Toggle-SteamVR-HMD.ps1 -Mode Null -StartSteamVR
.EXAMPLE
./Toggle-SteamVR-HMD.ps1 -Mode Pimax -WhatIf
.NOTES
Close SteamVR first. Pimax mode uses normal driver discovery; start Pimax Play
and connect the headset before launching. This is a manual display switch,
not the CSX automation head-pose/measurement qualification protocol.
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Low')]
param(
    [ValidateSet('Toggle', 'Null', 'Pimax', 'Status')]
    [string]$Mode = 'Toggle',
    [switch]$StartSteamVR,
    [switch]$Pause,
    [string]$SettingsPath,
    [string]$SteamVRRoot,
    [string]$OpenVRPathsPath = $(Join-Path $env:LOCALAPPDATA 'openvr/openvrpaths.vrpath')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-SteamVRClosed {
    $running = @(Get-Process -Name vrserver, vrcompositor, vrmonitor, vrstartup, vrdashboard, vrwebhelper -ErrorAction SilentlyContinue)
    if ($running.Count) {
        $names = ($running | ForEach-Object { '{0} (PID {1})' -f $_.ProcessName, $_.Id }) -join ', '
        throw "Close SteamVR first, then run this launcher again. Running: $names"
    }
}

function Get-SteamInstallation {
    $steam = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath
    if ([string]::IsNullOrWhiteSpace($steam)) { throw 'SteamPath is missing from the Steam registry key. Specify -SettingsPath and -SteamVRRoot.' }
    return [IO.Path]::GetFullPath($steam)
}

function Get-TargetControlDirectory {
    # Share the automation controller's lock domain. Never edit through a live
    # or interrupted automation transaction, even when its settings have drifted.
    $localData = [Environment]::GetFolderPath('LocalApplicationData')
    if ([string]::IsNullOrWhiteSpace($localData)) { throw 'Cannot resolve the Windows LocalApplicationData folder.' }
    $controlRoot = Join-Path $localData 'CSX-VR-Automation/SteamVR/transactions'
    if ($env:CSX_STEAMVR_TRANSACTION_ROOT) {
        $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        $controlRoot = [IO.Path]::GetFullPath($env:CSX_STEAMVR_TRANSACTION_ROOT)
        if (-not $SettingsPath.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not ($controlRoot.TrimEnd('\') + '\').StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'CSX_STEAMVR_TRANSACTION_ROOT is test-only; settings and control paths must both be inside the OS temporary directory.'
        }
    }
    $identity = $SettingsPath.TrimEnd('\').ToLowerInvariant() + "`n" + $OpenVRPathsPath.TrimEnd('\').ToLowerInvariant()
    $hash = [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($identity))
    return Join-Path $controlRoot ([Convert]::ToHexString($hash).ToLowerInvariant())
}

function Assert-NoAutomationTransaction([string]$ControlDirectory) {
    $journalPath = Join-Path $ControlDirectory 'transaction.journal.json'
    if (-not (Test-Path -LiteralPath $journalPath)) { return }
    $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json -AsHashtable
    if ($journal -isnot [Collections.IDictionary] -or -not $journal['settingsPath'] -or
        -not [string]::Equals([IO.Path]::GetFullPath($journal['settingsPath']), $SettingsPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unrecognized SteamVR automation journal; inspect it with steamvr-null-control: $journalPath"
    }
    $released = ($journal['operation'] -eq 'restore' -and $journal['phase'] -eq 'committed') -or
        ($journal['operation'] -eq 'apply' -and $journal['phase'] -in @('rolled-back', 'recovered'))
    if (-not $released) {
        throw "SteamVR settings belong to an active or interrupted automation transaction. Restore/recover it with steamvr-null-control first. Journal: $journalPath; evidence: $($journal['evidenceDirectory'])"
    }
}

$targetLock = $null
$temporaryPath = $null
try {
    if ([string]::IsNullOrWhiteSpace($SettingsPath)) {
        $SettingsPath = Join-Path (Get-SteamInstallation) 'config/steamvr.vrsettings'
    }
    $SettingsPath = [IO.Path]::GetFullPath($SettingsPath)
    $OpenVRPathsPath = [IO.Path]::GetFullPath($OpenVRPathsPath)
    if (-not (Test-Path -LiteralPath $SettingsPath -PathType Leaf)) { throw "SteamVR settings not found: $SettingsPath" }
    if ($Mode -eq 'Status' -and $StartSteamVR) { throw '-Mode Status cannot be combined with -StartSteamVR.' }

    $startupPath = $null
    if ($StartSteamVR) {
        if ([string]::IsNullOrWhiteSpace($SteamVRRoot)) {
            # OpenVR records the active runtime, including non-default libraries.
            if (Test-Path -LiteralPath $OpenVRPathsPath -PathType Leaf) {
                $paths = Get-Content -LiteralPath $OpenVRPathsPath -Raw | ConvertFrom-Json -AsHashtable
                $runtimes = @($paths['runtime'])
                if ($runtimes.Count -eq 0 -or [string]::IsNullOrWhiteSpace([string]$runtimes[0])) { throw "No runtime in $OpenVRPathsPath. Specify -SteamVRRoot." }
                $SteamVRRoot = [string]$runtimes[0]
            }
            else {
                $SteamVRRoot = Join-Path (Get-SteamInstallation) 'steamapps/common/SteamVR'
            }
        }
        $startupPath = Join-Path ([IO.Path]::GetFullPath($SteamVRRoot)) 'bin/win64/vrstartup.exe'
        if (-not (Test-Path -LiteralPath $startupPath -PathType Leaf)) { throw "SteamVR launcher not found: $startupPath. Specify -SteamVRRoot." }
    }

    $controlDirectory = Get-TargetControlDirectory
    if ($Mode -ne 'Status') {
        Assert-SteamVRClosed
        [IO.Directory]::CreateDirectory($controlDirectory) | Out-Null
        try {
            $targetLock = [IO.File]::Open((Join-Path $controlDirectory 'target.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        }
        catch [IO.IOException] { throw 'Another SteamVR settings operation is in progress. Wait for it to finish, then retry.' }
        Assert-NoAutomationTransaction $controlDirectory
    }

    $beforeBytes = [IO.File]::ReadAllBytes($SettingsPath)
    # StreamReader also accepts existing UTF-8 BOM / UTF-16 settings files.
    $config = [IO.File]::ReadAllText($SettingsPath) | ConvertFrom-Json -AsHashtable -DateKind String
    if ($config -isnot [Collections.IDictionary]) { throw 'SteamVR settings must be a JSON object.' }
    foreach ($section in @('steamvr', 'driver_null')) {
        if (-not $config.Contains($section)) { $config[$section] = [ordered]@{} }
        elseif ($config[$section] -isnot [Collections.IDictionary]) { throw "SteamVR settings section '$section' must be a JSON object." }
    }
    $currentMode = if ($config['steamvr']['forcedDriver'] -eq 'null') { 'Null' } else { 'Pimax' }
    $targetMode = if ($Mode -eq 'Toggle') { if ($currentMode -eq 'Null') { 'Pimax' } else { 'Null' } } elseif ($Mode -eq 'Status') { $currentMode } else { $Mode }
    $changed = $false
    $backupPath = $null
    $launchRequested = $false

    if ($Mode -ne 'Status') {
        $nullMode = $targetMode -eq 'Null'
        $desired = @(
            @('steamvr', 'forcedDriver', $(if ($nullMode) { 'null' } else { '' })),
            @('steamvr', 'requireHmd', (-not $nullMode)),
            @('steamvr', 'activateMultipleDrivers', $true),
            @('driver_null', 'enable', $nullMode)
        )
        $needsWrite = $false
        foreach ($entry in $desired) {
            $actual = $config[$entry[0]][$entry[1]]
            if ($null -eq $actual -or $actual.GetType() -ne $entry[2].GetType() -or $actual -cne $entry[2]) { $needsWrite = $true }
            $config[$entry[0]][$entry[1]] = $entry[2]
        }
        if ($needsWrite -and $PSCmdlet.ShouldProcess($SettingsPath, "Set SteamVR mode to $targetMode and retain an exact backup")) {
            $json = ConvertTo-Json -InputObject $config -Depth 100 -WarningAction Stop
            $suffix = '{0}-{1}' -f [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'), [guid]::NewGuid().ToString('N')
            $temporaryPath = "$SettingsPath.$suffix.tmp"
            $backupPath = "$SettingsPath.$suffix.backup"
            [IO.File]::WriteAllText($temporaryPath, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
            $expectedHash = (Get-FileHash -LiteralPath $temporaryPath -Algorithm SHA256).Hash
            Assert-SteamVRClosed
            if ([Convert]::ToHexString([IO.File]::ReadAllBytes($SettingsPath)) -cne [Convert]::ToHexString($beforeBytes)) {
                throw 'SteamVR settings changed while preparing the switch. Retry after closing other settings editors.'
            }
            # Replace atomically, retaining the exact original bytes as a unique backup.
            [IO.File]::Replace($temporaryPath, $SettingsPath, $backupPath)
            $temporaryPath = $null
            $changed = $true
            if ((Get-FileHash -LiteralPath $SettingsPath -Algorithm SHA256).Hash -ne $expectedHash) {
                throw "SteamVR settings changed during verification. Exact original backup: $backupPath"
            }
        }
        if ($StartSteamVR -and $PSCmdlet.ShouldProcess($startupPath, "Start SteamVR in $targetMode mode")) {
            # A declined settings confirmation must never start the old mode.
            if ($needsWrite -and -not $changed) { throw 'The requested mode was not applied; SteamVR was not started.' }
            Assert-SteamVRClosed
            Start-Process -FilePath $startupPath -WorkingDirectory (Split-Path -Parent $startupPath) -WindowStyle Hidden | Out-Null
            $launchRequested = $true
        }
    }

    if ($Mode -eq 'Status') { Write-Host "Configured SteamVR mode: $currentMode" }
    elseif ($WhatIfPreference) { Write-Host "Preview: $currentMode -> $targetMode" }
    else {
        Write-Host "SteamVR mode: $targetMode"
        if ($backupPath) { Write-Host "Backup: $backupPath" }
        if ($launchRequested) { Write-Host 'SteamVR startup requested.' }
    }
    [pscustomobject]@{
        mode = $targetMode
        changed = $changed
        backupPath = $backupPath
        settingsPath = $SettingsPath
        launchRequested = $launchRequested
    }
}
catch {
    if ($Pause) { Write-Host "SteamVR mode switch failed: $($_.Exception.Message)" -ForegroundColor Red }
    throw
}
finally {
    if ($temporaryPath -and (Test-Path -LiteralPath $temporaryPath)) { Remove-Item -LiteralPath $temporaryPath -ErrorAction SilentlyContinue }
    if ($targetLock) { $targetLock.Dispose() }
    if ($Pause) { $null = Read-Host 'Press Enter to close' }
}
