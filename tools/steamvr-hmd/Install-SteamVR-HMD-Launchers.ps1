#Requires -Version 7.5
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Low')]
param(
    [string]$DesktopPath = [Environment]::GetFolderPath('Desktop'),
    [string]$InstallDirectory = $(Join-Path $env:LOCALAPPDATA 'CSX/SteamVR-HMD'),
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($DesktopPath) -or -not (Test-Path -LiteralPath $DesktopPath -PathType Container)) {
    throw 'Desktop folder could not be found. Specify -DesktopPath.'
}
$source = Join-Path $PSScriptRoot 'Toggle-SteamVR-HMD.ps1'
$installedScript = Join-Path $InstallDirectory 'Toggle-SteamVR-HMD.ps1'
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop).Source
$links = @(
    @{ Name = 'SteamVR NULL'; Mode = 'Null'; Start = $true },
    @{ Name = 'SteamVR PIMAX'; Mode = 'Pimax'; Start = $true },
    @{ Name = 'SteamVR Toggle'; Mode = 'Toggle'; Start = $false }
)
# Check all destinations before installing anything. Explicit -Force permits
# replacement of an older installation; default preserves existing buttons.
$destinations = @($installedScript) + @($links | ForEach-Object { Join-Path $DesktopPath ($_.Name + '.lnk') })
foreach ($destination in $destinations) {
    if ((Test-Path -LiteralPath $destination) -and -not $Force) { throw "Already exists: $destination. Use -Force to replace these launchers." }
}
if ($PSCmdlet.ShouldProcess($InstallDirectory, 'Install SteamVR switch script and three desktop shortcuts')) {
    [IO.Directory]::CreateDirectory($InstallDirectory) | Out-Null
    Copy-Item -LiteralPath $source -Destination $installedScript -Force
    $shell = New-Object -ComObject WScript.Shell
    try {
        foreach ($link in $links) {
            $shortcutPath = Join-Path $DesktopPath ($link.Name + '.lnk')
            $shortcut = $shell.CreateShortcut($shortcutPath)
            try {
                $shortcut.TargetPath = $pwsh
                $shortcut.Arguments = '-NoLogo -NoProfile -ExecutionPolicy Bypass -File "{0}" -Mode {1} -Pause{2}' -f $installedScript, $link.Mode, $(if ($link.Start) { ' -StartSteamVR' } else { '' })
                $shortcut.WorkingDirectory = $InstallDirectory
                $shortcut.Description = if ($link.Start) { "Select $($link.Mode) mode and start SteamVR. Close SteamVR first." } else { 'Toggle SteamVR NULL/PIMAX settings. Close SteamVR first; start SteamVR afterward.' }
                $shortcut.IconLocation = "$env:SystemRoot\System32\shell32.dll,21"
                $shortcut.Save()
            }
            finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
            Write-Output $shortcutPath
        }
    }
    finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
    Write-Output "Installed script: $installedScript"
}
