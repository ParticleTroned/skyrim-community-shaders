param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Skylighting', 'ScreenSpaceShadows', 'Wetterness')]
    [string]$Feature,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$RunId,

    [ValidateSet('Performance', 'Balanced', 'Quality')]
    [string[]]$ProfileOrder = @('Performance', 'Balanced', 'Quality'),

    [ValidateRange(20, 1000)]
    [int]$Samples = 120,

    [ValidateRange(20, 1000)]
    [int]$PollMilliseconds = 50,

    [ValidateRange(0.0, 24.0)]
    [double]$GameHour,

    [ValidatePattern('^[A-Fa-f0-9]+$')]
    [string]$WeatherForm,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$DllSha256,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Fa-f0-9]{7,40}$')]
    [string]$SourceCommit,

    [string]$OutputRoot = '',

    [ValidateRange(10, 300)]
    [int]$PhaseTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'preset-calibration-storage.ps1')
$OutputRoot = Resolve-PresetCalibrationOutputRoot -OutputRoot $OutputRoot -Collection 'preset-automation-curves'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$controlName = 'qualityProfile'
$snapshotHeld = $false
$profilerWasEnabled = $false
$profilerStateKnown = $false
$hasGameHour = $PSBoundParameters.ContainsKey('GameHour')
$runFailure = $null

function Invoke-DevBenchTool {
    param(
        [Parameter(Mandatory = $true)][string]$Tool,
        [Parameter(Mandatory = $true)][hashtable]$Payload
    )
    $body = $Payload | ConvertTo-Json -Compress -Depth 20
    Invoke-RestMethod -Method Post -Uri ($baseUri + $Tool) -ContentType 'application/json' -Body $body -TimeoutSec 30
}

function Get-SceneSnapshot {
    [ordered]@{
        scene = Invoke-DevBenchTool -Tool 'inspect' -Payload @{ kind = 'scene' }
        camera = Invoke-DevBenchTool -Tool 'camera' -Payload @{ action = 'get' }
        health = Invoke-RestMethod -Uri 'http://127.0.0.1:8921/api/health' -TimeoutSec 5
    }
}

function Set-AnchorState {
    if ($hasGameHour) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{
            action = 'exec'
            command = "set gamehour to $GameHour"
        } | Out-Null
        Start-Sleep -Milliseconds 1500
    }
    if ($WeatherForm) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{
            action = 'exec'
            command = "fw $WeatherForm"
        } | Out-Null
        Start-Sleep -Seconds 3
    }
    Get-SceneSnapshot
}

function Get-Control {
    (Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'get'
        feature = $Feature
        control = $controlName
    }).control
}

function Wait-ControlSettle {
    param(
        [Parameter(Mandatory = $true)][double]$MinimumSeconds,
        [Parameter(Mandatory = $true)][string]$ExpectedProfile
    )
    if ($MinimumSeconds -gt 0) {
        Start-Sleep -Milliseconds ([int][Math]::Ceiling($MinimumSeconds * 1000.0))
    }
    $deadline = (Get-Date).AddSeconds([Math]::Max(20.0, $MinimumSeconds + 15.0))
    do {
        $control = Get-Control
        if ($control.ready -and $control.effectiveValue -eq $ExpectedProfile) {
            return $control
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "$Feature did not settle at $ExpectedProfile (effective=$($control.effectiveValue), ready=$($control.ready))"
}

function Get-Distribution {
    param([double[]]$Values)
    if (-not $Values -or $Values.Count -eq 0) {
        return $null
    }
    [Array]::Sort($Values)
    function At-Percentile([double]$fraction) {
        $index = [int][Math]::Ceiling($fraction * $Values.Count) - 1
        $Values[[Math]::Clamp($index, 0, $Values.Count - 1)]
    }
    [ordered]@{
        count = $Values.Count
        minimum = $Values[0]
        average = ($Values | Measure-Object -Average).Average
        median = At-Percentile 0.50
        p95 = At-Percentile 0.95
        p99 = At-Percentile 0.99
        maximum = $Values[-1]
    }
}

function Collect-ProfilerPhase {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$EffectiveProfile,
        [Parameter(Mandatory = $true)][object]$Scene
    )
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
        if ($capturedFrame -le $armingFrame) {
            $stale++
            continue
        }
        if (-not $seen.Add($capturedFrame)) {
            $duplicates++
            continue
        }
        if ([double]$status.resolvedTotalMs -le 0.0) {
            continue
        }
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
    if ($accepted.Count -lt $Samples) {
        throw "$Name collected $($accepted.Count) of $Samples resolved samples before timeout"
    }

    $gpu = [double[]]@($accepted | ForEach-Object { $_.resolvedTotalMs })
    $cpu = [double[]]@($accepted | ForEach-Object { $_.resolvedCpuTotalMs })
    [ordered]@{
        name = $Name
        effectiveProfile = $EffectiveProfile
        scene = $Scene
        armingFrame = $armingFrame
        requestedSamples = $Samples
        acceptedSamples = $accepted.Count
        staleResponses = $stale
        duplicateResponses = $duplicates
        firstCapturedFrame = $accepted[0].capturedFrameCount
        lastCapturedFrame = $accepted[-1].capturedFrameCount
        gpu = Get-Distribution -Values $gpu
        cpu = Get-Distribution -Values $cpu
        samples = $accepted
    }
}

$record = [ordered]@{
    schemaVersion = 1
    runId = $RunId
    feature = $Feature
    control = $controlName
    startedAt = [DateTime]::UtcNow.ToString('o')
    samplesPerPhase = $Samples
    pollMilliseconds = $PollMilliseconds
    requestedProfileOrder = $ProfileOrder
    requestedAnchor = [ordered]@{
        gameHour = if ($hasGameHour) { $GameHour } else { $null }
        weatherForm = if ($WeatherForm) { $WeatherForm } else { $null }
    }
    source = [ordered]@{
        branch = 'feat/preset-calibration-automation'
        commit = $SourceCommit
        dllSha256 = $DllSha256.ToUpperInvariant()
        build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    baselineControl = $null
    transitions = [System.Collections.Generic.List[object]]::new()
    phases = [System.Collections.Generic.List[object]]::new()
    validity = [ordered]@{ accepted = $false; reasons = [System.Collections.Generic.List[string]]::new() }
}

try {
    $health = Invoke-RestMethod -Uri 'http://127.0.0.1:8921/api/health' -TimeoutSec 5
    if (-not $health.ok -or -not $health.vr -or $health.exe -ne 'SkyrimVR.exe') {
        throw 'DevBench is not attached to a healthy Skyrim VR process'
    }
    $baseline = Get-Control
    $record.baselineControl = $baseline
    if (-not $baseline.available -or -not $baseline.writable) {
        throw "$Feature qualityProfile is unavailable: $($baseline.unavailableReason)"
    }
    if ($baseline.snapshotHeld) {
        throw "$Feature already has an outstanding qualityProfile snapshot"
    }

    $profilerStatus = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
    $profilerWasEnabled = [bool]$profilerStatus.status.enabled
    $profilerStateKnown = $true
    if (-not $profilerWasEnabled) {
        Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'enable' } | Out-Null
    }
    Start-Sleep -Seconds 3

    $baselineScene = Set-AnchorState
    $record.phases.Add((Collect-ProfilerPhase -Name 'baseline-before' -EffectiveProfile ([string]$baseline.effectiveValue) -Scene $baselineScene))

    foreach ($profile in $ProfileOrder) {
        $transition = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
            action = 'set'
            feature = $Feature
            control = $controlName
            value = $profile
        }
        $record.transitions.Add($transition)
        if ($transition.error) {
            throw "$Feature profile set failed: $($transition.error)"
        }
        $snapshotHeld = [bool]$transition.control.snapshotHeld
        Wait-ControlSettle -MinimumSeconds ([double]$transition.control.settle.minimumSeconds) -ExpectedProfile $profile | Out-Null
        $scene = Set-AnchorState
        $record.phases.Add((Collect-ProfilerPhase -Name ("profile-" + $profile.ToLowerInvariant()) -EffectiveProfile $profile -Scene $scene))
    }

    $restore = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'restore'
        feature = $Feature
        control = $controlName
    }
    $record.transitions.Add($restore)
    $snapshotHeld = [bool]$restore.control.snapshotHeld
    if (-not $restore.restoredSnapshot) {
        throw "$Feature profile restore did not use the held baseline snapshot"
    }
    Wait-ControlSettle -MinimumSeconds ([double]$restore.control.settle.minimumSeconds) -ExpectedProfile ([string]$baseline.effectiveValue) | Out-Null
    $returnScene = Set-AnchorState
    $record.phases.Add((Collect-ProfilerPhase -Name 'baseline-return' -EffectiveProfile ([string]$baseline.effectiveValue) -Scene $returnScene))

    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase retained the requested number of unique resolved post-arm profiler frames.')
    $record.validity.reasons.Add('Every profile reached exact effective enum readback and its declared readiness/settle gate.')
    $record.validity.reasons.Add('The run restored the exact in-memory baseline profile state and did not overlap screenshot capture.')
}
catch {
    $runFailure = $_.Exception.Message
    $record.validity.reasons.Add("Run rejected: $runFailure")
}
finally {
    if ($snapshotHeld) {
        try {
            Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
                action = 'restore'
                feature = $Feature
                control = $controlName
            } | Out-Null
            $snapshotHeld = $false
        }
        catch {
            Write-Warning "Feature profile restore failed: $_"
            try {
                Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{ action = 'restoreAll' } | Out-Null
            }
            catch {
                Write-Warning "restoreAll also failed: $_"
            }
        }
    }
    if ($profilerStateKnown -and -not $profilerWasEnabled) {
        try {
            Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'disable' } | Out-Null
        }
        catch {
            Write-Warning "Profiler preference restore failed: $_"
        }
    }
    $record.finishedAt = [DateTime]::UtcNow.ToString('o')
}

$outputDirectory = Join-Path $OutputRoot $RunId
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$outputPath = Join-Path $outputDirectory 'profile-curve-profiler-raw.json'
$temporaryPath = $outputPath + '.tmp'
$json = $record | ConvertTo-Json -Depth 30
[System.IO.File]::WriteAllText($temporaryPath, $json, [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryPath -Destination $outputPath -Force

[ordered]@{
    runId = $RunId
    feature = $Feature
    accepted = $record.validity.accepted
    artifact = $outputPath
    phases = @($record.phases | ForEach-Object {
        [ordered]@{
            name = $_.name
            effectiveProfile = $_.effectiveProfile
            acceptedSamples = $_.acceptedSamples
            gpu = $_.gpu
            cpu = $_.cpu
        }
    })
} | ConvertTo-Json -Depth 10

if ($runFailure) {
    throw $runFailure
}
