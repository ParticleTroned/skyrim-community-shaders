[CmdletBinding()]
param(
    [string]$MeasurementsRoot = (Join-Path $PSScriptRoot '..\docs\development\preset-automation\measurements'),
    [string]$SchemaPath = (Join-Path $PSScriptRoot '..\docs\development\preset-automation\schemas\measurement-run.schema.json'),
    [switch]$WriteIndex
)

$ErrorActionPreference = 'Stop'
$resolvedMeasurementsRoot = [System.IO.Path]::GetFullPath($MeasurementsRoot)
$resolvedSchemaPath = [System.IO.Path]::GetFullPath($SchemaPath)
$indexPath = Join-Path $resolvedMeasurementsRoot 'corpus-index.json'

function Test-AbsoluteLocator {
    param([string]$Locator)

    if ([string]::IsNullOrWhiteSpace($Locator)) { return $false }
    $Locator -match '^[A-Za-z]:[\\/]' -or $Locator -match '^\\\\'
}

$templateFiles = Get-ChildItem -LiteralPath $resolvedMeasurementsRoot -Recurse -Force -File -Filter '*.template.json'
foreach ($templateFile in $templateFiles) {
    if (-not (Test-Json -LiteralPath $templateFile.FullName -SchemaFile $resolvedSchemaPath -ErrorAction Stop)) {
        throw "Template failed measurement schema validation: $($templateFile.FullName)"
    }
}

$recordFiles = Get-ChildItem -LiteralPath $resolvedMeasurementsRoot -Recurse -Force -File -Filter '*.json' |
    Where-Object { $_.Name -notlike '*.template.json' -and $_.FullName -ne $indexPath } |
    Sort-Object FullName

$seenRunIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$indexRecords = @()
foreach ($recordFile in $recordFiles) {
    $recordJson = Get-Content -Raw -LiteralPath $recordFile.FullName
    if (-not (Test-Json -Json $recordJson -SchemaFile $resolvedSchemaPath -ErrorAction Stop)) {
        throw "Record failed measurement schema validation: $($recordFile.FullName)"
    }
    $record = $recordJson | ConvertFrom-Json
    if (-not $seenRunIds.Add([string]$record.runId)) {
        throw "Duplicate measurement runId: $($record.runId)"
    }
    if ($recordFile.BaseName -ne $record.runId) {
        throw "Measurement filename must equal runId: $($recordFile.FullName) vs $($record.runId)"
    }
    if ($recordJson -notmatch '"recordedAt"\s*:\s*"\d{4}-\d{2}-\d{2}T') {
        throw "recordedAt must retain an ISO 8601 date-time with an explicit date and T separator: $($record.runId)"
    }
    if ($record.status -eq 'valid' -and $record.validity.accepted -ne $true) {
        throw "Valid record must have validity.accepted=true: $($record.runId)"
    }
    if ($record.status -in @('invalid', 'incomplete') -and $record.validity.accepted -ne $false) {
        throw "$($record.status) record must have validity.accepted=false: $($record.runId)"
    }

    $artifactIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($artifact in $record.artifacts) {
        if (-not $artifactIds.Add([string]$artifact.id)) {
            throw "Duplicate artifact id '$($artifact.id)' in $($record.runId)"
        }
        if (Test-AbsoluteLocator -Locator $artifact.locator) {
            throw "Artifact locator must be stable and non-local; use localPath for machine convenience: $($record.runId) / $($artifact.id)"
        }
    }
    foreach ($pass in $record.passes) {
        if ($null -ne $pass.profilerArtifact -and -not $artifactIds.Contains([string]$pass.profilerArtifact)) {
            throw "Unknown profilerArtifact '$($pass.profilerArtifact)' in $($record.runId)"
        }
        if ($null -ne $pass.capture -and -not $artifactIds.Contains([string]$pass.capture.manifestArtifact)) {
            throw "Unknown capture manifestArtifact '$($pass.capture.manifestArtifact)' in $($record.runId)"
        }
    }

    $relativePath = [System.IO.Path]::GetRelativePath($resolvedMeasurementsRoot, $recordFile.FullName).Replace('\', '/')
    $provenanceGaps = @()
    if ($null -ne $record.psobject.Properties['provenanceGaps']) {
        $provenanceGaps = @($record.provenanceGaps)
    }
    $indexRecords += [ordered]@{
        runId = $record.runId
        path = $relativePath
        recordedAt = $record.recordedAt
        status = $record.status
        accepted = $record.validity.accepted
        gpuVendor = $record.hardware.gpuVendor
        adapterName = $record.hardware.adapterName
        runtimeLane = $record.runtimeLane.laneId
        applicationVrApi = $record.runtimeLane.applicationVrApi
        hmdMode = $record.hmd.mode
        passKinds = @($record.passes.kind)
        sourceCommit = $record.sourceSnapshot.commit
        dllSha256 = $record.sourceSnapshot.dllSha256
        settingsSha256 = $record.sourceSnapshot.settingsSha256
        provenanceGaps = $provenanceGaps
    }
}

$indexRecords = @($indexRecords | Sort-Object runId)
$index = [ordered]@{
    schemaVersion = 1
    recordCount = $indexRecords.Count
    validCount = @($indexRecords | Where-Object status -eq 'valid').Count
    incompleteCount = @($indexRecords | Where-Object status -eq 'incomplete').Count
    invalidCount = @($indexRecords | Where-Object status -eq 'invalid').Count
    plannedCount = @($indexRecords | Where-Object status -eq 'planned').Count
    vendors = @($indexRecords.gpuVendor | Sort-Object -Unique)
    records = $indexRecords
}
$indexJson = (($index | ConvertTo-Json -Depth 20) -replace "`r`n", "`n") + "`n"

if ($WriteIndex) {
    [System.IO.File]::WriteAllText($indexPath, $indexJson, [System.Text.UTF8Encoding]::new($false))
} else {
    if (-not (Test-Path -LiteralPath $indexPath)) {
        throw "Corpus index is absent. Run this tool once with -WriteIndex."
    }
    if ((Get-Content -Raw -LiteralPath $indexPath) -ne $indexJson) {
        throw "Corpus index is stale. Run this tool with -WriteIndex and commit the result."
    }
}

[ordered]@{
    records = $index.recordCount
    valid = $index.validCount
    incomplete = $index.incompleteCount
    invalid = $index.invalidCount
    planned = $index.plannedCount
    templates = $templateFiles.Count
    index = $indexPath
    state = if ($WriteIndex) { 'written' } else { 'verified' }
} | ConvertTo-Json -Depth 4
