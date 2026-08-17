[CmdletBinding()]
param(
    [string]$ArchiveRoot,
    [string]$StagingRoot,
    [datetime]$SessionDate = (Get-Date),
    [string]$Mo2OverwriteRoot = 'D:\Games\Skyrim\MadGod2\overwrite\Root'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'preset-calibration-storage.ps1')

$active = @(Get-Process -Name ModOrganizer,SkyrimVR,sksevr_loader -ErrorAction SilentlyContinue)
if ($active.Count -gt 0) {
    throw "Refusing to archive while runtime processes are active: $($active.ProcessName -join ', ')"
}

$resolvedStagingRoot = [System.IO.Path]::GetFullPath($Mo2OverwriteRoot)
$expectedStagingRoot = [System.IO.Path]::GetFullPath('D:\Games\Skyrim\MadGod2\overwrite\Root')
if (-not $resolvedStagingRoot.Equals($expectedStagingRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unexpected staging root: $resolvedStagingRoot"
}

$resolvedArchiveRoot = Get-PresetCalibrationArchiveRoot -ConfiguredRoot $ArchiveRoot
$resolvedCaptureStagingRoot = Get-PresetCalibrationStagingRoot -ConfiguredRoot $StagingRoot
$archiveSessionRoot = [System.IO.Path]::GetFullPath((Join-Path $resolvedArchiveRoot ('Sessions\{0}' -f $SessionDate.ToString('yyyy-MM-dd'))))
if (-not $archiveSessionRoot.StartsWith($resolvedArchiveRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Session archive escaped configured root: $archiveSessionRoot"
}
[System.IO.Directory]::CreateDirectory($archiveSessionRoot) | Out-Null
$mo2SessionRoot = Join-Path $archiveSessionRoot 'MO2-overwrite'
$logPath = Join-Path $archiveSessionRoot 'archive-session.log'

function Test-PathWithin {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Root)
    $normalizedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $normalizedPath = [System.IO.Path]::GetFullPath($Path)
    $normalizedPath.Equals($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
        $normalizedPath.StartsWith($normalizedRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)
}

function Move-StagingTree {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$AllowedSourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationRoot,
        [Parameter(Mandatory = $true)][string]$RelativeDestination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        return [ordered]@{ source = $Source; state = 'absent'; destination = $null; robocopyExitCode = $null }
    }

    $resolvedSource = [System.IO.Path]::GetFullPath($Source)
    $resolvedAllowedSourceRoot = [System.IO.Path]::GetFullPath($AllowedSourceRoot)
    if (-not (Test-PathWithin -Path $resolvedSource -Root $resolvedAllowedSourceRoot)) {
        throw "Source escaped its allowed staging root: $resolvedSource"
    }
    $resolvedDestinationRoot = [System.IO.Path]::GetFullPath($DestinationRoot)
    $destination = [System.IO.Path]::GetFullPath((Join-Path $resolvedDestinationRoot $RelativeDestination))
    if (-not (Test-PathWithin -Path $destination -Root $resolvedDestinationRoot)) {
        throw "Destination escaped its session archive root: $destination"
    }

    [System.IO.Directory]::CreateDirectory($destination) | Out-Null
    & robocopy.exe $resolvedSource $destination /E /MOVE /COPY:DAT /DCOPY:DAT /R:2 /W:2 /MT:8 /J /XJ /NFL /NDL /NP "/LOG+:$logPath" | Out-Null
    $exitCode = $LASTEXITCODE
    if ($exitCode -ge 8) {
        throw "Robocopy failed for $resolvedSource with exit code $exitCode. See $logPath"
    }
    if ((Test-Path -LiteralPath $resolvedSource) -and -not (Get-ChildItem -LiteralPath $resolvedSource -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $resolvedSource -Force
    }
    [ordered]@{ source = $resolvedSource; state = 'moved'; destination = $destination; robocopyExitCode = $exitCode }
}

$results = @(
    Move-StagingTree -Source (Join-Path $resolvedStagingRoot 'CSX Baselines') -AllowedSourceRoot $resolvedStagingRoot -DestinationRoot $mo2SessionRoot -RelativeDestination 'CSX Baselines'
    Move-StagingTree -Source (Join-Path $resolvedStagingRoot 'Screenshots') -AllowedSourceRoot $resolvedStagingRoot -DestinationRoot $mo2SessionRoot -RelativeDestination 'Screenshots'
    Move-StagingTree -Source (Join-Path $resolvedCaptureStagingRoot ('Sessions\{0}\CSX Baselines' -f $SessionDate.ToString('yyyy-MM-dd'))) -AllowedSourceRoot $resolvedCaptureStagingRoot -DestinationRoot $archiveSessionRoot -RelativeDestination 'CSX Baselines'
)

[ordered]@{
    archivedAt = (Get-Date).ToUniversalTime().ToString('o')
    stagingRoot = $resolvedStagingRoot
    captureStagingRoot = $resolvedCaptureStagingRoot
    archiveRoot = $resolvedArchiveRoot
    sessionRoot = $archiveSessionRoot
    results = $results
} | ConvertTo-Json -Depth 8
