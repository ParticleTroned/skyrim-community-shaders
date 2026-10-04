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

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("csx-python-wrapper-" + [guid]::NewGuid().ToString('N'))
$previousPython = $env:CSX_PYTHON
try {
    $null = New-Item -ItemType Directory -Path $temporaryRoot
    $failingWrapper = Join-Path $temporaryRoot 'failing-python.cmd'
    Set-Content -LiteralPath $failingWrapper -Value '@exit /b 17' -Encoding ascii
    $env:CSX_PYTHON = $wrapperPath
    $rejected = $false
    try {
        & (Join-Path $repoRoot 'tools/test-render-map-contracts.ps1') -PythonExecutable $failingWrapper
    } catch {
        if (-not $_.Exception.Message.Contains('Requested Python executable failed its Python 3 probe:')) {
            throw
        }
        $rejected = $true
    }
    if (-not $rejected) {
        throw 'An explicitly failing Python wrapper fell back to a healthy ambient interpreter.'
    }
} finally {
    $env:CSX_PYTHON = $previousPython
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Output 'Render-map contract runner rejected an explicit failing wrapper despite a healthy fallback.'
