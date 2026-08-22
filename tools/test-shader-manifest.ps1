[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$ManifestPath,
    [string]$SchemaPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath($RepositoryRoot)
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $root 'docs\development\shader-analysis\shader-manifest.generated.json'
}
if ([string]::IsNullOrWhiteSpace($SchemaPath)) {
    $SchemaPath = Join-Path $root 'docs\development\shader-analysis\shader-manifest.schema.json'
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$schema = Get-Content -LiteralPath $SchemaPath -Raw | ConvertFrom-Json
$failures = [System.Collections.Generic.List[string]]::new()
function Assert-Manifest([bool]$Condition, [string]$Message) {
    if (-not $Condition) { $failures.Add($Message) }
}

Assert-Manifest ($schema.'$schema' -eq 'https://json-schema.org/draft/2020-12/schema') 'Schema draft is not 2020-12.'
Assert-Manifest ($schema.'$id' -eq 'urn:csx:schema:shader-dependency-manifest:2') 'Schema ID is not the v2 dependency-manifest URN.'
Assert-Manifest ($manifest.schemaVersion -eq 2) 'Manifest schemaVersion is not 2.'
Assert-Manifest ($manifest.status -eq 'static-classified') 'Manifest is not statically classified.'
Assert-Manifest ($manifest.generatedBy -eq 'tools/shader_dependency_manifest.py') 'Unexpected generator identity.'
Assert-Manifest (@($manifest.sources).Count -eq $manifest.inventory.virtualSourceCount) 'Source count is inconsistent.'
Assert-Manifest (@($manifest.compileUnits).Count -eq $manifest.inventory.compileUnitCount) 'Compile-unit count is inconsistent.'
Assert-Manifest (@($manifest.passes).Count -eq $manifest.inventory.passCount) 'Pass count is inconsistent.'
Assert-Manifest ($manifest.inventory.unacceptedUnresolvedIncludeCount -eq 0) 'Unaccepted unresolved includes remain.'
Assert-Manifest ($manifest.inventory.unresolvedCompileSiteCount -eq 0) 'Unresolved compile sites remain.'
Assert-Manifest ($manifest.inventory.unclassifiedProductionEntryCount -eq 0) 'Unclassified production entries remain.'

$sourcePaths = @($manifest.sources | ForEach-Object { $_.virtualPath })
$unitIds = @($manifest.compileUnits | ForEach-Object { $_.id })
$passIds = @($manifest.passes | ForEach-Object { $_.id })
Assert-Manifest (@($sourcePaths | Sort-Object -Unique).Count -eq $sourcePaths.Count) 'Source virtual paths are not unique.'
Assert-Manifest (@($unitIds | Sort-Object -Unique).Count -eq $unitIds.Count) 'Compile-unit IDs are not unique.'
Assert-Manifest (@($passIds | Sort-Object -Unique).Count -eq $passIds.Count) 'Pass IDs are not unique.'

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

$generator = Join-Path $root 'tools\generate-shader-manifest.ps1'
& $generator -RepositoryRoot $root -OutputPath $ManifestPath -Check
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

[pscustomobject]@{
    ok = $true
    schemaVersion = $manifest.schemaVersion
    sourceCount = @($manifest.sources).Count
    compileUnitCount = @($manifest.compileUnits).Count
    passCount = @($manifest.passes).Count
    deterministicCheck = $true
} | ConvertTo-Json
