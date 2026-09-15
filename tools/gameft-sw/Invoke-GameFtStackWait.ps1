#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SaveNumberText,
    [string]$RunDirectory,
    [string]$GameFtDirectory = (Join-Path $PSScriptRoot '../../build/bisect/protocol'),
    [string]$Controller = 'C:\Users\Win10\.codex\plugins\cache\skyrim-vr-tools\skyrim-vr-automation\0.9.0+codex.20260903132432\tools\devbench-control\Invoke-DevBenchControl.ps1',
    [string]$FpsVrCmd = 'C:\Program Files (x86)\Steam\steamapps\common\fpsVR\fpsVRcmd.exe',
    [ValidateRange(1, 65535)][int]$DevBenchPort = 8921,
    [string]$WprPath,
    [string]$RecorderValidationPath = (Join-Path $PSScriptRoot '../../build/gameft-sw/recorder-validation.json'),
    [string]$ArchiveDirectory = 'D:\Coding\GitHub\CS logs'
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'GameFtStackWait.psm1') -Force
if ($SaveNumberText -notmatch '^\s*\d+(\s*,\s*\d+)*\s*$') {
    throw 'Which save numbers should I load? Give comma-separated numbers, for example 05 or 05, 07 or 05, 07, 12.'
}
$numbers = @($SaveNumberText -split ',' | ForEach-Object { [int]::Parse($_.Trim()) })
$hashes = Assert-GameFtScripts $GameFtDirectory
Assert-StackWaitElevation
$WprPath = (Resolve-StackWaitWpr $WprPath).path
$recorderValidation = Assert-StackWaitRecorderValidation $WprPath $RecorderValidationPath
foreach ($path in @($Controller, $WprPath, $FpsVrCmd)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required tool missing: $path" }
}
$games = @(Get-Process SkyrimVR -ErrorAction SilentlyContinue)
if ($games.Count -ne 1) { throw 'Exactly one SkyrimVR process must be running at the main menu.' }
$game = $games[0]
$gameStartTicks = $game.StartTime.ToUniversalTime().Ticks
if (!$RunDirectory) {
    $RunDirectory = Join-Path $PSScriptRoot ('../../build/bisect/measurements/gameft-sw-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
}
$RunDirectory = [IO.Path]::GetFullPath($RunDirectory)
if (Test-Path -LiteralPath $RunDirectory) { throw "A new empty run directory is required: $RunDirectory" }
New-Item -ItemType Directory -Path $RunDirectory | Out-Null
$traceDirectory = Join-Path $RunDirectory 'stack-wait'
New-Item -ItemType Directory -Path $traceDirectory | Out-Null
$runtimePath = Join-Path $traceDirectory 'runtime.json'
Write-StackWaitJson $runtimePath @{ port = $DevBenchPort; pid = $game.Id; exe = 'SkyrimVR.exe' }
$record = [ordered]@{
    schema = 'gameft-sw-v1'; protocol = 'gameft-sw'; baseProtocol = 'game-ft'
    saves = @($numbers | ForEach-Object { $_.ToString('00') }); baseScriptHashes = $hashes
    timingComparability = 'stack/wait tracing active; compare with identically traced runs'
    provenance = 'pending until timing/health results have been presented'; processId = $game.Id
    processStartTicks = $gameStartTicks; errors = @(); measurementComplete = $false
    recorder = $recorderValidation.contract.recorder; recorderValidationPath = [IO.Path]::GetFullPath($RecorderValidationPath)
}
$worker = $null
try {
    $before = Invoke-StackWaitSnapshot $Controller $runtimePath $traceDirectory 'before' $game.Id
    $record.runtimeBuildId = $before.producer.buildId
    $instance = 'CSX-gameft-sw-' + [guid]::NewGuid().ToString('N')
    $configurationPath = Join-Path $traceDirectory 'worker.json'
    $profilePath = Join-Path $traceDirectory 'CpuStackWait.wprp'
    [IO.File]::Copy((Join-Path $PSScriptRoot 'CpuStackWait.wprp'), $profilePath, $false)
    $record.traceProfileSha256 = (Get-FileHash -LiteralPath $profilePath).Hash
    Write-StackWaitJson $configurationPath @{
        Directory = $traceDirectory; WprPath = $WprPath
        ProfilePath = $profilePath
        Instance = $instance; MaximumSeconds = 30 + 240 * $numbers.Count
        OwnerProcessId = $PID; OwnerStartTicks = (Get-Process -Id $PID).StartTime.ToUniversalTime().Ticks
    }
    $worker = Start-StackWaitWorker $traceDirectory $configurationPath
    $readyDeadline = [DateTime]::UtcNow.AddSeconds(15)
    while (!(Test-Path -LiteralPath (Join-Path $traceDirectory 'trace-ready.json'))) {
        if ($worker.HasExited -or [DateTime]::UtcNow -ge $readyDeadline) {
            throw "Stack/wait recorder did not start; no saves loaded. See $traceDirectory"
        }
        Start-Sleep -Milliseconds 100
    }
    Write-Output "gameft-sw recording started: $RunDirectory"
    # Delegate every load, timing boundary, fpsVR control and health sample unchanged.
    & (Join-Path $GameFtDirectory 'Invoke-SaveLoadTimingV2.ps1') -RunDirectory $RunDirectory `
        -SaveNumberText $SaveNumberText -FpsVrCmd $FpsVrCmd -DevBenchPort $DevBenchPort
    $markers = @(Get-Content -LiteralPath (Join-Path $RunDirectory 'markers.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    $record.measurementComplete = @($markers | Where-Object { $_.label -eq 'requested-holds-complete' }).Count -eq 1 -and
        @($markers | Where-Object { $_.label -match 'error$' }).Count -eq 0
    if (!$record.measurementComplete) { $record.errors += 'Base game-ft measurement or cleanup is incomplete; see its markers.' }
} catch {
    $record.errors += $_.Exception.Message
} finally {
    if ($worker) { [IO.File]::WriteAllText((Join-Path $traceDirectory 'stop-trace'), 'stop owned trace') }
    if ($record.Contains('runtimeBuildId')) {
        try {
            $current = Get-Process -Id $game.Id -ErrorAction Stop
            if ($current.StartTime.ToUniversalTime().Ticks -ne $gameStartTicks) { throw 'The measured SkyrimVR process exited/restarted.' }
            $after = Invoke-StackWaitSnapshot $Controller $runtimePath $traceDirectory 'after' $game.Id $record.runtimeBuildId
            $record.observerCounts = @{ before = $before.acceptedDraws.subscribers; after = $after.acceptedDraws.subscribers }
        } catch { $record.errors += $_.Exception.Message }
    }
    Write-StackWaitJson (Join-Path $RunDirectory 'gameft-sw.json') $record
}

# Results precede ETL merging, symbol analysis and physical-DLL provenance.
if (Test-Path -LiteralPath (Join-Path $RunDirectory 'markers.jsonl')) {
    try {
        Save-GameFtRawArchive $RunDirectory $ArchiveDirectory
        & (Join-Path $GameFtDirectory 'Show-SaveLoadTimingQuickReport.ps1') -RunDirectory $RunDirectory
        Write-Output 'PRESENT THE TIMING/HEALTH TABLE NOW. Physical DLL provenance remains pending.'
    } catch {
        $record.errors += $_.Exception.Message
        Write-Output "Timing report incomplete: $($_.Exception.Message)"
    }
}
Write-Output "Stack/wait evidence: $traceDirectory"
$traceResult = Get-StackWaitResult $RunDirectory
$record.trace = $traceResult
Write-Output "Stack/wait finalization: $($traceResult.state)"
if ($traceResult.state -ne 'pending' -and !$traceResult.stackWaitComplete) { $record.errors += 'Stack/wait capture incomplete; see trace-result.json.' }
foreach ($failure in $record.errors) { Write-Output "INCOMPLETE: $failure" }
Write-StackWaitJson (Join-Path $RunDirectory 'gameft-sw.json') $record
if ($record.errors.Count) { throw "gameft-sw incomplete; evidence retained at $RunDirectory" }
