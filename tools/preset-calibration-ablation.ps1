param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Feature,

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Control = 'performanceActive',

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$RunId,

    [ValidateRange(0.0, 24.0)]
    [double]$GameHour,

    [ValidatePattern('^[A-Fa-f0-9]+$')]
    [string]$WeatherForm,

    [string]$ExpectedCell,

    [string]$ExpectedWorldspace,

    [double]$ExpectedYaw,

    [ValidateRange(20, 1000)]
    [int]$Samples = 120,

    [ValidateRange(20, 1000)]
    [int]$PollMilliseconds = 50,

    [string]$OutputRoot = 'D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-automation-screening',

    [ValidateRange(10, 300)]
    [int]$PhaseTimeoutSeconds = 90,

    [ValidatePattern('^[A-Fa-f0-9]{7,40}$')]
    [string]$SourceCommit = 'ab359c4d9',

    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$DllSha256 = '7582AD4F96662385105C9EFEE48FDD1EDDE9A9629451C082CF4E9F8E4A787043'
)

$ErrorActionPreference = 'Stop'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$controlName = $Control
$snapshotHeld = $false
$mutationStarted = $false
$baselineValue = $null
$profilerWasEnabled = $false
$profilerStateKnown = $false
$hasGameHour = $PSBoundParameters.ContainsKey('GameHour')
$hasExpectedYaw = $PSBoundParameters.ContainsKey('ExpectedYaw')

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

function Set-AnchorState {
    if ($hasGameHour) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{
            action = 'exec'; command = "set gamehour to $GameHour"
        } | Out-Null
        Start-Sleep -Milliseconds 1500
    }
    if ($WeatherForm) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{
            action = 'exec'; command = "fw $WeatherForm"
        } | Out-Null
        Start-Sleep -Seconds 3
    }
    $anchor = Get-SceneSnapshot
    if ($ExpectedCell -and $anchor.scene.cell.editorId -ne $ExpectedCell) {
        throw "Anchor cell mismatch: expected $ExpectedCell, got $($anchor.scene.cell.editorId)"
    }
    if ($ExpectedWorldspace -and $anchor.scene.worldspace.editorId -ne $ExpectedWorldspace) {
        throw "Anchor worldspace mismatch: expected $ExpectedWorldspace, got $($anchor.scene.worldspace.editorId)"
    }
    if ($WeatherForm) {
        $expectedWeather = '0x{0:X8}' -f [Convert]::ToUInt32($WeatherForm, 16)
        if ($anchor.scene.weather.formId -ne $expectedWeather) {
            throw "Anchor weather mismatch: expected $expectedWeather, got $($anchor.scene.weather.formId)"
        }
    }
    if ($hasGameHour -and [Math]::Abs([double]$anchor.scene.gameHour - $GameHour) -gt 0.10) {
        throw "Anchor hour mismatch: expected $GameHour, got $($anchor.scene.gameHour)"
    }
    if ($hasExpectedYaw -and [Math]::Abs([double]$anchor.camera.camYaw - $ExpectedYaw) -gt 0.0001) {
        throw "Anchor yaw mismatch: expected $ExpectedYaw, got $($anchor.camera.camYaw)"
    }
    $anchor
}

function Wait-ControlSettle {
    param(
        [Parameter(Mandatory = $true)][object]$TransitionControl,
        [Parameter(Mandatory = $true)][bool]$ExpectedValue,
        [Parameter(Mandatory = $true)][string]$ExpectedState
    )

    $minimumSeconds = 0.0
    if ($ExpectedValue -and $null -ne $TransitionControl.settle.whenEnabledSeconds) {
        $minimumSeconds = [double]$TransitionControl.settle.whenEnabledSeconds
    } elseif (-not $ExpectedValue -and $null -ne $TransitionControl.settle.whenDisabledSeconds) {
        $minimumSeconds = [double]$TransitionControl.settle.whenDisabledSeconds
    }
    $minimumFrames = if ($null -ne $TransitionControl.settle.minimumFrames) {
        [uint32]$TransitionControl.settle.minimumFrames
    } else { [uint32]0 }
    $startFrame = if ($minimumFrames -gt 0) {
        [uint32](Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }).status.frame_count
    } else { [uint32]0 }
    if ($MinimumSeconds -gt 0) {
        Start-Sleep -Milliseconds ([int][Math]::Ceiling($MinimumSeconds * 1000.0))
    }
    $deadline = (Get-Date).AddSeconds([Math]::Max(10.0, $MinimumSeconds + 10.0))
    do {
        $control = Get-Control
        $frameReady = $true
        if ($minimumFrames -gt 0) {
            $currentFrame = [uint32](Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }).status.frame_count
            $frameReady = ($currentFrame - $startFrame) -ge $minimumFrames
        }
        $reportedReady = if ($null -ne $control.ready) { [bool]$control.ready } else { $true }
        if ($reportedReady -and $frameReady -and [bool]$control.effectiveValue -eq $ExpectedValue) {
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
        scene = $scene
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
        commit = $SourceCommit
        dllSha256 = $DllSha256.ToUpperInvariant()
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
    $baselineValue = [bool]$baselineControl.effectiveValue
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
    $mutationStarted = $true
    $snapshotHeld = [bool]$off.control.snapshotHeld
    if ($off.control.effectiveValue) {
        throw "$Feature did not become inactive"
    }
    Wait-ControlSettle -TransitionControl $off.control -ExpectedValue $false -ExpectedState 'disable' | Out-Null
    $record.phases.Add((Collect-ProfilerPhase -Name 'ablated'))

    $restore = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'
        feature = $Feature
        control = $controlName
        value = $true
    }
    $record.transitions.Add($restore)
    $snapshotHeld = [bool]$restore.control.snapshotHeld
    if ($controlName -eq 'performanceActive' -and -not $restore.restoredSnapshot) {
        throw "$Feature restore did not use the held baseline snapshot"
    }
    Wait-ControlSettle -TransitionControl $restore.control -ExpectedValue $true -ExpectedState 'restore' | Out-Null
    $mutationStarted = $false
    $record.phases.Add((Collect-ProfilerPhase -Name 'baseline-return'))

    $record.sceneAfter = Get-SceneSnapshot
    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase retained the requested number of unique resolved post-arm profiler frames.')
    $record.validity.reasons.Add('The feature used its exposed live Boolean off transition and restored the exact starting Boolean value.')
    $record.validity.reasons.Add('No screenshot capture overlapped the timing pass.')
}
finally {
    if ($snapshotHeld -or $mutationStarted) {
        try {
            Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
                action = 'set'
                feature = $Feature
                control = $controlName
                value = $baselineValue
            } | Out-Null
            $snapshotHeld = $false
            $mutationStarted = $false
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
