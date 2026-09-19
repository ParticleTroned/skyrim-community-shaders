#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$checks = 0
function Assert-GameFtTest([bool]$Condition, [string]$Message) {
    $script:checks++
    if (!$Condition) { throw $Message }
}
$bridge = Get-Content -LiteralPath (Join-Path $repo 'src/ProfilerDevBenchBridge.cpp') -Raw
$guard = $bridge.IndexOf('#ifdef DEVBENCH_BRIDGE_ENABLED')
$off = $bridge.LastIndexOf('#else')
Assert-GameFtTest ($guard -ge 0 -and $off -gt $guard) 'missing compile gate'
Assert-GameFtTest ($bridge.Substring($off) -notmatch 'cpu_burst_snapshot|CaptureFeatureSettings|BuildCpuBurstSnapshot') 'new diagnostics leak into production'
$snapshot = [regex]::Match($bridge, '(?s)json BuildCpuBurstSnapshot\(\).*?(?=json BuildOCUEffectFoveationResult)').Value
Assert-GameFtTest ($snapshot -match 'RunOnMainThread' -and $snapshot -match 'CaptureFeatureSettings') 'snapshot does not use production service on main thread'
Assert-GameFtTest ($snapshot -notmatch 'RequestCapture|SetUserEnabled|SetOCU|CreateThread') 'snapshot mutates capture/render state'
$descriptor = [regex]::Match($bridge, 'R"\((\{"description":"Inspect and control the CSX GPU/CPU profiler\.[^\r\n]*)\)";').Groups[1].Value | ConvertFrom-Json
Assert-GameFtTest ('cpu_burst_snapshot' -in $descriptor.inputSchema.properties.action.enum) 'endpoint schema missing action'
Write-Output "DevBench CPU snapshot contract: $checks checks passed"
