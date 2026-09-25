#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$WprPath,
    [switch]$MainMenuConfirmed,
    [string]$ValidationPath = (Join-Path $PSScriptRoot '../../build/gameft-sw/recorder-validation.json')
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'GameFtStackWait.psm1') -Force
Assert-StackWaitElevation
if ((Get-Process SkyrimVR -ErrorAction SilentlyContinue) -and !$MainMenuConfirmed) {
    throw 'Validate before launching Skyrim, or supply -MainMenuConfirmed after the user confirms the main menu. Never validate during a measured hold.'
}
$contract = Get-StackWaitRecorderContract $WprPath
$root = Split-Path ([IO.Path]::GetFullPath($ValidationPath))
$directory = Join-Path $root ('validation-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$validation = [ordered]@{
    schema = 'csx-stack-wait-recorder-validation-v1'; passed = $false
    contract = $contract; evidenceDirectory = $directory; errors = @()
}
Write-StackWaitJson $ValidationPath $validation
$configurationPath = Join-Path $directory 'worker.json'
$profilePath = Join-Path $directory 'CpuStackWait.wprp'
[IO.File]::Copy((Join-Path $PSScriptRoot 'CpuStackWait.wprp'), $profilePath, $false)
Write-StackWaitJson $configurationPath @{
    Directory = $directory; WprPath = $contract.recorder.path; ProfilePath = $profilePath
    Instance = 'CSX-gameft-sw-validation-' + [guid]::NewGuid().ToString('N'); MaximumSeconds = 30
    OwnerProcessId = $PID; OwnerStartTicks = (Get-Process -Id $PID).StartTime.ToUniversalTime().Ticks
}
$worker = $null
try {
    $worker = Start-StackWaitWorker $directory $configurationPath
    try {
        $readyDeadline = [DateTime]::UtcNow.AddSeconds(15)
        while (!(Test-Path -LiteralPath (Join-Path $directory 'trace-ready.json'))) {
            if ($worker.HasExited -or [DateTime]::UtcNow -ge $readyDeadline) { throw "Recorder did not start. See $directory" }
            Start-Sleep -Milliseconds 100
        }
        # Generate bounded CPU work and waits without contacting Skyrim or fpsVR.
        $work = [Diagnostics.Stopwatch]::StartNew()
        while ($work.Elapsed.TotalSeconds -lt 3) {
            for ($index = 1; $index -lt 10000; $index++) { $null = [Math]::Sqrt($index) }
            Start-Sleep -Milliseconds 10
        }
    } finally {
        [IO.File]::WriteAllText((Join-Path $directory 'stop-trace'), 'stop owned validation trace')
    }
    $stopDeadline = [DateTime]::UtcNow.AddSeconds(120)
    while (!$worker.HasExited -and [DateTime]::UtcNow -lt $stopDeadline) { Start-Sleep -Milliseconds 200 }
    if (!$worker.HasExited) { throw "Recorder finalization is still pending. Retain its owned worker: $directory" }
    $result = Get-Content -LiteralPath (Join-Path $directory 'trace-result.json') -Raw | ConvertFrom-Json
    if ($result.state -ne 'stopped' -or $result.stopReason -ne 'requested' -or $result.errors.Count) {
        throw "Recorder failed its start/stop validation: $($result | ConvertTo-Json -Compress)"
    }
    $statisticsPath = Join-Path $directory 'trace-statistics.txt'
    & $contract.analyzer.path -i $result.etl -o $statisticsPath -a tracestats -detail stack -timespan actual > (Join-Path $directory 'xperf.log') 2>&1
    if ($LASTEXITCODE -ne 0) { throw "xperf could not analyze the validation trace. See $directory" }
    $validation.events = Test-StackWaitTraceStatistics (Get-Content -LiteralPath $statisticsPath -Raw)
    $validation.etl = @{ path = $result.etl; bytes = (Get-Item -LiteralPath $result.etl).Length; sha256 = (Get-FileHash -LiteralPath $result.etl).Hash }
    $validation.statistics = @{ path = $statisticsPath; sha256 = (Get-FileHash -LiteralPath $statisticsPath).Hash }
    $validation.passed = $true
} catch {
    $validation.errors += $_.Exception.Message
} finally {
    $validation.finishedUtc = [DateTime]::UtcNow.ToString('o')
    Write-StackWaitJson (Join-Path $directory 'validation.json') $validation
    Write-StackWaitJson $ValidationPath $validation
}
$validation | ConvertTo-Json -Depth 10
if (!$validation.passed) { throw "Recorder validation failed; no game controls sent. See $directory" }
