param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Feature,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$RunId,

    [ValidateRange(20, 1000)]
    [int]$Samples = 120,

    [ValidateRange(20, 1000)]
    [int]$PollMilliseconds = 50,

    [string]$OutputRoot = 'D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-automation-screening',

    [ValidateRange(10, 300)]
    [int]$PhaseTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$controlName = 'performanceActive'
$snapshotHeld = $false
$profilerWasEnabled = $false
$profilerStateKnown = $false

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

function Get-Control {
    $result = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'get'
        feature = $Feature
        control = $controlName
    }
    if ($result.control.error) {
        throw "Unknown control: $($result.control.error)"
    }
    $result.control
}

function Wait-ControlSettle {
    param(
        [Parameter(Mandatory = $true)][double]$MinimumSeconds,
        [Parameter(Mandatory = $true)][string]$ExpectedState
    )

    if ($MinimumSeconds -gt 0) {
        Start-Sleep -Milliseconds ([int][Math]::Ceiling($MinimumSeconds * 1000.0))
    }
    $deadline = (Get-Date).AddSeconds([Math]::Max(10.0, $MinimumSeconds + 10.0))
    do {
        $control = Get-Control
        if ($control.ready) {
            return $control
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)

    throw "$Feature did not become ready after ${ExpectedState}: $($control.waitText)"
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
    param([Parameter(Mandatory = $true)][string]$Name)

    $arming = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
    $armingFrame = [uint32]$arming.status.frame_count
    $seen = [System.Collections.Generic.HashSet[uint32]]::new()
    $accepted = [System.Collections.Generic.List[object]]::new()
    $stale = 0
    $duplicates = 0
    $deadline = (Get-Date).AddSeconds($PhaseTimeoutSeconds)

    while ($accepted.Count -lt $Samples -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds $PollMilliseconds
        $response = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
        $status = $response.status
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
    source = [ordered]@{
        branch = 'feat/preset-calibration-automation'
        commit = '9218ec2e8'
        dllSha256 = '03E8062D9401F03DBD0190E9EA119F3EAC8BD5D01FDF1433B1D0D5362D1E7B8D'
        build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    sceneBefore = $null
    sceneAfter = $null
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

    $record.sceneBefore = Get-SceneSnapshot
    $baselineControl = Get-Control
    $record.baselineControl = $baselineControl
    if (-not $baselineControl.available -or -not $baselineControl.writable) {
        throw "$Feature is not writable in this runtime lane: $($baselineControl.unavailableReason)"
    }
    if (-not $baselineControl.effectiveValue) {
        throw "$Feature starts inactive; on/off ablation requires an active baseline"
    }
    if ($baselineControl.snapshotHeld) {
        throw "$Feature already has an outstanding automation snapshot"
    }

    $profilerStatus = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
    $profilerWasEnabled = [bool]$profilerStatus.status.enabled
    $profilerStateKnown = $true
    if (-not $profilerWasEnabled) {
        Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'enable' } | Out-Null
    }
    Start-Sleep -Seconds 3

    $record.phases.Add((Collect-ProfilerPhase -Name 'baseline-before'))

    $off = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'
        feature = $Feature
        control = $controlName
        value = $false
    }
    $record.transitions.Add($off)
    $snapshotHeld = [bool]$off.control.snapshotHeld
    if ($off.control.effectiveValue) {
        throw "$Feature did not become inactive"
    }
    Wait-ControlSettle -MinimumSeconds ([double]$off.control.settle.whenDisabledSeconds) -ExpectedState 'disable' | Out-Null
    $record.phases.Add((Collect-ProfilerPhase -Name 'ablated'))

    $restore = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'
        feature = $Feature
        control = $controlName
        value = $true
    }
    $record.transitions.Add($restore)
    $snapshotHeld = [bool]$restore.control.snapshotHeld
    if (-not $restore.restoredSnapshot) {
        throw "$Feature restore did not use the held baseline snapshot"
    }
    Wait-ControlSettle -MinimumSeconds ([double]$restore.control.settle.whenEnabledSeconds) -ExpectedState 'restore' | Out-Null
    $record.phases.Add((Collect-ProfilerPhase -Name 'baseline-return'))

    $record.sceneAfter = Get-SceneSnapshot
    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase retained the requested number of unique resolved post-arm profiler frames.')
    $record.validity.reasons.Add('The feature used its production Performance Tuning off transition and exact in-memory state restore.')
    $record.validity.reasons.Add('No screenshot capture overlapped the timing pass.')
}
finally {
    if ($snapshotHeld) {
        try {
            Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
                action = 'set'
                feature = $Feature
                control = $controlName
                value = $true
            } | Out-Null
            $snapshotHeld = $false
        }
        catch {
            Write-Warning "Feature-specific restore failed: $_"
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
$outputPath = Join-Path $outputDirectory 'ablation-profiler-raw.json'
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
            acceptedSamples = $_.acceptedSamples
            firstCapturedFrame = $_.firstCapturedFrame
            lastCapturedFrame = $_.lastCapturedFrame
            gpu = $_.gpu
            cpu = $_.cpu
        }
    })
} | ConvertTo-Json -Depth 10
