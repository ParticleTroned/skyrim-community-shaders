[CmdletBinding()]
param(
    [string]$PolicyPath = (Join-Path $PSScriptRoot '..\docs\development\preset-automation\style-prototype-policy.json'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\MGO-Presets'),
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedPolicyPath = [System.IO.Path]::GetFullPath($PolicyPath)
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$policy = Get-Content -Raw -LiteralPath $resolvedPolicyPath | ConvertFrom-Json

if ($policy.schemaVersion -ne 1) {
    throw "Unsupported style prototype policy schema: $($policy.schemaVersion)"
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
    for ($index = 0; $index -lt $Path.Count; $index++) {
        if ($null -eq $current) {
            throw "JSON path has a null parent: $($Path -join ' / ')"
        }
        $segment = $Path[$index]
        if ($current -is [System.Array]) {
            $arrayIndex = 0
            if (-not [int]::TryParse([string]$segment, [ref]$arrayIndex) -or $arrayIndex -lt 0 -or $arrayIndex -ge $current.Count) {
                throw "JSON array index is absent: $($Path[0..$index] -join ' / ')"
            }
            $current = $current[$arrayIndex]
        } else {
            $property = $current.psobject.Properties[[string]$segment]
            if ($null -eq $property) {
                throw "JSON path is absent: $($Path[0..$index] -join ' / ')"
            }
            $current = $property.Value
        }
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
        $segment = $Path[$index]
        if ($current -is [System.Array]) {
            $arrayIndex = 0
            if (-not [int]::TryParse([string]$segment, [ref]$arrayIndex) -or $arrayIndex -lt 0 -or $arrayIndex -ge $current.Count) {
                throw "JSON array index is absent: $($Path[0..$index] -join ' / ')"
            }
            $current = $current[$arrayIndex]
        } else {
            $property = $current.psobject.Properties[[string]$segment]
            if ($null -eq $property) {
                throw "JSON path is absent: $($Path[0..$index] -join ' / ')"
            }
            $current = $property.Value
        }
    }

    $leaf = $Path[-1]
    if ($current -is [System.Array]) {
        $arrayIndex = 0
        if (-not [int]::TryParse([string]$leaf, [ref]$arrayIndex) -or $arrayIndex -lt 0 -or $arrayIndex -ge $current.Count) {
            throw "JSON array index is absent: $($Path -join ' / ')"
        }
        $current[$arrayIndex] = $Value
        return
    }

    $leafProperty = $current.psobject.Properties[[string]$leaf]
    if ($null -eq $leafProperty) {
        throw "JSON override may not create a schema field: $($Path -join ' / ')"
    }
    $leafProperty.Value = $Value
}

function Test-EquivalentValue {
    param($Actual, $Expected)

    if ($Actual -is [double] -or $Actual -is [float] -or $Expected -is [double] -or $Expected -is [float]) {
        return [math]::Abs([double]$Actual - [double]$Expected) -le 0.000001
    }
    $Actual -eq $Expected
}

function Test-AllowedPath {
    param([Parameter(Mandatory = $true)][object[]]$Path)

    foreach ($prefix in $policy.allowedPathPrefixes) {
        if ($Path.Count -lt $prefix.Count) {
            continue
        }
        $matches = $true
        for ($index = 0; $index -lt $prefix.Count; $index++) {
            if ([string]$Path[$index] -ne [string]$prefix[$index]) {
                $matches = $false
                break
            }
        }
        if ($matches) {
            return $true
        }
    }
    $false
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
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)][string]$DisplayName
    )

    $version = 'd2026.08.17.0'
    $installationFile = "CSX-Style-$Id-Balanced-Provisional.7z"
    $notes = "PROVISIONAL $DisplayName Balanced style prototype generated from docs/development/preset-automation/style-prototype-policy.json"
    if ($null -ne $policy.artifact) {
        if ($policy.artifact.version) { $version = [string]$policy.artifact.version }
        if ($policy.artifact.installationPrefix) { $installationFile = "$($policy.artifact.installationPrefix)-$Id-Balanced.7z" }
        if ($policy.artifact.classification -and $policy.artifact.sourcePolicy) {
            $notes = "$($policy.artifact.classification) $DisplayName Balanced style artifact generated from $($policy.artifact.sourcePolicy)"
        }
    }

    @"
[General]
gameName=SkyrimVR
modid=0
version=$version
newestVersion=
category="-1,"
nexusFileStatus=1
installationFile=$installationFile
repository=Nexus
ignoredVersion=
comments=WABBAJACK_ALWAYS_ENABLE
notes=$notes
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

$basePath = Resolve-RepositoryPath -RelativePath $policy.basePreset.settingsPath
$baseHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $basePath).Hash
if ($baseHash -ne $policy.basePreset.settingsSha256) {
    throw "Pinned Balanced base changed: expected $($policy.basePreset.settingsSha256), got $baseHash"
}
$baseJson = Get-Content -Raw -LiteralPath $basePath

$results = @()
foreach ($style in $policy.styles) {
    if ($style.outputDirectory -match '(?i)AMD|NVIDIA') {
        throw "Style output directory contains a vendor name: $($style.outputDirectory)"
    }

    # The JSON round-trip is the intentional deep copy. The serialized output
    # below uses the same canonical formatting as the unified preset generator.
    $settings = $baseJson | ConvertFrom-Json
    foreach ($override in $style.overrides) {
        if (-not (Test-AllowedPath -Path $override.path)) {
            throw "Style override escaped the approved rendering surface: $($override.path -join ' / ')"
        }
        $actualBase = Get-JsonPathValue -Root $settings -Path $override.path
        if (-not (Test-EquivalentValue -Actual $actualBase -Expected $override.expectedBase)) {
            throw "Style override base mismatch at $($override.path -join ' / '): expected $($override.expectedBase), got $actualBase"
        }
        Set-JsonPathValue -Root $settings -Path $override.path -Value $override.value
    }
    Assert-GuardValues -Settings $settings

    $json = (($settings | ConvertTo-Json -Depth 100) -replace "`r`n", "`n") + "`n"
    $meta = Get-GeneratedMetaIni -Id $style.id -DisplayName $style.displayName
    $outputDirectory = [System.IO.Path]::GetFullPath((Join-Path $resolvedOutputRoot $style.outputDirectory))
    if (-not $outputDirectory.StartsWith($resolvedOutputRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Output directory escaped the output root: $outputDirectory"
    }
    $settingsPath = Join-Path $outputDirectory 'SKSE\Plugins\CommunityShaders\SettingsUser.json'
    $metaPath = Join-Path $outputDirectory 'meta.ini'

    if ($Check) {
        if (-not (Test-Path -LiteralPath $settingsPath) -or -not (Test-Path -LiteralPath $metaPath)) {
            throw "Generated $($style.displayName) preset is absent. Run this tool without -Check first."
        }
        if ((Get-Content -Raw -LiteralPath $settingsPath) -ne $json) {
            throw "Generated $($style.displayName) SettingsUser.json is stale."
        }
        if ((Get-Content -Raw -LiteralPath $metaPath) -ne $meta) {
            throw "Generated $($style.displayName) meta.ini is stale."
        }
        $state = 'verified'
    } else {
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $settingsPath)) | Out-Null
        [System.IO.File]::WriteAllText($settingsPath, $json, [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText($metaPath, $meta, [System.Text.UTF8Encoding]::new($false))
        $state = 'generated'
    }

    $results += [ordered]@{
        id = $style.id
        displayName = $style.displayName
        state = $state
        outputDirectory = $outputDirectory
        settingsSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $settingsPath).Hash
        baseSettingsSha256 = $baseHash
        overrideCount = @($style.overrides).Count
    }
}

$results | ConvertTo-Json -Depth 5
