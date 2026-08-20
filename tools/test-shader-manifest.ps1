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

$failures = [System.Collections.Generic.List[string]]::new()
function Assert-Manifest([bool]$Condition, [string]$Message) {
    if (-not $Condition) { $failures.Add($Message) }
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$schema = Get-Content -LiteralPath $SchemaPath -Raw | ConvertFrom-Json

Assert-Manifest ($schema.'$schema' -eq 'https://json-schema.org/draft/2020-12/schema') 'Schema draft is not 2020-12.'
Assert-Manifest ($schema.'$id' -eq 'urn:csx:schema:shader-manifest:1') 'Schema ID is not the stable CSX URN.'
Assert-Manifest ($manifest.schemaVersion -eq 1) 'Manifest schemaVersion is not 1.'
Assert-Manifest ($manifest.status -eq 'inventory-only') 'Generated manifest must be inventory-only.'
Assert-Manifest ($manifest.generatedBy -eq 'tools/generate-shader-manifest.ps1') 'Unexpected manifest generator identity.'

$requiredInventoryFields = @('sourceRoots', 'featureCount', 'shaderSourceCount', 'passCount')
foreach ($field in $requiredInventoryFields) {
    Assert-Manifest ($null -ne $manifest.inventory.PSObject.Properties[$field]) "Inventory field is missing: $field"
}

$featuresRoot = Join-Path $root 'features'
$expectedFeatureIds = @(Get-ChildItem -LiteralPath $featuresRoot -Recurse -File -Filter '*.ini' | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($featuresRoot, $_.FullName)
    $relative.Split([IO.Path]::DirectorySeparatorChar)[0]
} | Sort-Object -Unique)
$expectedShaders = @(Get-ChildItem -LiteralPath $featuresRoot -Recurse -File | Where-Object { $_.Extension -in '.hlsl', '.hlsli' })

Assert-Manifest ($manifest.inventory.featureCount -eq $expectedFeatureIds.Count) 'Feature count does not match the source tree.'
Assert-Manifest ($manifest.inventory.shaderSourceCount -eq $expectedShaders.Count) 'Shader source count does not match the source tree.'
Assert-Manifest ($manifest.inventory.passCount -eq @($manifest.passes).Count) 'Pass count does not match the pass records.'
Assert-Manifest (@($manifest.features).Count -eq $manifest.inventory.featureCount) 'Feature record count does not match inventory.featureCount.'
Assert-Manifest (@($manifest.shaders).Count -eq $manifest.inventory.shaderSourceCount) 'Shader record count does not match inventory.shaderSourceCount.'

$featureIds = @($manifest.features | ForEach-Object { $_.id })
Assert-Manifest (@($featureIds | Sort-Object -Unique).Count -eq $featureIds.Count) 'Feature IDs are not unique.'
foreach ($feature in $manifest.features) {
    Assert-Manifest ($feature.classificationStatus -eq 'inventory-only') "Generated feature is not inventory-only: $($feature.id)"
    Assert-Manifest (Test-Path -LiteralPath (Join-Path $root $feature.featureDirectory) -PathType Container) "Feature directory is missing: $($feature.featureDirectory)"
    foreach ($iniPath in $feature.iniPaths) {
        Assert-Manifest (Test-Path -LiteralPath (Join-Path $root $iniPath) -PathType Leaf) "Feature INI is missing: $iniPath"
    }
}

$shaderPaths = @($manifest.shaders | ForEach-Object { $_.path })
Assert-Manifest (@($shaderPaths | Sort-Object -Unique).Count -eq $shaderPaths.Count) 'Shader paths are not unique.'
$requiredResourceFields = @('inputs', 'outputs', 'constantBuffers', 'srvs', 'uavs', 'samplers')
$requiredRouteFields = @('flat', 'vr', 'stereo', 'overlay', 'vendor', 'compatibility')
foreach ($shader in $manifest.shaders) {
    $sourcePath = Join-Path $root $shader.path
    Assert-Manifest (Test-Path -LiteralPath $sourcePath -PathType Leaf) "Shader source is missing: $($shader.path)"
    if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
        $actualHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
        Assert-Manifest ($shader.sha256 -eq $actualHash) "Shader hash is stale: $($shader.path)"
    }
    Assert-Manifest ($shader.classificationStatus -eq 'inventory-only') "Generated shader is not inventory-only: $($shader.path)"
    foreach ($field in $requiredResourceFields) {
        Assert-Manifest ($null -ne $shader.resources.PSObject.Properties[$field]) "Shader resource field is missing ($field): $($shader.path)"
    }
    foreach ($field in $requiredRouteFields) {
        Assert-Manifest ($null -ne $shader.routes.PSObject.Properties[$field]) "Shader route field is missing ($field): $($shader.path)"
    }
}

$pwsh = (Get-Process -Id $PID).Path
$generator = Join-Path $root 'tools\generate-shader-manifest.ps1'
$checkOutput = & $pwsh -NoProfile -File $generator -RepositoryRoot $root -OutputPath $ManifestPath -Check 2>&1
Assert-Manifest ($LASTEXITCODE -eq 0) "Deterministic freshness check failed: $($checkOutput -join ' ')"

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

[pscustomobject]@{
    ok = $true
    featureCount = @($manifest.features).Count
    shaderSourceCount = @($manifest.shaders).Count
    passCount = @($manifest.passes).Count
    deterministicCheck = $true
} | ConvertTo-Json
