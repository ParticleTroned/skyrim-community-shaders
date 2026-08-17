param(
    [ValidateSet('Ranges', 'Temporal')][string]$FactorSet = 'Ranges',
    [ValidateSet('A', 'B')][string]$ChangeFirst = 'A',
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$RunId,
    [ValidateRange(10, 240)][int]$Frames = 60,
    [ValidateRange(1, 120)][int]$FrameInterval = 4,
    [ValidateRange(1, 60)][int]$PreviewFramesPerSecond = 30,
    [ValidateRange(0.0, 24.0)][double]$GameHour = 14.0,
    [ValidatePattern('^[A-Fa-f0-9]+$')][string]$WeatherForm = 'C8220',
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$DllSha256,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Fa-f0-9]{7,40}$')][string]$SourceCommit,
    [switch]$LeaveHudVisible,
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'preset-calibration-storage.ps1')
$OutputRoot = Resolve-PresetCalibrationOutputRoot -OutputRoot $OutputRoot -Collection 'preset-automation-wetness-factorial'
$baseUri = 'http://127.0.0.1:8921/api/tool/'
$feature = 'Wetterness'
$controlName = 'qualityParameters'
$snapshotHeld = $false
$mutationStarted = $false
$hudToggled = $false
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
    param([Parameter(Mandatory = $true)]$Control, [Parameter(Mandatory = $true)][hashtable]$Expected)
    foreach ($name in $Expected.Keys) {
        if ([Math]::Abs([double]$Control.effectiveValue.$name - [double]$Expected[$name]) -gt 0.001) {
            return $false
        }
    }
    return $true
}

function Set-State {
    param([Parameter(Mandatory = $true)][hashtable]$Parameters)
    $script:mutationStarted = $true
    $transition = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'set'; feature = $feature; control = $controlName; value = $Parameters
    }
    if ($transition.error) { throw "Wetness parameter set failed: $($transition.error)" }
    $script:snapshotHeld = [bool]$transition.control.snapshotHeld
    $minimumSeconds = [double]$transition.control.settle.minimumSeconds
    if ($minimumSeconds -gt 0) {
        Start-Sleep -Milliseconds ([int][Math]::Ceiling($minimumSeconds * 1000.0))
    }
    $deadline = (Get-Date).AddSeconds([Math]::Max(20.0, $minimumSeconds + 15.0))
    do {
        $current = Get-Control
        if ($current.ready -and (Test-EffectiveState -Control $current -Expected $Parameters)) {
            return [ordered]@{ transition = $transition; settled = $current }
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "Wetness parameters did not settle: $($Parameters | ConvertTo-Json -Compress)"
}

function Set-AnchorState {
    Invoke-DevBenchTool -Tool 'console' -Payload @{
        action = 'exec'; command = "set gamehour to $GameHour"
    } | Out-Null
    Start-Sleep -Milliseconds 1500
    Invoke-DevBenchTool -Tool 'console' -Payload @{
        action = 'exec'; command = "fw $WeatherForm"
    } | Out-Null
    Start-Sleep -Seconds 3
    $anchor = [ordered]@{
        scene = Invoke-DevBenchTool -Tool 'inspect' -Payload @{ kind = 'scene' }
        camera = Invoke-DevBenchTool -Tool 'camera' -Payload @{ action = 'get' }
    }
    if ($anchor.scene.cell.editorId -ne 'GuardianStones' -or
        $anchor.scene.worldspace.editorId -ne 'Tamriel' -or
        $anchor.scene.weather.formId -ne '0x000C8220' -or
        [Math]::Abs([double]$anchor.scene.gameHour - $GameHour) -gt 0.10 -or
        [Math]::Abs([double]$anchor.camera.camYaw - (-1.5500009060)) -gt 0.0001) {
        throw "Guardian Stones storm anchor readback mismatch: $($anchor | ConvertTo-Json -Compress -Depth 10)"
    }
    $anchor
}

function Capture-Phase {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][bool]$A,
        [Parameter(Mandatory = $true)][bool]$B,
        [Parameter(Mandatory = $true)][hashtable]$Parameters
    )
    $transition = Set-State -Parameters $Parameters
    $script:record.transitions.Add($transition)
    $scene = Set-AnchorState
    $captureRoot = Join-Path $OutputRoot $RunId
    [System.IO.Directory]::CreateDirectory($captureRoot) | Out-Null
    $start = Invoke-DevBenchTool -Tool 'communityshaders.capture' -Payload @{
        action = 'start'; source = 'hmd_stereo'; label = "WETF-$Label"
        frameCount = $Frames; frameInterval = $FrameInterval
        previewFramesPerSecond = $PreviewFramesPerSecond; format = 'bmp'
        saveCombined = $true; saveSeparateEyes = $true; writePreviewVideo = $true
        outputPath = $captureRoot
    }
    if ($start.error) { throw "Capture start failed: $($start.error)" }
    $deadline = (Get-Date).AddSeconds(240)
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
        name = $Name; state = [ordered]@{ A = $A; B = $B; parameters = $Parameters }
        effective = $transition.settled.effectiveValue
        derivedEffective = $transition.settled.derivedEffectiveParameters
        scene = $scene; format = $status.format; frameCount = $status.framesSaved
        frameIntervalCompositorCycles = $status.frameIntervalCompositorCycles
        firstCompositorCycleToken = $status.frames[0].compositorCycleToken
        lastCompositorCycleToken = $status.frames[-1].compositorCycleToken
        backpressureDrops = $status.backpressureDrops; incompleteStereoDrops = $status.incompleteStereoDrops
        outputDirectory = $status.outputDirectory; manifestPath = $status.manifestPath
        previewVideoPath = $status.previewVideoPath
    }
}

function New-State {
    param([Parameter(Mandatory = $true)][bool]$A, [Parameter(Mandatory = $true)][bool]$B)
    $state = @{
        RaindropFxRangeWorldUnits = 1400.0
        WetnessDistanceFadeRange = 10000.0
        RaindropGridSize = 3.0
        RaindropInterval = 0.5
        RaindropChance = 0.8
        SplashesLifetime = 6.0
        RippleLifetime = 0.30
    }
    if ($FactorSet -eq 'Ranges') {
        if (-not $A) { $state.WetnessDistanceFadeRange = 7003.0 }
        if (-not $B) { $state.RaindropFxRangeWorldUnits = 700.0 }
    } else {
        if (-not $A) {
            $state.RaindropGridSize = 3.6
            $state.RaindropInterval = 0.65
            $state.RaindropChance = 0.60
        }
        if (-not $B) {
            $state.SplashesLifetime = 4.5
            $state.RippleLifetime = 0.22
        }
    }
    $state
}

$factorDefinitions = if ($FactorSet -eq 'Ranges') {
    [ordered]@{
        A = [ordered]@{ name = 'material-wetness-fade-range'; off = 7003.0; on = 10000.0; unit = 'game-units' }
        B = [ordered]@{ name = 'raindrop-effect-range'; off = 700.0; on = 1400.0; unit = 'game-units' }
    }
} else {
    [ordered]@{
        A = [ordered]@{
            name = 'raindrop-opportunity-density'
            off = [ordered]@{ RaindropGridSize = 3.6; RaindropInterval = 0.65; RaindropChance = 0.60 }
            on = [ordered]@{ RaindropGridSize = 3.0; RaindropInterval = 0.5; RaindropChance = 0.80 }
        }
        B = [ordered]@{
            name = 'splash-ripple-persistence'
            off = [ordered]@{ SplashesLifetime = 4.5; RippleLifetime = 0.22 }
            on = [ordered]@{ SplashesLifetime = 6.0; RippleLifetime = 0.30 }
        }
    }
}

$record = [ordered]@{
    schemaVersion = 1; runId = $RunId; feature = $feature; factorSet = $FactorSet
    startedAt = [DateTime]::UtcNow.ToString('o'); factors = $factorDefinitions; disableFirst = $ChangeFirst
    requested = [ordered]@{
        gameHour = $GameHour; weatherForm = $WeatherForm; frames = $Frames
        frameIntervalCompositorCycles = $FrameInterval; previewFramesPerSecond = $PreviewFramesPerSecond
        format = 'bmp'; output = 'combined stereo and separate eyes'; iblRequired = $true
    }
    source = [ordered]@{
        branch = 'feat/preset-calibration-automation'; commit = $SourceCommit
        dllSha256 = $DllSha256.ToUpperInvariant(); build = 'VR Release; Info logging; Release+DevBench bridge'
    }
    originalControl = $null
    iblControl = $null
    transitions = [System.Collections.Generic.List[object]]::new()
    phases = [System.Collections.Generic.List[object]]::new()
    restore = $null
    validity = [ordered]@{ accepted = $false; reasons = [System.Collections.Generic.List[string]]::new() }
}

try {
    Invoke-RestMethod -Uri 'http://127.0.0.1:8921/api/health' -TimeoutSec 5 | Out-Null
    $record.originalControl = Get-Control
    if (-not $record.originalControl.available -or -not $record.originalControl.writable) {
        throw "Wetness qualityParameters is unavailable: $($record.originalControl.unavailableReason)"
    }
    if ($record.originalControl.snapshotHeld) { throw 'Wetness already has an outstanding quality snapshot' }
    $record.iblControl = (Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'get'; feature = 'ImageBasedLighting'; control = 'EnableIBL'
    }).control
    if (-not $record.iblControl.effectiveValue) { throw 'IBL must be active for Wetness tier evidence' }

    if (-not $LeaveHudVisible) {
        Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = 'tm' } | Out-Null
        $hudToggled = $true
        Start-Sleep -Milliseconds 500
    }

    $state11 = New-State -A $true -B $true
    $state10 = New-State -A $true -B $false
    $state00 = New-State -A $false -B $false
    $state01 = New-State -A $false -B $true
    $record.phases.Add((Capture-Phase -Name 'state-11-before' -Label '11a' -A $true -B $true -Parameters $state11))
    if ($ChangeFirst -eq 'A') {
        $record.phases.Add((Capture-Phase -Name 'state-01' -Label '01' -A $false -B $true -Parameters $state01))
        $record.phases.Add((Capture-Phase -Name 'state-00' -Label '00' -A $false -B $false -Parameters $state00))
        $record.phases.Add((Capture-Phase -Name 'state-10' -Label '10' -A $true -B $false -Parameters $state10))
    } else {
        $record.phases.Add((Capture-Phase -Name 'state-10' -Label '10' -A $true -B $false -Parameters $state10))
        $record.phases.Add((Capture-Phase -Name 'state-00' -Label '00' -A $false -B $false -Parameters $state00))
        $record.phases.Add((Capture-Phase -Name 'state-01' -Label '01' -A $false -B $true -Parameters $state01))
    }
    $record.phases.Add((Capture-Phase -Name 'state-11-return' -Label '11b' -A $true -B $true -Parameters $state11))

    $restore = Invoke-DevBenchTool -Tool 'communityshaders.controls' -Payload @{
        action = 'restore'; feature = $feature; control = $controlName
    }
    $record.restore = $restore
    $snapshotHeld = [bool]$restore.control.snapshotHeld
    $mutationStarted = $false
    if (-not $restore.restoredSnapshot -or $snapshotHeld) { throw 'Wetness did not restore its held original-state snapshot' }
    $record.validity.accepted = $true
    $record.validity.reasons.Add('IBL remained active for the coupled Wetness review cluster.')
    $record.validity.reasons.Add('Every phase reached exact effective numeric readback after the five-second temporal reset gate.')
    $record.validity.reasons.Add('Every phase saved exact combined and separate-eye pairs with zero failed, incomplete, or backpressured pairs.')
    $record.validity.reasons.Add('The requested time and storm weather were reapplied and read back before every phase.')
    $record.validity.reasons.Add('The exact original Wetness state was restored from the in-memory snapshot.')
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
    if ($hudToggled) {
        try { Invoke-DevBenchTool -Tool 'console' -Payload @{ action = 'exec'; command = 'tm' } | Out-Null }
        catch { Write-Warning "HUD restore failed: $_" }
    }
    $record.finishedAt = [DateTime]::UtcNow.ToString('o')
}

$outputDirectory = Join-Path $OutputRoot $RunId
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$outputPath = Join-Path $outputDirectory 'visual-wetness-factorial-run.json'
$temporaryPath = $outputPath + '.tmp'
[System.IO.File]::WriteAllText($temporaryPath, ($record | ConvertTo-Json -Depth 30), [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryPath -Destination $outputPath -Force

[ordered]@{
    runId = $RunId; accepted = $record.validity.accepted; artifact = $outputPath
    phases = @($record.phases | ForEach-Object {
        [ordered]@{ name = $_.name; state = $_.state; frameCount = $_.frameCount; outputDirectory = $_.outputDirectory }
    })
} | ConvertTo-Json -Depth 12

if ($runFailure) { throw $runFailure }
