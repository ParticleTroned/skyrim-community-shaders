[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,

    [string]$ProfilePath = (Join-Path $PSScriptRoot '..\docs\development\preset-automation\imports\20260817-amd-svr-ovr-null.json'),

    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\docs\development\preset-automation\measurements\retrospective\20260817-amd-svr-ovr-null'),

    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$resolvedArchiveRoot = [System.IO.Path]::GetFullPath($ArchiveRoot)
$resolvedProfilePath = [System.IO.Path]::GetFullPath($ProfilePath)
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$profile = Get-Content -Raw -LiteralPath $resolvedProfilePath | ConvertFrom-Json

if ($profile.schemaVersion -ne 1) {
    throw "Unsupported retrospective import profile schema: $($profile.schemaVersion)"
}
if (-not (Test-Path -LiteralPath $resolvedArchiveRoot -PathType Container)) {
    throw "Archive root does not exist: $resolvedArchiveRoot"
}

function Get-OptionalProperty {
    param($Object, [Parameter(Mandatory = $true)][string]$Name)

    if ($null -eq $Object) {
        return $null
    }
    $property = $Object.psobject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    $property.Value
}

function Get-FirstPhase {
    param($Raw)

    $phases = @(Get-OptionalProperty -Object $Raw -Name 'phases')
    if ($phases.Count -eq 0) {
        return $null
    }
    $phases[0]
}

function Get-SceneEnvelope {
    param($Raw)

    $phase = Get-FirstPhase -Raw $Raw
    foreach ($candidate in @(
            (Get-OptionalProperty -Object $phase -Name 'scene'),
            (Get-OptionalProperty -Object $phase -Name 'anchor'),
            (Get-OptionalProperty -Object $Raw -Name 'sceneBefore'),
            (Get-OptionalProperty -Object $Raw -Name 'sceneAfter'))) {
        if ($null -ne $candidate) {
            return $candidate
        }
    }
    $null
}

function Get-SceneValue {
    param($Envelope)

    if ($null -eq $Envelope) {
        return $null
    }
    $scene = Get-OptionalProperty -Object $Envelope -Name 'scene'
    if ($null -ne $scene) {
        return $scene
    }
    $Envelope
}

function Get-FormLabel {
    param($Form)

    if ($null -eq $Form) {
        return 'not recorded'
    }
    $editorId = Get-OptionalProperty -Object $Form -Name 'editorId'
    $formId = Get-OptionalProperty -Object $Form -Name 'formId'
    if ($editorId -and $formId) {
        return "$editorId ($formId)"
    }
    if ($editorId) {
        return [string]$editorId
    }
    if ($formId) {
        return [string]$formId
    }
    'not recorded'
}

function Get-DistillationDocument {
    param([Parameter(Mandatory = $true)][string]$RelativePath, [Parameter(Mandatory = $true)][string]$RunId)

    if ($RelativePath -match 'preset-automation-interactions|preset-interaction-visual') {
        return '../../../interactions-20260817.md'
    }
    if ($RelativePath -match 'preset-automation-curves|preset-automation-visual-curves|preset-automation-sss-factorial|preset-automation-wetness-factorial') {
        return '../../../profile-curves-20260817.md'
    }
    if ($RelativePath -match 'preset-automation-screening|preset-automation-visual/') {
        return '../../../screening-20260817.md'
    }
    if ($RunId -like 'terrain-*') {
        return '../../../terrain-blending-20260817.md'
    }
    if ($RelativePath -match 'preset-automation-visual-motion|[/\\]M[/\\]') {
        if ($RunId -like 'ssgi-*') {
            return '../../../ssgi-volumetric-lighting-20260817.md'
        }
        return '../../../motion-tier-screen-20260817.md'
    }
    if ($RelativePath -match 'preset-automation-parameter-curves|preset-automation-visual-parameter-curves') {
        return '../../../ssgi-volumetric-lighting-20260817.md'
    }
    '../../../README.md'
}

function Get-ControlledVariables {
    param($Raw)

    $values = [System.Collections.Generic.List[string]]::new()
    foreach ($name in @('feature', 'control', 'parameter', 'factorSet')) {
        $value = Get-OptionalProperty -Object $Raw -Name $name
        if ($null -ne $value -and -not [string]::IsNullOrWhiteSpace([string]$value)) {
            $values.Add("$name=$value")
        }
    }
    $factors = Get-OptionalProperty -Object $Raw -Name 'factors'
    if ($null -ne $factors) {
        $factorJson = $factors | ConvertTo-Json -Compress -Depth 8
        $values.Add("factors=$factorJson")
    }
    $values.Add('retrospective import from the immutable archived runner record')
    @($values)
}

function New-MetricDistribution {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        $Distribution,
        [int]$FallbackSamples
    )

    if ($null -eq $Distribution) {
        return $null
    }
    $average = Get-OptionalProperty -Object $Distribution -Name 'average'
    $p95 = Get-OptionalProperty -Object $Distribution -Name 'p95'
    $p99 = Get-OptionalProperty -Object $Distribution -Name 'p99'
    if ($null -eq $average -or $null -eq $p95 -or $null -eq $p99) {
        return $null
    }
    $count = Get-OptionalProperty -Object $Distribution -Name 'count'
    if ($null -eq $count -or [int]$count -lt 1) {
        $count = [Math]::Max($FallbackSamples, 1)
    }
    [ordered]@{
        name = $Name
        unit = 'ms'
        samples = [int]$count
        minimum = Get-OptionalProperty -Object $Distribution -Name 'minimum'
        average = [double]$average
        median = Get-OptionalProperty -Object $Distribution -Name 'median'
        p95 = [double]$p95
        p99 = [double]$p99
        maximum = Get-OptionalProperty -Object $Distribution -Name 'maximum'
    }
}

function New-TimingPass {
    param($Raw, [bool]$SourceAccepted)

    $metrics = [System.Collections.Generic.List[object]]::new()
    $sampleFrames = 0
    foreach ($phase in @(Get-OptionalProperty -Object $Raw -Name 'phases')) {
        $phaseName = [string](Get-OptionalProperty -Object $phase -Name 'name')
        if ([string]::IsNullOrWhiteSpace($phaseName)) {
            $phaseName = 'unnamed-phase'
        }
        $acceptedSamples = Get-OptionalProperty -Object $phase -Name 'acceptedSamples'
        if ($null -eq $acceptedSamples) {
            $acceptedSamples = 0
        }
        $sampleFrames += [int]$acceptedSamples
        $gpu = New-MetricDistribution -Name "$phaseName total GPU" -Distribution (Get-OptionalProperty -Object $phase -Name 'gpu') -FallbackSamples $acceptedSamples
        $cpu = New-MetricDistribution -Name "$phaseName total CPU" -Distribution (Get-OptionalProperty -Object $phase -Name 'cpu') -FallbackSamples $acceptedSamples
        if ($null -ne $gpu) { $metrics.Add($gpu) }
        if ($null -ne $cpu) { $metrics.Add($cpu) }
    }

    [ordered]@{
        kind = 'timing'
        status = if ($SourceAccepted) { 'incomplete' } else { 'invalid' }
        warmupFrames = 0
        sampleFrames = $sampleFrames
        profilerArtifact = 'source-record'
        profilerMetrics = @($metrics)
        capture = $null
        issues = @('Exact full settings snapshot provenance is absent from the archived runner record.')
    }
}

function New-VisualPass {
    param($Raw, [bool]$SourceAccepted)

    $sampleFrames = 0
    $backpressure = 0
    $incompleteStereo = 0
    $phaseCount = 0
    foreach ($phase in @(Get-OptionalProperty -Object $Raw -Name 'phases')) {
        if ($null -eq $phase) { continue }
        $phaseCount++
        $frames = Get-OptionalProperty -Object $phase -Name 'frameCount'
        $backpressureDrops = Get-OptionalProperty -Object $phase -Name 'backpressureDrops'
        $incompleteDrops = Get-OptionalProperty -Object $phase -Name 'incompleteStereoDrops'
        if ($null -ne $frames) { $sampleFrames += [int]$frames }
        if ($null -ne $backpressureDrops) { $backpressure += [int]$backpressureDrops }
        if ($null -ne $incompleteDrops) { $incompleteStereo += [int]$incompleteDrops }
    }
    $complete = $SourceAccepted -and $phaseCount -gt 0 -and $backpressure -eq 0 -and $incompleteStereo -eq 0

    [ordered]@{
        kind = 'visual'
        status = if ($SourceAccepted) { 'incomplete' } else { 'invalid' }
        warmupFrames = 0
        sampleFrames = $sampleFrames
        profilerArtifact = $null
        profilerMetrics = @()
        capture = [ordered]@{
            source = 'hmd_stereo'
            manifestArtifact = 'source-record'
            complete = $complete
            capturedFrameCount = $sampleFrames
            incompleteStereoPairDrops = $incompleteStereo
            queueBackpressureDrops = $backpressure
        }
        issues = @('Exact full settings snapshot provenance is absent from the archived runner record.')
    }
}

$sourceFiles = Get-ChildItem -LiteralPath $resolvedArchiveRoot -Recurse -Force -File -Filter '*.json' |
    Where-Object { $_.Name -match '(run|raw)\.json$' } |
    Sort-Object FullName

$seenRunIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$results = @()
foreach ($sourceFile in $sourceFiles) {
    $raw = Get-Content -Raw -LiteralPath $sourceFile.FullName | ConvertFrom-Json
    $runId = [string](Get-OptionalProperty -Object $raw -Name 'runId')
    if ([string]::IsNullOrWhiteSpace($runId)) {
        continue
    }
    if (-not $seenRunIds.Add($runId)) {
        throw "Duplicate archived runId: $runId"
    }

    $sourceValidity = Get-OptionalProperty -Object $raw -Name 'validity'
    $sourceAccepted = (Get-OptionalProperty -Object $sourceValidity -Name 'accepted') -eq $true
    $relativeSourcePath = [System.IO.Path]::GetRelativePath($resolvedArchiveRoot, $sourceFile.FullName).Replace('\', '/')
    $distillationDocument = Get-DistillationDocument -RelativePath $relativeSourcePath -RunId $runId
    $source = Get-OptionalProperty -Object $raw -Name 'source'
    $envelope = Get-SceneEnvelope -Raw $raw
    $scene = Get-SceneValue -Envelope $envelope
    $cell = Get-OptionalProperty -Object $scene -Name 'cell'
    $worldspace = Get-OptionalProperty -Object $scene -Name 'worldspace'
    $weather = Get-OptionalProperty -Object $scene -Name 'weather'
    $position = Get-OptionalProperty -Object $scene -Name 'position'
    $gameHour = Get-OptionalProperty -Object $scene -Name 'gameHour'
    $daysPassed = Get-OptionalProperty -Object $scene -Name 'daysPassed'
    $sceneId = Get-OptionalProperty -Object $cell -Name 'editorId'
    if ([string]::IsNullOrWhiteSpace([string]$sceneId)) {
        $sceneId = $runId
    }
    $location = "cell=$(Get-FormLabel $cell); worldspace=$(Get-FormLabel $worldspace)"
    if ($null -ne $position) {
        $location += "; position=$($position -join ',')"
    }
    $gameTime = if ($null -ne $gameHour) { "daysPassed=$daysPassed; gameHour=$gameHour" } else { 'not recorded in archived runner record' }
    $isTiming = $sourceFile.Name -match 'profiler-raw\.json$'
    $pass = if ($isTiming) { New-TimingPass -Raw $raw -SourceAccepted $sourceAccepted } else { New-VisualPass -Raw $raw -SourceAccepted $sourceAccepted }
    $rawReasons = @((Get-OptionalProperty -Object $sourceValidity -Name 'reasons')) | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }
    $validityReasons = [System.Collections.Generic.List[string]]::new()
    if ($sourceAccepted) {
        $validityReasons.Add('The original runner marked the archived campaign accepted within its recorded lane.')
        $validityReasons.Add('Canonical acceptance is withheld because the exact complete settings snapshot digest is missing.')
    } else {
        $validityReasons.Add('The original runner rejected or did not accept this campaign; it is retained as negative harness evidence.')
    }
    foreach ($reason in $rawReasons) { $validityReasons.Add([string]$reason) }

    $feature = [string](Get-OptionalProperty -Object $raw -Name 'feature')
    if ([string]::IsNullOrWhiteSpace($feature)) { $feature = 'multi-factor rendering controls' }
    $kind = if ($isTiming) { 'timing' } else { 'visual' }
    $archiveLocator = $profile.archiveLocatorPrefix + $relativeSourcePath
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFile.FullName).Hash
    $recordedAt = Get-OptionalProperty -Object $raw -Name 'finishedAt'
    if ($null -eq $recordedAt) { $recordedAt = Get-OptionalProperty -Object $raw -Name 'startedAt' }
    if ($recordedAt -is [datetime]) {
        $recordedAt = $recordedAt.ToUniversalTime().ToString('o')
    } else {
        $recordedAt = [string]$recordedAt
    }

    $record = [ordered]@{
        '$schema' = '../../../schemas/measurement-run.schema.json'
        schemaVersion = 2
        runId = $runId
        recordedAt = $recordedAt
        status = if ($sourceAccepted) { 'incomplete' } else { 'invalid' }
        purpose = "Retrospectively normalize the $feature $kind campaign while preserving its original runner disposition."
        sourceSnapshot = [ordered]@{
            repository = 'Community Shaders Expanded'
            branch = [string](Get-OptionalProperty -Object $source -Name 'branch')
            commit = [string](Get-OptionalProperty -Object $source -Name 'commit')
            buildConfiguration = [string](Get-OptionalProperty -Object $source -Name 'build')
            dllVersion = 'CSX 3.18-VR'
            dllSha256 = [string](Get-OptionalProperty -Object $source -Name 'dllSha256')
            settingsSha256 = $null
            settingsArtifact = $null
            dependencies = $profile.dependencyFallback
        }
        hardware = $profile.hardware
        runtimeLane = $profile.runtimeLane
        hmd = $profile.hmd
        scene = [ordered]@{
            sceneId = [string]$sceneId
            saveArtifact = $null
            location = $location
            weather = Get-FormLabel $weather
            gameTime = $gameTime
            poseScript = if ($isTiming) { 'Recorded fixed null-HMD scene snapshot; see source record.' } elseif ($sourceFile.Name -match 'motion') { 'Recorded repeatable player-space translation; see source record.' } else { 'Recorded fixed null-HMD capture pose; see source record.' }
            motionProfile = if ($sourceFile.Name -match 'motion') { 'controlled player translation' } else { 'static null-HMD pose' }
            controlledVariables = @(Get-ControlledVariables -Raw $raw)
        }
        passes = @($pass)
        artifacts = @(
            [ordered]@{
                id = 'source-record'
                role = 'archived runner record and campaign manifest'
                locator = $archiveLocator
                mediaType = 'application/json'
                bytes = [int64]$sourceFile.Length
                sha256 = $sourceHash
                retention = 'external-retained'
            }
        )
        comparability = [ordered]@{
            baselineRunId = $null
            scope = 'within-lane'
            controlledVariables = @(Get-ControlledVariables -Raw $raw)
            deviations = @('The exact complete settings snapshot digest was not retained, so this record cannot enter a cross-vendor frontier comparison.')
        }
        validity = [ordered]@{
            accepted = $false
            reasons = @($validityReasons)
        }
        conclusions = @(
            "The archived source runner disposition was $(if ($sourceAccepted) { 'accepted' } else { 'rejected or incomplete' }).",
            "Reviewed interpretation is retained in $distillationDocument.",
            'This retrospective normalization is searchable and machine-readable but is not canonical cross-vendor evidence until the missing settings identity is recovered.'
        )
        provenanceGaps = @($profile.provenanceGaps)
        notes = @('Generated by tools/import-preset-calibration-archive.ps1 from the immutable archived source record.')
    }

    $json = (($record | ConvertTo-Json -Depth 100) -replace "`r`n", "`n") + "`n"
    $outputPath = Join-Path $resolvedOutputRoot "$runId.json"
    if ($Check) {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "Retrospective record is absent: $outputPath"
        }
        if ((Get-Content -Raw -LiteralPath $outputPath) -ne $json) {
            throw "Retrospective record is stale: $outputPath"
        }
        $state = 'verified'
    } else {
        [System.IO.Directory]::CreateDirectory($resolvedOutputRoot) | Out-Null
        [System.IO.File]::WriteAllText($outputPath, $json, [System.Text.UTF8Encoding]::new($false))
        $state = 'generated'
    }
    $results += [ordered]@{ runId = $runId; state = $state; sourceAccepted = $sourceAccepted; sourceSha256 = $sourceHash }
}

$results | ConvertTo-Json -Depth 4
