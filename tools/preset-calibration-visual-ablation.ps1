param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Feature,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$RunId,

    [ValidateRange(0.0, 24.0)]
    [double]$GameHour,

    [ValidatePattern('^[A-Fa-f0-9]+$')]
    [string]$WeatherForm,

    [ValidateRange(10, 240)]
    [int]$Frames = 60,

    [ValidateRange(1, 120)]
    [int]$FrameInterval = 3,

    [ValidateRange(1, 60)]
    [int]$PreviewFramesPerSecond = 30,

    [string]$OutputRoot = 'D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-automation-visual'
)

$ErrorActionPreference = 'Stop'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$controlName = 'performanceActive'
$snapshotHeld = $false
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

function Get-Control {
    (Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'get'
        feature = $Feature
        control = $controlName
    }).control
}

function Wait-ControlSettle {
    param([Parameter(Mandatory = $true)][double]$MinimumSeconds)

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
    [ordered]@{
        scene = Invoke-DevBenchTool -Tool 'inspect' -Payload @{ kind = 'scene' }
        camera = Invoke-DevBenchTool -Tool 'camera' -Payload @{ action = 'get' }
    }
}

function Capture-Phase {
    param([Parameter(Mandatory = $true)][string]$Name)

    $scene = Set-AnchorState
    $captureRoot = Join-Path $OutputRoot $RunId
    [System.IO.Directory]::CreateDirectory($captureRoot) | Out-Null
    $start = Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{
        action = 'start'
        source = 'hmd_stereo'
        label = "$Feature-$Name"
        frameCount = $Frames
        frameInterval = $FrameInterval
        previewFramesPerSecond = $PreviewFramesPerSecond
        format = 'bmp'
        saveCombined = $true
        saveSeparateEyes = $false
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
        commit = '9218ec2e8'
        dllSha256 = '03E8062D9401F03DBD0190E9EA119F3EAC8BD5D01FDF1433B1D0D5362D1E7B8D'
        build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    requested = [ordered]@{
        gameHour = if ($hasGameHour) { $GameHour } else { $null }
        weatherForm = if ($WeatherForm) { $WeatherForm } else { $null }
        frames = $Frames
        frameIntervalCompositorCycles = $FrameInterval
        previewFramesPerSecond = $PreviewFramesPerSecond
        format = 'bmp'
        output = 'combined stereo'
    }
    baselineControl = $null
    transitions = [System.Collections.Generic.List[object]]::new()
    phases = [System.Collections.Generic.List[object]]::new()
    validity = [ordered]@{ accepted = $false; reasons = [System.Collections.Generic.List[string]]::new() }
}

try {
    $baseline = Get-Control
    $record.baselineControl = $baseline
    if (-not $baseline.available -or -not $baseline.writable -or -not $baseline.effectiveValue) {
        throw "$Feature requires an available, active baseline"
    }
    if ($baseline.snapshotHeld) {
        throw "$Feature already has an outstanding automation snapshot"
    }

    $record.phases.Add((Capture-Phase -Name 'baseline-before'))

    $off = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'
        feature = $Feature
        control = $controlName
        value = $false
    }
    $record.transitions.Add($off)
    $snapshotHeld = [bool]$off.control.snapshotHeld
    Wait-ControlSettle -MinimumSeconds ([double]$off.control.settle.whenDisabledSeconds) | Out-Null
    $record.phases.Add((Capture-Phase -Name 'ablated'))

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
    Wait-ControlSettle -MinimumSeconds ([double]$restore.control.settle.whenEnabledSeconds) | Out-Null
    $record.phases.Add((Capture-Phase -Name 'baseline-return'))

    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase saved the requested exact stereo pairs with zero backpressure or incomplete-pair drops.')
    $record.validity.reasons.Add('Each phase re-applied and read back the requested time/weather anchor state.')
    $record.validity.reasons.Add('The candidate used the production off transition and exact in-memory baseline restore.')
}
catch {
    $runFailure = $_.Exception.Message
    $record.validity.reasons.Add("Run rejected: $runFailure")
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
