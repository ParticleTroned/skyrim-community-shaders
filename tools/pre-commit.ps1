Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$onWindows = $env:OS -eq "Windows_NT"

$supportedCommands = @("run", "hook", "install-hooks", "validate-config", "gc")
$command = "run"
$preCommitArguments = [string[]] $args
if ($preCommitArguments.Count -gt 0 -and $preCommitArguments[0] -in $supportedCommands) {
    $command = $preCommitArguments[0]
    $preCommitArguments = [string[]] @($preCommitArguments | Select-Object -Skip 1)
}

. (Join-Path $PSScriptRoot "tool-environment.ps1")

$scriptRepositoryRoot = Split-Path -Parent $PSScriptRoot
Enable-CsxRepositoryGitSafety -RepositoryRoot $scriptRepositoryRoot

function Get-RepositoryPath {
    param([Parameter(Mandatory = $true)][string[]] $Arguments)

    $value = (& git @Arguments 2>$null).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $value) {
        throw "Unable to resolve repository metadata with: git $($Arguments -join ' ')"
    }
    return $value
}

$repositoryRoot = Get-RepositoryPath -Arguments @("rev-parse", "--show-toplevel")
$commonGitDirectory = Get-RepositoryPath -Arguments @(
    "rev-parse",
    "--path-format=absolute",
    "--git-common-dir"
)

$cacheRoot = if ($env:CSX_PRE_COMMIT_HOME) {
    $env:CSX_PRE_COMMIT_HOME
} else {
    Join-Path $commonGitDirectory "pre-commit-cache"
}
$toolRoot = Join-Path $commonGitDirectory "csx-tools"
Initialize-CsxToolEnvironment `
    -RepositoryRoot $repositoryRoot `
    -CommonGitDirectory $commonGitDirectory `
    -ProtectPublicGitHub | Out-Null
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null

$env:PRE_COMMIT_HOME = $cacheRoot

$managedPython = if ($onWindows) {
    Join-Path $toolRoot "venv\Scripts\python.exe"
} else {
    Join-Path $toolRoot "venv/bin/python"
}

$pythonCandidates = @()
if ($env:CSX_PRE_COMMIT_PYTHON) {
    $pythonCandidates += $env:CSX_PRE_COMMIT_PYTHON
}
$pythonCandidates += $managedPython

$systemPython = Get-Command python -ErrorAction SilentlyContinue
if ($systemPython) {
    $pythonCandidates += $systemPython.Source
}

$python = $null
foreach ($candidate in $pythonCandidates | Select-Object -Unique) {
    if (-not (Test-Path -LiteralPath $candidate)) {
        continue
    }

    & $candidate -c "import pre_commit" 2>$null
    if ($LASTEXITCODE -eq 0) {
        $python = $candidate
        break
    }
}

if (-not $python) {
    throw @"
The repository pre-commit runner is not installed.
Run: pwsh ./tools/setup-dev.ps1
"@
}

[string[]] $preCommitInvocation = switch ($command) {
    "run" { @("run") + $PreCommitArguments }
    "hook" { @("run", "--hook-stage", "pre-commit") }
    "install-hooks" { @("install-hooks") }
    "validate-config" { @("validate-config", ".pre-commit-config.yaml") }
    "gc" { @("gc") }
}

Push-Location $repositoryRoot
try {
    & $python -m pre_commit @preCommitInvocation
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
