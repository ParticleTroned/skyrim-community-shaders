[CmdletBinding()]
param(
    [string]$ArchiveRoot,
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
$sessionRoot = [System.IO.Path]::GetFullPath((Join-Path $resolvedArchiveRoot ('Sessions\{0}\MO2-overwrite' -f $SessionDate.ToString('yyyy-MM-dd'))))
if (-not $sessionRoot.StartsWith($resolvedArchiveRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Session archive escaped configured root: $sessionRoot"
}
[System.IO.Directory]::CreateDirectory($sessionRoot) | Out-Null
$logPath = Join-Path $sessionRoot 'archive-session.log'

function Move-StagingTree {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$RelativeDestination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        return [ordered]@{ source = $Source; state = 'absent'; destination = $null; robocopyExitCode = $null }
    }

    $resolvedSource = [System.IO.Path]::GetFullPath($Source)
    if (-not $resolvedSource.StartsWith($resolvedStagingRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Source escaped MO2 staging root: $resolvedSource"
    }
    $destination = [System.IO.Path]::GetFullPath((Join-Path $sessionRoot $RelativeDestination))
    if (-not $destination.StartsWith($sessionRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Destination escaped session archive: $destination"
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
    Move-StagingTree -Source (Join-Path $resolvedStagingRoot 'CSX Baselines') -RelativeDestination 'CSX Baselines'
    Move-StagingTree -Source (Join-Path $resolvedStagingRoot 'Screenshots') -RelativeDestination 'Screenshots'
)

[ordered]@{
    archivedAt = (Get-Date).ToUniversalTime().ToString('o')
    stagingRoot = $resolvedStagingRoot
    archiveRoot = $resolvedArchiveRoot
    sessionRoot = $sessionRoot
    results = $results
} | ConvertTo-Json -Depth 8
