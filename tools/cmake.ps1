Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$cmakeArguments = [string[]] $args
$cmake = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1

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

$vsDevCmd = Initialize-CsxMsvcEnvironment -Required
if ($vsDevCmd) {
    Write-Host "Initialized the MSVC environment with $vsDevCmd"
}

$vcpkgRoot = Resolve-CsxVcpkgRoot -Required
Write-Host "Using vcpkg at $vcpkgRoot"

if (-not $cmake) {
    $cmake = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
}
if (-not $cmake) {
    throw "CMake was not found on PATH. Install CMake or the Visual Studio CMake component."
}

& $cmake.Source @cmakeArguments
exit $LASTEXITCODE
