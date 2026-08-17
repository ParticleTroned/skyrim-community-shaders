param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$FeatureA,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ControlA = 'performanceActive',
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$FeatureB,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ControlB = 'performanceActive',
    [ValidateSet('A', 'B')][string]$DisableFirst = 'B',
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$RunId,
    [ValidateRange(20, 1000)][int]$Samples = 120,
    [ValidateRange(20, 1000)][int]$PollMilliseconds = 50,
    [ValidateRange(10, 300)][int]$PhaseTimeoutSeconds = 90,
    [ValidateRange(0.0, 24.0)][double]$GameHour,
    [ValidatePattern('^[A-Fa-f0-9]+$')][string]$WeatherForm,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$DllSha256,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{7,40}$')][string]$SourceCommit,
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'preset-calibration-storage.ps1')
$OutputRoot = Resolve-PresetCalibrationOutputRoot -OutputRoot $OutputRoot -Collection 'preset-automation-interactions'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$hasGameHour = $PSBoundParameters.ContainsKey('GameHour')
$profilerWasEnabled = $false
$profilerStateKnown = $false
$mutationStarted = $false

function Invoke-DevBenchTool {
    param([Parameter(Mandatory = $true)][string]$Tool, [Parameter(Mandatory = $true)][hashtable]$Payload)
    $body = $Payload | ConvertTo-Json -Compress -Depth 20
    Invoke-RestMethod -Method Post -Uri ($baseUri + $Tool) -ContentType 'application/json' -Body $body -TimeoutSec 30
}

function Get-Control {
    param([Parameter(Mandatory = $true)][string]$Feature, [Parameter(Mandatory = $true)][string]$Control)
    $result = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'get'; feature = $Feature; control = $Control
    }
    if ($result.control.error) { throw "Unknown control $Feature/$Control`: $($result.control.error)" }
    $result.control
}

function Get-DeclaredSettleSeconds {
    param([Parameter(Mandatory = $true)]$Control, [Parameter(Mandatory = $true)][bool]$Expected)
    if ($Expected -and $null -ne $Control.settle.whenEnabledSeconds) {
        return [double]$Control.settle.whenEnabledSeconds
    }
    if (-not $Expected -and $null -ne $Control.settle.whenDisabledSeconds) {
        return [double]$Control.settle.whenDisabledSeconds
    }
    if ($null -ne $Control.settle.minimumSeconds) { return [double]$Control.settle.minimumSeconds }
    if ($null -ne $Control.settle.minimumFrames) {
        return [Math]::Max(0.25, [double]$Control.settle.minimumFrames / 30.0)
    }
    0.25
}

function Wait-ControlSettle {
    param(
        [Parameter(Mandatory = $true)][string]$Feature,
        [Parameter(Mandatory = $true)][string]$Control,
        [Parameter(Mandatory = $true)][bool]$Expected,
        [Parameter(Mandatory = $true)]$TransitionControl
    )
    $minimumSeconds = Get-DeclaredSettleSeconds -Control $TransitionControl -Expected $Expected
    if ($minimumSeconds -gt 0) {
        Start-Sleep -Milliseconds ([int][Math]::Ceiling($minimumSeconds * 1000.0))
    }
    $deadline = (Get-Date).AddSeconds([Math]::Max(15.0, $minimumSeconds + 10.0))
    do {
        $current = Get-Control -Feature $Feature -Control $Control
        $hasReady = $null -ne $current.ready
        if ([bool]$current.effectiveValue -eq $Expected -and (-not $hasReady -or [bool]$current.ready)) {
            return $current
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "$Feature/$Control did not settle at $Expected"
}

function Set-ControlValue {
    param(
        [Parameter(Mandatory = $true)][string]$Feature,
        [Parameter(Mandatory = $true)][string]$Control,
        [Parameter(Mandatory = $true)][bool]$Value
    )
    $transition = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'; feature = $Feature; control = $Control; value = $Value
    }
    Wait-ControlSettle -Feature $Feature -Control $Control -Expected $Value -TransitionControl $transition.control | Out-Null
    $transition
}

function Set-AnchorState {
    if ($hasGameHour) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = "set gamehour to $GameHour" } | Out-Null
        Start-Sleep -Milliseconds 1500
    }
    if ($WeatherForm) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = "fw $WeatherForm" } | Out-Null
        Start-Sleep -Seconds 3
    }
    [ordered]@{
        scene = Invoke-DevBenchTool -Tool 'inspect' -Payload @{ kind = 'scene' }
        camera = Invoke-DevBenchTool -Tool 'camera' -Payload @{ action = 'get' }
    }
}

function Get-Distribution {
    param([double[]]$Values)
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    [Array]::Sort($Values)
    function At-Percentile([double]$Fraction) {
        $index = [int][Math]::Ceiling($Fraction * $Values.Count) - 1
        $Values[[Math]::Clamp($index, 0, $Values.Count - 1)]
    }
    [ordered]@{
        count = $Values.Count; minimum = $Values[0]
        average = ($Values | Measure-Object -Average).Average
        median = At-Percentile 0.50; p95 = At-Percentile 0.95
        p99 = At-Percentile 0.99; maximum = $Values[-1]
    }
}

function Collect-ProfilerPhase {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$A,
        [Parameter(Mandatory = $true)][bool]$B
    )
    $scene = Set-AnchorState
    $arming = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
    $armingFrame = [uint32]$arming.status.frame_count
    $seen = [System.Collections.Generic.HashSet[uint32]]::new()
    $accepted = [System.Collections.Generic.List[object]]::new()
    $stale = 0
    $duplicates = 0
    $deadline = (Get-Date).AddSeconds($PhaseTimeoutSeconds)
    while ($accepted.Count -lt $Samples -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds $PollMilliseconds
        $status = (Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }).status
        $capturedFrame = [uint32]$status.capturedFrameCount
        if ($capturedFrame -le $armingFrame) { $stale++; continue }
        if (-not $seen.Add($capturedFrame)) { $duplicates++; continue }
        if ([double]$status.resolvedTotalMs -le 0.0) { continue }
        $accepted.Add([ordered]@{
            observedAt = [DateTime]::UtcNow.ToString('o')
            frameCount = [uint32]$status.frame_count
            capturedFrameCount = $capturedFrame
            resolvedTotalMs = [double]$status.resolvedTotalMs
            resolvedCpuTotalMs = [double]$status.resolvedCpuTotalMs
            acquiredSlots = [uint32]$status.acquiredSlots
            peakAcquiredSlots = [uint32]$status.peakAcquiredSlots
            slotRefusals = [uint64]$status.slotRefusals
            timers = $status.timers
        })
    }
    if ($accepted.Count -lt $Samples) { throw "$Name collected $($accepted.Count) of $Samples samples" }
    $gpu = [double[]]@($accepted | ForEach-Object { $_.resolvedTotalMs })
    $cpu = [double[]]@($accepted | ForEach-Object { $_.resolvedCpuTotalMs })
    [ordered]@{
        name = $Name; state = [ordered]@{ A = $A; B = $B }; scene = $scene
        armingFrame = $armingFrame; requestedSamples = $Samples; acceptedSamples = $accepted.Count
        staleResponses = $stale; duplicateResponses = $duplicates
        firstCapturedFrame = $accepted[0].capturedFrameCount
        lastCapturedFrame = $accepted[-1].capturedFrameCount
        gpu = Get-Distribution -Values $gpu; cpu = Get-Distribution -Values $cpu
        samples = $accepted
    }
}

$record = [ordered]@{
    schemaVersion = 1; runId = $RunId; startedAt = [DateTime]::UtcNow.ToString('o')
    factors = [ordered]@{
        A = [ordered]@{ feature = $FeatureA; control = $ControlA }
        B = [ordered]@{ feature = $FeatureB; control = $ControlB }
    }
    disableFirst = $DisableFirst; samplesPerPhase = $Samples; pollMilliseconds = $PollMilliseconds
    requestedAnchor = [ordered]@{ gameHour = if ($hasGameHour) { $GameHour } else { $null }; weatherForm = $WeatherForm }
    source = [ordered]@{
        branch = 'feat/preset-calibration-automation'; commit = $SourceCommit
        dllSha256 = $DllSha256; build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    baselineControls = $null
    transitions = [System.Collections.Generic.List[object]]::new()
    phases = [System.Collections.Generic.List[object]]::new()
    validity = [ordered]@{ accepted = $false; reasons = [System.Collections.Generic.List[string]]::new() }
}

try {
    $health = Invoke-RestMethod -Uri 'http://127.0.0.1:8921/api/health' -TimeoutSec 5
    if (-not $health.ok -or -not $health.vr -or $health.exe -ne 'SkyrimVR.exe') {
        throw 'DevBench is not attached to a healthy Skyrim VR process'
    }
    $baselineA = Get-Control -Feature $FeatureA -Control $ControlA
    $baselineB = Get-Control -Feature $FeatureB -Control $ControlB
    $record.baselineControls = [ordered]@{ A = $baselineA; B = $baselineB }
    foreach ($entry in @($baselineA, $baselineB)) {
        if (-not $entry.available -or -not $entry.writable -or -not [bool]$entry.effectiveValue) {
            throw 'Both factorial controls must begin available, writable, and enabled'
        }
        if ($null -ne $entry.snapshotHeld -and [bool]$entry.snapshotHeld) {
            throw 'A factorial control already holds an automation snapshot'
        }
    }

    $profilerStatus = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
    $profilerWasEnabled = [bool]$profilerStatus.status.enabled
    $profilerStateKnown = $true
    if (-not $profilerWasEnabled) {
        Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'enable' } | Out-Null
    }
    Start-Sleep -Seconds 3

    $record.phases.Add((Collect-ProfilerPhase -Name 'state-11-before' -A $true -B $true))
    $mutationStarted = $true
    if ($DisableFirst -eq 'B') {
        $record.transitions.Add((Set-ControlValue -Feature $FeatureB -Control $ControlB -Value $false))
        $record.phases.Add((Collect-ProfilerPhase -Name 'state-10' -A $true -B $false))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureA -Control $ControlA -Value $false))
        $record.phases.Add((Collect-ProfilerPhase -Name 'state-00' -A $false -B $false))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureB -Control $ControlB -Value $true))
        $record.phases.Add((Collect-ProfilerPhase -Name 'state-01' -A $false -B $true))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureA -Control $ControlA -Value $true))
    }
    else {
        $record.transitions.Add((Set-ControlValue -Feature $FeatureA -Control $ControlA -Value $false))
        $record.phases.Add((Collect-ProfilerPhase -Name 'state-01' -A $false -B $true))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureB -Control $ControlB -Value $false))
        $record.phases.Add((Collect-ProfilerPhase -Name 'state-00' -A $false -B $false))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureA -Control $ControlA -Value $true))
        $record.phases.Add((Collect-ProfilerPhase -Name 'state-10' -A $true -B $false))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureB -Control $ControlB -Value $true))
    }
    $mutationStarted = $false
    $record.phases.Add((Collect-ProfilerPhase -Name 'state-11-return' -A $true -B $true))
    $record.validity.accepted = $true
    $record.validity.reasons.Add('All five phases retained the requested unique resolved profiler samples.')
    $record.validity.reasons.Add('Both factors used typed reversible controls and returned to exact enabled readback.')
    $record.validity.reasons.Add('The requested time and weather anchor was reapplied before every phase.')
    $record.validity.reasons.Add('No screenshot capture overlapped the timing pass.')
}
finally {
    if ($mutationStarted) {
        try { Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{ action = 'restoreAll' } | Out-Null }
        catch { Write-Warning "restoreAll failed: $_" }
    }
    if ($profilerStateKnown -and -not $profilerWasEnabled) {
        try { Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'disable' } | Out-Null }
        catch { Write-Warning "Profiler restore failed: $_" }
    }
    $record.finishedAt = [DateTime]::UtcNow.ToString('o')
}

$outputDirectory = Join-Path $OutputRoot $RunId
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$outputPath = Join-Path $outputDirectory 'factorial-profiler-raw.json'
$temporaryPath = $outputPath + '.tmp'
[System.IO.File]::WriteAllText($temporaryPath, ($record | ConvertTo-Json -Depth 30), [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryPath -Destination $outputPath -Force

[ordered]@{
    runId = $RunId; accepted = $record.validity.accepted; artifact = $outputPath
    phases = @($record.phases | ForEach-Object {
        [ordered]@{ name = $_.name; state = $_.state; acceptedSamples = $_.acceptedSamples; gpu = $_.gpu; cpu = $_.cpu }
    })
} | ConvertTo-Json -Depth 10
