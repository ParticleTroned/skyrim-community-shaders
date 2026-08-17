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

    [ValidateRange(10, 240)]
    [int]$Frames = 60,

    [ValidateRange(1, 120)]
    [int]$FrameInterval = 3,

    [ValidateRange(1, 60)]
    [int]$PreviewFramesPerSecond = 30,

    [switch]$SaveSeparateEyes,

    [switch]$LeaveHudVisible,

    [ValidatePattern('^[A-Fa-f0-9]{7,40}$')]
    [string]$SourceCommit = 'ab359c4d9',

    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$DllSha256 = '7582AD4F96662385105C9EFEE48FDD1EDDE9A9629451C082CF4E9F8E4A787043',

    [string]$OutputRoot = 'D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-automation-visual'
)

$ErrorActionPreference = 'Stop'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$controlName = $Control
$snapshotHeld = $false
$mutationStarted = $false
$baselineValue = $null
$hasGameHour = $PSBoundParameters.ContainsKey('GameHour')
$hasExpectedYaw = $PSBoundParameters.ContainsKey('ExpectedYaw')
$hudToggled = $false
$runFailure = $null

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

function Wait-ControlSettle {
    param(
        [Parameter(Mandatory = $true)][object]$TransitionControl,
        [Parameter(Mandatory = $true)][bool]$ExpectedValue
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
    throw "$Feature did not become ready: $($control.waitText)"
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
    $anchor = [ordered]@{
        scene = Invoke-DevBenchTool -Tool 'inspect' -Payload @{ kind = 'scene' }
        camera = Invoke-DevBenchTool -Tool 'camera' -Payload @{ action = 'get' }
    }
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

function Capture-Phase {
    param([Parameter(Mandatory = $true)][string]$Name)

    $scene = Set-AnchorState
    $captureRoot = Join-Path $OutputRoot $RunId
    [System.IO.Directory]::CreateDirectory($captureRoot) | Out-Null
    $start = Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{
        action = 'start'
        source = 'hmd_stereo'
        label = "$Feature-$Control-$Name"
        frameCount = $Frames
        frameInterval = $FrameInterval
        previewFramesPerSecond = $PreviewFramesPerSecond
        format = 'bmp'
        saveCombined = $true
        saveSeparateEyes = [bool]$SaveSeparateEyes
        writePreviewVideo = $true
        outputPath = $captureRoot
    }
    if ($start.error) {
        throw "Capture start failed: $($start.error)"
    }

    $deadline = (Get-Date).AddSeconds(180)
    do {
        Start-Sleep -Milliseconds 500
        $status = (Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{ action = 'status' }).status
    } while (($status.state -in @('capturing', 'draining') -or -not $status.previewVideoFinished) -and (Get-Date) -lt $deadline)

    if ($status.state -ne 'complete' -or
        $status.framesSaved -ne $Frames -or
        $status.framesFailed -ne 0 -or
        $status.backpressureDrops -ne 0 -or
        $status.incompleteStereoDrops -ne 0 -or
        -not $status.previewVideoSucceeded) {
        throw "Capture $Name failed validity gates: state=$($status.state), saved=$($status.framesSaved), backpressure=$($status.backpressureDrops), incomplete=$($status.incompleteStereoDrops), preview=$($status.previewVideoSucceeded)"
    }

    [ordered]@{
        name = $Name
        scene = $scene
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
    startedAt = [DateTime]::UtcNow.ToString('o')
    source = [ordered]@{
        branch = 'feat/preset-calibration-automation'
        commit = $SourceCommit
        dllSha256 = $DllSha256.ToUpperInvariant()
        build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    requested = [ordered]@{
        gameHour = if ($hasGameHour) { $GameHour } else { $null }
        weatherForm = if ($WeatherForm) { $WeatherForm } else { $null }
        frames = $Frames
        frameIntervalCompositorCycles = $FrameInterval
        previewFramesPerSecond = $PreviewFramesPerSecond
        format = 'bmp'
        output = if ($SaveSeparateEyes) { 'combined stereo and separate eyes' } else { 'combined stereo' }
    }
    baselineControl = $null
    transitions = [System.Collections.Generic.List[object]]::new()
    phases = [System.Collections.Generic.List[object]]::new()
    validity = [ordered]@{ accepted = $false; reasons = [System.Collections.Generic.List[string]]::new() }
}

try {
    $baseline = Get-Control
    $record.baselineControl = $baseline
    $baselineValue = [bool]$baseline.effectiveValue
    if (-not $baseline.available -or -not $baseline.writable -or -not $baseline.effectiveValue) {
        throw "$Feature requires an available, active baseline"
    }
    if ($baseline.snapshotHeld) {
        throw "$Feature already has an outstanding automation snapshot"
    }

    if (-not $LeaveHudVisible) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = 'tm' } | Out-Null
        $hudToggled = $true
        Start-Sleep -Milliseconds 500
    }

    $record.phases.Add((Capture-Phase -Name 'baseline-before'))

    $off = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'
        feature = $Feature
        control = $controlName
        value = $false
    }
    $record.transitions.Add($off)
    $mutationStarted = $true
    $snapshotHeld = [bool]$off.control.snapshotHeld
    Wait-ControlSettle -TransitionControl $off.control -ExpectedValue $false | Out-Null
    $record.phases.Add((Capture-Phase -Name 'ablated'))

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
    Wait-ControlSettle -TransitionControl $restore.control -ExpectedValue $true | Out-Null
    $mutationStarted = $false
    $record.phases.Add((Capture-Phase -Name 'baseline-return'))

    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase saved the requested exact stereo pairs with zero backpressure or incomplete-pair drops.')
    $record.validity.reasons.Add('Each phase re-applied and read back the requested time/weather anchor state.')
    $record.validity.reasons.Add('The candidate used its exposed live Boolean off transition and restored the exact starting Boolean value.')
}
catch {
    $runFailure = $_.Exception.Message
    $record.validity.reasons.Add("Run rejected: $runFailure")
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
    if ($hudToggled) {
        try { Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = 'tm' } | Out-Null }
        catch { Write-Warning "HUD restore failed: $_" }
    }
    $record.finishedAt = [DateTime]::UtcNow.ToString('o')
}

$outputDirectory = Join-Path $OutputRoot $RunId
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$outputPath = Join-Path $outputDirectory 'visual-ablation-run.json'
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
            frameCount = $_.frameCount
            tokenSpan = $_.lastCompositorCycleToken - $_.firstCompositorCycleToken
            outputDirectory = $_.outputDirectory
            previewVideoPath = $_.previewVideoPath
        }
    })
} | ConvertTo-Json -Depth 10

if ($runFailure) {
    throw $runFailure
}
