#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Etl,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$RecorderDirectory = (Join-Path $PSScriptRoot '../stack-wait'),
    [string]$XperfPath = (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Windows Performance Toolkit/xperf.exe')
)
$ErrorActionPreference = 'Stop'
$tracePath = (Resolve-Path -LiteralPath $Etl).Path
$analyzer = (Resolve-Path -LiteralPath $XperfPath).Path
Import-Module (Join-Path $RecorderDirectory 'GameFtStackWait.psm1') -Force
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a NEW analysis output directory.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$directory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$statistics = Join-Path $directory 'trace-statistics.txt'
$result = @{ complete = $false; errors = @(); meaning = 'Global event/stack availability only; inspect the Skyrim thread stacks and clock coverage before attribution.' }
try {
    & $analyzer -i $tracePath -o $statistics -a tracestats -detail stack -timespan actual *> (Join-Path $directory 'xperf.log')
    if ($LASTEXITCODE -ne 0) { throw "xperf exited $LASTEXITCODE. See xperf.log." }
    $result.events = Test-StackWaitTraceStatistics (Get-Content -LiteralPath $statistics -Raw)
    $result.complete = $true
} catch {
    $result.errors += $_.Exception.Message
} finally {
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $directory 'stack-statistics-result.json') -Encoding utf8
}
$result | ConvertTo-Json -Depth 8
if (!$result.complete) { exit 1 }
