param([Parameter(Mandatory)] [string] $Config)

. (Join-Path $PSScriptRoot 'ReleaseWorker.Common.ps1')
$Config = (Resolve-Path -LiteralPath $Config).Path
$settings = Get-ReleaseWorkerSettings $Config
$arguments = Get-ReleaseWorkerArguments $Config
$existing = @(Get-ScheduledTask -TaskPath '\' -ErrorAction Stop | Where-Object TaskName -EQ $settings.taskName)
if ($existing.Count -gt 0) { Assert-ReleaseWorkerTask $existing[0] $settings $arguments }
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$action = New-ScheduledTaskAction -Execute $settings.pwsh -Argument $arguments -WorkingDirectory $settings.repository
$logon = New-ScheduledTaskTrigger -AtLogOn -User $identity
$retry = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) -RepetitionInterval (New-TimeSpan -Minutes 5)
$conditions = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries -DontStopOnIdleEnd -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -MultipleInstances IgnoreNew
$conditions.Enabled = $false
$principal = New-ScheduledTaskPrincipal -UserId $identity -LogonType Interactive -RunLevel Limited
if ($existing.Count -gt 0) {
    Set-ScheduledTask -TaskName $settings.taskName -TaskPath '\' -Action $action -Trigger @($logon, $retry) `
        -Settings $conditions -Principal $principal | Out-Null
} else {
    Register-ScheduledTask -TaskName $settings.taskName -TaskPath '\' -Action $action -Trigger @($logon, $retry) `
        -Settings $conditions -Principal $principal -Description 'CSX resumable release worker v1' | Out-Null
}
$task = Get-ScheduledTask -TaskName $settings.taskName -TaskPath '\'
Assert-ReleaseWorkerTask $task $settings $arguments
if ($task.Settings.WakeToRun -or $task.Settings.StopIfGoingOnBatteries -or $task.Settings.DisallowStartIfOnBatteries -or
    $task.Settings.MultipleInstances -ne 'IgnoreNew' -or
    [System.Xml.XmlConvert]::ToTimeSpan($task.Settings.ExecutionTimeLimit) -ne [TimeSpan]::Zero) {
    Disable-ScheduledTask -TaskName $settings.taskName -TaskPath '\' | Out-Null
    throw 'Scheduled-task power or lifetime settings failed verification; the task was disabled.'
}
New-Item -ItemType Directory -Path $settings.state -Force | Out-Null
Export-ScheduledTask -TaskName $settings.taskName -TaskPath '\' |
    Set-Content -LiteralPath (Join-Path $settings.state 'scheduled-task.xml') -Encoding utf8
Enable-ScheduledTask -TaskName $settings.taskName -TaskPath '\' | Out-Null
Start-ScheduledTask -TaskName $settings.taskName -TaskPath '\'
Get-ScheduledTask -TaskName $settings.taskName -TaskPath '\' | Select-Object TaskName,State
