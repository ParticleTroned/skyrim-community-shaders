[CmdletBinding(SupportsShouldProcess)]
param(
    [switch] $Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "tool-environment.ps1")

$scriptRepositoryRoot = Split-Path -Parent $PSScriptRoot
Enable-CsxRepositoryGitSafety -RepositoryRoot $scriptRepositoryRoot

$commonGitDirectory = (& git rev-parse --path-format=absolute --git-common-dir 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or -not $commonGitDirectory) {
    throw "This command must run inside a Git worktree."
}

$objectDirectory = [IO.Path]::GetFullPath((Join-Path $commonGitDirectory "objects"))
$temporaryObjects = @(Get-ChildItem -LiteralPath $objectDirectory -Recurse -File -Filter "tmp_obj_*" -ErrorAction Stop)
$temporarySize = ($temporaryObjects | Measure-Object -Property Length -Sum).Sum

Write-Host "Found $($temporaryObjects.Count) abandoned Git temporary object(s) using $([math]::Round($temporarySize / 1MB, 1)) MiB."
if (-not $Apply -or $temporaryObjects.Count -eq 0) {
    if (-not $Apply -and $temporaryObjects.Count -gt 0) {
        Write-Host "Rerun with -Apply to remove only these tmp_obj_* files."
    }
    return
}

$gitProcesses = @(Get-Process -Name git, git-remote-https, ssh -ErrorAction SilentlyContinue)
if ($gitProcesses.Count -gt 0) {
    throw "Git-related processes are running; wait for them to finish before cleanup."
}

$validatedPaths = [System.Collections.Generic.List[string]]::new()
foreach ($temporaryObject in $temporaryObjects) {
    $path = [IO.Path]::GetFullPath($temporaryObject.FullName)
    if (-not $path.StartsWith($objectDirectory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside the Git object directory: $path"
    }
    $validatedPaths.Add($path)
}

if ($PSCmdlet.ShouldProcess(
    "$($validatedPaths.Count) tmp_obj_* files under $objectDirectory",
    "remove abandoned Git temporary objects"
)) {
    Remove-Item -LiteralPath $validatedPaths -Force
    Write-Host "Removed $($validatedPaths.Count) abandoned Git temporary object(s)."
}
