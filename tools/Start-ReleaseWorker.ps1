param([Parameter(Mandatory)] [string] $Config)

. (Join-Path $PSScriptRoot 'ReleaseWorker.Common.ps1')
$Config = (Resolve-Path -LiteralPath $Config).Path
$settings = Get-ReleaseWorkerSettings $Config
$logPath = Join-Path $settings.state 'worker.log'
New-Item -ItemType Directory -Path $settings.state -Force | Out-Null
& $settings.python -u -B (Join-Path $PSScriptRoot 'release_worker.py') --config $Config *>> $logPath
$workerExit = $LASTEXITCODE
if ($workerExit -eq 75) { exit 0 }
$statusPath = Join-Path $settings.state 'status.json'
if ($workerExit -eq 0 -and (Test-Path -LiteralPath $statusPath)) {
    $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
    if ($status.state -eq 'complete') {
        try {
            $task = Get-ScheduledTask -TaskName $settings.taskName -TaskPath '\' -ErrorAction Stop
            Assert-ReleaseWorkerTask $task $settings (Get-ReleaseWorkerArguments $Config)
            Disable-ScheduledTask -TaskName $settings.taskName -TaskPath '\' -ErrorAction Stop | Out-Null
        } catch {
            "Build completed; scheduled-task cleanup needs attention: $_" | Add-Content -LiteralPath $logPath
        }
    }
}
exit $workerExit
