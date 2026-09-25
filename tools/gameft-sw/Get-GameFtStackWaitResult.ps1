#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'GameFtStackWait.psm1') -Force
Get-StackWaitResult $RunDirectory | ConvertTo-Json -Depth 10
