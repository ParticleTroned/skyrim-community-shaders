[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputPath,
    [string]$ReportPath,
    [string[]]$AdditionalShaderRoot = @(),
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath($RepositoryRoot)
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "Repository root does not exist: $root"
}
if ($AdditionalShaderRoot.Count -gt 0) {
    throw 'AdditionalShaderRoot is no longer supported: schema v2 models the exact CMake-deployed package/features virtual namespace.'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $root 'docs\development\shader-analysis\shader-manifest.generated.json'
}
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $root 'docs\development\shader-analysis\shader-dependency-report.generated.md'
}

$pythonExe = $null
$pythonPrefix = @()
foreach ($environmentName in @('CSX_PYTHON', 'CODEX_PYTHON')) {
    $candidate = [Environment]::GetEnvironmentVariable($environmentName)
    if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        $pythonExe = [IO.Path]::GetFullPath($candidate)
        break
    }
}
if ($null -eq $pythonExe) {
    $pythonCommand = Get-Command python -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $pythonCommand) {
        $pythonExe = $pythonCommand.Source
    }
}
if ($null -eq $pythonExe) {
    $launcher = Get-Command py -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $launcher) {
        $pythonExe = $launcher.Source
        $pythonPrefix = @('-3')
    }
}
if ($null -eq $pythonExe) {
    throw 'Python 3 is required. Put python on PATH or set CSX_PYTHON to the interpreter path.'
}

$generator = Join-Path $root 'tools\shader_dependency_manifest.py'
$arguments = @(
    $generator,
    '--repository-root', $root,
    '--output', ([IO.Path]::GetFullPath($OutputPath)),
    '--report', ([IO.Path]::GetFullPath($ReportPath))
)
if ($Check) {
    $arguments += '--check'
}

& $pythonExe @pythonPrefix @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Shader dependency manifest generator failed with exit code $LASTEXITCODE."
}
