[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$generator = Join-Path $repositoryRoot 'tools\generate-unified-presets.ps1'
$policyPath = Join-Path $repositoryRoot 'docs\development\unified-preset-policy.json'
$policy = Get-Content -Raw -LiteralPath $policyPath | ConvertFrom-Json -Depth 100
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("csx-unified-preset-test-" + [guid]::NewGuid().ToString('N'))
$utf8 = [System.Text.UTF8Encoding]::new($false)

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Write-JsonFile {
    param([string]$Path, $Value)
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $Path)) | Out-Null
    $json = (($Value | ConvertTo-Json -Depth 100) -replace "`r`n", "`n") + "`n"
    [System.IO.File]::WriteAllText($Path, $json, $utf8)
}

function Get-LeafMap {
    param($Value, [string]$Prefix = '')

    $result = @{}
    if ($null -eq $Value) {
        $result[$Prefix] = '<null>'
    }
    elseif ($Value -is [System.Management.Automation.PSCustomObject]) {
        foreach ($property in $Value.psobject.Properties) {
            $path = if ($Prefix) { "$Prefix/$($property.Name)" } else { $property.Name }
            $child = Get-LeafMap -Value $property.Value -Prefix $path
            foreach ($key in $child.Keys) { $result[$key] = $child[$key] }
        }
    }
    elseif ($Value -is [System.Collections.IEnumerable] -and $Value -isnot [string]) {
        $index = 0
        foreach ($entry in $Value) {
            $child = Get-LeafMap -Value $entry -Prefix "$Prefix[$index]"
            foreach ($key in $child.Keys) { $result[$key] = $child[$key] }
            $index++
        }
    }
    else {
        $result[$Prefix] = "$($Value.GetType().FullName):$Value"
    }
    $result
}

function Get-PublicationSnapshot {
    param([string]$OutputRoot, [string]$ReportPath)

    $entries = [ordered]@{}
    if (Test-Path -LiteralPath $OutputRoot -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $OutputRoot -Recurse -File | Sort-Object FullName) {
            $relative = $file.FullName.Substring($OutputRoot.Length + 1)
            $entries["output/$relative"] = [ordered]@{
                sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
                length = $file.Length
                lastWriteTicks = $file.LastWriteTimeUtc.Ticks
            }
        }
    }
    if (Test-Path -LiteralPath $ReportPath -PathType Leaf) {
        $file = Get-Item -LiteralPath $ReportPath
        $entries['report'] = [ordered]@{
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
            length = $file.Length
            lastWriteTicks = $file.LastWriteTimeUtc.Ticks
        }
    }
    ($entries | ConvertTo-Json -Depth 10 -Compress)
}

function Invoke-ExpectedFailure {
    param(
        [string]$GeneratorPath,
        [hashtable]$Arguments,
        [string]$Pattern,
        [string]$Message
    )

    $failure = $null
    try { & $GeneratorPath @Arguments 2>&1 | Out-Null }
    catch { $failure = $_ }
    Assert-True ($null -ne $failure) $Message
    Assert-True ($failure.Exception.Message -match $Pattern) "$Message Actual: $($failure.Exception.Message)"
    $failure.Exception.Message
}

function Copy-RepositoryFile {
    param([string]$RelativePath, [string]$FixtureRoot)

    $source = Join-Path $repositoryRoot $RelativePath
    $destination = Join-Path $FixtureRoot $RelativePath
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

try {
    [System.IO.Directory]::CreateDirectory($scratch) | Out-Null

    & $generator -Check | Out-Null
    Assert-True $? 'Committed unified preset outputs did not pass -Check.'

    $fixtureRoot = Join-Path $scratch 'repo'
    Copy-RepositoryFile -RelativePath 'tools\generate-unified-presets.ps1' -FixtureRoot $fixtureRoot
    Copy-RepositoryFile -RelativePath 'docs\development\unified-preset-policy.json' -FixtureRoot $fixtureRoot
    Copy-RepositoryFile -RelativePath 'docs\development\unified-preset-templates\Base.SettingsUser.json' -FixtureRoot $fixtureRoot
    foreach ($sourcePath in $policy.runtimeSettingsContract.sources) {
        Copy-RepositoryFile -RelativePath ([string]$sourcePath) -FixtureRoot $fixtureRoot
    }

    $isolatedGenerator = Join-Path $fixtureRoot 'tools\generate-unified-presets.ps1'
    $isolatedPolicyPath = Join-Path $fixtureRoot 'docs\development\unified-preset-policy.json'
    $isolatedBasePath = Join-Path $fixtureRoot 'docs\development\unified-preset-templates\Base.SettingsUser.json'
    $baselinePolicyText = Get-Content -Raw -LiteralPath $isolatedPolicyPath
    $baselineBaseText = Get-Content -Raw -LiteralPath $isolatedBasePath
    $outputRoot = Join-Path $fixtureRoot 'outputs'
    $reportPath = Join-Path $fixtureRoot 'report.json'

    & $isolatedGenerator -OutputRoot $outputRoot -ReportPath $reportPath | Out-Null
    Assert-True $? 'Temporary unified preset generation failed.'

    $tierProperties = @($policy.tiers.psobject.Properties)
    Assert-True ($tierProperties.Count -eq 3) 'The policy must define exactly three tiers.'
    Assert-True (($tierProperties.Name -join '|') -ceq 'Performance|Balanced|Quality') 'The tier order changed unexpectedly.'

    $ownedPaths = @($policy.tierOwnedPaths | ForEach-Object { @($_ | ForEach-Object { [string]$_ }) -join '/' })
    $maps = @{}
    foreach ($tierProperty in $tierProperties) {
        $settingsPath = Join-Path $outputRoot "$($tierProperty.Value.outputDirectory)\SKSE\Plugins\CommunityShaders\SettingsUser.json"
        Assert-True (Test-Path -LiteralPath $settingsPath -PathType Leaf) "Missing generated settings for $($tierProperty.Name)."
        $settings = Get-Content -Raw -LiteralPath $settingsPath | ConvertFrom-Json -Depth 100
        $compatibility = $settings.'Preset Compatibility'
        Assert-True ($compatibility.contractVersion -eq 1) "Missing compatibility contract for $($tierProperty.Name)."
        Assert-True ($compatibility.presetId -ceq "csx-unified-$($tierProperty.Name.ToLowerInvariant())") "Wrong preset identity for $($tierProperty.Name)."
        Assert-True ($compatibility.presetVersion -ceq $policy.packageVersion) "Wrong preset version for $($tierProperty.Name)."
        Assert-True ($compatibility.target.runtime -ceq 'VR') "Wrong runtime target for $($tierProperty.Name)."
        Assert-True ($compatibility.target.minimumVersion -ceq '3.19') "Wrong minimum CSX version for $($tierProperty.Name)."
        Assert-True ($compatibility.target.maximumVersionExclusive -ceq '3.20') "Wrong maximum CSX version for $($tierProperty.Name)."
        Assert-True ($settings.'Weather Picker'.Enabled -eq $true) "Weather Picker was disabled for $($tierProperty.Name)."
        Assert-True ($null -eq $settings.'Disable at Boot'.psobject.Properties['CS Editor']) "CS Editor appeared in Disable at Boot for $($tierProperty.Name)."
        Assert-True ($null -eq $settings.'Disable at Boot'.psobject.Properties['Weather Picker']) "Weather Picker appeared in Disable at Boot for $($tierProperty.Name)."
        foreach ($disabledAtBootEntry in $settings.'Disable at Boot'.psobject.Properties) {
            Assert-True ($disabledAtBootEntry.Value -eq $false) "Feature was hard-disabled for $($tierProperty.Name): $($disabledAtBootEntry.Name)"
        }
        $maps[$tierProperty.Name] = Get-LeafMap $settings
    }

    $reference = $maps['Performance']
    foreach ($tier in 'Balanced', 'Quality') {
        $candidate = $maps[$tier]
        $allPaths = @($reference.Keys + $candidate.Keys | Sort-Object -Unique)
        foreach ($path in $allPaths) {
            if ($reference[$path] -cne $candidate[$path] -and
                $path -notin $ownedPaths -and
                -not $path.StartsWith('Preset Compatibility/', [System.StringComparison]::Ordinal)) {
                throw "Non-tier path diverged between Performance and ${tier}: $path"
            }
        }
    }

    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json -Depth 100
    Assert-True ($report.tiers.Count -eq 3) 'Generated evidence report omitted a tier.'
    Assert-True ($null -ne $report.runtimeSettingsContract.sourceTreeSha256) 'Generated evidence report omitted the runtime settings contract.'
    Assert-True ($null -ne $report.qualifications.'ssgi-ambient-composition') 'Generated evidence report omitted qualification metadata.'

    $baselineSnapshot = Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath
    & $isolatedGenerator -OutputRoot $outputRoot -ReportPath $reportPath -Check | Out-Null
    $afterCheckSnapshot = Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath
    Assert-True ($baselineSnapshot -ceq $afterCheckSnapshot) '-Check mutated the generated package set.'

    $invalidPolicy = $baselinePolicyText | ConvertFrom-Json -Depth 100
    $invalidPolicy.tierOrder = @('Performance', 'Balanced')
    Write-JsonFile -Path $isolatedPolicyPath -Value $invalidPolicy
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'tierOrder must be exactly' -Message 'A non-three-tier policy was accepted.'
    Assert-True ($baselineSnapshot -ceq (Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath)) 'A rejected tier contract mutated the published generation.'
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)

    $invalidPolicy = $baselinePolicyText | ConvertFrom-Json -Depth 100
    $invalidPolicy.presetCompatibility.target.maximumVersionExclusive = '3.19'
    Write-JsonFile -Path $isolatedPolicyPath -Value $invalidPolicy
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'Preset compatibility must target' -Message 'An invalid preset compatibility range was accepted.'
    Assert-True ($baselineSnapshot -ceq (Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath)) 'A rejected compatibility contract mutated the published generation.'
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)

    $invalidPolicy = $baselinePolicyText | ConvertFrom-Json -Depth 100
    $invalidPolicy.tierOwnedPaths[0] = @('menu', 'UI Mode')
    foreach ($tier in $invalidPolicy.tiers.psobject.Properties) {
        $tier.Value.overrides[0].path = @('menu', 'UI Mode')
        $tier.Value.overrides[0].value = 0
    }
    Write-JsonFile -Path $isolatedPolicyPath -Value $invalidPolicy
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'commonOverrides and tierOwnedPaths|common/operational path' -Message 'A case-variant operational tier path was accepted.'
    Assert-True ($baselineSnapshot -ceq (Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath)) 'A rejected case-variant path mutated the published generation.'
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)

    $invalidBase = $baselineBaseText | ConvertFrom-Json -Depth 100
    $invalidBase.Advanced.psobject.Properties.Remove('Log Level')
    Write-JsonFile -Path $isolatedBasePath -Value $invalidBase
    $invalidPolicy = $baselinePolicyText | ConvertFrom-Json -Depth 100
    $invalidPolicy.baseTemplate.sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $isolatedBasePath).Hash
    Write-JsonFile -Path $isolatedPolicyPath -Value $invalidPolicy
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'JSON path is absent.*Advanced/Log Level' -Message 'A missing common settings path was fabricated.'
    Assert-True ($baselineSnapshot -ceq (Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath)) 'A missing base path mutated the published generation.'

    $invalidBase = $baselineBaseText | ConvertFrom-Json -Depth 100
    $invalidBase.Advanced.'Log Level' = '2'
    Write-JsonFile -Path $isolatedBasePath -Value $invalidBase
    $invalidPolicy.baseTemplate.sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $isolatedBasePath).Hash
    Write-JsonFile -Path $isolatedPolicyPath -Value $invalidPolicy
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'Required settings value mismatch|type mismatch' -Message 'A wrong-type common settings value was accepted.'
    Assert-True ($baselineSnapshot -ceq (Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath)) 'A wrong-type base value mutated the published generation.'
    [System.IO.File]::WriteAllText($isolatedBasePath, $baselineBaseText, $utf8)
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)

    $invalidBase = $baselineBaseText | ConvertFrom-Json -Depth 100
    $invalidBase | Add-Member -NotePropertyName 'water effects' -NotePropertyValue ([pscustomobject]@{})
    Write-JsonFile -Path $isolatedBasePath -Value $invalidBase
    $invalidPolicy = $baselinePolicyText | ConvertFrom-Json -Depth 100
    $invalidPolicy.baseTemplate.sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $isolatedBasePath).Hash
    Write-JsonFile -Path $isolatedPolicyPath -Value $invalidPolicy
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'JSON path case mismatch.*Water Effects' -Message 'A case-variant stale path was treated as absent.'

    $invalidBase = $baselineBaseText | ConvertFrom-Json -Depth 100
    $invalidBase.'Volumetric Lighting' = 1
    Write-JsonFile -Path $isolatedBasePath -Value $invalidBase
    $invalidPolicy.baseTemplate.sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $isolatedBasePath).Hash
    Write-JsonFile -Path $isolatedPolicyPath -Value $invalidPolicy
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'JSON path has a non-object parent.*Volumetric Lighting/CustomColorBlue' -Message 'A malformed stale-path ancestor was treated as absent.'
    [System.IO.File]::WriteAllText($isolatedBasePath, $baselineBaseText, $utf8)
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)

    $generatorHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $isolatedGenerator).Hash
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $isolatedGenerator
    } -Pattern 'Publication output overlaps protected input' -Message 'ReportPath was allowed to overwrite the generator.'
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $isolatedGenerator).Hash -ceq $generatorHash) 'Rejected ReportPath overlap altered the generator.'

    $protectedRuntimeSource = Join-Path $fixtureRoot ([string]$policy.runtimeSettingsContract.sources[0])
    $protectedRuntimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $protectedRuntimeSource).Hash
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $protectedRuntimeSource
    } -Pattern 'Publication output overlaps protected input' -Message 'ReportPath was allowed to overwrite a runtime contract source.'
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $protectedRuntimeSource).Hash -ceq $protectedRuntimeHash) 'Rejected ReportPath overlap altered a runtime source.'

    $invalidPolicy = $baselinePolicyText | ConvertFrom-Json -Depth 100
    $invalidPolicy.tiers.Performance.outputDirectory = 'CSX Unified- Alias\..\CSX Unified- Performance - Press END on PC to Customize'
    Write-JsonFile -Path $isolatedPolicyPath -Value $invalidPolicy
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'outputDirectory must be exactly' -Message 'An aliased tier output directory was accepted.'
    Assert-True ($baselineSnapshot -ceq (Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath)) 'A rejected output alias mutated the published generation.'
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)

    $refreshSource = Join-Path $scratch 'refresh-source.json'
    [System.IO.File]::WriteAllText($refreshSource, $baselineBaseText, $utf8)
    $invalidPolicy = $baselinePolicyText | ConvertFrom-Json -Depth 100
    $invalidPolicy.baseTemplate.path = 'tools/generate-unified-presets.ps1'
    Write-JsonFile -Path $isolatedPolicyPath -Value $invalidPolicy
    $generatorHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $isolatedGenerator).Hash
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath; RefreshBaseFromPath = $refreshSource
    } -Pattern 'authorized template' -Message 'Refresh accepted an arbitrary repository target.'
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $isolatedGenerator).Hash -ceq $generatorHash) 'Rejected refresh altered the generator.'
    Assert-True ($baselineSnapshot -ceq (Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath)) 'A rejected refresh target mutated the published generation.'
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)

    $contractSource = Join-Path $fixtureRoot ([string]$policy.runtimeSettingsContract.sources[0])
    $contractSourceText = Get-Content -Raw -LiteralPath $contractSource
    [System.IO.File]::AppendAllText($contractSource, "`n", $utf8)
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'Runtime settings contract changed' -Message 'A runtime settings source change did not invalidate the preset contract.'
    Assert-True ($baselineSnapshot -ceq (Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath)) 'Runtime source drift mutated the published generation.'
    [System.IO.File]::WriteAllText($contractSource, $contractSourceText, $utf8)

    $unlistedSettingsOwner = Join-Path $fixtureRoot 'src\Features\UnlistedSettingsOwner.h'
    [System.IO.File]::WriteAllText($unlistedSettingsOwner, 'void SaveSettings(json&);', $utf8)
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'source inventory differs.*UnlistedSettingsOwner' -Message 'An unlisted settings owner did not invalidate the runtime source inventory.'
    Remove-Item -LiteralPath $unlistedSettingsOwner -Force

    $changedPolicy = $baselinePolicyText | ConvertFrom-Json -Depth 100
    $changedPolicy.packageVersion = 'd2099.01.01.1'
    Write-JsonFile -Path $isolatedPolicyPath -Value $changedPolicy
    foreach ($failurePoint in 'after-stage', 'after-publish-1', 'before-final-verify') {
        $beforeFailure = Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath
        $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
            OutputRoot = $outputRoot; ReportPath = $reportPath; InternalTestFailurePoint = $failurePoint
        } -Pattern 'previous complete generation was restored' -Message "Failure point $failurePoint did not fail safely."
        $afterFailure = Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath
        Assert-True ($beforeFailure -ceq $afterFailure) "Failure point $failurePoint changed the published generation."
    }

    $null = & $isolatedGenerator -OutputRoot $outputRoot -ReportPath $reportPath -InternalTestFailurePoint during-cleanup 3>&1
    Assert-True $? 'A post-commit cleanup failure incorrectly failed the committed generation.'
    $committedSnapshot = Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath
    Assert-True ($committedSnapshot -cne $baselineSnapshot) 'The cleanup-failure test did not commit the new generation.'
    Assert-True (Test-Path -LiteralPath (Join-Path $fixtureRoot '.csx-unified-presets.transaction.json') -PathType Leaf) 'Cleanup failure did not preserve the committed transaction journal.'
    & $isolatedGenerator -OutputRoot $outputRoot -ReportPath $reportPath -Check | Out-Null
    Assert-True $? 'The next run did not finish committed-generation cleanup.'

    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)
    & $isolatedGenerator -OutputRoot $outputRoot -ReportPath $reportPath | Out-Null
    $baselineSnapshot = Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath

    Write-JsonFile -Path $isolatedPolicyPath -Value $changedPolicy
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath; InternalTestFailurePoint = 'during-rollback'
    } -Pattern 'rollback was incomplete' -Message 'An incomplete rollback did not fail closed.'
    Assert-True (Test-Path -LiteralPath (Join-Path $fixtureRoot '.csx-unified-presets.transaction.json') -PathType Leaf) 'Incomplete rollback deleted its recovery journal.'
    Assert-True (@(Get-ChildItem -LiteralPath $fixtureRoot -Recurse -File | Where-Object { $_.Name -match '\.csx-[^.]+\.(tmp|bak)$' }).Count -gt 0) 'Incomplete rollback deleted all recovery artifacts.'
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)
    & $isolatedGenerator -OutputRoot $outputRoot -ReportPath $reportPath -Check | Out-Null
    Assert-True $? 'A later run did not complete the preserved rollback.'

    Write-JsonFile -Path $isolatedPolicyPath -Value $changedPolicy
    $crashSignalPath = Join-Path $scratch 'crash-owner.ready.txt'
    $crashStdoutPath = Join-Path $scratch 'crash-owner.stdout.txt'
    $crashStderrPath = Join-Path $scratch 'crash-owner.stderr.txt'
    $crashOwner = Start-Process -FilePath 'pwsh' -ArgumentList @(
        '-NoProfile', '-File', $isolatedGenerator,
        '-OutputRoot', $outputRoot,
        '-ReportPath', $reportPath,
        '-InternalTestFailurePoint', 'hard-stop-after-publish-1',
        '-InternalTestCrashSignalPath', $crashSignalPath) -PassThru -RedirectStandardOutput $crashStdoutPath -RedirectStandardError $crashStderrPath
    $crashDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path -LiteralPath $crashSignalPath -PathType Leaf) -and
        -not $crashOwner.HasExited -and [DateTime]::UtcNow -lt $crashDeadline) {
        Start-Sleep -Milliseconds 50
    }
    Assert-True (Test-Path -LiteralPath $crashSignalPath -PathType Leaf) 'The crash-test process did not reach a partially published generation.'
    Stop-Process -Id $crashOwner.Id -Force
    $crashOwner.WaitForExit()
    $crashOwner.Dispose()
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)
    & $isolatedGenerator -OutputRoot $outputRoot -ReportPath $reportPath -Check | Out-Null
    Assert-True $? 'The next run did not recover a hard-stopped publication.'
    Assert-True ($baselineSnapshot -ceq (Get-PublicationSnapshot -OutputRoot $outputRoot -ReportPath $reportPath)) 'Hard-stop recovery did not restore the complete prior generation.'

    Assert-True (@(Get-ChildItem -LiteralPath $fixtureRoot -Recurse -File | Where-Object {
                $_.Name -match '\.csx-[^.]+\.(tmp|bak)$|\.csx-restore\.tmp$|\.csx-unified-presets\.transaction\.json'
            }).Count -eq 0) 'Successful recovery left transaction artifacts behind.'
    [System.IO.File]::WriteAllText($isolatedPolicyPath, $baselinePolicyText, $utf8)

    $stdoutPath = Join-Path $scratch 'lock-owner.stdout.txt'
    $stderrPath = Join-Path $scratch 'lock-owner.stderr.txt'
    $lockSignalPath = Join-Path $scratch 'lock-owner.ready.txt'
    $fixtureAlias = Join-Path $scratch 'repo-alias'
    New-Item -ItemType Junction -Path $fixtureAlias -Target $fixtureRoot | Out-Null
    $aliasGenerator = Join-Path $fixtureAlias 'tools\generate-unified-presets.ps1'
    $lockOwner = Start-Process -FilePath 'pwsh' -ArgumentList @(
        '-NoProfile', '-File', $isolatedGenerator,
        '-OutputRoot', $outputRoot,
        '-ReportPath', $reportPath,
        '-InternalTestLockSignalPath', $lockSignalPath,
        '-InternalTestLockHoldMilliseconds', '5000') -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $lockDeadline = [DateTime]::UtcNow.AddSeconds(5)
    while (-not (Test-Path -LiteralPath $lockSignalPath -PathType Leaf) -and
        -not $lockOwner.HasExited -and [DateTime]::UtcNow -lt $lockDeadline) {
        Start-Sleep -Milliseconds 50
    }
    Assert-True (Test-Path -LiteralPath $lockSignalPath -PathType Leaf) 'The lock-owner process did not signal lock acquisition.'
    Assert-True (-not $lockOwner.HasExited) 'The lock-owner process exited before the overlap test.'
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = (Join-Path $fixtureRoot 'different-report.json')
    } -Pattern 'Another unified-preset generator or checker owns' -Message 'A concurrent generator with a different argument tuple bypassed repository ownership.'
    $null = Invoke-ExpectedFailure -GeneratorPath $aliasGenerator -Arguments @{
        OutputRoot = (Join-Path $fixtureAlias 'outputs'); ReportPath = (Join-Path $fixtureAlias 'report.json')
    } -Pattern 'Another unified-preset generator or checker owns' -Message 'A physical repository alias bypassed generator ownership.'
    $lockOwner.WaitForExit()
    $lockOwnerExitCode = $lockOwner.ExitCode
    $lockOwner.Dispose()
    Assert-True ($lockOwnerExitCode -eq 0) "The lock-owner generation failed: $(Get-Content -Raw -LiteralPath $stderrPath)"
    Remove-Item -LiteralPath $fixtureAlias -Force

    $extraOutput = Join-Path $outputRoot 'CSX Unified- Unmanaged - Press END on PC to Customize'
    [System.IO.Directory]::CreateDirectory($extraOutput) | Out-Null
    $null = Invoke-ExpectedFailure -GeneratorPath $isolatedGenerator -Arguments @{
        OutputRoot = $outputRoot; ReportPath = $reportPath
    } -Pattern 'Unmanaged unified preset output director' -Message 'An unmanaged unified preset output directory was accepted.'
    Assert-True (Test-Path -LiteralPath $extraOutput -PathType Container) 'Rejected unmanaged output was deleted.'

    Write-Output 'Unified preset generator tests passed.'
}
finally {
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
