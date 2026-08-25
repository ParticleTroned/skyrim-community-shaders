Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$gitArguments = [string[]] $args

. (Join-Path $PSScriptRoot "tool-environment.ps1")

$repositoryRoot = Split-Path -Parent $PSScriptRoot
Enable-CsxRepositoryGitSafety -RepositoryRoot $repositoryRoot

$git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $git) {
    throw "git.exe was not found on PATH. Install Git for Windows."
}

Push-Location $repositoryRoot
try {
    & $git.Source @gitArguments
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
