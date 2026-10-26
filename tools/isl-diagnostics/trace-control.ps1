#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('preflight', 'start')][string]$Action,
    [Parameter(Mandatory)][string]$RecorderDirectory,
    [string]$WprPath,
    [string]$Directory,
    [int]$OwnerProcessId
)
$ErrorActionPreference = 'Stop'
$recorder = (Resolve-Path -LiteralPath $RecorderDirectory).Path
foreach ($name in @('GameFtStackWait.psm1', 'Invoke-StackWaitWorker.ps1', 'CpuStackWait.wprp')) {
    if (!(Test-Path -LiteralPath (Join-Path $recorder $name) -PathType Leaf)) {
        throw "Missing recorder file: $name. Extract the complete toolkit."
    }
}
Import-Module (Join-Path $recorder 'GameFtStackWait.psm1') -Force
try { Assert-StackWaitElevation }
catch { throw 'Open PowerShell 7 AS ADMINISTRATOR, then run this command again. No trace or game controls have started.' }
$identity = Resolve-StackWaitWpr $WprPath
if ($Action -eq 'preflight') {
    $identity | ConvertTo-Json -Compress
    exit 0
}
if (!$Directory -or ![IO.Path]::IsPathFullyQualified($Directory) -or $OwnerProcessId -le 0) {
    throw 'An absolute NEW trace directory and live owner process are required.'
}
$owner = Get-Process -Id $OwnerProcessId -ErrorAction Stop
New-Item -ItemType Directory -Path $Directory -ErrorAction Stop | Out-Null
$profilePath = Join-Path $Directory 'CpuStackWait.wprp'
[IO.File]::Copy((Join-Path $recorder 'CpuStackWait.wprp'), $profilePath, $false)
$configurationPath = Join-Path $Directory 'worker.json'
Write-StackWaitJson $configurationPath @{
    Directory = $Directory; WprPath = $identity.path; ProfilePath = $profilePath
    Instance = 'CSX-isl-stacks-' + [guid]::NewGuid().ToString('N')
    MaximumSeconds = 900
    OwnerProcessId = $OwnerProcessId; OwnerStartTicks = $owner.StartTime.ToUniversalTime().Ticks
}
$worker = Start-StackWaitWorker $Directory $configurationPath
@{ workerProcessId = $worker.Id; directory = $Directory } | ConvertTo-Json -Compress
