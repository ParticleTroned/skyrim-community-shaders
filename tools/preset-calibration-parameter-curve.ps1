param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9]+$')][string]$Feature,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9]+$')][string]$Parameter,
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][double[]]$Values,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$RunId,
    [ValidateRange(20, 1000)][int]$Samples = 120,
    [ValidateRange(20, 1000)][int]$PollMilliseconds = 50,
    [ValidateRange(0.0, 24.0)][double]$GameHour,
    [ValidatePattern('^[A-Fa-f0-9]+$')][string]$WeatherForm,
    [ValidateRange(0.0, 1000.0)][double]$RestoreTimescale = 20.0,
    [switch]$LeaveTimeRunning,
    [switch]$UseExistingSnapshot,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$DllSha256,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{7,40}$')][string]$SourceCommit,
    [string]$OutputRoot = '',
    [ValidateRange(10, 300)][int]$PhaseTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'preset-calibration-storage.ps1')
$OutputRoot = Resolve-PresetCalibrationOutputRoot -OutputRoot $OutputRoot -Collection 'preset-automation-parameter-curves'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$controlName = 'qualityParameters'
$snapshotHeld = $false
$profilerWasEnabled = $false
$profilerStateKnown = $false
$hasGameHour = $PSBoundParameters.ContainsKey('GameHour')
$timeFrozen = $false
$runFailure = $null

function Invoke-DevBenchTool {
    param([Parameter(Mandatory = $true)][string]$Tool, [Parameter(Mandatory = $true)][hashtable]$Payload)
    $body = $Payload | ConvertTo-Json -Compress -Depth 20
    Invoke-RestMethod -Method Post -Uri ($baseUri + $Tool) -ContentType 'application/json' -Body $body -TimeoutSec 30
}

function Get-Control {
    (Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'get'; feature = $Feature; control = $controlName
    }).control
}

function Get-EffectiveValue {
    param([Parameter(Mandatory = $true)]$Control)
    $property = $Control.effectiveValue.PSObject.Properties[$Parameter]
    if (-not $property) { throw "$Feature does not expose quality parameter $Parameter" }
    [double]$property.Value
}

function Set-ParameterValue {
    param([Parameter(Mandatory = $true)][double]$Value)
    $current = Get-Control
    $definitionProperty = $current.parameterDefinitions.PSObject.Properties[$Parameter]
    if (-not $definitionProperty) { throw "$Feature does not define quality parameter $Parameter" }
    $requestValue = $Value
    if ($definitionProperty.Value.valueType -eq 'integer') {
        if ([Math]::Abs($Value - [Math]::Round($Value)) -gt 0.000001) { throw "$Feature parameter $Parameter requires an integer value" }
        $requestValue = [int][Math]::Round($Value)
    }
    $transition = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'; feature = $Feature; control = $controlName; value = @{ $Parameter = $requestValue }
    }
    if ($transition.error) { throw "$Feature parameter set failed: $($transition.error)" }
    $script:snapshotHeld = [bool]$transition.control.snapshotHeld
    $deadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        $current = Get-Control
        if ($current.ready -and [Math]::Abs((Get-EffectiveValue -Control $current) - $Value) -le 0.001) {
            return [ordered]@{ transition = $transition; settled = $current }
        }
    } while ((Get-Date) -lt $deadline)
    throw "$Feature parameter $Parameter did not settle at $Value"
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
        health = Invoke-RestMethod -Uri 'http://127.0.0.1:8921/api/health' -TimeoutSec 5
    }
}

function Get-Distribution {
    param([double[]]$Data)
    if (-not $Data -or $Data.Count -eq 0) { return $null }
    [Array]::Sort($Data)
    function At-Percentile([double]$Fraction) {
        $index = [int][Math]::Ceiling($Fraction * $Data.Count) - 1
        $Data[[Math]::Clamp($index, 0, $Data.Count - 1)]
    }
    [ordered]@{
        count = $Data.Count; minimum = $Data[0]
        average = ($Data | Measure-Object -Average).Average
        median = At-Percentile 0.50; p95 = At-Percentile 0.95
        p99 = At-Percentile 0.99; maximum = $Data[-1]
    }
}

function Collect-ProfilerPhase {
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][double]$EffectiveValue, [Parameter(Mandatory = $true)]$Scene)
    $arming = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
    $armingFrame = [uint32]$arming.status.frame_count
    $seen = [System.Collections.Generic.HashSet[uint32]]::new()
    $accepted = [System.Collections.Generic.List[object]]::new()
    $stale = 0; $duplicates = 0
    $deadline = (Get-Date).AddSeconds($PhaseTimeoutSeconds)
    while ($accepted.Count -lt $Samples -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds $PollMilliseconds
        $status = (Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }).status
        $capturedFrame = [uint32]$status.capturedFrameCount
        if ($capturedFrame -le $armingFrame) { $stale++; continue }
        if (-not $seen.Add($capturedFrame)) { $duplicates++; continue }
        if ([double]$status.resolvedTotalMs -le 0.0) { continue }
        $accepted.Add([ordered]@{
            observedAt = [DateTime]::UtcNow.ToString('o'); frameCount = [uint32]$status.frame_count
            capturedFrameCount = $capturedFrame; resolvedTotalMs = [double]$status.resolvedTotalMs
            resolvedCpuTotalMs = [double]$status.resolvedCpuTotalMs; acquiredSlots = [uint32]$status.acquiredSlots
            peakAcquiredSlots = [uint32]$status.peakAcquiredSlots; slotRefusals = [uint64]$status.slotRefusals
            timers = $status.timers
        })
    }
    if ($accepted.Count -lt $Samples) { throw "$Name collected $($accepted.Count) of $Samples resolved samples before timeout" }
    [ordered]@{
        name = $Name; effectiveValue = $EffectiveValue; scene = $Scene; armingFrame = $armingFrame
        requestedSamples = $Samples; acceptedSamples = $accepted.Count; staleResponses = $stale
        duplicateResponses = $duplicates; firstCapturedFrame = $accepted[0].capturedFrameCount
        lastCapturedFrame = $accepted[-1].capturedFrameCount
        gpu = Get-Distribution -Data ([double[]]@($accepted | ForEach-Object { $_.resolvedTotalMs }))
        cpu = Get-Distribution -Data ([double[]]@($accepted | ForEach-Object { $_.resolvedCpuTotalMs }))
        samples = $accepted
    }
}

$record = [ordered]@{
    schemaVersion = 1; runId = $RunId; feature = $Feature; control = $controlName; parameter = $Parameter
    startedAt = [DateTime]::UtcNow.ToString('o'); samplesPerPhase = $Samples; pollMilliseconds = $PollMilliseconds
    requestedValues = $Values; requestedAnchor = [ordered]@{
        gameHour = if ($hasGameHour) { $GameHour } else { $null }; weatherForm = $WeatherForm
        timeFrozen = $hasGameHour -and -not $LeaveTimeRunning; restoreTimescale = $RestoreTimescale
    }
    source = [ordered]@{
        branch = 'feat/preset-calibration-automation'; commit = $SourceCommit
        dllSha256 = $DllSha256.ToUpperInvariant(); build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    baselineControl = $null; transitions = [System.Collections.Generic.List[object]]::new()
    phases = [System.Collections.Generic.List[object]]::new()
    validity = [ordered]@{ accepted = $false; reasons = [System.Collections.Generic.List[string]]::new() }
}

try {
    $health = Invoke-RestMethod -Uri 'http://127.0.0.1:8921/api/health' -TimeoutSec 5
    if (-not $health.ok -or -not $health.vr -or $health.exe -ne 'SkyrimVR.exe') { throw 'DevBench is not attached to a healthy Skyrim VR process' }
    $baseline = Get-Control
    $record.baselineControl = $baseline
    if (-not $baseline.available -or -not $baseline.writable) { throw "$Feature qualityParameters is unavailable: $($baseline.unavailableReason)" }
    $snapshotHeld = [bool]$baseline.snapshotHeld
    if ($snapshotHeld -and -not $UseExistingSnapshot) { throw "$Feature already has an outstanding qualityParameters snapshot; pass -UseExistingSnapshot only when this run owns that deliberate preconfiguration snapshot" }
    $baselineValue = Get-EffectiveValue -Control $baseline

    if ($hasGameHour -and -not $LeaveTimeRunning) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = 'set timescale to 0' } | Out-Null
        $timeFrozen = $true; Start-Sleep -Milliseconds 250
    }

    $profilerStatus = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
    $profilerWasEnabled = [bool]$profilerStatus.status.enabled; $profilerStateKnown = $true
    if (-not $profilerWasEnabled) { Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'enable' } | Out-Null }
    Start-Sleep -Seconds 3

    $record.phases.Add((Collect-ProfilerPhase -Name 'baseline-before' -EffectiveValue $baselineValue -Scene (Set-AnchorState)))
    for ($index = 0; $index -lt $Values.Count; $index++) {
        $value = [double]$Values[$index]
        $transition = Set-ParameterValue -Value $value
        $record.transitions.Add($transition)
        $record.phases.Add((Collect-ProfilerPhase -Name ('value-{0:D2}' -f ($index + 1)) -EffectiveValue $value -Scene (Set-AnchorState)))
    }

    $restore = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{ action = 'restore'; feature = $Feature; control = $controlName }
    $record.transitions.Add($restore); $snapshotHeld = [bool]$restore.control.snapshotHeld
    if (-not $restore.restoredSnapshot) { throw "$Feature parameter restore did not use the held baseline snapshot" }
    $restored = Get-Control
    if (-not $restored.ready -or [Math]::Abs((Get-EffectiveValue -Control $restored) - $baselineValue) -gt 0.001) { throw "$Feature did not restore $Parameter to $baselineValue" }
    $record.phases.Add((Collect-ProfilerPhase -Name 'baseline-return' -EffectiveValue $baselineValue -Scene (Set-AnchorState)))
    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase retained the requested number of unique resolved post-arm profiler frames.')
    $record.validity.reasons.Add('Every scalar value reached exact effective readback before measurement.')
    $record.validity.reasons.Add('The run restored the exact in-memory baseline parameter state and did not overlap screenshot capture.')
}
catch { $runFailure = $_.Exception.Message; $record.validity.reasons.Add("Run rejected: $runFailure") }
finally {
    if ($snapshotHeld) {
        try { Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{ action = 'restoreAll' } | Out-Null } catch { Write-Warning "restoreAll failed: $_" }
    }
    if ($profilerStateKnown -and -not $profilerWasEnabled) {
        try { Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'disable' } | Out-Null } catch { Write-Warning "Profiler restore failed: $_" }
    }
    if ($timeFrozen) {
        try { Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = "set timescale to $RestoreTimescale" } | Out-Null } catch { Write-Warning "Timescale restore failed: $_" }
    }
    $record.finishedAt = [DateTime]::UtcNow.ToString('o')
}

$outputDirectory = Join-Path $OutputRoot $RunId
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$outputPath = Join-Path $outputDirectory 'parameter-curve-profiler-raw.json'
$temporaryPath = $outputPath + '.tmp'
[System.IO.File]::WriteAllText($temporaryPath, ($record | ConvertTo-Json -Depth 30), [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryPath -Destination $outputPath -Force

[ordered]@{
    runId = $RunId; feature = $Feature; parameter = $Parameter; accepted = $record.validity.accepted; artifact = $outputPath
    phases = @($record.phases | ForEach-Object { [ordered]@{ name = $_.name; effectiveValue = $_.effectiveValue; acceptedSamples = $_.acceptedSamples; gpu = $_.gpu; cpu = $_.cpu } })
} | ConvertTo-Json -Depth 10
if ($runFailure) { throw $runFailure }
