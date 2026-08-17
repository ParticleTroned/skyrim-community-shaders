param(
    [ValidateSet('Samples', 'Range')][string]$ChangeFirst = 'Range',
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$RunId,
    [ValidateRange(16.0, 96.0)][double]$LowSamples = 30.0,
    [ValidateRange(16.0, 96.0)][double]$HighSamples = 44.0,
    [ValidateRange(0.0, 20480.0)][double]$CappedDistance = 20480.0,
    [ValidateRange(10, 120)][int]$Frames = 30,
    [ValidateRange(1, 30)][int]$FactorFrames = 5,
    [ValidateRange(1, 120)][int]$FrameInterval = 6,
    [ValidateRange(1, 60)][int]$PreviewFramesPerSecond = 30,
    [ValidateRange(0.0, 24.0)][double]$GameHour,
    [ValidatePattern('^[A-Fa-f0-9]+$')][string]$WeatherForm,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$DllSha256,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{7,40}$')][string]$SourceCommit,
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'preset-calibration-storage.ps1')
$OutputRoot = Resolve-PresetCalibrationOutputRoot -OutputRoot $OutputRoot -Collection 'preset-automation-sss-factorial'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$feature = 'ScreenSpaceShadows'
$controlName = 'qualityParameters'
$hasGameHour = $PSBoundParameters.ContainsKey('GameHour')
$snapshotHeld = $false
$mutationStarted = $false
$runFailure = $null

function Invoke-DevBenchTool {
    param([Parameter(Mandatory = $true)][string]$Tool, [Parameter(Mandatory = $true)][hashtable]$Payload)
    $body = $Payload | ConvertTo-Json -Compress -Depth 20
    Invoke-RestMethod -Method Post -Uri ($baseUri + $Tool) -ContentType 'application/json' -Body $body -TimeoutSec 30
}

function Get-Control {
    (Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'get'; feature = $feature; control = $controlName
    }).control
}

function Test-EffectiveState {
    param([Parameter(Mandatory = $true)]$Control, [Parameter(Mandatory = $true)][double]$Samples, [Parameter(Mandatory = $true)][double]$Distance)
    [Math]::Abs([double]$Control.effectiveValue.VRBaseSamplesAtReference - $Samples) -le 0.001 -and
        [Math]::Abs([double]$Control.effectiveValue.VRCullDistance - $Distance) -le 0.001
}

function Set-State {
    param([Parameter(Mandatory = $true)][double]$Samples, [Parameter(Mandatory = $true)][double]$Distance)
    $script:mutationStarted = $true
    $transition = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'; feature = $feature; control = $controlName
        value = @{ VRBaseSamplesAtReference = $Samples; VRCullDistance = $Distance }
    }
    if ($transition.error) { throw "SSS parameter set failed: $($transition.error)" }
    $script:snapshotHeld = [bool]$transition.control.snapshotHeld
    $minimumSeconds = [double]$transition.control.settle.minimumSeconds
    if ($minimumSeconds -gt 0) { Start-Sleep -Milliseconds ([int][Math]::Ceiling($minimumSeconds * 1000.0)) }
    $deadline = (Get-Date).AddSeconds([Math]::Max(20.0, $minimumSeconds + 15.0))
    do {
        $current = Get-Control
        if ($current.ready -and (Test-EffectiveState -Control $current -Samples $Samples -Distance $Distance)) {
            return [ordered]@{ transition = $transition; settled = $current }
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "SSS parameters did not settle at samples=$Samples, distance=$Distance"
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

function Capture-FactorMeasurements {
    param(
        [Parameter(Mandatory = $true)][string]$PhaseName,
        [Parameter(Mandatory = $true)][string]$CaptureRoot
    )
    $measurements = [System.Collections.Generic.List[object]]::new()
    for ($index = 1; $index -le $FactorFrames; $index++) {
        $outputPath = Join-Path $CaptureRoot ('{0}_factor_{1:D3}.png' -f $PhaseName, $index)
        $start = Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{
            action = 'measure'; source = 'screen_space_shadows_factor'; outputPath = $outputPath
        }
        if ($start.error) { throw "Factor measurement start failed for $PhaseName/$index`: $($start.error)" }
        $measurementId = [uint64]$start.status.id
        $deadline = (Get-Date).AddSeconds(30)
        do {
            Start-Sleep -Milliseconds 100
            $status = (Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{ action = 'measurementStatus' }).status
        } while ($status.id -eq $measurementId -and $status.state -in @('pending', 'queued') -and (Get-Date) -lt $deadline)
        if ($status.id -ne $measurementId -or $status.state -ne 'complete') {
            throw "Factor measurement failed for $PhaseName/$index`: id=$($status.id), state=$($status.state), error=$($status.error)"
        }
        if (-not (Test-Path -LiteralPath $status.outputPath) -or -not (Test-Path -LiteralPath $status.statisticsPath)) {
            throw "Factor measurement artifacts are incomplete for $PhaseName/$index"
        }
        $measurements.Add([ordered]@{
            id = $measurementId; outputPath = $status.outputPath; statisticsPath = $status.statisticsPath
        })
    }
    $measurements
}

function Capture-Phase {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][bool]$A,
        [Parameter(Mandatory = $true)][bool]$B,
        [Parameter(Mandatory = $true)][double]$Samples,
        [Parameter(Mandatory = $true)][double]$Distance
    )
    $scene = Set-AnchorState
    $captureRoot = Join-Path $OutputRoot $RunId
    [System.IO.Directory]::CreateDirectory($captureRoot) | Out-Null
    $start = Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{
        action = 'start'; source = 'hmd_stereo'; label = "SSSF-$Label"
        frameCount = $Frames; frameInterval = $FrameInterval
        previewFramesPerSecond = $PreviewFramesPerSecond; format = 'bmp'
        saveCombined = $true; saveSeparateEyes = $true; writePreviewVideo = $true
        outputPath = $captureRoot
    }
    if ($start.error) { throw "Capture start failed: $($start.error)" }
    $deadline = (Get-Date).AddSeconds(180)
    do {
        Start-Sleep -Milliseconds 500
        $status = (Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{ action = 'status' }).status
    } while (($status.state -in @('capturing', 'draining') -or -not $status.previewVideoFinished) -and (Get-Date) -lt $deadline)
    if ($status.state -ne 'complete' -or $status.framesSaved -ne $Frames -or
        $status.framesFailed -ne 0 -or $status.backpressureDrops -ne 0 -or
        $status.incompleteStereoDrops -ne 0 -or -not $status.previewVideoSucceeded) {
        throw "Capture $Name failed: state=$($status.state), saved=$($status.framesSaved), backpressure=$($status.backpressureDrops), incomplete=$($status.incompleteStereoDrops), preview=$($status.previewVideoSucceeded)"
    }
    $factorMeasurements = @(Capture-FactorMeasurements -PhaseName $Name -CaptureRoot $captureRoot)
    [ordered]@{
        name = $Name
        state = [ordered]@{ A = $A; B = $B; VRBaseSamplesAtReference = $Samples; VRCullDistance = $Distance }
        scene = $scene; format = $status.format; frameCount = $status.framesSaved
        frameIntervalCompositorCycles = $status.frameIntervalCompositorCycles
        firstCompositorCycleToken = $status.frames[0].compositorCycleToken
        lastCompositorCycleToken = $status.frames[-1].compositorCycleToken
        backpressureDrops = $status.backpressureDrops; incompleteStereoDrops = $status.incompleteStereoDrops
        outputDirectory = $status.outputDirectory; manifestPath = $status.manifestPath
        previewVideoPath = $status.previewVideoPath
        factorMeasurements = $factorMeasurements
    }
}

$record = [ordered]@{
    schemaVersion = 1; runId = $RunId; startedAt = [DateTime]::UtcNow.ToString('o')
    factors = [ordered]@{
        A = [ordered]@{ parameter = 'VRBaseSamplesAtReference'; off = $LowSamples; on = $HighSamples }
        B = [ordered]@{ parameter = 'VRCullDistance'; off = $CappedDistance; on = 0.0; onMeaning = 'unlimited' }
    }
    disableFirst = if ($ChangeFirst -eq 'Range') { 'B' } else { 'A' }
    requested = [ordered]@{
        gameHour = if ($hasGameHour) { $GameHour } else { $null }; weatherForm = $WeatherForm
        frames = $Frames; frameIntervalCompositorCycles = $FrameInterval
        factorFramesPerPhase = $FactorFrames
        previewFramesPerSecond = $PreviewFramesPerSecond; format = 'bmp'
        output = 'combined stereo and separate eyes'
    }
    source = [ordered]@{
        branch = 'feat/preset-calibration-automation'; commit = $SourceCommit
        dllSha256 = $DllSha256.ToUpperInvariant(); build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    baselineControl = $null
    transitions = [System.Collections.Generic.List[object]]::new()
    phases = [System.Collections.Generic.List[object]]::new()
    validity = [ordered]@{ accepted = $false; reasons = [System.Collections.Generic.List[string]]::new() }
}

try {
    $baseline = Get-Control
    $record.baselineControl = $baseline
    if (-not $baseline.available -or -not $baseline.writable) { throw "SSS qualityParameters is unavailable: $($baseline.unavailableReason)" }
    if ($baseline.snapshotHeld) { throw 'SSS already has an outstanding quality snapshot' }
    if (-not (Test-EffectiveState -Control $baseline -Samples $HighSamples -Distance 0.0)) {
        throw "SSS must begin at the high-samples unlimited baseline ($HighSamples, 0)"
    }

    $record.phases.Add((Capture-Phase -Name 'state-11-before' -Label '11a' -A $true -B $true -Samples $HighSamples -Distance 0.0))
    if ($ChangeFirst -eq 'Range') {
        $record.transitions.Add((Set-State -Samples $HighSamples -Distance $CappedDistance))
        $record.phases.Add((Capture-Phase -Name 'state-10' -Label '10' -A $true -B $false -Samples $HighSamples -Distance $CappedDistance))
        $record.transitions.Add((Set-State -Samples $LowSamples -Distance $CappedDistance))
        $record.phases.Add((Capture-Phase -Name 'state-00' -Label '00' -A $false -B $false -Samples $LowSamples -Distance $CappedDistance))
        $record.transitions.Add((Set-State -Samples $LowSamples -Distance 0.0))
        $record.phases.Add((Capture-Phase -Name 'state-01' -Label '01' -A $false -B $true -Samples $LowSamples -Distance 0.0))
    }
    else {
        $record.transitions.Add((Set-State -Samples $LowSamples -Distance 0.0))
        $record.phases.Add((Capture-Phase -Name 'state-01' -Label '01' -A $false -B $true -Samples $LowSamples -Distance 0.0))
        $record.transitions.Add((Set-State -Samples $LowSamples -Distance $CappedDistance))
        $record.phases.Add((Capture-Phase -Name 'state-00' -Label '00' -A $false -B $false -Samples $LowSamples -Distance $CappedDistance))
        $record.transitions.Add((Set-State -Samples $HighSamples -Distance $CappedDistance))
        $record.phases.Add((Capture-Phase -Name 'state-10' -Label '10' -A $true -B $false -Samples $HighSamples -Distance $CappedDistance))
    }

    $restore = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{ action = 'restore'; feature = $feature; control = $controlName }
    $record.transitions.Add($restore)
    $snapshotHeld = [bool]$restore.control.snapshotHeld
    $mutationStarted = $false
    if (-not $restore.restoredSnapshot) { throw 'SSS parameter restore did not use the held baseline snapshot' }
    $minimumSeconds = [double]$restore.control.settle.minimumSeconds
    if ($minimumSeconds -gt 0) { Start-Sleep -Milliseconds ([int][Math]::Ceiling($minimumSeconds * 1000.0)) }
    $restored = Get-Control
    if (-not $restored.ready -or -not (Test-EffectiveState -Control $restored -Samples $HighSamples -Distance 0.0)) {
        throw 'SSS did not return to the exact high-samples unlimited baseline'
    }
    $record.phases.Add((Capture-Phase -Name 'state-11-return' -Label '11b' -A $true -B $true -Samples $HighSamples -Distance 0.0))
    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase saved exact combined and separate-eye pairs with zero failed, incomplete, or backpressured pairs.')
    $record.validity.reasons.Add('Every parameter state reached exact effective readback and the declared shader-readiness gate.')
    $record.validity.reasons.Add("Every phase saved $FactorFrames raw packed-stereo SSS factor frames and statistics sidecars through the bounded Info-level measurement path.")
    $record.validity.reasons.Add('The requested time and weather anchor was reapplied before every phase.')
    $record.validity.reasons.Add('The exact original SSS state was restored from the in-memory snapshot.')
}
catch {
    $runFailure = $_.Exception.Message
    $record.validity.reasons.Add("Run rejected: $runFailure")
}
finally {
    if ($snapshotHeld -or $mutationStarted) {
        try { Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{ action = 'restoreAll' } | Out-Null }
        catch { Write-Warning "restoreAll failed: $_" }
    }
    $record.finishedAt = [DateTime]::UtcNow.ToString('o')
}

$outputDirectory = Join-Path $OutputRoot $RunId
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$outputPath = Join-Path $outputDirectory 'visual-factorial-run.json'
$temporaryPath = $outputPath + '.tmp'
[System.IO.File]::WriteAllText($temporaryPath, ($record | ConvertTo-Json -Depth 30), [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryPath -Destination $outputPath -Force

[ordered]@{
    runId = $RunId; accepted = $record.validity.accepted; artifact = $outputPath
    phases = @($record.phases | ForEach-Object {
        [ordered]@{ name = $_.name; state = $_.state; frameCount = $_.frameCount; outputDirectory = $_.outputDirectory }
    })
} | ConvertTo-Json -Depth 10

if ($runFailure) { throw $runFailure }
