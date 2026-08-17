param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9]+$')][string]$Feature,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9]+$')][string]$Parameter,
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][double[]]$Values,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$RunId,
    [ValidateRange(3, 120)][int]$Frames = 12,
    [ValidateRange(1, 120)][int]$FrameInterval = 10,
    [ValidateRange(1, 60)][int]$PreviewFramesPerSecond = 30,
    [ValidateSet('png', 'bmp')][string]$Format = 'png',
    [switch]$SaveCombined,
    [ValidateRange(0.0, 24.0)][double]$GameHour,
    [ValidatePattern('^[A-Fa-f0-9]+$')][string]$WeatherForm,
    [ValidateRange(0.0, 1000.0)][double]$RestoreTimescale = 20.0,
    [switch]$LeaveTimeRunning,
    [switch]$UseExistingSnapshot,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$DllSha256,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{7,40}$')][string]$SourceCommit,
    [switch]$LeaveHudVisible,
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'preset-calibration-storage.ps1')
$OutputRoot = Resolve-PresetCalibrationOutputRoot -OutputRoot $OutputRoot -Collection 'preset-automation-visual-parameter-curves'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$controlName = 'qualityParameters'
$snapshotHeld = $false
$hudToggled = $false
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
    elseif ($definitionProperty.Value.valueType -eq 'boolean') {
        if ($Value -ne 0.0 -and $Value -ne 1.0) { throw "$Feature parameter $Parameter requires 0 or 1 for Boolean automation" }
        $requestValue = [bool][int]$Value
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
    }
}

function Capture-Phase {
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][string]$Label, [Parameter(Mandatory = $true)][double]$EffectiveValue)
    $scene = Set-AnchorState
    $captureRoot = Join-Path $OutputRoot $RunId
    [System.IO.Directory]::CreateDirectory($captureRoot) | Out-Null
    $previewRequired = [bool]$SaveCombined
    $start = Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{
        action = 'start'; source = 'hmd_stereo'; label = $Label
        frameCount = $Frames; frameInterval = $FrameInterval; previewFramesPerSecond = $PreviewFramesPerSecond
        format = $Format; saveCombined = [bool]$SaveCombined; saveSeparateEyes = $true; writePreviewVideo = $previewRequired
        outputPath = $captureRoot
    }
    if ($start.error) { throw "Capture start failed: $($start.error)" }
    $deadline = (Get-Date).AddSeconds(180)
    do {
        Start-Sleep -Milliseconds 500
        $status = (Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{ action = 'status' }).status
    } while (($status.state -in @('capturing', 'draining') -or ($previewRequired -and -not $status.previewVideoFinished)) -and (Get-Date) -lt $deadline)
    if ($status.state -ne 'complete' -or $status.framesSaved -ne $Frames -or $status.framesFailed -ne 0 -or
        $status.backpressureDrops -ne 0 -or $status.incompleteStereoDrops -ne 0 -or ($previewRequired -and -not $status.previewVideoSucceeded)) {
        throw "Capture $Name failed: state=$($status.state), saved=$($status.framesSaved), failed=$($status.framesFailed), backpressure=$($status.backpressureDrops), incomplete=$($status.incompleteStereoDrops), preview=$($status.previewVideoSucceeded)"
    }
    [ordered]@{
        name = $Name; effectiveValue = $EffectiveValue; scene = $scene; format = $status.format
        frameCount = $status.framesSaved; frameIntervalCompositorCycles = $status.frameIntervalCompositorCycles
        firstCompositorCycleToken = $status.frames[0].compositorCycleToken
        lastCompositorCycleToken = $status.frames[-1].compositorCycleToken
        backpressureDrops = $status.backpressureDrops; incompleteStereoDrops = $status.incompleteStereoDrops
        outputDirectory = $status.outputDirectory; manifestPath = $status.manifestPath; previewVideoPath = $status.previewVideoPath
    }
}

$record = [ordered]@{
    schemaVersion = 1; runId = $RunId; feature = $Feature; control = $controlName; parameter = $Parameter
    startedAt = [DateTime]::UtcNow.ToString('o'); requestedValues = $Values
    requested = [ordered]@{
        gameHour = if ($hasGameHour) { $GameHour } else { $null }; weatherForm = $WeatherForm
        timeFrozen = $hasGameHour -and -not $LeaveTimeRunning; restoreTimescale = $RestoreTimescale
        frames = $Frames; frameIntervalCompositorCycles = $FrameInterval; previewFramesPerSecond = $PreviewFramesPerSecond
        format = $Format; output = if ($SaveCombined) { 'combined stereo and separate eyes' } else { 'separate eyes only' }
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
    $profiler = Invoke-DevBenchTool -Tool 'communityshaders.profiler' -Payload @{ action = 'status' }
    if ($profiler.status.enabled) { throw 'Visual capture must not overlap profiler measurement' }
    $baseline = Get-Control; $record.baselineControl = $baseline
    if (-not $baseline.available -or -not $baseline.writable) { throw "$Feature qualityParameters is unavailable: $($baseline.unavailableReason)" }
    $snapshotHeld = [bool]$baseline.snapshotHeld
    if ($snapshotHeld -and -not $UseExistingSnapshot) { throw "$Feature already has an outstanding qualityParameters snapshot; pass -UseExistingSnapshot only when this run owns that deliberate preconfiguration snapshot" }
    $baselineValue = Get-EffectiveValue -Control $baseline

    if ($hasGameHour -and -not $LeaveTimeRunning) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = 'set timescale to 0' } | Out-Null
        $timeFrozen = $true; Start-Sleep -Milliseconds 250
    }

    if (-not $LeaveHudVisible) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = 'tm' } | Out-Null
        $hudToggled = $true; Start-Sleep -Milliseconds 500
    }
    $record.phases.Add((Capture-Phase -Name 'baseline-before' -Label 'PARAM-A1' -EffectiveValue $baselineValue))
    for ($index = 0; $index -lt $Values.Count; $index++) {
        $value = [double]$Values[$index]
        $transition = Set-ParameterValue -Value $value; $record.transitions.Add($transition)
        $record.phases.Add((Capture-Phase -Name ('value-{0:D2}' -f ($index + 1)) -Label ('PARAM-V{0:D2}' -f ($index + 1)) -EffectiveValue $value))
    }
    $restore = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{ action = 'restore'; feature = $Feature; control = $controlName }
    $record.transitions.Add($restore); $snapshotHeld = [bool]$restore.control.snapshotHeld
    if (-not $restore.restoredSnapshot) { throw "$Feature parameter restore did not use the held baseline snapshot" }
    $restored = Get-Control
    $record['restoredControl'] = $restored
    if (-not $UseExistingSnapshot) {
        if (-not $restored.ready -or [Math]::Abs((Get-EffectiveValue -Control $restored) - $baselineValue) -gt 0.001) { throw "$Feature did not restore $Parameter to $baselineValue" }
        $record.phases.Add((Capture-Phase -Name 'baseline-return' -Label 'PARAM-A2' -EffectiveValue $baselineValue))
    }
    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase saved exact separate-eye pairs with zero failed, incomplete, or backpressured pairs; combined stereo was optional derivative output.')
    $record.validity.reasons.Add('Every scalar value reached exact effective readback before capture.')
    if ($UseExistingSnapshot) { $record.validity.reasons.Add('The requested time and weather were reapplied before each phase. The run consumed its deliberate preconfiguration snapshot and recorded the earlier state restored by that snapshot; a post-restore capture was intentionally omitted because the restored feature may be inactive.') }
    else { $record.validity.reasons.Add('The requested time and weather were reapplied before each phase, and the exact baseline parameter was restored.') }
}
catch { $runFailure = $_.Exception.Message; $record.validity.reasons.Add("Run rejected: $runFailure") }
finally {
    if ($snapshotHeld) {
        try { Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{ action = 'restoreAll' } | Out-Null } catch { Write-Warning "restoreAll failed: $_" }
    }
    if ($hudToggled) {
        try { Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = 'tm' } | Out-Null } catch { Write-Warning "HUD restore failed: $_" }
    }
    if ($timeFrozen) {
        try { Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = "set timescale to $RestoreTimescale" } | Out-Null } catch { Write-Warning "Timescale restore failed: $_" }
    }
    $record.finishedAt = [DateTime]::UtcNow.ToString('o')
}

$outputDirectory = Join-Path $OutputRoot $RunId
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$outputPath = Join-Path $outputDirectory 'visual-parameter-curve-run.json'
$temporaryPath = $outputPath + '.tmp'
[System.IO.File]::WriteAllText($temporaryPath, ($record | ConvertTo-Json -Depth 30), [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryPath -Destination $outputPath -Force

[ordered]@{
    runId = $RunId; feature = $Feature; parameter = $Parameter; accepted = $record.validity.accepted; artifact = $outputPath
    phases = @($record.phases | ForEach-Object { [ordered]@{ name = $_.name; effectiveValue = $_.effectiveValue; frameCount = $_.frameCount; outputDirectory = $_.outputDirectory } })
} | ConvertTo-Json -Depth 10
if ($runFailure) { throw $runFailure }
