[CmdletBinding()]
param(
    [string]$PolicyPath = (Join-Path $PSScriptRoot '..\docs\development\unified-preset-policy.json'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\MGO-Presets'),
    [string]$ReportPath = (Join-Path $PSScriptRoot '..\docs\development\generated-unified-preset-report.json'),
    [string]$RefreshBaseFromPath,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedPolicyPath = [System.IO.Path]::GetFullPath($PolicyPath)
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$resolvedReportPath = [System.IO.Path]::GetFullPath($ReportPath)
$policy = Get-Content -Raw -LiteralPath $resolvedPolicyPath | ConvertFrom-Json -Depth 100

if ($policy.schemaVersion -ne 2) {
    throw "Unsupported unified preset policy schema: $($policy.schemaVersion)"
}
if ($Check -and $RefreshBaseFromPath) {
    throw '-Check and -RefreshBaseFromPath cannot be combined.'
}

function Resolve-RepositoryPath {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $normalized = $RelativePath.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $resolved = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $normalized))
    if (-not $resolved.StartsWith($repositoryRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Policy path escaped the repository: $RelativePath"
    }
    $resolved
}

function ConvertTo-CanonicalJson {
    param([Parameter(Mandatory = $true)]$Value)

    (($Value | ConvertTo-Json -Depth 100) -replace "`r`n", "`n") + "`n"
}

function ConvertTo-CanonicalPath {
    param([Parameter(Mandatory = $true)][object[]]$Path)

    (@($Path | ForEach-Object { [string]$_ }) -join '/')
}

function Get-JsonPathValue {
    param(
        [Parameter(Mandatory = $true)]$Root,
        [Parameter(Mandatory = $true)][object[]]$Path
    )

    $current = $Root
    foreach ($segmentValue in $Path) {
        if ($null -eq $current) {
            throw "JSON path has a null parent: $(ConvertTo-CanonicalPath $Path)"
        }
        $segment = [string]$segmentValue
        $property = $current.psobject.Properties[$segment]
        if ($null -eq $property) {
            throw "JSON path is absent: $(ConvertTo-CanonicalPath $Path)"
        }
        $current = $property.Value
    }
    $current
}

function Test-JsonPathPresent {
    param(
        [Parameter(Mandatory = $true)]$Root,
        [Parameter(Mandatory = $true)][object[]]$Path
    )

    try {
        $null = Get-JsonPathValue -Root $Root -Path $Path
        $true
    }
    catch {
        $false
    }
}

function Set-JsonPathValue {
    param(
        [Parameter(Mandatory = $true)]$Root,
        [Parameter(Mandatory = $true)][object[]]$Path,
        [Parameter(Mandatory = $false)]$Value
    )

    if ($Path.Count -lt 1) {
        throw 'An override path must contain at least one segment.'
    }
    $current = $Root
    for ($index = 0; $index -lt $Path.Count - 1; $index++) {
        $segment = [string]$Path[$index]
        $property = $current.psobject.Properties[$segment]
        if ($null -eq $property) {
            $child = [pscustomobject]@{}
            $current | Add-Member -NotePropertyName $segment -NotePropertyValue $child
            $current = $child
        }
        elseif ($null -eq $property.Value) {
            $child = [pscustomobject]@{}
            $property.Value = $child
            $current = $child
        }
        else {
            $current = $property.Value
        }
    }

    $leaf = [string]$Path[-1]
    $leafProperty = $current.psobject.Properties[$leaf]
    if ($null -eq $leafProperty) {
        $current | Add-Member -NotePropertyName $leaf -NotePropertyValue $Value
    }
    else {
        $leafProperty.Value = $Value
    }
}

function Remove-JsonPath {
    param(
        [Parameter(Mandatory = $true)]$Root,
        [Parameter(Mandatory = $true)][object[]]$Path
    )

    if ($Path.Count -lt 1) {
        throw 'A removal path must contain at least one segment.'
    }
    $current = $Root
    for ($index = 0; $index -lt $Path.Count - 1; $index++) {
        $property = $current.psobject.Properties[[string]$Path[$index]]
        if ($null -eq $property -or $null -eq $property.Value) {
            return
        }
        $current = $property.Value
    }
    $current.psobject.Properties.Remove([string]$Path[-1])
}

function Test-EquivalentValue {
    param($Actual, $Expected)

    if ($Actual -is [double] -or $Actual -is [float] -or $Expected -is [double] -or $Expected -is [float]) {
        return [math]::Abs([double]$Actual - [double]$Expected) -le 0.000001
    }
    $Actual -eq $Expected
}

function Assert-CurrentSchema {
    param([Parameter(Mandatory = $true)]$Settings)

    foreach ($stalePath in $policy.validation.stalePaths) {
        if (Test-JsonPathPresent -Root $Settings -Path $stalePath) {
            throw "Stale settings path is present: $(ConvertTo-CanonicalPath $stalePath)"
        }
    }
    foreach ($required in $policy.validation.requiredValues) {
        $actual = Get-JsonPathValue -Root $Settings -Path $required.path
        if (-not (Test-EquivalentValue -Actual $actual -Expected $required.value)) {
            throw "Required settings value mismatch at $(ConvertTo-CanonicalPath $required.path): expected $($required.value), got $actual"
        }
    }
    foreach ($arrayContract in $policy.validation.requiredArrayLengths) {
        $actual = @(Get-JsonPathValue -Root $Settings -Path $arrayContract.path)
        if ($actual.Count -ne $arrayContract.length) {
            throw "Required array length mismatch at $(ConvertTo-CanonicalPath $arrayContract.path): expected $($arrayContract.length), got $($actual.Count)"
        }
    }
}

function Assert-TierContract {
    $actualTierOrder = @($policy.tiers.psobject.Properties.Name)
    $expectedTierOrder = @($policy.tierOrder)
    if ((Compare-Object $expectedTierOrder $actualTierOrder -SyncWindow 0).Count -ne 0) {
        throw "Tier order must be: $($expectedTierOrder -join ', ')"
    }

    $ownedPaths = @($policy.tierOwnedPaths | ForEach-Object { ConvertTo-CanonicalPath $_ })
    if (($ownedPaths | Sort-Object -Unique).Count -ne $ownedPaths.Count) {
        throw 'tierOwnedPaths contains a duplicate path.'
    }
    $ownedPaths = @($ownedPaths | Sort-Object)

    $commonPaths = @($policy.commonOverrides | ForEach-Object { ConvertTo-CanonicalPath $_.path })
    if (($commonPaths | Sort-Object -Unique).Count -ne $commonPaths.Count) {
        throw 'commonOverrides contains a duplicate path.'
    }
    if ((Compare-Object $ownedPaths @($commonPaths | Sort-Object) -IncludeEqual -ExcludeDifferent).Count -ne 0) {
        throw 'A path cannot be owned by both commonOverrides and tierOwnedPaths.'
    }

    foreach ($tierProperty in $policy.tiers.psobject.Properties) {
        $tier = $tierProperty.Name
        $definition = $tierProperty.Value
        $overridePaths = @($definition.overrides | ForEach-Object { ConvertTo-CanonicalPath $_.path })
        if (($overridePaths | Sort-Object -Unique).Count -ne $overridePaths.Count) {
            throw "Tier $tier contains a duplicate override path."
        }
        foreach ($path in $overridePaths) {
            foreach ($prefixPath in $policy.tierForbiddenPrefixes) {
                $prefix = ConvertTo-CanonicalPath $prefixPath
                if ($path -eq $prefix -or $path.StartsWith($prefix + '/', [System.StringComparison]::Ordinal)) {
                    throw "Tier $tier attempts to own common/operational path $path."
                }
            }
        }
        $actual = @($overridePaths | Sort-Object)
        if ((Compare-Object $ownedPaths $actual).Count -ne 0) {
            throw "Tier $tier must override every tier-owned path exactly once."
        }
        foreach ($qualificationReference in $definition.qualificationRefs) {
            if ($null -eq $policy.qualifications.psobject.Properties[[string]$qualificationReference]) {
                throw "Tier $tier references unknown qualification '$qualificationReference'."
            }
        }
    }

    $outputDirectories = @($policy.tiers.psobject.Properties.Value.outputDirectory)
    if (($outputDirectories | Sort-Object -Unique).Count -ne $outputDirectories.Count) {
        throw 'Tier outputDirectory values must be unique.'
    }
    foreach ($outputDirectory in $outputDirectories) {
        if ($outputDirectory -notmatch '^CSX Unified- .+ - Press END on PC to Customize$') {
            throw "Tier outputDirectory does not follow the managed unified preset naming contract: $outputDirectory"
        }
    }
}

function Assert-ManagedOutputSet {
    if (-not (Test-Path -LiteralPath $resolvedOutputRoot -PathType Container)) {
        return
    }

    $expected = @($policy.tiers.psobject.Properties.Value.outputDirectory)
    $actual = @(Get-ChildItem -LiteralPath $resolvedOutputRoot -Directory | Where-Object {
        $_.Name -match '^CSX Unified- .+ - Press END on PC to Customize$'
    } | ForEach-Object { $_.Name })
    $extra = @(Compare-Object -ReferenceObject $expected -DifferenceObject $actual -PassThru | Where-Object { $_ -in $actual })
    if ($extra.Count -gt 0) {
        $noun = if ($extra.Count -eq 1) { 'directory' } else { 'directories' }
        throw "Unmanaged unified preset output ${noun}: $($extra -join ', ')"
    }
}

function Assert-NeutralBase {
    param([Parameter(Mandatory = $true)]$Settings)

    $neutralTier = $policy.tiers.psobject.Properties[[string]$policy.baseNormalization.neutralTier]
    if ($null -eq $neutralTier) {
        throw "Unknown neutral tier: $($policy.baseNormalization.neutralTier)"
    }
    foreach ($override in $neutralTier.Value.overrides) {
        $actual = Get-JsonPathValue -Root $Settings -Path $override.path
        if (-not (Test-EquivalentValue -Actual $actual -Expected $override.value)) {
            throw "Neutral base mismatch at $(ConvertTo-CanonicalPath $override.path): expected $($override.value), got $actual"
        }
    }
}

function Assert-GuardValues {
    param([Parameter(Mandatory = $true)]$Settings)

    foreach ($guard in $policy.guards) {
        $actual = Get-JsonPathValue -Root $Settings -Path $guard.path
        if (-not (Test-EquivalentValue -Actual $actual -Expected $guard.value)) {
            throw "Generated guard mismatch at $(ConvertTo-CanonicalPath $guard.path): expected $($guard.value), got $actual"
        }
    }
}

function Get-GeneratedMetaIni {
    param(
        [Parameter(Mandatory = $true)][string]$Tier,
        [Parameter(Mandatory = $true)]$Definition
    )

    $slug = $Tier.Replace(' ', '-')
    $qualificationSummary = @($Definition.qualificationRefs) -join ', '
    @"
[General]
gameName=SkyrimVR
modid=0
version=$($policy.packageVersion)
newestVersion=
category="-1,"
nexusFileStatus=1
installationFile=CSX-Unified-$slug-Provisional.7z
repository=Nexus
ignoredVersion=
comments=WABBAJACK_ALWAYS_ENABLE
notes=PROVISIONAL unified $Tier preset generated from policy schema v2; qualification=$qualificationSummary
nexusDescription=
url=
hasCustomURL=false
nexusLastModified=$($policy.generatedAtEvidenceDate)T00:00:00Z
converted=false
validated=false
color=@Variant(\0\0\0\x43\0\xff\xff\0\0\0\0\0\0\0\0\0)
tracked=0
nexusCategory=0

[installedFiles]
1\modid=0
1\fileid=0
size=1
"@
}

Assert-TierContract
Assert-ManagedOutputSet
$basePath = Resolve-RepositoryPath -RelativePath $policy.baseTemplate.path

if ($RefreshBaseFromPath) {
    $sourcePath = [System.IO.Path]::GetFullPath($RefreshBaseFromPath)
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Refresh source does not exist: $sourcePath"
    }
    $settings = Get-Content -Raw -LiteralPath $sourcePath | ConvertFrom-Json -Depth 100
    foreach ($path in $policy.baseNormalization.removePaths) {
        Remove-JsonPath -Root $settings -Path $path
    }
    foreach ($override in $policy.baseNormalization.overrides) {
        Set-JsonPathValue -Root $settings -Path $override.path -Value $override.value
    }
    $neutralTier = $policy.tiers.psobject.Properties[[string]$policy.baseNormalization.neutralTier]
    if ($null -eq $neutralTier) {
        throw "Unknown neutral tier: $($policy.baseNormalization.neutralTier)"
    }
    foreach ($override in $neutralTier.Value.overrides) {
        Set-JsonPathValue -Root $settings -Path $override.path -Value $override.value
    }
    Assert-CurrentSchema -Settings $settings
    Assert-NeutralBase -Settings $settings

    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $basePath)) | Out-Null
    [System.IO.File]::WriteAllText($basePath, (ConvertTo-CanonicalJson $settings), [System.Text.UTF8Encoding]::new($false))
    [ordered]@{
        state = 'base-refreshed'
        path = $policy.baseTemplate.path
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $basePath).Hash
    } | ConvertTo-Json
    exit 0
}

if (-not (Test-Path -LiteralPath $basePath -PathType Leaf)) {
    throw "Unified preset base is absent: $basePath"
}
$baseHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $basePath).Hash
if ($baseHash -ne $policy.baseTemplate.sha256) {
    throw "Pinned unified base changed: expected $($policy.baseTemplate.sha256), got $baseHash"
}
$baseSettings = Get-Content -Raw -LiteralPath $basePath | ConvertFrom-Json -Depth 100
Assert-CurrentSchema -Settings $baseSettings
Assert-NeutralBase -Settings $baseSettings

$results = @()
foreach ($tierProperty in $policy.tiers.psobject.Properties) {
    $tier = $tierProperty.Name
    $definition = $tierProperty.Value
    if ($definition.outputDirectory -match '(?i)AMD|NVIDIA') {
        throw "Unified output directory contains a vendor name: $($definition.outputDirectory)"
    }

    $settings = (ConvertTo-CanonicalJson $baseSettings) | ConvertFrom-Json -Depth 100
    foreach ($override in @($policy.commonOverrides) + @($definition.overrides)) {
        Set-JsonPathValue -Root $settings -Path $override.path -Value $override.value
    }
    Assert-CurrentSchema -Settings $settings
    Assert-GuardValues -Settings $settings

    $json = ConvertTo-CanonicalJson $settings
    $meta = Get-GeneratedMetaIni -Tier $tier -Definition $definition
    $outputDirectory = [System.IO.Path]::GetFullPath((Join-Path $resolvedOutputRoot $definition.outputDirectory))
    if (-not $outputDirectory.StartsWith($resolvedOutputRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Output directory escaped the output root: $outputDirectory"
    }
    $settingsPath = Join-Path $outputDirectory 'SKSE\Plugins\CommunityShaders\SettingsUser.json'
    $metaPath = Join-Path $outputDirectory 'meta.ini'

    if ($Check) {
        if (-not (Test-Path -LiteralPath $settingsPath) -or -not (Test-Path -LiteralPath $metaPath)) {
            throw "Generated $tier preset is absent. Run this tool without -Check first."
        }
        if ((Get-Content -Raw -LiteralPath $settingsPath) -ne $json) {
            throw "Generated $tier SettingsUser.json is stale."
        }
        if ((Get-Content -Raw -LiteralPath $metaPath) -ne $meta) {
            throw "Generated $tier meta.ini is stale."
        }
        $state = 'verified'
    }
    else {
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $settingsPath)) | Out-Null
        [System.IO.File]::WriteAllText($settingsPath, $json, [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText($metaPath, $meta, [System.Text.UTF8Encoding]::new($false))
        $state = 'generated'
    }

    $results += [ordered]@{
        tier = $tier
        state = $state
        outputDirectory = $definition.outputDirectory
        settingsSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $settingsPath).Hash
        qualificationRefs = @($definition.qualificationRefs)
    }
}

$report = [ordered]@{
    schemaVersion = 1
    policySchemaVersion = $policy.schemaVersion
    policyStatus = $policy.status
    evidenceDate = $policy.generatedAtEvidenceDate
    baseTemplate = $policy.baseTemplate.path
    baseSha256 = $baseHash
    tiers = @($results | ForEach-Object {
        [ordered]@{
            tier = $_.tier
            outputDirectory = $_.outputDirectory
            settingsSha256 = $_.settingsSha256
            qualificationRefs = @($_.qualificationRefs)
        }
    })
    qualifications = $policy.qualifications
}
$reportJson = ConvertTo-CanonicalJson $report
if ($Check) {
    if (-not (Test-Path -LiteralPath $resolvedReportPath -PathType Leaf)) {
        throw 'Generated unified preset report is absent.'
    }
    if ((Get-Content -Raw -LiteralPath $resolvedReportPath) -ne $reportJson) {
        throw 'Generated unified preset report is stale.'
    }
}
else {
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $resolvedReportPath)) | Out-Null
    [System.IO.File]::WriteAllText($resolvedReportPath, $reportJson, [System.Text.UTF8Encoding]::new($false))
}

$results | ConvertTo-Json -Depth 10
