[CmdletBinding()]
param(
    [string]$PolicyPath = (Join-Path $PSScriptRoot '..\docs\development\unified-preset-policy.json'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\MGO-Presets'),
    [string]$ReportPath = (Join-Path $PSScriptRoot '..\docs\development\generated-unified-preset-report.json'),
    [string]$RefreshBaseFromPath,
    [switch]$Check,
    [Parameter(DontShow)]
    [ValidateSet('', 'after-stage', 'after-publish-1', 'before-final-verify', 'during-cleanup', 'during-rollback', 'hard-stop-after-publish-1')]
    [string]$InternalTestFailurePoint = '',
    [Parameter(DontShow)]
    [ValidateRange(0, 30000)]
    [int]$InternalTestLockHoldMilliseconds = 0,
    [Parameter(DontShow)]
    [string]$InternalTestLockSignalPath,
    [Parameter(DontShow)]
    [string]$InternalTestCrashSignalPath
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedPolicyPath = [System.IO.Path]::GetFullPath($PolicyPath)
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$resolvedReportPath = [System.IO.Path]::GetFullPath($ReportPath)
$authorizedBasePath = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'docs\development\unified-preset-templates\Base.SettingsUser.json'))
$transactionJournalPath = Join-Path $repositoryRoot '.csx-unified-presets.transaction.json'
$generatorLockPath = Join-Path $repositoryRoot '.csx-unified-presets.lock'
$policy = Get-Content -Raw -LiteralPath $resolvedPolicyPath | ConvertFrom-Json -Depth 100

if ($policy.schemaVersion -ne 4) {
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

if (-not ('UnifiedPresetPathIdentity' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class UnifiedPresetPathIdentity
{
    private const uint FileReadAttributes = 0x80;
    private const uint ShareAll = 0x7;
    private const uint OpenExisting = 3;
    private const uint BackupSemantics = 0x02000000;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(
        string name, uint access, uint share, IntPtr security,
        uint disposition, uint flags, IntPtr templateFile);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetFinalPathNameByHandleW(
        SafeFileHandle handle, [Out] char[] path, uint length, uint flags);

    public static string ResolveExisting(string path)
    {
        using (var handle = CreateFileW(path, FileReadAttributes, ShareAll, IntPtr.Zero,
            OpenExisting, BackupSemantics, IntPtr.Zero))
        {
            if (handle.IsInvalid)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot resolve path identity: " + path);
            var buffer = new char[32768];
            uint length = GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Length, 0);
            if (length == 0 || length >= buffer.Length)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot resolve path identity: " + path);
            var result = new string(buffer, 0, (int)length);
            if (result.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase))
                return @"\\" + result.Substring(8);
            if (result.StartsWith(@"\\?\", StringComparison.OrdinalIgnoreCase))
                return result.Substring(4);
            return result;
        }
    }
}
'@
}

function Resolve-PhysicalPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $suffix = [System.Collections.Generic.List[string]]::new()
    $probe = $fullPath
    while (-not (Test-Path -LiteralPath $probe)) {
        $leaf = [System.IO.Path]::GetFileName($probe)
        if ([string]::IsNullOrEmpty($leaf)) {
            throw "No existing ancestor can establish path identity: $fullPath"
        }
        $suffix.Insert(0, $leaf)
        $probe = [System.IO.Path]::GetDirectoryName($probe)
    }
    $physical = [UnifiedPresetPathIdentity]::ResolveExisting($probe)
    foreach ($segment in $suffix) {
        $physical = Join-Path $physical $segment
    }
    [System.IO.Path]::GetFullPath($physical)
}

function Get-TextSha256 {
    param([Parameter(Mandatory = $true)][string]$Text)

    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Text)
    $hash = [System.Security.Cryptography.SHA256]::HashData($bytes)
    ([System.Convert]::ToHexString($hash))
}

function Get-JsonValueKind {
    param($Value)

    if ($null -eq $Value) { return 'null' }
    if ($Value -is [bool]) { return 'boolean' }
    if ($Value -is [string]) { return 'string' }
    if ($Value -is [sbyte] -or $Value -is [byte] -or
        $Value -is [int16] -or $Value -is [uint16] -or
        $Value -is [int32] -or $Value -is [uint32] -or
        $Value -is [int64] -or $Value -is [uint64]) { return 'integer' }
    if ($Value -is [single] -or $Value -is [double] -or $Value -is [decimal]) { return 'number' }
    if ($Value -is [System.Management.Automation.PSCustomObject]) { return 'object' }
    if ($Value -is [System.Collections.IEnumerable]) { return 'array' }
    return $Value.GetType().FullName
}

function Get-ExactJsonProperty {
    param(
        [Parameter(Mandatory = $true)]$Root,
        [Parameter(Mandatory = $true)][string]$Segment,
        [Parameter(Mandatory = $true)][object[]]$FullPath
    )

    if ($Root -isnot [System.Management.Automation.PSCustomObject]) {
        throw "JSON path has a non-object parent: $(ConvertTo-CanonicalPath $FullPath)"
    }
    $exact = @($Root.psobject.Properties | Where-Object { $_.Name -ceq $Segment })
    if ($exact.Count -eq 1) {
        return $exact[0]
    }
    $caseVariant = @($Root.psobject.Properties | Where-Object { $_.Name -ieq $Segment })
    if ($caseVariant.Count -gt 0) {
        throw "JSON path case mismatch at $(ConvertTo-CanonicalPath $FullPath): expected '$Segment', found '$($caseVariant[0].Name)'"
    }
    throw "JSON path is absent: $(ConvertTo-CanonicalPath $FullPath)"
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
        $property = Get-ExactJsonProperty -Root $current -Segment $segment -FullPath $Path
        $current = $property.Value
    }
    $current
}

function Test-JsonPathPresent {
    param(
        [Parameter(Mandatory = $true)]$Root,
        [Parameter(Mandatory = $true)][object[]]$Path
    )

    $current = $Root
    foreach ($segmentValue in $Path) {
        if ($null -eq $current) {
            throw "JSON path has a null parent: $(ConvertTo-CanonicalPath $Path)"
        }
        if ($current -isnot [System.Management.Automation.PSCustomObject]) {
            throw "JSON path has a non-object parent: $(ConvertTo-CanonicalPath $Path)"
        }
        $segment = [string]$segmentValue
        $exact = @($current.psobject.Properties | Where-Object { $_.Name -ceq $segment })
        if ($exact.Count -eq 0) {
            $caseVariant = @($current.psobject.Properties | Where-Object { $_.Name -ieq $segment })
            if ($caseVariant.Count -gt 0) {
                throw "JSON path case mismatch at $(ConvertTo-CanonicalPath $Path): expected '$segment', found '$($caseVariant[0].Name)'"
            }
            return $false
        }
        $current = $exact[0].Value
    }
    $true
}

function Set-ExistingJsonPathValue {
    param(
        [Parameter(Mandatory = $true)]$Root,
        [Parameter(Mandatory = $true)][object[]]$Path,
        [Parameter(Mandatory = $false)]$Value
    )

    if ($Path.Count -lt 1) {
        throw 'An override path must contain at least one segment.'
    }
    $currentValue = Get-JsonPathValue -Root $Root -Path $Path
    $currentKind = Get-JsonValueKind $currentValue
    $newKind = Get-JsonValueKind $Value
    if ($currentKind -cne $newKind) {
        throw "JSON type mismatch at $(ConvertTo-CanonicalPath $Path): expected $currentKind, got $newKind"
    }

    $current = $Root
    for ($index = 0; $index -lt $Path.Count - 1; $index++) {
        $property = Get-ExactJsonProperty -Root $current -Segment ([string]$Path[$index]) -FullPath $Path
        $current = $property.Value
    }
    $leafProperty = Get-ExactJsonProperty -Root $current -Segment ([string]$Path[-1]) -FullPath $Path
    $leafProperty.Value = $Value
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
        $segment = [string]$Path[$index]
        if ($current -isnot [System.Management.Automation.PSCustomObject]) {
            throw "JSON removal path has a non-object parent: $(ConvertTo-CanonicalPath $Path)"
        }
        $property = @($current.psobject.Properties | Where-Object { $_.Name -ceq $segment })
        if ($property.Count -eq 0 -or $null -eq $property[0].Value) {
            return
        }
        $current = $property[0].Value
    }
    $leaf = [string]$Path[-1]
    $leafProperty = @($current.psobject.Properties | Where-Object { $_.Name -ceq $leaf })
    if ($leafProperty.Count -eq 1) {
        $current.psobject.Properties.Remove($leaf)
    }
}

function Test-EquivalentValue {
    param($Actual, $Expected)

    if ((Get-JsonValueKind $Actual) -cne (Get-JsonValueKind $Expected)) {
        return $false
    }
    if ($Actual -is [double] -or $Actual -is [float] -or $Expected -is [double] -or $Expected -is [float]) {
        return [math]::Abs([double]$Actual - [double]$Expected) -le 0.000001
    }
    if ($Actual -is [System.Management.Automation.PSCustomObject] -or
        ($Actual -is [System.Collections.IEnumerable] -and $Actual -isnot [string])) {
        return (ConvertTo-CanonicalJson $Actual) -ceq (ConvertTo-CanonicalJson $Expected)
    }
    $Actual -ceq $Expected
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

function Assert-NoHardDisabledFeatures {
    param([Parameter(Mandatory = $true)]$Settings)

    $disabledAtBoot = Get-JsonPathValue -Root $Settings -Path @('Disable at Boot')
    if ($disabledAtBoot -isnot [System.Management.Automation.PSCustomObject]) {
        throw 'Disable at Boot must be a JSON object.'
    }
    foreach ($entry in $disabledAtBoot.psobject.Properties) {
        if ($entry.Value -isnot [bool]) {
            throw "Disable at Boot value must be boolean: $($entry.Name)"
        }
        if ($entry.Value) {
            throw "Unified presets must not hard-disable features: $($entry.Name)"
        }
    }
    foreach ($operationalFeature in 'CS Editor', 'Weather Picker') {
        if ($null -ne $disabledAtBoot.psobject.Properties[$operationalFeature]) {
            throw "Operational tool must not appear in Disable at Boot: $operationalFeature"
        }
    }
}

function New-PresetCompatibilityMarker {
    param(
        [Parameter(Mandatory = $true)][string]$Tier,
        [Parameter(Mandatory = $true)][string]$RuntimeSettingsContractHash
    )

    $target = $policy.presetCompatibility.target
    [ordered]@{
        contractVersion = [int]$policy.presetCompatibility.contractVersion
        presetId = "csx-unified-$($Tier.ToLowerInvariant())"
        presetVersion = [string]$policy.packageVersion
        target = [ordered]@{
            runtime = [string]$target.runtime
            minimumVersion = [string]$target.minimumVersion
            maximumVersionExclusive = [string]$target.maximumVersionExclusive
        }
        settingsContract = [ordered]@{
            revision = [int]$policy.runtimeSettingsContract.revision
            sourceTreeSha256 = $RuntimeSettingsContractHash
        }
    }
}

function Add-PresetCompatibilityMarker {
    param(
        [Parameter(Mandatory = $true)]$Settings,
        [Parameter(Mandatory = $true)][string]$Tier,
        [Parameter(Mandatory = $true)][string]$RuntimeSettingsContractHash
    )

    if ($null -ne $Settings.psobject.Properties['Preset Compatibility']) {
        throw 'The pinned base must not contain generated preset compatibility metadata.'
    }
    $result = [ordered]@{}
    foreach ($property in $Settings.psobject.Properties) {
        $result[$property.Name] = $property.Value
        if ($property.Name -ceq 'Version') {
            $result['Preset Compatibility'] = New-PresetCompatibilityMarker `
                -Tier $Tier `
                -RuntimeSettingsContractHash $RuntimeSettingsContractHash
        }
    }
    if (-not $result.Contains('Preset Compatibility')) {
        throw 'The pinned base has no Version field after which compatibility metadata can be placed.'
    }
    [pscustomobject]$result
}

function Test-OrdinalSequenceEqual {
    param([object[]]$Left, [object[]]$Right)

    if ($Left.Count -ne $Right.Count) { return $false }
    for ($index = 0; $index -lt $Left.Count; $index++) {
        if ([string]$Left[$index] -cne [string]$Right[$index]) { return $false }
    }
    $true
}

function Assert-OverridePathContract {
    param(
        [Parameter(Mandatory = $true)]$Settings,
        [Parameter(Mandatory = $true)]$Override,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $actual = Get-JsonPathValue -Root $Settings -Path $Override.path
    $actualKind = Get-JsonValueKind $actual
    $expectedKind = Get-JsonValueKind $Override.value
    if ($actualKind -cne $expectedKind) {
        throw "$Context type mismatch at $(ConvertTo-CanonicalPath $Override.path): expected $actualKind, got $expectedKind"
    }
}

function Assert-AllPolicyPathsAgainstSettings {
    param([Parameter(Mandatory = $true)]$Settings)

    foreach ($override in $policy.commonOverrides) {
        Assert-OverridePathContract -Settings $Settings -Override $override -Context 'Common override'
    }
    foreach ($tierProperty in $policy.tiers.psobject.Properties) {
        foreach ($override in $tierProperty.Value.overrides) {
            Assert-OverridePathContract -Settings $Settings -Override $override -Context "Tier $($tierProperty.Name) override"
        }
    }
    foreach ($guard in $policy.guards) {
        Assert-OverridePathContract -Settings $Settings -Override $guard -Context 'Guard'
    }
}

function Get-RuntimeSettingsInventoryPaths {
    $contract = $policy.runtimeSettingsContract
    $inventory = $contract.inventory
    if ($null -eq $inventory -or $null -eq $inventory.fixedSources -or
        $null -eq $inventory.roots -or [string]::IsNullOrWhiteSpace([string]$inventory.ownerPattern)) {
        throw 'The runtime settings contract inventory is incomplete.'
    }

    $paths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($fixedSource in $inventory.fixedSources) {
        $null = $paths.Add(([string]$fixedSource).Replace('\', '/'))
    }
    $extensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($extension in $inventory.extensions) {
        $null = $extensions.Add([string]$extension)
    }
    foreach ($rootValue in $inventory.roots) {
        $root = Resolve-RepositoryPath -RelativePath ([string]$rootValue)
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            throw "Runtime settings inventory root is absent: $rootValue"
        }
        foreach ($candidate in Get-ChildItem -LiteralPath $root -Recurse -File) {
            if (-not $extensions.Contains($candidate.Extension)) {
                continue
            }
            if (Select-String -LiteralPath $candidate.FullName -Pattern ([string]$inventory.ownerPattern) -Quiet) {
                $relative = [System.IO.Path]::GetRelativePath($repositoryRoot, $candidate.FullName).Replace('\', '/')
                $null = $paths.Add($relative)
            }
        }
    }
    @($paths | Sort-Object)
}

function Get-RuntimeSettingsContractHash {
    $contract = $policy.runtimeSettingsContract
    if ($null -eq $contract -or $contract.revision -lt 1) {
        throw 'The runtime settings contract is absent or has an invalid revision.'
    }
    $sourcePaths = @($contract.sources | ForEach-Object { [string]$_ })
    if ($sourcePaths.Count -eq 0) {
        throw 'The runtime settings contract does not name any source files.'
    }
    $inventoryPaths = @(Get-RuntimeSettingsInventoryPaths)
    $declaredSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($sourcePath in $sourcePaths) { $null = $declaredSet.Add($sourcePath) }
    $inventorySet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($inventoryPath in $inventoryPaths) { $null = $inventorySet.Add($inventoryPath) }
    $missing = @($inventoryPaths | Where-Object { -not $declaredSet.Contains($_) })
    $unexpected = @($sourcePaths | Where-Object { -not $inventorySet.Contains($_) })
    if ($missing.Count -gt 0 -or $unexpected.Count -gt 0) {
        throw "Runtime settings source inventory differs from the declared contract. Missing: $($missing -join ', ') Unexpected: $($unexpected -join ', ')"
    }
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $records = foreach ($sourcePath in $sourcePaths) {
        if (-not $seen.Add($sourcePath)) {
            throw "The runtime settings contract contains a duplicate source: $sourcePath"
        }
        $resolved = Resolve-RepositoryPath -RelativePath $sourcePath
        $sourceRoot = Join-Path $repositoryRoot 'src'
        if (-not $resolved.StartsWith($sourceRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Runtime settings contract source is outside src: $sourcePath"
        }
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "Runtime settings contract source is absent: $sourcePath"
        }
        "${sourcePath}`0$((Get-FileHash -Algorithm SHA256 -LiteralPath $resolved).Hash)"
    }
    $inventoryRecord = "inventory`0$(Get-TextSha256 (ConvertTo-CanonicalJson $contract.inventory))"
    Get-TextSha256 (((@($records | Sort-Object) + $inventoryRecord) -join "`n") + "`n")
}

function Assert-RuntimeSettingsContract {
    $actual = Get-RuntimeSettingsContractHash
    if ($actual -cne [string]$policy.runtimeSettingsContract.sourceTreeSha256) {
        throw "Runtime settings contract changed: expected $($policy.runtimeSettingsContract.sourceTreeSha256), got $actual. Review the settings schema, refresh the base when required, and update the contract deliberately."
    }
    $actual
}

function Assert-TierContract {
    $requiredTierOrder = @('Performance', 'Balanced', 'Quality')
    $declaredTierOrder = @($policy.tierOrder | ForEach-Object { [string]$_ })
    if (-not (Test-OrdinalSequenceEqual -Left $requiredTierOrder -Right $declaredTierOrder)) {
        throw 'tierOrder must be exactly: Performance, Balanced, Quality'
    }
    $actualTierOrder = @($policy.tiers.psobject.Properties.Name)
    if (-not (Test-OrdinalSequenceEqual -Left $requiredTierOrder -Right $actualTierOrder)) {
        throw 'tiers must contain exactly Performance, Balanced, and Quality in that order.'
    }
    if ($policy.presetCompatibility.contractVersion -ne 1 -or
        $policy.presetCompatibility.target.runtime -cne 'VR' -or
        $policy.presetCompatibility.target.minimumVersion -cne '3.19' -or
        $policy.presetCompatibility.target.maximumVersionExclusive -cne '3.20') {
        throw 'Preset compatibility must target CSX VR >= 3.19 and < 3.20 with contract version 1.'
    }

    $ownedPaths = @($policy.tierOwnedPaths | ForEach-Object { ConvertTo-CanonicalPath $_ })
    $ownedPathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($ownedPath in $ownedPaths) {
        if (-not $ownedPathSet.Add($ownedPath)) {
            throw "tierOwnedPaths contains a duplicate or case-variant path: $ownedPath"
        }
    }
    $ownedPaths = @($ownedPaths | Sort-Object)

    $commonPaths = @($policy.commonOverrides | ForEach-Object { ConvertTo-CanonicalPath $_.path })
    $commonPathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($commonPath in $commonPaths) {
        if (-not $commonPathSet.Add($commonPath)) {
            throw "commonOverrides contains a duplicate or case-variant path: $commonPath"
        }
    }
    foreach ($commonPath in $commonPaths) {
        if ($ownedPathSet.Contains($commonPath)) {
            throw "A path cannot be owned by both commonOverrides and tierOwnedPaths: $commonPath"
        }
    }

    foreach ($tierProperty in $policy.tiers.psobject.Properties) {
        $tier = $tierProperty.Name
        $definition = $tierProperty.Value
        $overridePaths = @($definition.overrides | ForEach-Object { ConvertTo-CanonicalPath $_.path })
        $overridePathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
        foreach ($overridePath in $overridePaths) {
            if (-not $overridePathSet.Add($overridePath)) {
                throw "Tier $tier contains a duplicate or case-variant override path: $overridePath"
            }
        }
        foreach ($path in $overridePaths) {
            foreach ($prefixPath in $policy.tierForbiddenPrefixes) {
                $prefix = ConvertTo-CanonicalPath $prefixPath
                if ($path.Equals($prefix, [System.StringComparison]::OrdinalIgnoreCase) -or
                    $path.StartsWith($prefix + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
                    throw "Tier $tier attempts to own common/operational path $path."
                }
            }
        }
        if ($overridePathSet.Count -ne $ownedPathSet.Count -or
            @($overridePaths | Where-Object { -not $ownedPathSet.Contains($_) }).Count -ne 0) {
            throw "Tier $tier must override every tier-owned path exactly once."
        }
        foreach ($qualificationReference in $definition.qualificationRefs) {
            if ($null -eq $policy.qualifications.psobject.Properties[[string]$qualificationReference]) {
                throw "Tier $tier references unknown qualification '$qualificationReference'."
            }
        }
    }

    $script:resolvedTierOutputDirectories = @{}
    $resolvedOutputSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($tierProperty in $policy.tiers.psobject.Properties) {
        $tier = $tierProperty.Name
        $outputDirectory = [string]$tierProperty.Value.outputDirectory
        $expectedOutputDirectory = "CSX Unified- $tier - Press END on PC to Customize"
        if ($outputDirectory -cne $expectedOutputDirectory) {
            throw "Tier $tier outputDirectory must be exactly: $expectedOutputDirectory"
        }
        if ($outputDirectory -notmatch '^CSX Unified- .+ - Press END on PC to Customize$') {
            throw "Tier outputDirectory does not follow the managed unified preset naming contract: $outputDirectory"
        }
        if ($outputDirectory -match '(?i)AMD|NVIDIA') {
            throw "Unified output directory contains a vendor name: $outputDirectory"
        }
        if ([System.IO.Path]::GetFileName($outputDirectory) -cne $outputDirectory -or
            $outputDirectory.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
            $outputDirectory.EndsWith('.') -or $outputDirectory.EndsWith(' ')) {
            throw "Tier outputDirectory must be one safe Windows directory segment: $outputDirectory"
        }
        $resolved = [System.IO.Path]::GetFullPath((Join-Path $resolvedOutputRoot $outputDirectory))
        if ([System.IO.Path]::GetDirectoryName($resolved) -ine $resolvedOutputRoot) {
            throw "Tier outputDirectory must resolve to an immediate child of the output root: $outputDirectory"
        }
        if (-not $resolvedOutputSet.Add($resolved)) {
            throw "Tier outputDirectory values resolve to the same Windows path: $outputDirectory"
        }
        $script:resolvedTierOutputDirectories[$tier] = $resolved
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
notes=PROVISIONAL unified $Tier preset generated from policy schema v4 for CSX 3.19-VR; qualification=$qualificationSummary
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

function Get-ProtectedInputPaths {
    $paths = [System.Collections.Generic.List[string]]::new()
    foreach ($path in @(
            $resolvedPolicyPath,
            $authorizedBasePath,
            [System.IO.Path]::GetFullPath($PSCommandPath),
            (Join-Path $repositoryRoot 'tests\unified_preset_generator_test.ps1'),
            (Join-Path $repositoryRoot '.github\workflows\unified-preset-validation.yaml'),
            (Join-Path $repositoryRoot 'docs\development\unified-presets.md'))) {
        $paths.Add([System.IO.Path]::GetFullPath($path))
    }
    foreach ($sourcePath in $policy.runtimeSettingsContract.sources) {
        $paths.Add((Resolve-RepositoryPath -RelativePath ([string]$sourcePath)))
    }
    @($paths)
}

function Assert-PublicationPathOwnership {
    param(
        [Parameter(Mandatory = $true)][string[]]$OutputPaths,
        [string[]]$AdditionalInputPaths = @()
    )

    $inputs = [System.Collections.Generic.Dictionary[string, string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($inputPath in @((Get-ProtectedInputPaths)) + @($AdditionalInputPaths)) {
        $physical = Resolve-PhysicalPath -Path $inputPath
        if (-not $inputs.ContainsKey($physical)) { $inputs[$physical] = $inputPath }
    }
    $outputs = [System.Collections.Generic.Dictionary[string, string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($outputPath in $OutputPaths) {
        $fullPath = [System.IO.Path]::GetFullPath($outputPath)
        if (Test-Path -LiteralPath $fullPath -PathType Container) {
            throw "Publication target is a directory: $fullPath"
        }
        $physical = Resolve-PhysicalPath -Path $fullPath
        if ($outputs.ContainsKey($physical)) {
            throw "Publication contains duplicate or physically aliased targets: $fullPath and $($outputs[$physical])"
        }
        if ($inputs.ContainsKey($physical)) {
            throw "Publication output overlaps protected input '$($inputs[$physical])': $fullPath"
        }
        $outputs[$physical] = $fullPath
    }
}

function Enter-GeneratorLock {
    $lockPath = Resolve-PhysicalPath -Path $generatorLockPath
    try {
        $stream = [System.IO.FileStream]::new(
            $lockPath,
            [System.IO.FileMode]::OpenOrCreate,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None)
    }
    catch [System.IO.IOException] {
        throw "Another unified-preset generator or checker owns $lockPath."
    }
    [pscustomobject]@{ Path = $lockPath; Stream = $stream }
}

function Write-StagedText {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )

    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Content)
    $stream = [System.IO.FileStream]::new(
        $Path,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None)
    try {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    }
    finally {
        $stream.Dispose()
    }
}

function Copy-DurableFile {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $sourceInfo = Get-Item -LiteralPath $Source
    $input = [System.IO.FileStream]::new(
        $Source, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $output = [System.IO.FileStream]::new(
            $Destination, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        try {
            $input.CopyTo($output)
            $output.Flush($true)
        }
        finally { $output.Dispose() }
    }
    finally { $input.Dispose() }
    [System.IO.File]::SetLastWriteTimeUtc($Destination, $sourceInfo.LastWriteTimeUtc)
}

function Write-TransactionJournal {
    param([Parameter(Mandatory = $true)]$Journal)

    $content = ConvertTo-CanonicalJson $Journal
    $temporary = "$transactionJournalPath.tmp"
    if (Test-Path -LiteralPath $temporary -PathType Leaf) {
        Remove-Item -LiteralPath $temporary -Force
    }
    Write-StagedText -Path $temporary -Content $content
    if (Test-Path -LiteralPath $transactionJournalPath -PathType Leaf) {
        [System.IO.File]::Move($temporary, $transactionJournalPath, $true)
    }
    else {
        [System.IO.File]::Move($temporary, $transactionJournalPath)
    }
}

function Remove-TransactionArtifacts {
    param([Parameter(Mandatory = $true)]$Journal)

    $failures = [System.Collections.Generic.List[string]]::new()
    foreach ($record in $Journal.records) {
        foreach ($artifact in @([string]$record.temporary, [string]$record.backup)) {
            if (Test-Path -LiteralPath $artifact -PathType Leaf) {
                try { Remove-Item -LiteralPath $artifact -Force }
                catch { $failures.Add("${artifact}: $($_.Exception.Message)") }
            }
        }
    }
    if ($InternalTestFailurePoint -ceq 'during-cleanup') {
        $failures.Add('Injected unified-preset cleanup failure.')
    }
    if ($failures.Count -eq 0) {
        foreach ($directory in @($Journal.createdDirectories | Sort-Object Length -Descending)) {
            if ((Test-Path -LiteralPath $directory -PathType Container) -and
                (@(Get-ChildItem -LiteralPath $directory -Force).Count -eq 0)) {
                try { Remove-Item -LiteralPath $directory -Force }
                catch { $failures.Add("${directory}: $($_.Exception.Message)") }
            }
        }
    }
    if ($failures.Count -eq 0 -and (Test-Path -LiteralPath $transactionJournalPath -PathType Leaf)) {
        try { Remove-Item -LiteralPath $transactionJournalPath -Force }
        catch { $failures.Add("${transactionJournalPath}: $($_.Exception.Message)") }
    }
    @($failures)
}

function Restore-TransactionJournal {
    param([Parameter(Mandatory = $true)]$Journal)

    $failures = [System.Collections.Generic.List[string]]::new()
    if ($InternalTestFailurePoint -ceq 'during-rollback') {
        $failures.Add('Injected unified-preset rollback failure.')
    }
    for ($index = @($Journal.records).Count - 1; $index -ge 0; $index--) {
        $record = @($Journal.records)[$index]
        try {
            $target = [string]$record.target
            if ([bool]$record.existed) {
                $targetIsOld = (Test-Path -LiteralPath $target -PathType Leaf) -and
                    ((Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash -ceq [string]$record.oldSha256)
                if (-not $targetIsOld) {
                    if (-not (Test-Path -LiteralPath $record.backup -PathType Leaf) -or
                        (Get-FileHash -Algorithm SHA256 -LiteralPath $record.backup).Hash -cne [string]$record.oldSha256) {
                        throw 'A verified original backup is unavailable.'
                    }
                    $restore = "$target.csx-restore.tmp"
                    Copy-DurableFile -Source $record.backup -Destination $restore
                    if (Test-Path -LiteralPath $target -PathType Leaf) {
                        [System.IO.File]::Move($restore, $target, $true)
                    }
                    else {
                        [System.IO.File]::Move($restore, $target)
                    }
                }
            }
            elseif (Test-Path -LiteralPath $target -PathType Leaf) {
                if ((Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash -cne [string]$record.newSha256) {
                    throw 'A newly created target was changed by another owner.'
                }
                Remove-Item -LiteralPath $target -Force
            }
        }
        catch {
            $failures.Add("$($record.target): $($_.Exception.Message)")
        }
    }
    if ($failures.Count -gt 0) {
        throw "Unified-preset rollback was incomplete; recovery artifacts were preserved. $($failures -join '; ')"
    }
    $cleanupFailures = @(Remove-TransactionArtifacts -Journal $Journal)
    if ($cleanupFailures.Count -gt 0) {
        throw "Unified-preset rollback restored all targets but cleanup was incomplete. $($cleanupFailures -join '; ')"
    }
}

function Repair-PendingPublication {
    if (-not (Test-Path -LiteralPath $transactionJournalPath -PathType Leaf)) {
        return
    }
    $journal = Get-Content -Raw -LiteralPath $transactionJournalPath | ConvertFrom-Json -Depth 20
    if ($journal.schemaVersion -ne 1 -or $journal.repositoryRoot -ine $repositoryRoot) {
        throw "Unified-preset transaction journal is invalid; preserve it for manual recovery: $transactionJournalPath"
    }
    if ($journal.state -ceq 'committed') {
        foreach ($record in $journal.records) {
            if (-not (Test-Path -LiteralPath $record.target -PathType Leaf) -or
                (Get-FileHash -Algorithm SHA256 -LiteralPath $record.target).Hash -cne [string]$record.newSha256) {
                throw "Committed unified-preset generation failed verification; recovery artifacts were preserved: $($record.target)"
            }
        }
        $cleanupFailures = @(Remove-TransactionArtifacts -Journal $journal)
        if ($cleanupFailures.Count -gt 0) {
            throw "Committed unified-preset generation is valid but cleanup remains incomplete. $($cleanupFailures -join '; ')"
        }
        return
    }
    if ($journal.state -notin @('preparing', 'prepared', 'publishing')) {
        throw "Unified-preset transaction journal has an unknown state '$($journal.state)'."
    }
    Restore-TransactionJournal -Journal $journal
}

function Invoke-PublicationTransaction {
    param([Parameter(Mandatory = $true)][object[]]$Files)

    $transactionId = [guid]::NewGuid().ToString('N')
    Assert-PublicationPathOwnership -OutputPaths @($Files | ForEach-Object { [string]$_.Path })
    $records = [System.Collections.Generic.List[object]]::new()
    $createdDirectories = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $Files) {
        $target = [System.IO.Path]::GetFullPath([string]$file.Path)
        $parent = [System.IO.Path]::GetDirectoryName($target)
        $missing = [System.Collections.Generic.List[string]]::new()
        while (-not (Test-Path -LiteralPath $parent)) {
            $missing.Insert(0, $parent)
            $parent = [System.IO.Path]::GetDirectoryName($parent)
        }
        foreach ($directory in $missing) { $null = $createdDirectories.Add($directory) }
        $content = [string]$file.Content
        $existed = Test-Path -LiteralPath $target -PathType Leaf
        $records.Add([ordered]@{
                target = $target
                temporary = "$target.csx-$transactionId.tmp"
                backup = "$target.csx-$transactionId.bak"
                existed = $existed
                oldSha256 = if ($existed) { (Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash } else { $null }
                newSha256 = Get-TextSha256 $content
                content = $content
            })
    }
    $journal = [ordered]@{
        schemaVersion = 1
        transactionId = $transactionId
        repositoryRoot = $repositoryRoot
        state = 'preparing'
        createdDirectories = @($createdDirectories)
        records = @($records | ForEach-Object {
                [ordered]@{
                    target = $_.target; temporary = $_.temporary; backup = $_.backup
                    existed = $_.existed; oldSha256 = $_.oldSha256; newSha256 = $_.newSha256
                }
            })
    }
    Write-TransactionJournal -Journal $journal
    try {
        foreach ($directory in $createdDirectories) {
            [System.IO.Directory]::CreateDirectory($directory) | Out-Null
        }
        foreach ($record in $records) {
            Write-StagedText -Path $record.temporary -Content $record.content
            if ((Get-FileHash -Algorithm SHA256 -LiteralPath $record.temporary).Hash -cne $record.newSha256) {
                throw "Staged publication readback failed: $($record.target)"
            }
            if ($record.existed) {
                Copy-DurableFile -Source $record.target -Destination $record.backup
                if ((Get-FileHash -Algorithm SHA256 -LiteralPath $record.backup).Hash -cne $record.oldSha256) {
                    throw "Publication backup verification failed: $($record.target)"
                }
            }
        }
        if ($InternalTestFailurePoint -ceq 'after-stage') {
            throw 'Injected unified-preset failure after staging.'
        }
        $journal.state = 'prepared'
        Write-TransactionJournal -Journal $journal
        $journal.state = 'publishing'
        Write-TransactionJournal -Journal $journal
        $publishedCount = 0
        foreach ($record in $records) {
            if ($record.existed) {
                [System.IO.File]::Move($record.temporary, $record.target, $true)
            }
            else {
                [System.IO.File]::Move($record.temporary, $record.target)
            }
            $publishedCount++
            if ($InternalTestFailurePoint -ceq 'after-publish-1' -and $publishedCount -eq 1) {
                throw 'Injected unified-preset failure after the first publication.'
            }
            if ($InternalTestFailurePoint -ceq 'during-rollback' -and $publishedCount -eq 1) {
                throw 'Injected unified-preset publication failure before rollback.'
            }
            if ($InternalTestFailurePoint -ceq 'hard-stop-after-publish-1' -and $publishedCount -eq 1) {
                if ([string]::IsNullOrWhiteSpace($InternalTestCrashSignalPath)) {
                    throw 'A crash-test signal path is required.'
                }
                [System.IO.File]::WriteAllText(
                    [System.IO.Path]::GetFullPath($InternalTestCrashSignalPath),
                    $transactionId,
                    [System.Text.UTF8Encoding]::new($false))
                Start-Sleep -Seconds 30
                throw 'The crash-test owner was not stopped within its bounded pause.'
            }
        }
        if ($InternalTestFailurePoint -ceq 'before-final-verify') {
            throw 'Injected unified-preset failure before final verification.'
        }
        foreach ($record in $records) {
            if ((Get-FileHash -Algorithm SHA256 -LiteralPath $record.target).Hash -cne $record.newSha256) {
                throw "Published unified-preset readback failed: $($record.target)"
            }
        }
        $journal.state = 'committed'
        Write-TransactionJournal -Journal $journal
    }
    catch {
        $publicationFailure = $_
        try {
            Restore-TransactionJournal -Journal $journal
        }
        catch {
            throw "Unified-preset publication failed and rollback was incomplete. Failure: $($publicationFailure.Exception.Message) Rollback: $($_.Exception.Message)"
        }
        throw "Unified-preset publication failed; the previous complete generation was restored. $($publicationFailure.Exception.Message)"
    }
    $cleanupFailures = @(Remove-TransactionArtifacts -Journal $journal)
    if ($cleanupFailures.Count -gt 0) {
        Write-Warning "Unified-preset generation committed successfully; cleanup will resume on the next run. $($cleanupFailures -join '; ')"
    }
}

$generatorLock = $null
try {
    $generatorLock = Enter-GeneratorLock
    Repair-PendingPublication
    if ($InternalTestLockSignalPath) {
        [System.IO.File]::WriteAllText(
            [System.IO.Path]::GetFullPath($InternalTestLockSignalPath),
            $generatorLock.Path,
            [System.Text.UTF8Encoding]::new($false))
    }
    if ($InternalTestLockHoldMilliseconds -gt 0) {
        Start-Sleep -Milliseconds $InternalTestLockHoldMilliseconds
    }

    Assert-TierContract
    Assert-ManagedOutputSet
    $basePath = Resolve-RepositoryPath -RelativePath $policy.baseTemplate.path
    if ($basePath -ine $authorizedBasePath) {
        throw "baseTemplate.path must resolve to the authorized template: $authorizedBasePath"
    }
    $runtimeSettingsContractHash = Assert-RuntimeSettingsContract

    if ($RefreshBaseFromPath) {
        $sourcePath = [System.IO.Path]::GetFullPath($RefreshBaseFromPath)
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "Refresh source does not exist: $sourcePath"
        }
        $forbiddenRefreshSources = @($basePath, $resolvedPolicyPath, $resolvedReportPath, [System.IO.Path]::GetFullPath($PSCommandPath))
        if (@($forbiddenRefreshSources | Where-Object { $_ -ieq $sourcePath }).Count -gt 0 -or
            $sourcePath.StartsWith($resolvedOutputRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refresh source overlaps a generator input or output.'
        }

        $settings = Get-Content -Raw -LiteralPath $sourcePath | ConvertFrom-Json -Depth 100
        Assert-AllPolicyPathsAgainstSettings -Settings $settings
        foreach ($override in $policy.baseNormalization.overrides) {
            Assert-OverridePathContract -Settings $settings -Override $override -Context 'Base normalization override'
        }
        foreach ($path in $policy.baseNormalization.removePaths) {
            Remove-JsonPath -Root $settings -Path $path
        }
        foreach ($override in $policy.baseNormalization.overrides) {
            Set-ExistingJsonPathValue -Root $settings -Path $override.path -Value $override.value
        }
        $neutralTier = $policy.tiers.psobject.Properties[[string]$policy.baseNormalization.neutralTier]
        if ($null -eq $neutralTier) {
            throw "Unknown neutral tier: $($policy.baseNormalization.neutralTier)"
        }
        foreach ($override in $neutralTier.Value.overrides) {
            Set-ExistingJsonPathValue -Root $settings -Path $override.path -Value $override.value
        }
        Assert-CurrentSchema -Settings $settings
        Assert-NeutralBase -Settings $settings

        $baseJson = ConvertTo-CanonicalJson $settings
        Assert-PublicationPathOwnership -OutputPaths @($basePath) -AdditionalInputPaths @($sourcePath)
        Invoke-PublicationTransaction -Files @([pscustomobject]@{ Path = $basePath; Content = $baseJson })
        [ordered]@{
            state = 'base-refreshed'
            path = $policy.baseTemplate.path
            sha256 = Get-TextSha256 $baseJson
            runtimeSettingsContractSha256 = $runtimeSettingsContractHash
        } | ConvertTo-Json
        return
    }

    if (-not (Test-Path -LiteralPath $basePath -PathType Leaf)) {
        throw "Unified preset base is absent: $basePath"
    }
    $baseHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $basePath).Hash
    if ($baseHash -cne [string]$policy.baseTemplate.sha256) {
        throw "Pinned unified base changed: expected $($policy.baseTemplate.sha256), got $baseHash"
    }
    $baseSettings = Get-Content -Raw -LiteralPath $basePath | ConvertFrom-Json -Depth 100
    Assert-CurrentSchema -Settings $baseSettings
    Assert-NeutralBase -Settings $baseSettings
    Assert-AllPolicyPathsAgainstSettings -Settings $baseSettings
    Assert-NoHardDisabledFeatures -Settings $baseSettings

    $results = @()
    $publicationFiles = [System.Collections.Generic.List[object]]::new()
    foreach ($tierProperty in $policy.tiers.psobject.Properties) {
        $tier = $tierProperty.Name
        $definition = $tierProperty.Value
        $settings = (ConvertTo-CanonicalJson $baseSettings) | ConvertFrom-Json -Depth 100
        foreach ($override in @($policy.commonOverrides) + @($definition.overrides)) {
            Set-ExistingJsonPathValue -Root $settings -Path $override.path -Value $override.value
        }
        $settings = Add-PresetCompatibilityMarker `
            -Settings $settings `
            -Tier $tier `
            -RuntimeSettingsContractHash $runtimeSettingsContractHash
        Assert-CurrentSchema -Settings $settings
        Assert-GuardValues -Settings $settings
        Assert-NoHardDisabledFeatures -Settings $settings

        $json = ConvertTo-CanonicalJson $settings
        $meta = Get-GeneratedMetaIni -Tier $tier -Definition $definition
        $outputDirectory = [string]$script:resolvedTierOutputDirectories[$tier]
        $settingsPath = Join-Path $outputDirectory 'SKSE\Plugins\CommunityShaders\SettingsUser.json'
        $metaPath = Join-Path $outputDirectory 'meta.ini'
        $publicationFiles.Add([pscustomobject]@{ Path = $settingsPath; Content = $json })
        $publicationFiles.Add([pscustomobject]@{ Path = $metaPath; Content = $meta })

        $results += [ordered]@{
            tier = $tier
            state = if ($Check) { 'verified' } else { 'generated' }
            outputDirectory = $definition.outputDirectory
            settingsSha256 = Get-TextSha256 $json
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
        runtimeSettingsContract = [ordered]@{
            revision = $policy.runtimeSettingsContract.revision
            sourceTreeSha256 = $runtimeSettingsContractHash
        }
        presetCompatibility = $policy.presetCompatibility
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
    $publicationFiles.Add([pscustomobject]@{ Path = $resolvedReportPath; Content = $reportJson })
    Assert-PublicationPathOwnership -OutputPaths @($publicationFiles | ForEach-Object { [string]$_.Path })

    if ($Check) {
        foreach ($file in $publicationFiles) {
            if (-not (Test-Path -LiteralPath $file.Path -PathType Leaf)) {
                throw "Generated unified-preset file is absent: $($file.Path)"
            }
            if ((Get-Content -Raw -LiteralPath $file.Path) -cne $file.Content) {
                throw "Generated unified-preset file is stale: $($file.Path)"
            }
        }
    }
    else {
        Invoke-PublicationTransaction -Files @($publicationFiles)
    }

    $results | ConvertTo-Json -Depth 10
}
finally {
    if ($null -ne $generatorLock -and $null -ne $generatorLock.Stream) {
        $generatorLock.Stream.Dispose()
    }
}
