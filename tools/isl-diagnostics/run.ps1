[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Controller,
    [Parameter(Mandatory)][string]$Runtime,
    # Accepted for older commands; diagnostic support is discovered from the live API.
    [string]$PluginDirectory,
    [string]$OutputDirectory,
    [ValidateSet('core', 'luminance-cost', 'both', 'particle', 'shadow', 'editor', 'conversion', 'observer', 'prune', 'mountains', 'batch')]
    [string]$Plan = 'core',
    [ValidateSet('off', 'counters', 'timed')][string]$Observation = 'counters',
    [switch]$OffUnlimited,
    [switch]$DisableNeuralRendering
)
$ErrorActionPreference = 'Stop'
$controllerPath = (Resolve-Path -LiteralPath $Controller).Path
$requiredScripts = @(
    $controllerPath,
    (Join-Path (Split-Path -Parent $controllerPath) 'DevBenchControl.psm1'),
    (Join-Path $PSScriptRoot 'check-window.ps1'),
    (Join-Path $PSScriptRoot '../reflex-measurement/invoke-controller.ps1')
)
foreach ($scriptPath in $requiredScripts) {
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        throw "Required toolkit file is missing: $scriptPath. Extract the complete toolkit ZIP."
    }
    $zone = Get-Content -LiteralPath $scriptPath -Stream Zone.Identifier -ErrorAction SilentlyContinue
    if ($zone -match '^ZoneId=[34]$') {
        throw "Toolkit file is download-blocked: $scriptPath. Unblock the toolkit files using TESTER-INSTRUCTIONS.txt before retrying."
    }
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path (Get-Location) ('isl-capture-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw 'OutputDirectory already exists. Choose a NEW folder, or omit -OutputDirectory for an automatic name. Preserve earlier results.'
}
Write-Host "Results will be saved to: $([IO.Path]::GetFullPath($OutputDirectory))"
$arguments = @(
    (Join-Path $PSScriptRoot 'runner.py'), '--output', $OutputDirectory,
    '--controller', $controllerPath,
    '--runtime', (Resolve-Path -LiteralPath $Runtime).Path,
    '--plan', $Plan, '--observation', $Observation
)
if ($OffUnlimited) { $arguments += '--off-unlimited' }
if ($DisableNeuralRendering) { $arguments += '--disable-neural-rendering' }
& python @arguments
$testExitCode = $LASTEXITCODE
if ($testExitCode -ne 0) {
    Write-Host "TEST FAILED / INCOMPLETE (exit $testExitCode). Preserve the output folder and console errors."
}
exit $testExitCode
