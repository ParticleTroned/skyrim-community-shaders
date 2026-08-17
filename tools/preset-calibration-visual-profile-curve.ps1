param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Skylighting', 'ScreenSpaceShadows', 'Wetterness')]
    [string]$Feature,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$RunId,

    [ValidateSet('Performance', 'Balanced', 'Quality')]
    [string[]]$ProfileOrder = @('Performance', 'Balanced', 'Quality'),

    [ValidateRange(10, 120)]
    [int]$Frames = 30,

    [ValidateRange(1, 120)]
    [int]$FrameInterval = 4,

    [ValidateRange(1, 60)]
    [int]$PreviewFramesPerSecond = 30,

    [switch]$SaveSeparateEyes,

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

    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'preset-calibration-storage.ps1')
$OutputRoot = Resolve-PresetCalibrationOutputRoot -OutputRoot $OutputRoot -Collection 'preset-automation-visual-curves'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$controlName = 'qualityProfile'
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
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$EffectiveProfile
    )
    $scene = Set-AnchorState
    $captureRoot = Join-Path $OutputRoot $RunId
    [System.IO.Directory]::CreateDirectory($captureRoot) | Out-Null
    $featureLabel = switch ($Feature) {
        'Skylighting' { 'Sky' }
        'ScreenSpaceShadows' { 'SSS' }
        'Wetterness' { 'Wet' }
    }
    $phaseLabel = switch ($Name) {
        'baseline-before' { 'A1' }
        'profile-performance' { 'P' }
        'profile-balanced' { 'B' }
        'profile-quality' { 'Q' }
        'baseline-return' { 'A2' }
    }
    $start = Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{
        action = 'start'
        source = 'hmd_stereo'
        label = "$featureLabel-$phaseLabel"
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
        effectiveProfile = $EffectiveProfile
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
        profileOrder = $ProfileOrder
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
    if (-not $baseline.available -or -not $baseline.writable) {
        throw "$Feature qualityProfile is unavailable: $($baseline.unavailableReason)"
    }
    if ($baseline.snapshotHeld) {
        throw "$Feature already has an outstanding qualityProfile snapshot"
    }

    $record.phases.Add((Capture-Phase -Name 'baseline-before' -EffectiveProfile ([string]$baseline.effectiveValue)))

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
        $record.phases.Add((Capture-Phase -Name ("profile-" + $profile.ToLowerInvariant()) -EffectiveProfile $profile))
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
    $record.phases.Add((Capture-Phase -Name 'baseline-return' -EffectiveProfile ([string]$baseline.effectiveValue)))

    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase saved the requested exact stereo pairs with zero backpressure or incomplete-pair drops.')
    $record.validity.reasons.Add('Every profile reached exact effective enum readback and its declared readiness/settle gate.')
    $record.validity.reasons.Add('Each phase re-applied and read back the requested time/weather anchor state.')
    $record.validity.reasons.Add('The run restored the exact in-memory baseline profile state.')
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
    $record.finishedAt = [DateTime]::UtcNow.ToString('o')
}

$outputDirectory = Join-Path $OutputRoot $RunId
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$outputPath = Join-Path $outputDirectory 'visual-profile-curve-run.json'
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
