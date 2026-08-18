[CmdletBinding()]
param(
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\MGO-Presets'),
    [switch]$Check
)

$arguments = @{
    PolicyPath = (Join-Path $PSScriptRoot '..\docs\development\preset-automation\style-range-probe-policy.json')
    OutputRoot = $OutputRoot
}
if ($Check) {
    $arguments.Check = $true
}

& (Join-Path $PSScriptRoot 'generate-style-prototypes.ps1') @arguments
