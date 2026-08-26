[CmdletBinding()]
param(
    [switch] $Network
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$onWindows = $env:OS -eq "Windows_NT"

. (Join-Path $PSScriptRoot "tool-environment.ps1")

$scriptRepositoryRoot = Split-Path -Parent $PSScriptRoot
Enable-CsxRepositoryGitSafety -RepositoryRoot $scriptRepositoryRoot

$failures = [System.Collections.Generic.List[string]]::new()
$warnings = [System.Collections.Generic.List[string]]::new()

function Add-CheckFailure([string] $Message) {
    $script:failures.Add($Message)
    Write-Host "FAIL: $Message" -ForegroundColor Red
}

function Add-CheckWarning([string] $Message) {
    $script:warnings.Add($Message)
    Write-Host "WARN: $Message" -ForegroundColor Yellow
}

function Write-CheckPass([string] $Message) {
    Write-Host "PASS: $Message" -ForegroundColor Green
}

$repositoryRoot = (& git rev-parse --show-toplevel 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or -not $repositoryRoot) {
    throw "This command must run inside a Git worktree."
}
$commonGitDirectory = (& git rev-parse --path-format=absolute --git-common-dir).Trim()
$toolEnvironment = Initialize-CsxToolEnvironment `
    -RepositoryRoot $repositoryRoot `
    -CommonGitDirectory $commonGitDirectory `
    -ProtectPublicGitHub

$hooksPath = & git config --local --get core.hooksPath 2>$null
if ($hooksPath -eq ".githooks" -and (Test-Path -LiteralPath (Join-Path $repositoryRoot ".githooks\pre-commit"))) {
    Write-CheckPass "tracked Git hooks are enabled"
} else {
    Add-CheckFailure "the tracked pre-commit hook is unavailable; run pwsh ./tools/setup-dev.ps1"
}

$venvPython = if ($onWindows) {
    Join-Path $commonGitDirectory "csx-tools\venv\Scripts\python.exe"
} else {
    Join-Path $commonGitDirectory "csx-tools/venv/bin/python"
}
if (Test-Path -LiteralPath $venvPython) {
    $preCommitVersion = & $venvPython -m pre_commit --version 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-CheckPass $preCommitVersion
    } else {
        Add-CheckFailure "the managed Python environment cannot run pre-commit"
    }
} else {
    Add-CheckFailure "the managed developer-tool environment is missing"
}

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($cmake) {
    Write-CheckPass "cmake.exe resolves to $($cmake.Source)"
} else {
    Add-CheckFailure "cmake.exe is not available on PATH"
}

if ($onWindows) {
    try {
        if (Test-CsxMsvcEnvironment) {
            Write-CheckPass "the MSVC build environment is active"
        } else {
            $vsDevCmd = Resolve-CsxVsDevCmd -Required
            Write-CheckPass "the MSVC build environment can be initialized with $vsDevCmd"
        }
    } catch {
        Add-CheckFailure $_.Exception.Message
    }
}

try {
    $vcpkgRoot = Resolve-CsxVcpkgRoot -Required
    Write-CheckPass "vcpkg root resolves to $vcpkgRoot"
} catch {
    Add-CheckFailure $_.Exception.Message
}

foreach ($cache in ([ordered]@{
    "vcpkg downloads" = $toolEnvironment.VcpkgDownloads
    "vcpkg registries" = $toolEnvironment.VcpkgRegistries
    "vcpkg binary cache" = $toolEnvironment.VcpkgBinaryCache
}).GetEnumerator()) {
    try {
        $probe = Join-Path $cache.Value "doctor-write-probe.txt"
        [IO.File]::WriteAllText($probe, "ok")
        Remove-Item -LiteralPath $probe
        Write-CheckPass "$($cache.Key) path is writable: $($cache.Value)"
    } catch {
        Add-CheckFailure "$($cache.Key) path is not writable: $($_.Exception.Message)"
    }
}

if ($env:CODEX_CI -eq "1") {
    if ($toolEnvironment.VcpkgAssetDownloader) {
        Write-CheckPass "vcpkg uses the isolated HTTPS downloader"
    } elseif ($env:X_VCPKG_ASSET_SOURCES) {
        Write-CheckPass "vcpkg uses an explicit asset source"
    } else {
        Add-CheckFailure "vcpkg has no usable isolated HTTPS downloader"
    }

    if ($toolEnvironment.CmakeAssetDownloader) {
        Write-CheckPass "CMake uses the isolated HTTPS downloader"
    } else {
        Add-CheckFailure "CMake has no usable isolated HTTPS downloader"
    }
}

$toolTemp = Join-Path $repositoryRoot "build\tool-temp"
try {
    New-Item -ItemType Directory -Force -Path $toolTemp | Out-Null
    $probe = Join-Path $toolTemp "doctor-write-probe.txt"
    [IO.File]::WriteAllText($probe, "ok")
    Remove-Item -LiteralPath $probe
    Write-CheckPass "repository tool temp is writable"
} catch {
    Add-CheckFailure "repository tool temp is not writable: $($_.Exception.Message)"
}

if ($onWindows) {
    $sslBackend = & git config --get http.sslBackend 2>$null
    if ($sslBackend -eq "openssl") {
        Write-CheckPass "Git uses the OpenSSL backend"
    } else {
        Add-CheckFailure "Git does not use the OpenSSL backend in this repository"
    }
}

$publicHookUrl = & git ls-remote --get-url "https://github.com/pre-commit/pre-commit-hooks" 2>$null
if ($publicHookUrl -eq "https://github.com/pre-commit/pre-commit-hooks") {
    Write-CheckPass "public GitHub tool URLs remain HTTPS"
} else {
    Add-CheckFailure "public GitHub tool URLs resolve to '$publicHookUrl'; run pwsh ./tools/setup-git-user.ps1 in a normal user terminal"
}

$globalConfiguration = @(& git config --global --list 2>$null)
if ($LASTEXITCODE -ne 0) {
    Add-CheckWarning "the isolated account cannot inspect the user's .gitconfig; repository transport checks remain authoritative"
} else {
    $pushRewrite = & git config --global --get-all "url.git@github.com:.pushInsteadOf" 2>$null
    if ($pushRewrite -contains "https://github.com/") {
        Write-CheckPass "cross-repository GitHub pushes use SSH without rewriting fetches"
    } else {
        Add-CheckWarning "cross-repository push-only SSH routing is not configured; run pwsh ./tools/setup-git-user.ps1"
    }
}

foreach ($remote in (& git remote)) {
    $url = & git config --local --get "remote.$remote.url" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $url -or $url -notmatch "github\.com") {
        continue
    }

    if ($url -match "^git@github\.com:") {
        Write-CheckPass "remote '$remote' uses SSH"
    } else {
        Add-CheckWarning "remote '$remote' uses '$url'; run setup-dev.ps1 to normalize GitHub remotes"
    }
}

$staleWorktrees = @(& git worktree prune --dry-run --verbose 2>$null)
if ($staleWorktrees.Count -gt 0) {
    Add-CheckWarning "$($staleWorktrees.Count) stale worktree registrations can be pruned with 'git worktree prune --verbose'"
} else {
    Write-CheckPass "worktree registrations are consistent"
}

$objectDirectory = Join-Path $commonGitDirectory "objects"
$temporaryObjects = @(Get-ChildItem -LiteralPath $objectDirectory -Recurse -File -Filter "tmp_obj_*" -ErrorAction SilentlyContinue)
if ($temporaryObjects.Count -gt 0) {
    $temporarySize = ($temporaryObjects | Measure-Object -Property Length -Sum).Sum
    Add-CheckWarning "Git has $($temporaryObjects.Count) abandoned temporary object(s) using $([math]::Round($temporarySize / 1MB, 1)) MiB; run pwsh ./tools/clean-git-temporary-objects.ps1 -Apply"
} else {
    Write-CheckPass "Git object storage has no abandoned temporary objects"
}

$gh = Get-Command gh -ErrorAction SilentlyContinue
if ($gh) {
    & $gh.Source auth status *> $null
    if ($LASTEXITCODE -eq 0) {
        Write-CheckPass "GitHub CLI authentication is valid"
    } else {
        Add-CheckWarning "GitHub CLI authentication is invalid; SSH Git operations remain independent"
    }
}

if ($Network) {
    $env:GIT_TERMINAL_PROMPT = "0"
    & git ls-remote --exit-code origin HEAD *> $null
    if ($LASTEXITCODE -eq 0) {
        Write-CheckPass "origin is reachable without an interactive prompt"
    } else {
        Add-CheckFailure "origin is not reachable non-interactively"
    }
}

Write-Host ""
Write-Host "Doctor summary: $($failures.Count) failure(s), $($warnings.Count) warning(s)."
if ($failures.Count -gt 0) {
    exit 1
}
