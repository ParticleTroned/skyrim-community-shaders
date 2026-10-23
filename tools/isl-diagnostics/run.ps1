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
    $OutputDirectory = Join-Path (Get-Location) ('isl-capture-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$arguments = @(
    (Join-Path $PSScriptRoot 'runner.py'), '--output', $OutputDirectory,
    '--controller', (Resolve-Path -LiteralPath $Controller).Path,
    '--runtime', (Resolve-Path -LiteralPath $Runtime).Path,
    '--build-id', $manifest.buildId, '--artifact', $dll, '--artifact-sha256', $hash,
    '--plan', $Plan, '--observation', $Observation
)
if ($OffUnlimited) { $arguments += '--off-unlimited' }
& python @arguments
exit $LASTEXITCODE
