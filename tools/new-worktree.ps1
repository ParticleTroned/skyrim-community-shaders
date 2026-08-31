param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [string]$Branch = $Name,
    [string]$Path,
    [string]$StartPoint,
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

$commonIsBare = ([string](& git --git-dir=$commonDir config --get --bool core.bare 2>$null)).Trim() -eq "true"
if ($commonIsBare) {
    $repoName = [System.IO.Path]::GetFileNameWithoutExtension($commonDir)
    $repositoryArguments = @("--git-dir=$commonDir")
    $presetSourceRoot = $repoRoot
    $defaultWorktreeRoot = Join-Path (Split-Path $repoRoot -Parent) ($repoName + ".worktrees")
}
else {
    $mainRepoRoot = Split-Path $commonDir -Parent
    $repoName = Split-Path $mainRepoRoot -Leaf
    $repositoryArguments = @("-C", $mainRepoRoot)
    $presetSourceRoot = $mainRepoRoot
    $defaultWorktreeRoot = Join-Path (Split-Path $mainRepoRoot -Parent) ($repoName + ".worktrees")
}

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

& git @repositoryArguments show-ref --verify --quiet "refs/heads/$Branch"
$branchExists = $LASTEXITCODE -eq 0

if ($branchExists) {
    Write-Host "Creating worktree for existing branch '$Branch' at $Path"
    & git @repositoryArguments worktree add $Path $Branch
}
else {
    if ([string]::IsNullOrWhiteSpace($StartPoint)) {
        $configuredStartPoint = ([string](& git @repositoryArguments config --get csx.worktreeStartPoint 2>$null)).Trim()
        if ($LASTEXITCODE -eq 0 -and $configuredStartPoint) {
            $StartPoint = $configuredStartPoint
            $startPointSource = "csx.worktreeStartPoint"
        }
        else {
            $remoteHead = ([string](& git @repositoryArguments symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>$null)).Trim()
            if ($LASTEXITCODE -eq 0 -and $remoteHead) {
                $StartPoint = $remoteHead
                $startPointSource = "origin/HEAD"
            }
            else {
                $StartPoint = "HEAD"
                $startPointSource = "current HEAD fallback"
            }
        }
    }
    else {
        $startPointSource = "explicit -StartPoint"
    }

    & git @repositoryArguments rev-parse --verify --quiet "$StartPoint^{commit}"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Worktree start point '$StartPoint' from $startPointSource does not resolve to a commit."
        exit 1
    }

    Write-Host "Creating worktree at $Path with new branch '$Branch' from '$StartPoint'"
    Write-Host "Start point source: $startPointSource"
    & git @repositoryArguments worktree add -b $Branch $Path $StartPoint
}

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($commonIsBare) {
    $worktreeConfigEnabled = ([string](& git --git-dir=$commonDir config --get --bool extensions.worktreeConfig 2>$null)).Trim() -eq "true"
    if (-not $worktreeConfigEnabled) {
        Write-Error "The common repository is bare but extensions.worktreeConfig is not enabled. The worktree was created but cannot be made operational safely."
        exit 1
    }

    $dotGitFile = Join-Path $Path ".git"
    $gitDirLine = (Get-Content -LiteralPath $dotGitFile -Raw).Trim()
    if ($gitDirLine -notmatch '^gitdir:\s*(.+)$') {
        Write-Error "The new worktree has a malformed .git file: $dotGitFile"
        exit 1
    }
    $worktreeAdminDir = $Matches[1].Trim()
    if (-not [System.IO.Path]::IsPathRooted($worktreeAdminDir)) {
        $worktreeAdminDir = [System.IO.Path]::GetFullPath((Join-Path $Path $worktreeAdminDir))
    }

    & git --git-dir=$worktreeAdminDir --work-tree=$Path config --worktree core.bare false
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to set core.bare=false for the new linked worktree. The worktree was created but is not operational."
        exit $LASTEXITCODE
    }
}

$insideWorktree = ([string](& git -C $Path rev-parse --is-inside-work-tree 2>$null)).Trim()
if ($LASTEXITCODE -ne 0 -or $insideWorktree -ne "true") {
    Write-Error "Git does not recognize the newly created path as a working tree."
    exit 1
}

if (-not $NoSubmodules) {
    Write-Host "Initializing submodules in new worktree"
    & git -C $Path submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Submodule initialization failed. The worktree was created but may be incomplete."
        exit $LASTEXITCODE
    }
}

$sourcePreset = Join-Path $presetSourceRoot "CMakeUserPresets.json"
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
