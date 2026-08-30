[CmdletBinding()]
param(
    [string]$PolicyPath = (Join-Path $PSScriptRoot '..\docs\development\unified-preset-policy.json'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\MGO-Presets'),
    [string]$ReportPath = (Join-Path $PSScriptRoot '..\docs\development\generated-unified-preset-report.json'),
    [string]$RefreshBaseFromPath,
    [switch]$Check,
    [Parameter(DontShow)]
    [ValidateSet('', 'after-stage', 'after-publish-1', 'before-final-verify')]
    [string]$InternalTestFailurePoint = '',
    [Parameter(DontShow)]
    [ValidateRange(0, 30000)]
    [int]$InternalTestLockHoldMilliseconds = 0,
    [Parameter(DontShow)]
    [string]$InternalTestLockSignalPath
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedPolicyPath = [System.IO.Path]::GetFullPath($PolicyPath)
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$resolvedReportPath = [System.IO.Path]::GetFullPath($ReportPath)
$authorizedBasePath = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'docs\development\unified-preset-templates\Base.SettingsUser.json'))
$policy = Get-Content -Raw -LiteralPath $resolvedPolicyPath | ConvertFrom-Json -Depth 100

if ($policy.schemaVersion -ne 3) {
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

    try {
        $null = Get-JsonPathValue -Root $Root -Path $Path
        $true
    }
    catch {
        $false
    }
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

function Get-RuntimeSettingsContractHash {
    $contract = $policy.runtimeSettingsContract
    if ($null -eq $contract -or $contract.revision -lt 1) {
        throw 'The runtime settings contract is absent or has an invalid revision.'
    }
    $sourcePaths = @($contract.sources | ForEach-Object { [string]$_ })
    if ($sourcePaths.Count -eq 0) {
        throw 'The runtime settings contract does not name any source files.'
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
    Get-TextSha256 ((@($records | Sort-Object) -join "`n") + "`n")
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
notes=PROVISIONAL unified $Tier preset generated from policy schema v3; qualification=$qualificationSummary
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

function Enter-GeneratorLock {
    $identity = "$repositoryRoot`n$resolvedOutputRoot`n$resolvedReportPath`n$authorizedBasePath"
    $lockName = "csx-unified-presets-$((Get-TextSha256 $identity).Substring(0, 24)).lock"
    $lockPath = Join-Path ([System.IO.Path]::GetTempPath()) $lockName
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

function Invoke-PublicationTransaction {
    param([Parameter(Mandatory = $true)][object[]]$Files)

    $transactionId = [guid]::NewGuid().ToString('N')
    $targets = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $records = [System.Collections.Generic.List[object]]::new()
    try {
        foreach ($file in $Files) {
            $target = [System.IO.Path]::GetFullPath([string]$file.Path)
            if (-not $targets.Add($target)) {
                throw "Publication contains a duplicate or aliased target: $target"
            }
            [System.IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
            $temporary = "$target.csx-$transactionId.tmp"
            $backup = "$target.csx-$transactionId.bak"
            $record = [pscustomobject]@{
                Target = $target
                Temporary = $temporary
                Backup = $backup
                Content = [string]$file.Content
                Existed = Test-Path -LiteralPath $target -PathType Leaf
                Published = $false
            }
            $records.Add($record)
            Write-StagedText -Path $temporary -Content $record.Content
            if ((Get-Content -Raw -LiteralPath $temporary) -cne $record.Content) {
                throw "Staged publication readback failed: $target"
            }
        }
        if ($InternalTestFailurePoint -ceq 'after-stage') {
            throw 'Injected unified-preset failure after staging.'
        }
        $publishedCount = 0
        foreach ($record in $records) {
            if ($record.Existed) {
                [System.IO.File]::Replace($record.Temporary, $record.Target, $record.Backup, $true)
            }
            else {
                [System.IO.File]::Move($record.Temporary, $record.Target)
            }
            $record.Published = $true
            $publishedCount++
            if ($InternalTestFailurePoint -ceq 'after-publish-1' -and $publishedCount -eq 1) {
                throw 'Injected unified-preset failure after the first publication.'
            }
        }
        if ($InternalTestFailurePoint -ceq 'before-final-verify') {
            throw 'Injected unified-preset failure before final verification.'
        }
        foreach ($record in $records) {
            if ((Get-Content -Raw -LiteralPath $record.Target) -cne $record.Content) {
                throw "Published unified-preset readback failed: $($record.Target)"
            }
        }
        foreach ($record in $records) {
            if (Test-Path -LiteralPath $record.Backup -PathType Leaf) {
                Remove-Item -LiteralPath $record.Backup -Force
            }
        }
    }
    catch {
        $publicationFailure = $_
        $rollbackFailures = [System.Collections.Generic.List[string]]::new()
        for ($recordIndex = $records.Count - 1; $recordIndex -ge 0; $recordIndex--) {
            $record = $records[$recordIndex]
            try {
                if ($record.Published) {
                    if ($record.Existed) {
                        if (-not (Test-Path -LiteralPath $record.Backup -PathType Leaf)) {
                            throw 'Original backup is absent.'
                        }
                        if (Test-Path -LiteralPath $record.Target -PathType Leaf) {
                            $rollbackDiscard = "$($record.Target).csx-$transactionId.rollback"
                            [System.IO.File]::Replace($record.Backup, $record.Target, $rollbackDiscard, $true)
                            Remove-Item -LiteralPath $rollbackDiscard -Force
                        }
                        else {
                            [System.IO.File]::Move($record.Backup, $record.Target)
                        }
                    }
                    elseif (Test-Path -LiteralPath $record.Target -PathType Leaf) {
                        Remove-Item -LiteralPath $record.Target -Force
                    }
                }
            }
            catch {
                $rollbackFailures.Add("$($record.Target): $($_.Exception.Message)")
            }
        }
        foreach ($record in $records) {
            foreach ($artifact in $record.Temporary, $record.Backup, "$($record.Target).csx-$transactionId.rollback") {
                if (Test-Path -LiteralPath $artifact -PathType Leaf) {
                    Remove-Item -LiteralPath $artifact -Force -ErrorAction SilentlyContinue
                }
            }
        }
        if ($rollbackFailures.Count -gt 0) {
            throw "Unified-preset publication failed and rollback was incomplete. Failure: $($publicationFailure.Exception.Message) Rollback: $($rollbackFailures -join '; ')"
        }
        throw "Unified-preset publication failed; the previous complete generation was restored. $($publicationFailure.Exception.Message)"
    }
    finally {
        foreach ($record in $records) {
            if (Test-Path -LiteralPath $record.Temporary -PathType Leaf) {
                Remove-Item -LiteralPath $record.Temporary -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

$generatorLock = $null
try {
    $generatorLock = Enter-GeneratorLock
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

    $results = @()
    $publicationFiles = [System.Collections.Generic.List[object]]::new()
    foreach ($tierProperty in $policy.tiers.psobject.Properties) {
        $tier = $tierProperty.Name
        $definition = $tierProperty.Value
        $settings = (ConvertTo-CanonicalJson $baseSettings) | ConvertFrom-Json -Depth 100
        foreach ($override in @($policy.commonOverrides) + @($definition.overrides)) {
            Set-ExistingJsonPathValue -Root $settings -Path $override.path -Value $override.value
        }
        Assert-CurrentSchema -Settings $settings
        Assert-GuardValues -Settings $settings

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
