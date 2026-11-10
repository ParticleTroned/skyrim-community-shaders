$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
. (Join-Path $repository 'tools/ReleaseWorker.Common.ps1')
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$settings = [pscustomobject]@{ pwsh = (Get-Command pwsh).Source }
$arguments = '-NoProfile -File "worker.ps1" -Config "config.json"'

function New-TestTask {
    return [pscustomobject]@{
        TaskPath = '\'
        Actions = @([pscustomobject]@{ Execute = $settings.pwsh; Arguments = $arguments })
        Principal = [pscustomobject]@{ UserId = $identity.User.Value; LogonType = 'Interactive'; RunLevel = 'Limited' }
        Description = 'CSX resumable release worker v1'
    }
}

Assert-ReleaseWorkerTask (New-TestTask) $settings $arguments
$shortNameTask = New-TestTask
$shortNameTask.Principal.UserId = $identity.Name.Split('\')[-1]
Assert-ReleaseWorkerTask $shortNameTask $settings $arguments
$changes = @(
    { param($task) $task.Actions[0].Arguments = 'another worker' },
    { param($task) $task.Actions[0].Execute = 'another executable' },
    { param($task) $task.Principal.UserId = 'another user' },
    { param($task) $task.Principal.RunLevel = 'Highest' },
    { param($task) $task.Principal.LogonType = 'Password' },
    { param($task) $task.Description = 'unrelated task' },
    { param($task) $task.TaskPath = '\Other\' }
)
foreach ($change in $changes) {
    $task = New-TestTask
    & $change $task
    $rejected = $false
    try { Assert-ReleaseWorkerTask $task $settings $arguments } catch { $rejected = $true }
    if (-not $rejected) { throw 'Task ownership mutation was accepted.' }
}
foreach ($name in @('ReleaseWorker.Common.ps1', 'Start-ReleaseWorker.ps1', 'Register-ReleaseWorker.ps1')) {
    $parseErrors = $null
    $tokens = $null
    [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $repository "tools/$name"), [ref]$tokens, [ref]$parseErrors) | Out-Null
    if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
}
Write-Output 'Passed task ownership (7 rejected mutations) and PowerShell syntax checks.'
