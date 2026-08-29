[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$generator = Join-Path $repositoryRoot 'tools\generate-unified-presets.ps1'
$policyPath = Join-Path $repositoryRoot 'docs\development\unified-preset-policy.json'
$policy = Get-Content -Raw -LiteralPath $policyPath | ConvertFrom-Json -Depth 100
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("csx-unified-preset-test-" + [guid]::NewGuid().ToString('N'))

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
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
            foreach ($key in $child.Keys) {
                $result[$key] = $child[$key]
            }
        }
    }
    elseif ($Value -is [System.Collections.IEnumerable] -and $Value -isnot [string]) {
        $index = 0
        foreach ($entry in $Value) {
            $child = Get-LeafMap -Value $entry -Prefix "$Prefix[$index]"
            foreach ($key in $child.Keys) {
                $result[$key] = $child[$key]
            }
            $index++
        }
    }
    else {
        $result[$Prefix] = [string]$Value
    }
    $result
}

try {
    [System.IO.Directory]::CreateDirectory($scratch) | Out-Null
    $outputRoot = Join-Path $scratch 'outputs'
    $reportPath = Join-Path $scratch 'report.json'

    & $generator -Check | Out-Null
    Assert-True $? 'Committed unified preset outputs did not pass -Check.'

    & $generator -OutputRoot $outputRoot -ReportPath $reportPath | Out-Null
    Assert-True $? 'Temporary unified preset generation failed.'

    $tierProperties = @($policy.tiers.psobject.Properties)
    Assert-True ($tierProperties.Count -eq 3) 'The policy must define exactly three tiers.'
    Assert-True (($tierProperties.Name -join '|') -eq 'Performance|Balanced|Quality') 'The tier order changed unexpectedly.'

    $ownedPaths = @($policy.tierOwnedPaths | ForEach-Object { @($_ | ForEach-Object { [string]$_ }) -join '/' })
    $maps = @{}
    foreach ($tierProperty in $tierProperties) {
        $settingsPath = Join-Path $outputRoot "$($tierProperty.Value.outputDirectory)\SKSE\Plugins\CommunityShaders\SettingsUser.json"
        Assert-True (Test-Path -LiteralPath $settingsPath -PathType Leaf) "Missing generated settings for $($tierProperty.Name)."
        $settings = Get-Content -Raw -LiteralPath $settingsPath | ConvertFrom-Json -Depth 100
        $maps[$tierProperty.Name] = Get-LeafMap $settings
    }

    $reference = $maps['Performance']
    foreach ($tier in 'Balanced', 'Quality') {
        $candidate = $maps[$tier]
        $allPaths = @($reference.Keys + $candidate.Keys | Sort-Object -Unique)
        foreach ($path in $allPaths) {
            if ($reference[$path] -ne $candidate[$path] -and $path -notin $ownedPaths) {
                throw "Non-tier path diverged between Performance and ${tier}: $path"
            }
        }
    }

    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json -Depth 100
    Assert-True ($report.tiers.Count -eq 3) 'Generated evidence report omitted a tier.'
    Assert-True ($null -ne $report.qualifications.'ssgi-ambient-composition') 'Generated evidence report omitted qualification metadata.'

    $invalidPolicy = Get-Content -Raw -LiteralPath $policyPath | ConvertFrom-Json -Depth 100
    $invalidPolicy.tiers.Performance.overrides[0].path = @('Menu', 'UI Mode')
    $invalidPolicyPath = Join-Path $scratch 'invalid-policy.json'
    [System.IO.File]::WriteAllText(
        $invalidPolicyPath,
        (($invalidPolicy | ConvertTo-Json -Depth 100) -replace "`r`n", "`n") + "`n",
        [System.Text.UTF8Encoding]::new($false))

    $failure = $null
    try {
        & $generator -PolicyPath $invalidPolicyPath -OutputRoot (Join-Path $scratch 'invalid') -ReportPath (Join-Path $scratch 'invalid-report.json') 2>&1 | Out-Null
    }
    catch {
        $failure = $_
    }
    Assert-True ($null -ne $failure) 'A tier override of an operational path was accepted.'
    Assert-True ($failure.Exception.Message -match 'common/operational path') 'The invalid tier failed for the wrong reason.'

    $extraOutputRoot = Join-Path $scratch 'extra-output'
    $extraOutput = Join-Path $extraOutputRoot 'CSX Unified- Unmanaged - Press END on PC to Customize'
    [System.IO.Directory]::CreateDirectory($extraOutput) | Out-Null
    $failure = $null
    try {
        & $generator -OutputRoot $extraOutputRoot -ReportPath (Join-Path $scratch 'extra-report.json') 2>&1 | Out-Null
    }
    catch {
        $failure = $_
    }
    Assert-True ($null -ne $failure) 'An unmanaged unified preset output directory was accepted.'
    Assert-True ($failure.Exception.Message -match 'Unmanaged unified preset output director') 'The unmanaged output failed for the wrong reason.'

    Write-Output 'Unified preset generator tests passed.'
}
finally {
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
