param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [string]$Branch = $Name,
    [string]$Path,
    [string]$StartPoint = "HEAD",
    [switch]$NoSubmodules,
    [switch]$ForcePresetCopy
)

. (Join-Path $PSScriptRoot "tool-environment.ps1")

$scriptRepositoryRoot = Split-Path -Parent $PSScriptRoot
Enable-CsxRepositoryGitSafety -RepositoryRoot $scriptRepositoryRoot

$repoRoot = ([string](git rev-parse --show-toplevel 2>$null)).Trim()
if (-not $repoRoot) {
    Write-Error "Run this script from within a git checkout or worktree for this repository."
    exit 1
}

$commonDir = ([string](git rev-parse --path-format=absolute --git-common-dir 2>$null)).Trim()
if (-not $commonDir) {
    Write-Error "Failed to resolve the repository common git directory."
    exit 1
}

$mainRepoRoot = Split-Path $commonDir -Parent
$repoName = Split-Path $mainRepoRoot -Leaf
$defaultWorktreeRoot = Join-Path (Split-Path $mainRepoRoot -Parent) ($repoName + ".worktrees")

if (-not $Path) {
    $Path = Join-Path $defaultWorktreeRoot $Name
}

$Path = [System.IO.Path]::GetFullPath($Path)
$targetParent = Split-Path $Path -Parent
if (-not (Test-Path $targetParent)) {
    New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
}

if (Test-Path $Path) {
    Write-Error "Target path already exists: $Path"
    exit 1
}

& git -C $mainRepoRoot show-ref --verify --quiet "refs/heads/$Branch"
$branchExists = $LASTEXITCODE -eq 0

if ($branchExists) {
    Write-Host "Creating worktree for existing branch '$Branch' at $Path"
    & git -C $mainRepoRoot worktree add $Path $Branch
}
else {
    Write-Host "Creating worktree at $Path with new branch '$Branch' from '$StartPoint'"
    & git -C $mainRepoRoot worktree add -b $Branch $Path $StartPoint
}

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $NoSubmodules) {
    Write-Host "Initializing submodules in new worktree"
    & git -C $Path submodule update --init --recursive
    $submoduleExitCode = $LASTEXITCODE
    if ($submoduleExitCode -ne 0) {
        try {
            $fetchedMissingCommit = $false
            $entries = @(& git -C $Path config -f .gitmodules --get-regexp '^submodule\..*\.path$')
            if ($LASTEXITCODE -notin @(0, 1)) {
                throw "Failed to read the new worktree's submodule paths."
            }

            foreach ($entry in $entries) {
                if ($entry -notmatch '^(submodule\..+)\.path[ \t]+(.+)$') {
                    throw "Invalid submodule path entry: $entry"
                }
                $moduleKey = $Matches[1]
                $relativePath = $Matches[2]
                $submodulePath = [IO.Path]::GetFullPath((Join-Path $Path $relativePath))
                $worktreePrefix = $Path.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
                if (-not $submodulePath.StartsWith($worktreePrefix, [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Submodule path is outside the new worktree: $relativePath"
                }

                # An empty submodule directory otherwise resolves to its parent repository.
                if (-not (Test-Path -LiteralPath (Join-Path $submodulePath ".git"))) {
                    continue
                }
                Add-CsxGitCommandConfig -Key "safe.directory" -Value $submodulePath.Replace('\', '/')
                $checkoutRoot = [string](& git -C $submodulePath rev-parse --show-toplevel)
                if ($LASTEXITCODE -ne 0 -or
                    -not [string]::Equals([IO.Path]::GetFullPath($checkoutRoot), $submodulePath, [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Cannot verify the submodule checkout: $relativePath"
                }

                $gitlink = @(& git --literal-pathspecs -C $Path ls-tree HEAD -- $relativePath)
                if ($LASTEXITCODE -ne 0 -or $gitlink.Count -ne 1 -or
                    $gitlink[0] -notmatch '^160000 commit ([0-9a-f]{40}|[0-9a-f]{64})\t') {
                    throw "Cannot resolve the pinned submodule commit: $relativePath"
                }
                $pinnedCommit = $Matches[1]
                $null = & git -C $submodulePath rev-parse --verify --quiet "${pinnedCommit}^{commit}"
                if ($LASTEXITCODE -eq 0) {
                    continue
                }
                if ($LASTEXITCODE -ne 1) {
                    throw "Cannot check the pinned commit in $relativePath."
                }

                $canonicalUrl = [string](& git -C $Path config -f .gitmodules --get "$moduleKey.url")
                if ($LASTEXITCODE -ne 0 -or -not $canonicalUrl) {
                    throw "No canonical submodule URL is configured for $relativePath."
                }
                if ($canonicalUrl -notmatch '^[A-Za-z][A-Za-z0-9+.-]*://' -and
                    $canonicalUrl -notmatch '^(?:[^/@:\s]+@)?[^/:\s]+:.+' -and
                    -not [IO.Path]::IsPathRooted($canonicalUrl)) {
                    throw "Recovery requires an absolute submodule URL for $relativePath."
                }

                Write-Host "Fetching missing pinned commit $pinnedCommit for $relativePath"
                & git -C $submodulePath fetch --no-tags --no-recurse-submodules --no-write-fetch-head -- $canonicalUrl $pinnedCommit
                if ($LASTEXITCODE -ne 0) {
                    throw "Failed to fetch the pinned commit for $relativePath."
                }
                $null = & git -C $submodulePath rev-parse --verify --quiet "${pinnedCommit}^{commit}"
                if ($LASTEXITCODE -ne 0) {
                    throw "The pinned commit is still unavailable for $relativePath."
                }
                $fetchedMissingCommit = $true
            }

            if ($fetchedMissingCommit) {
                Write-Host "Retrying recursive submodule initialization once"
                & git -C $Path submodule update --init --recursive
                $submoduleExitCode = $LASTEXITCODE
            }
        }
        catch {
            Write-Warning "Submodule recovery failed: $($_.Exception.Message)"
        }
    }
    if ($submoduleExitCode -ne 0) {
        Write-Error "Submodule initialization failed. The worktree was created but may be incomplete."
        exit $submoduleExitCode
    }
}

$sourcePreset = Join-Path $mainRepoRoot "CMakeUserPresets.json"
$targetPreset = Join-Path $Path "CMakeUserPresets.json"

if (Test-Path $sourcePreset) {
    if ((-not (Test-Path $targetPreset)) -or $ForcePresetCopy) {
        try {
            Copy-Item $sourcePreset $targetPreset -Force -ErrorAction Stop
            Write-Host "Copied CMakeUserPresets.json into the new worktree"
        }
        catch {
            Write-Error "Failed to copy CMakeUserPresets.json: $($_.Exception.Message)"
            exit 1
        }
    }
    else {
        Write-Host "Skipped preset copy because the worktree already has CMakeUserPresets.json"
    }
}
else {
    Write-Host "No CMakeUserPresets.json found in the main repo checkout; skipping preset copy"
}

Write-Host "Worktree ready: $Path"
