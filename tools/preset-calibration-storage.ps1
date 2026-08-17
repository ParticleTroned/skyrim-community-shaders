$presetCalibrationStorageRepositoryRoot = Split-Path -Parent $PSScriptRoot

function Assert-PresetCalibrationRootSafe {
    param([Parameter(Mandatory = $true)][string]$Root, [Parameter(Mandatory = $true)][string]$Purpose)

    $resolved = [System.IO.Path]::GetFullPath($Root.Trim())
    $unsafeRoots = @(
        [System.IO.Path]::GetFullPath('D:\Games\Skyrim\MadGod2'),
        [System.IO.Path]::GetFullPath('D:\SteamLibrary\steamapps\common\SkyrimVR')
    )
    foreach ($unsafeRoot in $unsafeRoots) {
        if ($resolved.StartsWith($unsafeRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "$Purpose must be outside MO2 and SkyrimVR: $resolved"
        }
    }
    $resolved
}

function Get-PresetCalibrationArchiveRoot {
    param([string]$ConfiguredRoot)

    $archiveRoot = $ConfiguredRoot
    if ([string]::IsNullOrWhiteSpace($archiveRoot)) {
        $archiveRoot = $env:CSX_CALIBRATION_ARCHIVE_ROOT
    }
    if ([string]::IsNullOrWhiteSpace($archiveRoot)) {
        $archiveRoot = (& git -C $presetCalibrationStorageRepositoryRoot config --local --get csx.calibrationArchiveRoot 2>$null)
    }
    if ([string]::IsNullOrWhiteSpace($archiveRoot)) {
        throw "No calibration archive is configured. Set repository-local git config csx.calibrationArchiveRoot or CSX_CALIBRATION_ARCHIVE_ROOT. Repository lookup: '$presetCalibrationStorageRepositoryRoot'."
    }
    Assert-PresetCalibrationRootSafe -Root $archiveRoot -Purpose 'Calibration archive'
}

Set-Item -Path Function:\Get-PresetCalibrationArchiveRoot -Value ((Get-Item Function:\Get-PresetCalibrationArchiveRoot).ScriptBlock.GetNewClosure())

function Get-PresetCalibrationStagingRoot {
    param([string]$ConfiguredRoot)

    $stagingRoot = $ConfiguredRoot
    if ([string]::IsNullOrWhiteSpace($stagingRoot)) {
        $stagingRoot = $env:CSX_CALIBRATION_STAGING_ROOT
    }
    if ([string]::IsNullOrWhiteSpace($stagingRoot)) {
        $stagingRoot = (& git -C $presetCalibrationStorageRepositoryRoot config --local --get csx.calibrationStagingRoot 2>$null)
    }
    if ([string]::IsNullOrWhiteSpace($stagingRoot)) {
        throw "No calibration staging root is configured. Set repository-local git config csx.calibrationStagingRoot or CSX_CALIBRATION_STAGING_ROOT. Repository lookup: '$presetCalibrationStorageRepositoryRoot'."
    }
    Assert-PresetCalibrationRootSafe -Root $stagingRoot -Purpose 'Calibration staging root'
}

Set-Item -Path Function:\Get-PresetCalibrationStagingRoot -Value ((Get-Item Function:\Get-PresetCalibrationStagingRoot).ScriptBlock.GetNewClosure())

function Resolve-PresetCalibrationOutputRoot {
    param(
        [string]$OutputRoot,
        [Parameter(Mandatory = $true)][string]$Collection,
        [datetime]$SessionDate = (Get-Date)
    )

    if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) {
        return [System.IO.Path]::GetFullPath($OutputRoot)
    }

    $stagingRoot = Get-PresetCalibrationStagingRoot
    $resolved = Join-Path $stagingRoot ('Sessions\{0}\CSX Baselines\{1}' -f $SessionDate.ToString('yyyy-MM-dd'), $Collection)
    [System.IO.Directory]::CreateDirectory($resolved) | Out-Null
    [System.IO.Path]::GetFullPath($resolved)
}
