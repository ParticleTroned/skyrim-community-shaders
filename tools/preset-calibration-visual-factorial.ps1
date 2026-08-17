param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$FeatureA,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ControlA = 'performanceActive',
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$FeatureB,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ControlB = 'performanceActive',
    [ValidateSet('A', 'B')][string]$DisableFirst = 'B',
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$RunId,
    [ValidateRange(10, 120)][int]$Frames = 30,
    [ValidateRange(1, 120)][int]$FrameInterval = 4,
    [ValidateRange(1, 60)][int]$PreviewFramesPerSecond = 30,
    [ValidateRange(0.0, 24.0)][double]$GameHour,
    [ValidatePattern('^[A-Fa-f0-9]+$')][string]$WeatherForm,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$DllSha256,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{7,40}$')][string]$SourceCommit,
    [string]$OutputRoot = 'D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-interaction-visual'
)

$ErrorActionPreference = 'Stop'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$hasGameHour = $PSBoundParameters.ContainsKey('GameHour')
$mutationStarted = $false
$runFailure = $null

function Invoke-DevBenchTool {
    param([Parameter(Mandatory = $true)][string]$Tool, [Parameter(Mandatory = $true)][hashtable]$Payload)
    $body = $Payload | ConvertTo-Json -Compress -Depth 20
    Invoke-RestMethod -Method Post -Uri ($baseUri + $Tool) -ContentType 'application/json' -Body $body -TimeoutSec 30
}

function Get-Control {
    param([Parameter(Mandatory = $true)][string]$Feature, [Parameter(Mandatory = $true)][string]$Control)
    $result = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'get'; feature = $Feature; control = $Control
    }
    if ($result.control.error) { throw "Unknown control $Feature/$Control`: $($result.control.error)" }
    $result.control
}

function Get-DeclaredSettleSeconds {
    param([Parameter(Mandatory = $true)]$Control, [Parameter(Mandatory = $true)][bool]$Expected)
    if ($Expected -and $null -ne $Control.settle.whenEnabledSeconds) { return [double]$Control.settle.whenEnabledSeconds }
    if (-not $Expected -and $null -ne $Control.settle.whenDisabledSeconds) { return [double]$Control.settle.whenDisabledSeconds }
    if ($null -ne $Control.settle.minimumSeconds) { return [double]$Control.settle.minimumSeconds }
    if ($null -ne $Control.settle.minimumFrames) { return [Math]::Max(0.25, [double]$Control.settle.minimumFrames / 30.0) }
    0.25
}

function Wait-ControlSettle {
    param(
        [Parameter(Mandatory = $true)][string]$Feature,
        [Parameter(Mandatory = $true)][string]$Control,
        [Parameter(Mandatory = $true)][bool]$Expected,
        [Parameter(Mandatory = $true)]$TransitionControl
    )
    $minimumSeconds = Get-DeclaredSettleSeconds -Control $TransitionControl -Expected $Expected
    if ($minimumSeconds -gt 0) { Start-Sleep -Milliseconds ([int][Math]::Ceiling($minimumSeconds * 1000.0)) }
    $deadline = (Get-Date).AddSeconds([Math]::Max(15.0, $minimumSeconds + 10.0))
    do {
        $current = Get-Control -Feature $Feature -Control $Control
        $hasReady = $null -ne $current.ready
        if ([bool]$current.effectiveValue -eq $Expected -and (-not $hasReady -or [bool]$current.ready)) { return $current }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "$Feature/$Control did not settle at $Expected"
}

function Set-ControlValue {
    param(
        [Parameter(Mandatory = $true)][string]$Feature,
        [Parameter(Mandatory = $true)][string]$Control,
        [Parameter(Mandatory = $true)][bool]$Value
    )
    $transition = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'; feature = $Feature; control = $Control; value = $Value
    }
    Wait-ControlSettle -Feature $Feature -Control $Control -Expected $Value -TransitionControl $transition.control | Out-Null
    $transition
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
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][bool]$A,
        [Parameter(Mandatory = $true)][bool]$B
    )
    $scene = Set-AnchorState
    $captureRoot = Join-Path $OutputRoot $RunId
    [System.IO.Directory]::CreateDirectory($captureRoot) | Out-Null
    $start = Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{
        action = 'start'; source = 'hmd_stereo'; label = "F-$Label"
        frameCount = $Frames; frameInterval = $FrameInterval
        previewFramesPerSecond = $PreviewFramesPerSecond; format = 'bmp'
        saveCombined = $true; saveSeparateEyes = $false; writePreviewVideo = $true
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
    [ordered]@{
        name = $Name; state = [ordered]@{ A = $A; B = $B }; scene = $scene
        format = $status.format; frameCount = $status.framesSaved
        frameIntervalCompositorCycles = $status.frameIntervalCompositorCycles
        firstCompositorCycleToken = $status.frames[0].compositorCycleToken
        lastCompositorCycleToken = $status.frames[-1].compositorCycleToken
        backpressureDrops = $status.backpressureDrops
        incompleteStereoDrops = $status.incompleteStereoDrops
        outputDirectory = $status.outputDirectory; manifestPath = $status.manifestPath
        previewVideoPath = $status.previewVideoPath
    }
}

$record = [ordered]@{
    schemaVersion = 1; runId = $RunId; startedAt = [DateTime]::UtcNow.ToString('o')
    factors = [ordered]@{
        A = [ordered]@{ feature = $FeatureA; control = $ControlA }
        B = [ordered]@{ feature = $FeatureB; control = $ControlB }
    }
    disableFirst = $DisableFirst
    requested = [ordered]@{
        gameHour = if ($hasGameHour) { $GameHour } else { $null }; weatherForm = $WeatherForm
        frames = $Frames; frameIntervalCompositorCycles = $FrameInterval
        previewFramesPerSecond = $PreviewFramesPerSecond; format = 'bmp'; output = 'combined stereo'
    }
    source = [ordered]@{
        branch = 'feat/preset-calibration-automation'; commit = $SourceCommit
        dllSha256 = $DllSha256.ToUpperInvariant(); build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    baselineControls = $null
    transitions = [System.Collections.Generic.List[object]]::new()
    phases = [System.Collections.Generic.List[object]]::new()
    validity = [ordered]@{ accepted = $false; reasons = [System.Collections.Generic.List[string]]::new() }
}

try {
    $baselineA = Get-Control -Feature $FeatureA -Control $ControlA
    $baselineB = Get-Control -Feature $FeatureB -Control $ControlB
    $record.baselineControls = [ordered]@{ A = $baselineA; B = $baselineB }
    foreach ($entry in @($baselineA, $baselineB)) {
        if (-not $entry.available -or -not $entry.writable -or -not [bool]$entry.effectiveValue) {
            throw 'Both factorial controls must begin available, writable, and enabled'
        }
        if ($null -ne $entry.snapshotHeld -and [bool]$entry.snapshotHeld) {
            throw 'A factorial control already holds an automation snapshot'
        }
    }
    $record.phases.Add((Capture-Phase -Name 'state-11-before' -Label '11a' -A $true -B $true))
    $mutationStarted = $true
    if ($DisableFirst -eq 'B') {
        $record.transitions.Add((Set-ControlValue -Feature $FeatureB -Control $ControlB -Value $false))
        $record.phases.Add((Capture-Phase -Name 'state-10' -Label '10' -A $true -B $false))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureA -Control $ControlA -Value $false))
        $record.phases.Add((Capture-Phase -Name 'state-00' -Label '00' -A $false -B $false))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureB -Control $ControlB -Value $true))
        $record.phases.Add((Capture-Phase -Name 'state-01' -Label '01' -A $false -B $true))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureA -Control $ControlA -Value $true))
    }
    else {
        $record.transitions.Add((Set-ControlValue -Feature $FeatureA -Control $ControlA -Value $false))
        $record.phases.Add((Capture-Phase -Name 'state-01' -Label '01' -A $false -B $true))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureB -Control $ControlB -Value $false))
        $record.phases.Add((Capture-Phase -Name 'state-00' -Label '00' -A $false -B $false))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureA -Control $ControlA -Value $true))
        $record.phases.Add((Capture-Phase -Name 'state-10' -Label '10' -A $true -B $false))
        $record.transitions.Add((Set-ControlValue -Feature $FeatureB -Control $ControlB -Value $true))
    }
    $mutationStarted = $false
    $record.phases.Add((Capture-Phase -Name 'state-11-return' -Label '11b' -A $true -B $true))
    $record.validity.accepted = $true
    $record.validity.reasons.Add('Every phase saved exact stereo pairs with zero failed, incomplete, or backpressured pairs.')
    $record.validity.reasons.Add('Both typed controls reached exact effective readback and their declared settle gates.')
    $record.validity.reasons.Add('The requested time and weather anchor was reapplied before every phase.')
    $record.validity.reasons.Add('Both controls returned to their exact enabled baseline state.')
}
catch {
    $runFailure = $_.Exception.Message
    $record.validity.reasons.Add("Run rejected: $runFailure")
}
finally {
    if ($mutationStarted) {
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
        [ordered]@{
            name = $_.name; state = $_.state; frameCount = $_.frameCount
            tokenSpan = $_.lastCompositorCycleToken - $_.firstCompositorCycleToken
            outputDirectory = $_.outputDirectory; previewVideoPath = $_.previewVideoPath
        }
    })
} | ConvertTo-Json -Depth 10

if ($runFailure) { throw $runFailure }
