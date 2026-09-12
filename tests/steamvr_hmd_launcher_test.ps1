# Run with PowerShell 7.5+: pwsh -NoProfile -File tests/steamvr_hmd_launcher_test.ps1
# All settings, journals, locks, and backups belong to temporary fixtures.
# SteamVR process enumeration is mocked; no runtime is launched or stopped.
#Requires -Version 7.5

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$launcherPath = Join-Path $PSScriptRoot '..\tools\steamvr-hmd\Toggle-SteamVR-HMD.ps1'
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('steamvr-hmd-tests-' + [guid]::NewGuid().ToString('N'))
$previousControlRoot = $env:CSX_STEAMVR_TRANSACTION_ROOT
$previousMockState = Get-Variable -Name CSXHMDLauncherTestState -Scope Global -ErrorAction SilentlyContinue
$global:CSXHMDLauncherTestState = [pscustomobject]@{ Processes = @(); Launches = @(); SettingsPath = $null }
$script:Passed = 0
$script:Failures = [Collections.Generic.List[string]]::new()

function Get-Process {
    [CmdletBinding()]
    param([string[]]$Name)
    # The launcher is a different script scope. Use one dedicated global state
    # object and restore any prior value after the suite.
    return @($global:CSXHMDLauncherTestState.Processes | Where-Object { $_.ProcessName -in $Name })
}

function Start-Process {
    [CmdletBinding()]
    param([string]$FilePath, [string]$WorkingDirectory, [string]$WindowStyle)
    $settingsAtLaunch = [IO.File]::ReadAllText($global:CSXHMDLauncherTestState.SettingsPath) | ConvertFrom-Json -AsHashtable -DateKind String
    $global:CSXHMDLauncherTestState.Launches += [pscustomobject]@{
        FilePath = $FilePath
        WorkingDirectory = $WorkingDirectory
        WindowStyle = $WindowStyle
        Settings = $settingsAtLaunch
    }
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Throws([scriptblock]$Action, [string]$ExpectedMessagePattern, [string]$Message) {
    $didThrow = $false
    try { $null = & $Action }
    catch {
        $didThrow = $true
        Assert-True ($_.Exception.Message -match $ExpectedMessagePattern) "Unexpected rejection: $($_.Exception.Message)"
    }
    Assert-True $didThrow $Message
}

function Read-Settings($Fixture) {
    return [IO.File]::ReadAllText($Fixture.settingsPath) | ConvertFrom-Json -AsHashtable -DateKind String
}

function Get-SettingsHash($Fixture) {
    return (Get-FileHash -LiteralPath $Fixture.settingsPath -Algorithm SHA256).Hash
}

function New-Fixture([string]$Text = '{}') {
    $directory = Join-Path $fixtureRoot ([guid]::NewGuid().ToString('N'))
    $null = [IO.Directory]::CreateDirectory($directory)
    $settingsPath = Join-Path $directory 'steamvr.vrsettings'
    $openVRPathsPath = Join-Path $directory 'openvrpaths.vrpath'
    [IO.File]::WriteAllText($settingsPath, $Text, [Text.UTF8Encoding]::new($false))
    $identity = [IO.Path]::GetFullPath($settingsPath).TrimEnd('\').ToLowerInvariant() + "`n" +
        [IO.Path]::GetFullPath($openVRPathsPath).TrimEnd('\').ToLowerInvariant()
    $key = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($identity))).ToLowerInvariant()
    return [pscustomobject]@{
        directory = $directory
        settingsPath = $settingsPath
        openVRPathsPath = $openVRPathsPath
        controlDirectory = Join-Path $env:CSX_STEAMVR_TRANSACTION_ROOT $key
    }
}

function Invoke-Launcher($Fixture, [string]$Mode, [hashtable]$Extra = @{}) {
    return & $launcherPath -Mode $Mode -SettingsPath $Fixture.settingsPath -OpenVRPathsPath $Fixture.openVRPathsPath @Extra
}

function Test-Case([string]$Name, [scriptblock]$Action) {
    try {
        & $Action
        $script:Passed++
        Write-Host "PASS $Name"
    }
    catch {
        $script:Failures.Add("${Name}: $($_.Exception.Message)")
        Write-Host "FAIL ${Name}: $($_.Exception.Message)"
    }
    finally {
        $global:CSXHMDLauncherTestState.Processes = @()
        $global:CSXHMDLauncherTestState.Launches = @()
        $global:CSXHMDLauncherTestState.SettingsPath = $null
    }
}

try {
    $null = [IO.Directory]::CreateDirectory($fixtureRoot)
    $env:CSX_STEAMVR_TRANSACTION_ROOT = Join-Path $fixtureRoot 'transactions'

    Test-Case 'NULL creates missing sections, exact backup, and BOM-free settings' {
        $fixture = New-Fixture "{`r`n  `"LastKnown`": {`"ActualHMDDriver`": `"pimax`"},`r`n  `"unknown`": {`"unicode`": `"雪 🥽`", `"timestamp`": `"2026-09-09T01:02:03+02:00`", `"values`": [1, true, null, {`"nested`": `"kept`"}]}`r`n}`r`n"
        $beforeHash = Get-SettingsHash $fixture
        $result = Invoke-Launcher $fixture Null
        $settings = Read-Settings $fixture
        Assert-True ($result.mode -eq 'Null' -and $result.changed) 'NULL transition was not reported.'
        Assert-True ($settings.steamvr.forcedDriver -ceq 'null') 'NULL driver was not selected.'
        Assert-True ($settings.steamvr.requireHmd -ceq $false) 'NULL must not require a physical HMD.'
        Assert-True ($settings.steamvr.activateMultipleDrivers -ceq $true) 'Multiple drivers must remain enabled.'
        Assert-True ($settings.driver_null.enable -ceq $true) 'NULL driver was not enabled.'
        Assert-True ($settings.LastKnown.ActualHMDDriver -ceq 'pimax') 'LastKnown was modified.'
        Assert-True ($settings.unknown.unicode -ceq '雪 🥽') 'Unicode value was modified.'
        Assert-True ($settings.unknown.timestamp -is [string] -and $settings.unknown.timestamp -ceq '2026-09-09T01:02:03+02:00') 'Timestamp string was modified.'
        Assert-True ($settings.unknown.values.Count -eq 4 -and $null -eq $settings.unknown.values[2] -and $settings.unknown.values[3].nested -ceq 'kept') 'Unknown nested values were modified.'
        Assert-True ((Get-FileHash -LiteralPath $result.backupPath -Algorithm SHA256).Hash -eq $beforeHash) 'Backup is not the exact preimage.'
        $bytes = [IO.File]::ReadAllBytes($fixture.settingsPath)
        Assert-True (-not ($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191)) 'Settings contain a UTF-8 BOM.'
        Assert-True (-not $result.launchRequested) 'A settings-only call requested launch.'
    }

    Test-Case 'Explicit modes are idempotent and toggle works both ways' {
        $fixture = New-Fixture
        $first = Invoke-Launcher $fixture Null
        $firstBackup = $first.backupPath
        $nullHash = Get-SettingsHash $fixture
        $nullAgain = Invoke-Launcher $fixture Null
        Assert-True (-not $nullAgain.changed -and (Get-SettingsHash $fixture) -eq $nullHash) 'Repeated NULL rewrote settings.'
        $pimax = Invoke-Launcher $fixture Toggle
        $settings = Read-Settings $fixture
        Assert-True ($pimax.mode -eq 'Pimax' -and $pimax.changed) 'Toggle from NULL did not select PIMAX.'
        Assert-True ($settings.steamvr.forcedDriver -ceq '' -and $settings.steamvr.requireHmd -ceq $true -and $settings.driver_null.enable -ceq $false) 'PIMAX settings are incorrect.'
        Assert-True ($pimax.backupPath -ne $firstBackup -and (Test-Path -LiteralPath $firstBackup)) 'New transition replaced the previous backup.'
        $pimaxHash = Get-SettingsHash $fixture
        $pimaxAgain = Invoke-Launcher $fixture Pimax
        Assert-True (-not $pimaxAgain.changed -and (Get-SettingsHash $fixture) -eq $pimaxHash) 'Repeated PIMAX rewrote settings.'
        $backToNull = Invoke-Launcher $fixture Toggle
        Assert-True ($backToNull.mode -eq 'Null' -and $backToNull.changed) 'Toggle from PIMAX did not select NULL.'
    }

    Test-Case 'Status and WhatIf preserve settings without creating backups' {
        $fixture = New-Fixture '{"steamvr":{"forcedDriver":"null"},"LastKnown":{"ActualHMDDriver":"null"}}'
        $beforeHash = Get-SettingsHash $fixture
        $status = Invoke-Launcher $fixture Status
        Assert-True ($status.mode -eq 'Null' -and -not $status.changed) 'Status changed settings or misreported mode.'
        $null = Invoke-Launcher $fixture Pimax @{ WhatIf = $true }
        Assert-True ((Get-SettingsHash $fixture) -eq $beforeHash) 'Status or WhatIf modified settings.'
        Assert-True (@(Get-ChildItem -LiteralPath $fixture.directory -File).Count -eq 1) 'Status or WhatIf created a backup or temporary settings file.'
    }

    Test-Case 'Explicit NULL repairs partially configured NULL settings' {
        $fixture = New-Fixture '{"steamvr":{"forcedDriver":"null","requireHmd":false,"activateMultipleDrivers":false},"driver_null":{"enable":true}}'
        $result = Invoke-Launcher $fixture Null
        $settings = Read-Settings $fixture
        Assert-True ($result.changed -and $settings.steamvr.activateMultipleDrivers -ceq $true) 'Partial NULL configuration was mistaken for an idempotent selection.'
    }

    Test-Case 'Startup follows the requested settings; WhatIf never launches' {
        $fixture = New-Fixture
        $runtimeRoot = Join-Path $fixture.directory 'SteamVR'
        $runtimeBin = Join-Path $runtimeRoot 'bin/win64'
        $null = [IO.Directory]::CreateDirectory($runtimeBin)
        $runtimeExe = Join-Path $runtimeBin 'vrstartup.exe'
        [IO.File]::WriteAllText($runtimeExe, 'Fixture only. Start-Process is mocked.')
        $global:CSXHMDLauncherTestState.SettingsPath = $fixture.settingsPath
        $arguments = @{ StartSteamVR = $true; SteamVRRoot = $runtimeRoot }
        $nullStart = Invoke-Launcher $fixture Null $arguments
        $launches = $global:CSXHMDLauncherTestState.Launches
        Assert-True ($nullStart.changed -and $nullStart.launchRequested -and $launches.Count -eq 1) 'NULL startup was not requested exactly once.'
        Assert-True ($launches[0].Settings.steamvr.forcedDriver -ceq 'null' -and $launches[0].Settings.driver_null.enable -ceq $true) 'SteamVR launch preceded the NULL settings write.'
        Assert-True ($launches[0].FilePath -eq $runtimeExe -and $launches[0].WindowStyle -eq 'Hidden') 'Unexpected startup executable or window mode.'
        $pimaxStart = Invoke-Launcher $fixture Pimax $arguments
        $launches = $global:CSXHMDLauncherTestState.Launches
        Assert-True ($pimaxStart.changed -and $pimaxStart.launchRequested -and $launches.Count -eq 2) 'PIMAX startup was not requested exactly once.'
        Assert-True ($launches[1].Settings.steamvr.forcedDriver -ceq '' -and $launches[1].Settings.driver_null.enable -ceq $false) 'SteamVR launch preceded the PIMAX settings write.'
        $beforeHash = Get-SettingsHash $fixture
        $beforeFiles = @(Get-ChildItem -LiteralPath $fixture.directory -File).Count
        $preview = Invoke-Launcher $fixture Null @{ StartSteamVR = $true; SteamVRRoot = $runtimeRoot; WhatIf = $true }
        Assert-True (-not $preview.changed -and -not $preview.launchRequested -and $global:CSXHMDLauncherTestState.Launches.Count -eq 2) 'WhatIf wrote settings or launched SteamVR.'
        Assert-True ((Get-SettingsHash $fixture) -eq $beforeHash -and @(Get-ChildItem -LiteralPath $fixture.directory -File).Count -eq $beforeFiles) 'Startup preview modified settings or created a backup.'
    }

    Test-Case 'Malformed JSON and invalid section types are rejected unchanged' {
        foreach ($invalid in @(
            @{ text = '{"steamvr":'; pattern = 'Conversion from JSON failed' },
            @{ text = '{"steamvr":17}'; pattern = "section 'steamvr' must be a JSON object" },
            @{ text = '{"driver_null":[1,2]}'; pattern = "section 'driver_null' must be a JSON object" }
        )) {
            $fixture = New-Fixture $invalid.text
            $beforeHash = Get-SettingsHash $fixture
            Assert-Throws { Invoke-Launcher $fixture Null } $invalid.pattern 'Invalid settings were accepted.'
            Assert-True ((Get-SettingsHash $fixture) -eq $beforeHash) 'Invalid settings were overwritten.'
            Assert-True (@(Get-ChildItem -LiteralPath $fixture.directory -File).Count -eq 1) 'Rejected settings created a backup.'
        }
    }

    Test-Case 'Running SteamVR blocks configuration changes' {
        $fixture = New-Fixture
        $beforeHash = Get-SettingsHash $fixture
        $global:CSXHMDLauncherTestState.Processes = @([pscustomobject]@{ ProcessName = 'vrserver'; Id = 12345; Path = 'C:\Fixture\SteamVR\bin\win64\vrserver.exe' })
        Assert-Throws { Invoke-Launcher $fixture Null } 'Close SteamVR first.*vrserver.*12345' 'Running SteamVR did not block mutation.'
        Assert-True ((Get-SettingsHash $fixture) -eq $beforeHash) 'Settings changed while SteamVR was running.'
    }

    Test-Case 'Active and pending automation transactions block mutation' {
        foreach ($transaction in @(
            @{ operation = 'apply'; phase = 'committed' },
            @{ operation = 'apply'; phase = 'prepared' },
            @{ operation = 'restore'; phase = 'rolled-back' },
            @{ operation = 'restore'; phase = 'recovered' }
        )) {
            $fixture = New-Fixture
            $beforeHash = Get-SettingsHash $fixture
            $null = [IO.Directory]::CreateDirectory($fixture.controlDirectory)
            $transaction.settingsPath = $fixture.settingsPath
            $transaction.openVRPathsPath = $fixture.openVRPathsPath
            $transaction.evidenceDirectory = Join-Path $fixture.directory 'prior-evidence'
            $journalPath = Join-Path $fixture.controlDirectory 'transaction.journal.json'
            [IO.File]::WriteAllText($journalPath, ($transaction | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
            Assert-Throws { Invoke-Launcher $fixture Pimax } 'belong to an active or interrupted automation transaction' 'Owned automation transaction was overwritten.'
            Assert-True ((Get-SettingsHash $fixture) -eq $beforeHash) 'Automation journal refusal changed settings.'
        }
    }

    Test-Case 'Concurrent target lock blocks mutation' {
        $fixture = New-Fixture
        $beforeHash = Get-SettingsHash $fixture
        $null = [IO.Directory]::CreateDirectory($fixture.controlDirectory)
        $lease = [IO.File]::Open((Join-Path $fixture.controlDirectory 'target.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        try { Assert-Throws { Invoke-Launcher $fixture Null } 'Another SteamVR settings operation is in progress' 'Concurrent target lock did not block mutation.' }
        finally { $lease.Dispose() }
        Assert-True ((Get-SettingsHash $fixture) -eq $beforeHash) 'Lock refusal changed settings.'
    }
}
finally {
    $env:CSX_STEAMVR_TRANSACTION_ROOT = $previousControlRoot
    if ($previousMockState) { Set-Variable -Name CSXHMDLauncherTestState -Scope Global -Value $previousMockState.Value }
    else { Remove-Variable -Name CSXHMDLauncherTestState -Scope Global }
    $resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
    $resolvedTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedFixtureRoot.StartsWith($resolvedTempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixtureRoot) -notlike 'steamvr-hmd-tests-*') {
        throw "Refusing to remove an unproven fixture path: $resolvedFixtureRoot"
    }
    if (Test-Path -LiteralPath $resolvedFixtureRoot) { Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force }
}

Write-Host "$($script:Passed) cases passed; $($script:Failures.Count) failed."
if ($script:Failures.Count -gt 0) { throw ($script:Failures -join "`n") }
