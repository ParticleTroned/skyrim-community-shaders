#Requires -Version 7.0
param([Parameter(Mandatory)][string]$ConfigurationPath)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'GameFtStackWait.psm1') -Force
$configuration = Get-Content -LiteralPath $ConfigurationPath -Raw | ConvertFrom-Json -AsHashtable
Invoke-OwnedStackWaitTrace @configuration
