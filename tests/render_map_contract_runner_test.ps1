[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $PythonWrapper
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$wrapperPath = (Resolve-Path -LiteralPath $PythonWrapper).Path

if ([System.IO.Path]::GetExtension($wrapperPath) -notin @('.cmd', '.bat')) {
    throw 'The render-map contract runner regression requires a .cmd or .bat Python forwarding wrapper.'
}

& (Join-Path $repoRoot 'tools/test-render-map-contracts.ps1') -PythonExecutable $wrapperPath
if ($LASTEXITCODE -ne 0) {
    throw 'The render-map contract suite failed through the Python forwarding wrapper.'
}

Write-Output 'Render-map contract runner accepted the Python forwarding wrapper and completed the suite.'
