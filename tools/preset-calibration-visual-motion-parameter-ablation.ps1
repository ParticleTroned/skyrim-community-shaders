param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9]+$')]
    [string]$Feature,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9]+$')]
    [string]$Parameter,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$RunId,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^coc [A-Za-z0-9_]+$')]
    [string]$EntryCommand,

    [Parameter(Mandatory = $true)]
    [ValidateSet('x', 'y', 'z')]
    [string]$MotionAxis,

    [Parameter(Mandatory = $true)]
    [ValidateRange(-512.0, 512.0)]
    [ValidateScript({ [Math]::Abs($_) -ge 0.5 })]
    [double]$MotionOffset,

    [ValidateRange(8, 120)]
    [int]$Frames = 44,

    [ValidateRange(1, 120)]
    [int]$FrameInterval = 3,

    [ValidateRange(2, 100)]
    [int]$PreStepFrames = 12,

    [ValidateRange(1, 60)]
    [int]$PreviewFramesPerSecond = 30,

    [ValidateSet('png', 'bmp')]
    [string]$Format = 'bmp',

    [ValidateRange(1, 60)]
    [int]$PostEntryWaitSeconds = 12,

    [ValidateRange(0.0, 24.0)]
    [double]$GameHour,

    [ValidatePattern('^[A-Fa-f0-9]+$')]
    [string]$WeatherForm,

    [string]$ExpectedCell,

    [string]$ExpectedWorldspace,

    [double]$ExpectedYaw,

    [ValidateRange(0.0, 1000.0)]
    [double]$RestoreTimescale = 20.0,

    [switch]$UseExistingSnapshot,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$DllSha256,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Fa-f0-9]{7,40}$')]
    [string]$SourceCommit,

    [switch]$LeaveHudVisible,

    [switch]$WritePreviewVideo,

    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'preset-calibration-storage.ps1')
$OutputRoot = Resolve-PresetCalibrationOutputRoot -OutputRoot $OutputRoot -Collection 'preset-automation-visual-motion'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$controlName = 'qualityParameters'
$hasGameHour = $PSBoundParameters.ContainsKey('GameHour')
$hasExpectedYaw = $PSBoundParameters.ContainsKey('ExpectedYaw')
$snapshotHeld = $false
$hudToggled = $false
$timeFrozen = $false
$runFailure = $null
$baselineParameterValue = $null

if ($PreStepFrames -ge $Frames) {
    throw 'PreStepFrames must be less than Frames'
}

function Invoke-DevBenchTool {
    param(
        [Parameter(Mandatory = $true)][string]$Tool,
        [Parameter(Mandatory = $true)][hashtable]$Payload
    )
    $body = $Payload | ConvertTo-Json -Compress -Depth 20
    Invoke-RestMethod -Method Post -Uri ($baseUri + $Tool) -ContentType 'application/json' -Body $body -TimeoutSec 30
}

function Get-Control {
    (Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'get'
        feature = $Feature
        control = $controlName
    }).control
}

function Get-BooleanParameterValue {
    param([Parameter(Mandatory = $true)]$Control)
    $definition = $Control.parameterDefinitions.PSObject.Properties[$Parameter]
    if (-not $definition) {
        throw "$Feature does not define quality parameter $Parameter"
    }
    if ($definition.Value.valueType -ne 'boolean') {
        throw "$Feature parameter $Parameter is not Boolean"
    }
    $effective = $Control.effectiveValue.PSObject.Properties[$Parameter]
    if (-not $effective) {
        throw "$Feature does not report effective parameter $Parameter"
    }
    [bool]$effective.Value
}

function Wait-ParameterValue {
    param([Parameter(Mandatory = $true)][bool]$ExpectedValue)
    $deadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        $current = Get-Control
        if ($current.ready -and (Get-BooleanParameterValue -Control $current) -eq $ExpectedValue) {
            return $current
        }
    } while ((Get-Date) -lt $deadline)
    throw "$Feature parameter $Parameter did not settle at $ExpectedValue"
}

function Set-ParameterValue {
    param([Parameter(Mandatory = $true)][bool]$Value)
    $transition = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'
        feature = $Feature
        control = $controlName
        value = @{ $Parameter = $Value }
    }
    if ($transition.error) {
        throw "$Feature parameter set failed: $($transition.error)"
    }
    $script:snapshotHeld = [bool]$transition.control.snapshotHeld
    [ordered]@{
        transition = $transition
        settled = Wait-ParameterValue -ExpectedValue $Value
    }
}

function Invoke-ConsoleCommand {
    param([Parameter(Mandatory = $true)][string]$Command)
    Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = $Command } | Out-Null
}

function Get-AnchorState {
    [ordered]@{
        scene = Invoke-DevBenchTool -Tool 'inspect' -Payload @{ kind = 'scene' }
        camera = Invoke-DevBenchTool -Tool 'camera' -Payload @{ action = 'get' }
    }
}

function Assert-AnchorState {
    param([Parameter(Mandatory = $true)]$Anchor)
    if (-not $Anchor.scene.playerLoaded) {
        throw 'Anchor scene has no loaded player'
    }
    if ($ExpectedCell -and $Anchor.scene.cell.editorId -ne $ExpectedCell) {
        throw "Anchor cell mismatch: expected $ExpectedCell, got $($Anchor.scene.cell.editorId)"
    }
    if ($ExpectedWorldspace -and $Anchor.scene.worldspace.editorId -ne $ExpectedWorldspace) {
        throw "Anchor worldspace mismatch: expected $ExpectedWorldspace, got $($Anchor.scene.worldspace.editorId)"
    }
    if ($WeatherForm) {
        $expectedWeather = '0x{0:X8}' -f [Convert]::ToUInt32($WeatherForm, 16)
        if ($Anchor.scene.weather.formId -ne $expectedWeather) {
            throw "Anchor weather mismatch: expected $expectedWeather, got $($Anchor.scene.weather.formId)"
        }
    }
    if ($hasGameHour -and [Math]::Abs([double]$Anchor.scene.gameHour - $GameHour) -gt 0.10) {
        throw "Anchor hour mismatch: expected $GameHour, got $($Anchor.scene.gameHour)"
    }
    if ($hasExpectedYaw -and [Math]::Abs([double]$Anchor.camera.camYaw - $ExpectedYaw) -gt 0.0001) {
        throw "Anchor yaw mismatch: expected $ExpectedYaw, got $($Anchor.camera.camYaw)"
    }
}

function Establish-AnchorState {
    Invoke-ConsoleCommand -Command $EntryCommand
    Start-Sleep -Seconds $PostEntryWaitSeconds
    Invoke-ConsoleCommand -Command 'set timescale to 0'
    Start-Sleep -Milliseconds 250
    if ($hasGameHour) {
        Invoke-ConsoleCommand -Command "set gamehour to $GameHour"
        Start-Sleep -Milliseconds 1500
    }
    if ($WeatherForm) {
        Invoke-ConsoleCommand -Command "fw $WeatherForm"
        Start-Sleep -Seconds 3
    }
    $anchor = Get-AnchorState
    Assert-AnchorState -Anchor $anchor
    $anchor
}

function Get-PositionComponent {
    param(
        [Parameter(Mandatory = $true)]$Scene,
        [Parameter(Mandatory = $true)][string]$Axis
    )
    $index = switch ($Axis) {
        'x' { 0 }
        'y' { 1 }
        'z' { 2 }
    }
    [double]$Scene.position[$index]
}

function Wait-PositionComponent {
    param([Parameter(Mandatory = $true)][double]$ExpectedValue)
    $deadline = (Get-Date).AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
        $scene = Invoke-DevBenchTool -Tool 'inspect' -Payload @{ kind = 'scene' }
        $actual = Get-PositionComponent -Scene $scene -Axis $MotionAxis
        if ([Math]::Abs($actual - $ExpectedValue) -le 0.25) {
            return $scene
        }
    } while ((Get-Date) -lt $deadline)
    throw "Motion step did not settle on $MotionAxis at $ExpectedValue; last readback was $actual"
}

function Capture-MotionPhase {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$EffectiveValue
    )

    $anchor = Establish-AnchorState
    $settledControl = Wait-ParameterValue -ExpectedValue $EffectiveValue
    $startCoordinate = Get-PositionComponent -Scene $anchor.scene -Axis $MotionAxis
    $targetCoordinate = $startCoordinate + $MotionOffset
    $captureRoot = Join-Path $OutputRoot $RunId
    [System.IO.Directory]::CreateDirectory($captureRoot) | Out-Null

    $start = Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{
        action = 'start'
        source = 'hmd_stereo'
        label = "$Feature-$Parameter-$Name-motion"
        frameCount = $Frames
        frameInterval = $FrameInterval
        previewFramesPerSecond = $PreviewFramesPerSecond
        format = $Format
        saveCombined = $false
        saveSeparateEyes = $true
        writePreviewVideo = [bool]$WritePreviewVideo
        outputPath = $captureRoot
    }
    if ($start.error) {
        throw "Capture start failed: $($start.error)"
    }

    $triggerDeadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 25
        $preStepStatus = (Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{ action = 'status' }).status
    } while ($preStepStatus.state -eq 'capturing' -and
        [int]$preStepStatus.framesQueued -lt $PreStepFrames -and
        (Get-Date) -lt $triggerDeadline)

    if ([int]$preStepStatus.framesQueued -lt $PreStepFrames) {
        throw "Capture reached state $($preStepStatus.state) before the requested motion trigger"
    }

    $commandIssuedAtUtc = [DateTime]::UtcNow.ToString('o')
    Invoke-ConsoleCommand -Command ("player.setpos {0} {1:R}" -f $MotionAxis, $targetCoordinate)
    $postStepScene = Wait-PositionComponent -ExpectedValue $targetCoordinate
    $postStepStatus = (Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{ action = 'status' }).status

    $completionDeadline = (Get-Date).AddSeconds(240)
    do {
        Start-Sleep -Milliseconds 250
        $status = (Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{ action = 'status' }).status
    } while (($status.state -in @('capturing', 'draining') -or
        ([bool]$WritePreviewVideo -and -not $status.previewVideoFinished)) -and
        (Get-Date) -lt $completionDeadline)

    if ($status.state -ne 'complete' -or
        $status.framesSaved -ne $Frames -or
        $status.framesFailed -ne 0 -or
        $status.backpressureDrops -ne 0 -or
        $status.incompleteStereoDrops -ne 0 -or
        ([bool]$WritePreviewVideo -and -not $status.previewVideoSucceeded)) {
        throw "Capture $Name failed: state=$($status.state), saved=$($status.framesSaved), failed=$($status.framesFailed), backpressure=$($status.backpressureDrops), incomplete=$($status.incompleteStereoDrops), preview=$($status.previewVideoSucceeded)"
    }

    [ordered]@{
        name = $Name
        effectiveValue = $EffectiveValue
        settledControl = $settledControl
        anchor = $anchor
        motion = [ordered]@{
            axis = $MotionAxis
            startCoordinate = $startCoordinate
            requestedOffset = $MotionOffset
            targetCoordinate = $targetCoordinate
            commandIssuedAtUtc = $commandIssuedAtUtc
            queuedFramesBeforeCommand = [int]$preStepStatus.framesQueued
            queuedFramesAfterObservedMove = [int]$postStepStatus.framesQueued
            observedPosition = @($postStepScene.position)
        }
        format = $status.format
        frameCount = $status.framesSaved
        frameIntervalCompositorCycles = $status.frameIntervalCompositorCycles
        firstCompositorCycleToken = $status.frames[0].compositorCycleToken
        lastCompositorCycleToken = $status.frames[-1].compositorCycleToken
        backpressureDrops = $status.backpressureDrops
        incompleteStereoDrops = $status.incompleteStereoDrops
        outputDirectory = $status.outputDirectory
        manifestPath = $status.manifestPath
        previewVideoPath = $status.previewVideoPath
    }
}

$record = [ordered]@{
    schemaVersion = 1
    runId = $RunId
    feature = $Feature
    control = $controlName
    parameter = $Parameter
    startedAt = [DateTime]::UtcNow.ToString('o')
    source = [ordered]@{
        branch = 'feat/preset-calibration-automation'
        commit = $SourceCommit
        dllSha256 = $DllSha256.ToUpperInvariant()
        build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    requested = [ordered]@{
        entryCommand = $EntryCommand
        gameHour = if ($hasGameHour) { $GameHour } else { $null }
        weatherForm = if ($WeatherForm) { $WeatherForm } else { $null }
        motionAxis = $MotionAxis
        motionOffset = $MotionOffset
        frames = $Frames
        preStepFrames = $PreStepFrames
        frameIntervalCompositorCycles = $FrameInterval
        format = $Format
        output = 'separate eyes only'
        previewVideo = [bool]$WritePreviewVideo
    }
    baselineControl = $null
    transitions = [System.Collections.Generic.List[object]]::new()
    phases = [System.Collections.Generic.List[object]]::new()
    restoredControl = $null
    validity = [ordered]@{
        accepted = $false
        reasons = [System.Collections.Generic.List[string]]::new()
    }
}

try {
    $health = Invoke-RestMethod -Uri 'http://127.0.0.1:8921/api/health' -TimeoutSec 5
    if (-not $health.ok -or -not $health.vr -or $health.exe -ne 'SkyrimVR.exe') {
        throw 'DevBench is not attached to a healthy Skyrim VR process'
    }
    $profiler = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
    if ($profiler.status.enabled) {
        throw 'Motion capture must not overlap profiler measurement'
    }

    $baseline = Get-Control
    $record.baselineControl = $baseline
    if (-not $baseline.available -or -not $baseline.writable) {
        throw "$Feature qualityParameters is unavailable: $($baseline.unavailableReason)"
    }
    $snapshotHeld = [bool]$baseline.snapshotHeld
    if ($snapshotHeld -and -not $UseExistingSnapshot) {
        throw "$Feature already has an outstanding qualityParameters snapshot; pass -UseExistingSnapshot only when this run owns that deliberate preconfiguration snapshot"
    }
    $baselineParameterValue = Get-BooleanParameterValue -Control $baseline
    if (-not $baselineParameterValue) {
        throw "$Feature parameter $Parameter must be active at the beginning of the motion ablation"
    }

    Invoke-ConsoleCommand -Command 'set timescale to 0'
    $timeFrozen = $true
    Start-Sleep -Milliseconds 250
    if (-not $LeaveHudVisible) {
        Invoke-ConsoleCommand -Command 'tm'
        $hudToggled = $true
        Start-Sleep -Milliseconds 500
    }

    $record.phases.Add((Capture-MotionPhase -Name 'baseline-before' -EffectiveValue $true))
    $off = Set-ParameterValue -Value $false
    $record.transitions.Add($off)
    $record.phases.Add((Capture-MotionPhase -Name 'ablated' -EffectiveValue $false))
    $on = Set-ParameterValue -Value $true
    $record.transitions.Add($on)
    $record.phases.Add((Capture-MotionPhase -Name 'baseline-return' -EffectiveValue $true))

    $restore = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'restore'
        feature = $Feature
        control = $controlName
    }
    $record.transitions.Add($restore)
    if (-not $restore.restoredSnapshot) {
        throw "$Feature parameter restore did not use the held quality snapshot"
    }
    $snapshotHeld = [bool]$restore.control.snapshotHeld
    $record.restoredControl = Get-Control

    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase reproduced and verified the declared anchor before the same player-space translation step.')
    $record.validity.reasons.Add('Every phase saved exact separate-eye pairs with zero failed, incomplete, or backpressured pairs.')
    $record.validity.reasons.Add('The motion command was issued only after the declared number of compositor pairs had queued, and effective player-position readback verified the step.')
    $record.validity.reasons.Add('The Boolean feature parameter followed an on/off/on order and the original quality snapshot was restored.')
}
catch {
    $runFailure = $_.Exception.Message
    $record.validity.reasons.Add("Run rejected: $runFailure")
}
finally {
    if ($snapshotHeld) {
        try {
            Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{ action = 'restoreAll' } | Out-Null
        }
        catch {
            Write-Warning "restoreAll failed: $_"
        }
    }
    if ($hudToggled) {
        try { Invoke-ConsoleCommand -Command 'tm' } catch { Write-Warning "HUD restore failed: $_" }
    }
    if ($timeFrozen) {
        try { Invoke-ConsoleCommand -Command "set timescale to $RestoreTimescale" } catch { Write-Warning "Timescale restore failed: $_" }
    }
    $record.finishedAt = [DateTime]::UtcNow.ToString('o')
}

$outputDirectory = Join-Path $OutputRoot $RunId
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$outputPath = Join-Path $outputDirectory 'visual-motion-parameter-ablation-run.json'
$temporaryPath = $outputPath + '.tmp'
[System.IO.File]::WriteAllText($temporaryPath, ($record | ConvertTo-Json -Depth 30), [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryPath -Destination $outputPath -Force

[ordered]@{
    runId = $RunId
    feature = $Feature
    parameter = $Parameter
    accepted = $record.validity.accepted
    artifact = $outputPath
    phases = @($record.phases | ForEach-Object {
        [ordered]@{
            name = $_.name
            effectiveValue = $_.effectiveValue
            frameCount = $_.frameCount
            queuedFramesBeforeCommand = $_.motion.queuedFramesBeforeCommand
            queuedFramesAfterObservedMove = $_.motion.queuedFramesAfterObservedMove
            outputDirectory = $_.outputDirectory
        }
    })
} | ConvertTo-Json -Depth 10

if ($runFailure) {
    throw $runFailure
}
