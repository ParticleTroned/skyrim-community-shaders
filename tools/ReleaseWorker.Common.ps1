$ErrorActionPreference = 'Stop'

function Get-ReleaseWorkerSettings([string] $Config) {
    $configPath = (Resolve-Path -LiteralPath $Config).Path
    $settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    if (-not $settings.python -or -not $settings.pwsh -or -not $settings.taskName) {
        throw 'Scheduled workers require explicit python, pwsh and taskName settings.'
    }
    $validated = & $settings.python -B (Join-Path $PSScriptRoot 'release_worker.py') --config $configPath --validate-config
    if ($LASTEXITCODE -ne 0) { throw 'Release worker configuration validation failed.' }
    return ($validated | ConvertFrom-Json)
}

function Get-ReleaseWorkerArguments([string] $Config) {
    $launcher = Join-Path $PSScriptRoot 'Start-ReleaseWorker.ps1'
    $configPath = (Resolve-Path -LiteralPath $Config).Path
    if ($launcher.Contains('"') -or $configPath.Contains('"')) { throw 'Invalid task argument path.' }
    return '-NoLogo -NoProfile -WindowStyle Hidden -File "' + $launcher + '" -Config "' + $configPath + '"'
}

function Assert-ReleaseWorkerTask($Task, $Settings, [string] $Arguments) {
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principalSid = if ($Task.Principal.UserId.StartsWith('S-1-')) {
            [System.Security.Principal.SecurityIdentifier]::new($Task.Principal.UserId).Value
        } else {
            [System.Security.Principal.NTAccount]::new($Task.Principal.UserId).Translate(
                [System.Security.Principal.SecurityIdentifier]).Value
        }
    } catch { throw 'Task principal could not be resolved; no changes were made.' }
    if ($Task.TaskPath -ne '\' -or $Task.Actions.Count -ne 1 -or
        $Task.Actions[0].Execute -ne $Settings.pwsh -or $Task.Actions[0].Arguments -cne $Arguments -or
        $principalSid -ne $identity.User.Value -or
        $Task.Principal.LogonType.ToString() -ne 'Interactive' -or $Task.Principal.RunLevel.ToString() -ne 'Limited' -or
        $Task.Description -ne 'CSX resumable release worker v1') {
        throw 'A different task owns this name; no changes were made.'
    }
}
