[CmdletBinding()]
param(
    [switch] $SkipHookEnvironments,
    [switch] $KeepHttpsRemotes
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$onWindows = $env:OS -eq "Windows_NT"

. (Join-Path $PSScriptRoot "tool-environment.ps1")

$scriptRepositoryRoot = Split-Path -Parent $PSScriptRoot
Enable-CsxRepositoryGitSafety -RepositoryRoot $scriptRepositoryRoot

function Invoke-GitText {
    param([Parameter(Mandatory = $true)][string[]] $Arguments)

    $value = (& git @Arguments 2>$null).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: git $($Arguments -join ' ')"
    }
    return $value
}

$repositoryRoot = Invoke-GitText -Arguments @("rev-parse", "--show-toplevel")
$commonGitDirectory = Invoke-GitText -Arguments @(
    "rev-parse",
    "--path-format=absolute",
    "--git-common-dir"
)
$toolRoot = Join-Path $commonGitDirectory "csx-tools"
$venvRoot = Join-Path $toolRoot "venv"
$venvPython = if ($onWindows) {
    Join-Path $venvRoot "Scripts\python.exe"
} else {
    Join-Path $venvRoot "bin/python"
}

Initialize-CsxToolEnvironment `
    -RepositoryRoot $repositoryRoot `
    -CommonGitDirectory $commonGitDirectory `
    -ProtectPublicGitHub | Out-Null

if (-not (Test-Path -LiteralPath $venvPython)) {
    $basePython = if ($env:CSX_BOOTSTRAP_PYTHON) {
        Get-Command $env:CSX_BOOTSTRAP_PYTHON -ErrorAction Stop
    } else {
        Get-Command python -ErrorAction Stop
    }

    $version = & $basePython.Source -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to query Python at $($basePython.Source)."
    }

    $parts = $version.Split(".")
    if ([int] $parts[0] -lt 3 -or ([int] $parts[0] -eq 3 -and [int] $parts[1] -lt 10)) {
        throw "Python 3.10 or newer is required; found $version."
    }

    Write-Host "Creating repository tool environment with Python $version..."
    & $basePython.Source -m venv $venvRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create the repository tool environment."
    }
}

$installedVersion = & $venvPython -c "import importlib.metadata; print(importlib.metadata.version('pre-commit'))" 2>$null
if ($LASTEXITCODE -ne 0 -or $installedVersion -ne "4.6.0") {
    Write-Host "Installing pinned repository developer tools..."
    $requirements = Join-Path $repositoryRoot "tools\dev-requirements.txt"
    & $venvPython -m pip install --disable-pip-version-check --requirement $requirements
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install repository developer tools."
    }
}

Push-Location $repositoryRoot
try {
    & git config --local core.hooksPath .githooks
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to configure the tracked Git hooks directory."
    }

    if ($onWindows) {
        & git config --local http.sslBackend openssl
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to configure the repository Git SSL backend."
        }

        $localSshCommand = & git config --local --get core.sshCommand 2>$null
        if (-not $localSshCommand) {
            $sshCommand = $env:CSX_GITHUB_SSH_COMMAND

            if ($sshCommand) {
                & git config --local core.sshCommand $sshCommand
                if ($LASTEXITCODE -ne 0) {
                    throw "Failed to configure the repository SSH command."
                }
                Write-Host "Configured repository-local SSH for isolated tooling."
            }
        }
    }

    if (-not $KeepHttpsRemotes) {
        foreach ($remote in (& git remote)) {
            $url = & git config --local --get "remote.$remote.url" 2>$null
            if ($LASTEXITCODE -ne 0 -or -not $url) {
                continue
            }

            if ($url -match "^https://github\.com/(.+)$") {
                $sshUrl = "git@github.com:$($Matches[1])"
                Write-Host "Using SSH for remote '$remote': $sshUrl"
                & git remote set-url $remote $sshUrl
                if ($LASTEXITCODE -ne 0) {
                    throw "Failed to update remote '$remote'."
                }
            }
        }
    }

    & $venvPython -m pre_commit validate-config .pre-commit-config.yaml
    if ($LASTEXITCODE -ne 0) {
        throw "The pre-commit configuration is invalid."
    }

    if (-not $SkipHookEnvironments) {
        Write-Host "Installing hook environments. The first run can take several minutes; do not interrupt it."
        & (Join-Path $repositoryRoot "tools\pre-commit.ps1") install-hooks
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to install pre-commit hook environments."
        }
    }
} finally {
    Pop-Location
}

Write-Host "Developer tooling is ready."
Write-Host "Run 'pwsh ./tools/dev-doctor.ps1 -Network' to verify GitHub connectivity."
