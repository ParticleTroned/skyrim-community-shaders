#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Controller = (Join-Path $PSScriptRoot '../../transport/Invoke-DevBenchControl.ps1'),
    [Parameter(Mandatory)][string]$Runtime,
    [string]$OutputDirectory,
    [string]$WprPath,
    [string]$RecorderDirectory = (Join-Path $PSScriptRoot '../stack-wait'),
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$controllerPath = (Resolve-Path -LiteralPath $Controller).Path
$runtimePath = (Resolve-Path -LiteralPath $Runtime).Path
$recorderPath = (Resolve-Path -LiteralPath $RecorderDirectory).Path
$scriptRoots = @($PSScriptRoot, (Join-Path $PSScriptRoot '../reflex-measurement'),
                 (Split-Path -Parent $controllerPath), $recorderPath)
foreach ($root in $scriptRoots) {
    foreach ($file in Get-ChildItem -LiteralPath $root -File) {
        if ($file.Extension -notin @('.ps1', '.psm1', '.py')) { continue }
        $zone = Get-Content -LiteralPath $file.FullName -Stream Zone.Identifier -ErrorAction SilentlyContinue
        if ($zone -match '^ZoneId=[34]$') {
            throw "Download-blocked file: $($file.FullName). Unblock the toolkit ZIP before extracting it."
        }
    }
}
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path (Get-Location) ('isl-stacks-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
if (!$CheckOnly -and (Test-Path -LiteralPath $OutputDirectory)) {
    throw 'OutputDirectory already exists. Omit it for an automatic NEW name. Preserve earlier results.'
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if (!$CheckOnly) { Write-Host "Results will be saved to: $outputPath" }
$arguments = @((Join-Path $PSScriptRoot 'stack_runner.py'), '--controller', $controllerPath,
               '--runtime', $runtimePath, '--recorder-directory', $recorderPath, '--output', $outputPath)
if ($WprPath) { $arguments += @('--wpr-path', $WprPath) }
if ($CheckOnly) { $arguments += '--check-only' }
& python @arguments
exit $LASTEXITCODE
