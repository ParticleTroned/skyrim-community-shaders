[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Controller,
    [Parameter(Mandatory)][string]$ParametersPath
)
$ErrorActionPreference = 'Stop'
# Redirected JSON must not inherit a legacy console code page.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$controllerParameters = Get-Content -LiteralPath $ParametersPath -Raw -Encoding utf8 |
    ConvertFrom-Json -AsHashtable
& $Controller @controllerParameters
