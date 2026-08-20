[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputPath,
    [string[]]$AdditionalShaderRoot = @(),
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath($RepositoryRoot)
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "Repository root does not exist: $root"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $root 'docs\development\shader-analysis\shader-manifest.generated.json'
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

function Get-RelativePath([string]$Path) {
    [IO.Path]::GetRelativePath($root, [IO.Path]::GetFullPath($Path)).Replace('\', '/')
}

function Get-NormalizedName([string]$Value) {
    ($Value -replace '[^A-Za-z0-9]', '').ToLowerInvariant()
}

function Get-ShaderStage([IO.FileInfo]$File) {
    if ($File.Extension -ieq '.hlsli') { return 'include' }
    switch -Regex ($File.BaseName) {
        '(?i)CS$' { return 'compute' }
        '(?i)PS$' { return 'pixel' }
        '(?i)VS$' { return 'vertex' }
        '(?i)GS$' { return 'geometry' }
        '(?i)HS$' { return 'hull' }
        '(?i)DS$' { return 'domain' }
        default { return 'unknown' }
    }
}

$featuresRoot = Join-Path $root 'features'
$sourceRoots = [System.Collections.Generic.List[string]]::new()
$sourceRoots.Add('features')
$resolvedShaderRoots = [System.Collections.Generic.List[string]]::new()
$resolvedShaderRoots.Add($featuresRoot)
foreach ($additionalRoot in $AdditionalShaderRoot) {
    $resolved = [IO.Path]::GetFullPath($additionalRoot)
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        throw "Additional shader root does not exist: $resolved"
    }
    $resolvedShaderRoots.Add($resolved)
    $sourceRoots.Add((Get-RelativePath $resolved))
}

$featureIniFiles = @(Get-ChildItem -LiteralPath $featuresRoot -Recurse -File -Filter '*.ini' | Sort-Object FullName)
$featureGroups = @($featureIniFiles | Group-Object {
    $relative = [IO.Path]::GetRelativePath($featuresRoot, $_.FullName)
    $relative.Split([IO.Path]::DirectorySeparatorChar)[0]
} | Sort-Object Name)
$featureSourceFiles = @(Get-ChildItem -LiteralPath (Join-Path $root 'src\Features') -File -ErrorAction SilentlyContinue | Sort-Object FullName)

$features = foreach ($group in $featureGroups) {
    $normalizedCandidates = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $null = $normalizedCandidates.Add((Get-NormalizedName $group.Name))
    foreach ($ini in $group.Group) { $null = $normalizedCandidates.Add((Get-NormalizedName $ini.BaseName)) }
    $sourceCandidates = @($featureSourceFiles | Where-Object {
        $normalizedCandidates.Contains((Get-NormalizedName $_.BaseName))
    } | ForEach-Object { Get-RelativePath $_.FullName } | Sort-Object -Unique)
    [ordered]@{
        id = Get-NormalizedName $group.Name
        displayName = $group.Name
        featureDirectory = "features/$($group.Name)"
        iniPaths = @($group.Group | ForEach-Object { Get-RelativePath $_.FullName } | Sort-Object -Unique)
        sourceCandidates = $sourceCandidates
        stateCapability = [ordered]@{ installed = $true; resident = $null; active = $null }
        shaderDefineScopes = @()
        classificationStatus = 'inventory-only'
        notes = @('HasShaderDefine scope and installed/resident/active behaviour require code and runtime annotation.')
    }
}

$shaderFilesByPath = @{}
foreach ($shaderRoot in $resolvedShaderRoots) {
    foreach ($file in Get-ChildItem -LiteralPath $shaderRoot -Recurse -File | Where-Object { $_.Extension -in '.hlsl', '.hlsli' }) {
        $relative = Get-RelativePath $file.FullName
        $shaderFilesByPath[$relative] = $file
    }
}
$shaders = foreach ($relative in @($shaderFilesByPath.Keys | Sort-Object)) {
    $file = $shaderFilesByPath[$relative]
    $segments = $relative.Split('/')
    $ownerFeature = if ($segments.Count -gt 1 -and $segments[0] -eq 'features') { $segments[1] } else { $null }
    $shadersIndex = [Array]::IndexOf($segments, 'Shaders')
    $family = if ($shadersIndex -ge 0 -and $segments.Count -gt ($shadersIndex + 2) -and $segments[$shadersIndex + 1] -ne 'Features') {
        $segments[$shadersIndex + 1]
    } else { $null }
    [ordered]@{
        id = $relative
        path = $relative
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        ownerFeature = $ownerFeature
        family = $family
        stage = Get-ShaderStage $file
        profile = $null
        entryPoints = @()
        includeClosure = @()
        compileDefines = @()
        pipelines = @()
        resources = [ordered]@{ inputs = @(); outputs = @(); constantBuffers = @(); srvs = @(); uavs = @(); samplers = @() }
        routes = [ordered]@{ flat = $null; vr = $null; stereo = $null; overlay = $null; vendor = $null; compatibility = $null }
        classificationStatus = 'inventory-only'
        notes = @('Entry points, include closure, pipeline membership, resources, and route flags require compile-site or runtime annotation.')
    }
}

$manifest = [ordered]@{
    schemaVersion = 1
    status = 'inventory-only'
    generatedBy = 'tools/generate-shader-manifest.ps1'
    inventory = [ordered]@{
        sourceRoots = @($sourceRoots | Sort-Object -Unique)
        featureCount = @($features).Count
        shaderSourceCount = @($shaders).Count
        passCount = 0
    }
    features = @($features)
    shaders = @($shaders)
    passes = @()
}
$json = ($manifest | ConvertTo-Json -Depth 20) + [Environment]::NewLine

if ($Check) {
    if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
        throw "Generated shader manifest is missing: $OutputPath"
    }
    $current = Get-Content -LiteralPath $OutputPath -Raw
    if ($current.Replace("`r`n", "`n") -ne $json.Replace("`r`n", "`n")) {
        throw 'Generated shader manifest is stale. Run tools/generate-shader-manifest.ps1.'
    }
    [pscustomobject]@{ ok = $true; check = $true; outputPath = $OutputPath; featureCount = @($features).Count; shaderSourceCount = @($shaders).Count } | ConvertTo-Json
    exit 0
}

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
Set-Content -LiteralPath $OutputPath -Value $json -Encoding utf8 -NoNewline
[pscustomobject]@{ ok = $true; check = $false; outputPath = $OutputPath; featureCount = @($features).Count; shaderSourceCount = @($shaders).Count } | ConvertTo-Json
