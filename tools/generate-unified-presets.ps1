[CmdletBinding()]
param(
    [string]$PolicyPath = (Join-Path $PSScriptRoot '..\docs\development\unified-preset-policy.json'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\MGO-Presets'),
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedPolicyPath = [System.IO.Path]::GetFullPath($PolicyPath)
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$policy = Get-Content -Raw -LiteralPath $resolvedPolicyPath | ConvertFrom-Json

if ($policy.schemaVersion -ne 1) {
    throw "Unsupported unified preset policy schema: $($policy.schemaVersion)"
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

function Get-JsonPathValue {
    param(
        [Parameter(Mandatory = $true)]$Root,
        [Parameter(Mandatory = $true)][object[]]$Path
    )

    $current = $Root
    foreach ($segment in $Path) {
        if ($null -eq $current) {
            throw "JSON path has a null parent: $($Path -join ' / ')"
        }
        $property = $current.psobject.Properties[[string]$segment]
        if ($null -eq $property) {
            throw "JSON path is absent: $($Path -join ' / ')"
        }
        $current = $property.Value
    }
    $current
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
        } elseif ($null -eq $property.Value) {
            $child = [pscustomobject]@{}
            $property.Value = $child
            $current = $child
        } else {
            $current = $property.Value
        }
    }

    $leaf = [string]$Path[-1]
    $leafProperty = $current.psobject.Properties[$leaf]
    if ($null -eq $leafProperty) {
        $current | Add-Member -NotePropertyName $leaf -NotePropertyValue $Value
    } else {
        $leafProperty.Value = $Value
    }
}

function Test-EquivalentValue {
    param($Actual, $Expected)

    if ($Actual -is [double] -or $Actual -is [float] -or $Expected -is [double] -or $Expected -is [float]) {
        return [math]::Abs([double]$Actual - [double]$Expected) -le 0.000001
    }
    $Actual -eq $Expected
}

function Assert-GuardValues {
    param([Parameter(Mandatory = $true)]$Settings)

    foreach ($guard in $policy.guards) {
        $actual = Get-JsonPathValue -Root $Settings -Path $guard.path
        if (-not (Test-EquivalentValue -Actual $actual -Expected $guard.value)) {
            throw "Generated guard mismatch at $($guard.path -join ' / '): expected $($guard.value), got $actual"
        }
    }
}

function Get-GeneratedMetaIni {
    param([Parameter(Mandatory = $true)][string]$Tier)

    @"
[General]
gameName=SkyrimVR
modid=0
version=d2026.08.17.0
newestVersion=
category="-1,"
nexusFileStatus=1
installationFile=CSX-Unified-$Tier-Provisional.7z
repository=Nexus
ignoredVersion=
comments=WABBAJACK_ALWAYS_ENABLE
notes=PROVISIONAL unified $Tier preset generated from docs/development/unified-preset-policy.json
nexusDescription=
url=
hasCustomURL=false
lastNexusQuery=
lastNexusUpdate=
nexusLastModified=2026-08-17T00:00:00Z
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

$results = @()
foreach ($tierProperty in $policy.tiers.psobject.Properties) {
    $tier = $tierProperty.Name
    $definition = $tierProperty.Value
    if ($definition.outputDirectory -match '(?i)AMD|NVIDIA') {
        throw "Unified output directory contains a vendor name: $($definition.outputDirectory)"
    }

    $templatePath = Resolve-RepositoryPath -RelativePath $definition.sourceTemplate
    $templateHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $templatePath).Hash
    if ($templateHash -ne $definition.sourceSha256) {
        throw "Pinned $tier template changed: expected $($definition.sourceSha256), got $templateHash"
    }

    $settings = Get-Content -Raw -LiteralPath $templatePath | ConvertFrom-Json
    foreach ($override in @($policy.schemaDefaults) + @($policy.commonOverrides) + @($definition.overrides)) {
        Set-JsonPathValue -Root $settings -Path $override.path -Value $override.value
    }
    Assert-GuardValues -Settings $settings

    $json = (($settings | ConvertTo-Json -Depth 100) -replace "`r`n", "`n") + "`n"
    $meta = Get-GeneratedMetaIni -Tier $tier
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
    } else {
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $settingsPath)) | Out-Null
        [System.IO.File]::WriteAllText($settingsPath, $json, [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText($metaPath, $meta, [System.Text.UTF8Encoding]::new($false))
        $state = 'generated'
    }

    $results += [ordered]@{
        tier = $tier
        state = $state
        outputDirectory = $outputDirectory
        settingsSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $settingsPath).Hash
        templateSha256 = $templateHash
    }
}

$results | ConvertTo-Json -Depth 5
