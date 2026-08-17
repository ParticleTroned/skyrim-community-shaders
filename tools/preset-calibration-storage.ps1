$script:PresetCalibrationStorageRepositoryRoot = Split-Path -Parent $PSScriptRoot

function Get-PresetCalibrationArchiveRoot {
    param([string]$ConfiguredRoot)

    $archiveRoot = $ConfiguredRoot
    if ([string]::IsNullOrWhiteSpace($archiveRoot)) {
        $archiveRoot = $env:CSX_CALIBRATION_ARCHIVE_ROOT
    }
    if ([string]::IsNullOrWhiteSpace($archiveRoot)) {
        $archiveRoot = (& git -C $script:PresetCalibrationStorageRepositoryRoot config --local --get csx.calibrationArchiveRoot 2>$null)
    }
    if ([string]::IsNullOrWhiteSpace($archiveRoot)) {
        throw 'No calibration archive is configured. Set repository-local git config csx.calibrationArchiveRoot or CSX_CALIBRATION_ARCHIVE_ROOT.'
    }

    $resolved = [System.IO.Path]::GetFullPath($archiveRoot.Trim())
    $unsafeRoots = @(
        [System.IO.Path]::GetFullPath('D:\Games\Skyrim\MadGod2'),
        [System.IO.Path]::GetFullPath('D:\SteamLibrary\steamapps\common\SkyrimVR')
    )
    foreach ($unsafeRoot in $unsafeRoots) {
        if ($resolved.StartsWith($unsafeRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Calibration archive must be outside MO2 and SkyrimVR: $resolved"
        }
    }
    $resolved
}

function Resolve-PresetCalibrationOutputRoot {
    param(
        [string]$OutputRoot,
        [Parameter(Mandatory = $true)][string]$Collection,
        [datetime]$SessionDate = (Get-Date)
    )

    if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) {
        return [System.IO.Path]::GetFullPath($OutputRoot)
    }

    $archiveRoot = Get-PresetCalibrationArchiveRoot
    $resolved = Join-Path $archiveRoot ('Sessions\{0}\CSX Baselines\{1}' -f $SessionDate.ToString('yyyy-MM-dd'), $Collection)
    [System.IO.Directory]::CreateDirectory($resolved) | Out-Null
    [System.IO.Path]::GetFullPath($resolved)
}
