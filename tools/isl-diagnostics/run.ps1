[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Controller,
    [Parameter(Mandatory)][string]$Runtime,
    [Parameter(Mandatory)][string]$PluginDirectory,
    [string]$OutputDirectory,
    [ValidateSet('core', 'luminance-cost', 'both', 'particle', 'shadow', 'editor', 'conversion', 'observer', 'prune', 'mountains', 'batch')]
    [string]$Plan = 'core',
    [ValidateSet('off', 'counters', 'timed')][string]$Observation = 'counters',
    [switch]$OffUnlimited
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
$plugin = (Resolve-Path -LiteralPath $PluginDirectory).Path
$dll = Join-Path $plugin 'CommunityShaders.dll'
$manifest = Get-Content -LiteralPath (Join-Path $plugin 'CSX.BuildManifest.json') -Raw | ConvertFrom-Json
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
if ($manifest.artifact.sha256 -ne $hash -or $manifest.artifact.sizeBytes -ne (Get-Item -LiteralPath $dll).Length) {
    throw 'The DLL and its adjacent build manifest do not match.'
}
if ($manifest.identity.build.options.DEVBENCH_BRIDGE -ne 'ON') {
    throw 'This DLL was not compiled with DevBench diagnostics.'
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
    '--build-id', $manifest.buildId, '--artifact', $dll, '--artifact-sha256', $hash,
    '--plan', $Plan, '--observation', $Observation
)
if ($OffUnlimited) { $arguments += '--off-unlimited' }
& python @arguments
$testExitCode = $LASTEXITCODE
if ($testExitCode -ne 0) {
    Write-Host "TEST FAILED / INCOMPLETE (exit $testExitCode). Preserve the output folder and console errors."
}
exit $testExitCode
