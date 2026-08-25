Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$cmakeArguments = [string[]] $args

. (Join-Path $PSScriptRoot "tool-environment.ps1")

$scriptRepositoryRoot = Split-Path -Parent $PSScriptRoot
Enable-CsxRepositoryGitSafety -RepositoryRoot $scriptRepositoryRoot

$repositoryRoot = (& git rev-parse --show-toplevel 2>$null).Trim()
if (-not $repositoryRoot) {
    $repositoryRoot = Split-Path -Parent $PSScriptRoot
}

$commonGitDirectory = (& git rev-parse --path-format=absolute --git-common-dir 2>$null).Trim()
if (-not $commonGitDirectory) {
    throw "Unable to resolve the repository's common Git directory."
}
Initialize-CsxToolEnvironment `
    -RepositoryRoot $repositoryRoot `
    -CommonGitDirectory $commonGitDirectory `
    -ProtectPublicGitHub | Out-Null

$vcpkgRoot = Resolve-CsxVcpkgRoot -Required
Write-Host "Using vcpkg at $vcpkgRoot"

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmake) {
    throw "cmake.exe was not found on PATH. Install CMake or the Visual Studio CMake component."
}

& $cmake.Source @cmakeArguments
exit $LASTEXITCODE
