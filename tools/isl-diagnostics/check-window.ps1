param(
    [Parameter(Mandatory)][string]$ModulePath,
    [Parameter(Mandatory)][string]$BeforePath,
    [Parameter(Mandatory)][string]$AfterPath
)
$ErrorActionPreference = 'Stop'
Import-Module $ModulePath -Force
$before = Get-Content -LiteralPath $BeforePath -Raw | ConvertFrom-Json
$after = Get-Content -LiteralPath $AfterPath -Raw | ConvertFrom-Json
if (-not $before.data.performanceWindow.valid -or -not $after.data.performanceWindow.valid) {
    throw 'Missing or invalid endpoint performance qualification.'
}
Test-DevBenchPerformanceWindow -Before $before.data.performanceWindow.after -After $after.data.performanceWindow.before |
    ConvertTo-Json -Depth 30 -Compress
