#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$module = Import-Module (Join-Path $repo 'tools/gameft-sw/GameFtStackWait.psm1') -Force -PassThru
$checks = 0
function Assert-GameFtTest([bool]$Condition, [string]$Message) {
    $script:checks++
    if (!$Condition) { throw $Message }
}
function Assert-GameFtRejected([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert-GameFtTest $rejected $Message
}
$root = Join-Path $repo ('build/tests/gameft-sw-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null

# Inject file metadata while exercising the production recorder policy.
$toolFixture = Join-Path $root 'wpr.exe'
[IO.File]::WriteAllBytes($toolFixture, [byte[]]@(1))
& $module {
    $script:originalToolIdentity = ${function:Get-StackWaitToolIdentity}
    $script:toolVersion = '10.0.19041.7548'
    function script:Get-StackWaitToolIdentity {
        param($Path)
        [pscustomobject]@{ path = $Path; version = $script:toolVersion; sha256 = ('a' * 64) }
    }
}
try {
    Assert-GameFtRejected { Resolve-StackWaitWpr $toolFixture } 'old explicit WPR override accepted'
    & $module { $script:toolVersion = '10.0.26100.7705' }
    Assert-GameFtTest ((Resolve-StackWaitWpr $toolFixture).path -eq $toolFixture) 'supported WPT recorder rejected'
    Assert-GameFtRejected { Resolve-StackWaitWpr (Join-Path $root 'missing-wpr.exe') } 'missing recorder accepted'
} finally { & $module { Set-Item Function:script:Get-StackWaitToolIdentity $script:originalToolIdentity } }

$statistics = @'
Total # Lost Buffers : 0
Total # Lost Events  : 0
    0x2e 0x00 0x0002 150 2000 Sampled Profile 140
    0x24 0x00 0x0002 300 4000 Thread: CSwitch 290
    0x32 0x00 0x0002 100 1200 Thread: ReadyThread 90
'@
$events = Test-StackWaitTraceStatistics $statistics
Assert-GameFtTest ($events.SampledProfile.stacks -eq 140 -and $events.CSwitch.count -eq 300 -and $events.ReadyThread.stacks -eq 90) 'usable CPU/wait stacks rejected'
foreach ($invalidStatistics in @(
    $statistics.Replace('Lost Events  : 0', 'Lost Events  : 1'),
    $statistics.Replace('Lost Buffers : 0', 'Lost Buffers : 1'),
    $statistics.Replace('Sampled Profile 140', 'Sampled Profile 0'),
    $statistics.Replace('CSwitch 290', 'CSwitch 0'),
    $statistics.Replace('ReadyThread 90', 'ReadyThread 0'),
    'Total # Lost Events : 0'
)) {
    Assert-GameFtRejected { Test-StackWaitTraceStatistics $invalidStatistics } 'incomplete or lost trace evidence accepted'
}

$validationPath = Join-Path $root 'recorder-validation.json'
$validationEtl = Join-Path $root 'validation.etl'
$validationStatistics = Join-Path $root 'statistics.txt'
[IO.File]::WriteAllBytes($validationEtl, [byte[]]@(1, 2, 3))
[IO.File]::WriteAllText($validationStatistics, $statistics)
$validationContract = [pscustomobject]@{ recorder = [pscustomobject]@{ path = $toolFixture; sha256 = ('a' * 64) }; files = @{ profile = 'same-profile' } }
& $module {
    param($Contract)
    $script:originalRecorderContract = ${function:Get-StackWaitRecorderContract}
    $script:validationContract = $Contract
    function script:Get-StackWaitRecorderContract { param($WprPath) return $script:validationContract }
} $validationContract
try {
    Assert-GameFtRejected { Assert-StackWaitRecorderValidation $toolFixture $validationPath } 'missing recorder validation accepted'
    $validation = @{
        schema = 'csx-stack-wait-recorder-validation-v1'; passed = $true; contract = $validationContract
        etl = @{ path = $validationEtl; bytes = 3 }
        statistics = @{ path = $validationStatistics; sha256 = (Get-FileHash -LiteralPath $validationStatistics).Hash }
    }
    Write-StackWaitJson $validationPath $validation
    Assert-GameFtTest (Assert-StackWaitRecorderValidation $toolFixture $validationPath).passed 'matching recorder validation rejected'
    foreach ($invalidValidation in @(
        @{ key = 'schema'; value = 'future-schema' },
        @{ key = 'passed'; value = $false },
        @{ key = 'passed'; value = 'true' },
        @{ key = 'contract'; value = @{ recorder = 'changed-recorder-or-profile' } }
    )) {
        $original = $validation[$invalidValidation.key]
        $validation[$invalidValidation.key] = $invalidValidation.value
        Write-StackWaitJson $validationPath $validation
        Assert-GameFtRejected { Assert-StackWaitRecorderValidation $toolFixture $validationPath } "Invalid validation accepted: $($invalidValidation.key)"
        $validation[$invalidValidation.key] = $original
    }
    Write-StackWaitJson $validationPath $validation
    [IO.File]::WriteAllText($validationStatistics, 'changed')
    Assert-GameFtRejected { Assert-StackWaitRecorderValidation $toolFixture $validationPath } 'changed trace statistics accepted'
    [IO.File]::WriteAllText($validationStatistics, $statistics)
    [IO.File]::WriteAllBytes($validationEtl, [byte[]]@(1))
    Assert-GameFtRejected { Assert-StackWaitRecorderValidation $toolFixture $validationPath } 'truncated validation ETL accepted'
} finally { & $module { Set-Item Function:script:Get-StackWaitRecorderContract $script:originalRecorderContract } }

$payload = [pscustomobject]@{
    schema = 'csx-cpu-burst-snapshot-v1'; action = 'cpu_burst_snapshot'; devbenchOnly = $true
    processId = 123; producer = [pscustomobject]@{ buildId = ('a' * 64) }
    profilerCapturing = $false; vr = $true; qpcBegin = 10; qpcEnd = 20; qpcFrequency = 1000
}
$envelope = [pscustomobject]@{ transportOk = $true; data = [pscustomobject]@{ content = @($payload) } }
Assert-GameFtTest ((Assert-StackWaitSnapshot $envelope 123 ('a' * 64)).processId -eq 123) 'valid DevBench snapshot rejected'
foreach ($invalid in @(
    @{ key = 'schema'; value = 'csx-cpu-burst-snapshot-v2' },
    @{ key = 'devbenchOnly'; value = $false },
    @{ key = 'devbenchOnly'; value = 'true' },
    @{ key = 'action'; value = 'status' },
    @{ key = 'profilerCapturing'; value = $true },
    @{ key = 'vr'; value = $false },
    @{ key = 'processId'; value = 124 },
    @{ key = 'qpcEnd'; value = 5 }
)) {
    $original = $payload.($invalid.key)
    $payload.($invalid.key) = $invalid.value
    Assert-GameFtRejected { Assert-StackWaitSnapshot $envelope 123 ('a' * 64) } "Accepted invalid $($invalid.key)"
    $payload.($invalid.key) = $original
}
Assert-GameFtRejected { Assert-StackWaitSnapshot $envelope 123 ('b' * 64) } 'changed build accepted'
$envelope.transportOk = $false
Assert-GameFtRejected { Assert-StackWaitSnapshot $envelope 123 } 'failed transport accepted'
$envelope.transportOk = $true

# Exercise the actual ownership controller with a fake WPR boundary.
& $module {
    $script:calls = @()
    $script:failStart = $false
    $script:failStop = $false
    function script:Invoke-StackWaitWpr {
        param($Executable, $Arguments, $LogPath)
        $script:calls += ,$Arguments
        if ($Arguments[0] -eq '-start' -and $script:failStart) { throw 'start failed' }
        if ($Arguments[0] -eq '-stop') {
            if ($script:failStop) { throw 'stop failed' }
            [IO.File]::WriteAllBytes($Arguments[1], [byte[]]@(1, 2, 3))
        }
    }
}
foreach ($mode in @('normal', 'start-failure', 'stop-failure', 'deadline', 'owner-exited')) {
    $dir = Join-Path $root $mode
    New-Item -ItemType Directory -Path $dir | Out-Null
    if ($mode -notin @('deadline', 'owner-exited')) { [IO.File]::WriteAllText((Join-Path $dir 'stop-trace'), '') }
    & $module { param($Mode) $script:calls = @(); $script:failStart = $Mode -eq 'start-failure'; $script:failStop = $Mode -eq 'stop-failure' } $mode
    $maximum = if ($mode -eq 'deadline') { 0 } else { 10 }
    $owner = if ($mode -eq 'owner-exited') { 2147483647 } else { $PID }
    Invoke-OwnedStackWaitTrace $dir 'fake-wpr' 'profile.wprp' 'owned-test-instance' $maximum $owner (Get-Process -Id $PID).StartTime.ToUniversalTime().Ticks
    $receipt = Get-Content -LiteralPath (Join-Path $dir 'trace-result.json') -Raw | ConvertFrom-Json
    $calls = & $module { ,$script:calls }
    foreach ($call in $calls) {
        Assert-GameFtTest ($call[-2] -eq '-instancename' -and $call[-1] -eq 'owned-test-instance') 'WPR command did not restrict ownership'
        Assert-GameFtTest ('-cancel' -notin $call) 'global cancellation prohibited'
    }
    if ($mode -eq 'start-failure') {
        Assert-GameFtTest ($receipt.state -eq 'start_failed' -and $calls.Count -eq 1) 'failed start must not stop another recording'
    } elseif ($mode -eq 'stop-failure') {
        Assert-GameFtTest ($receipt.state -eq 'stop_failed' -and $receipt.errors.Count -gt 0) 'stop failure hidden'
    } else {
        Assert-GameFtTest ($receipt.state -eq 'stopped') 'owned capture did not stop'
        if ($mode -eq 'deadline') { Assert-GameFtTest ($receipt.stopReason -eq 'deadline') 'deadline unbounded' }
        if ($mode -eq 'owner-exited') { Assert-GameFtTest ($receipt.stopReason -eq 'owner_exited') 'orphan capture unbounded' }
    }
}

[xml]$profile = Get-Content -LiteralPath (Join-Path $repo 'tools/gameft-sw/CpuStackWait.wprp') -Raw
foreach ($stack in @('SampledProfile', 'CSwitch', 'ReadyThread')) {
    Assert-GameFtTest ($null -ne $profile.SelectSingleNode("//SystemProvider/Stacks/Stack[@Value='$stack']")) "missing $stack stacks"
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
$wrapper = Get-Content -LiteralPath (Join-Path $repo 'tools/gameft-sw/Invoke-GameFtStackWait.ps1') -Raw
Assert-GameFtTest ($wrapper -notmatch '\$WprPath\s*=\s*"\$env:WINDIR\\System32\\wpr.exe"') 'Default recorder selects the unsupported Windows WPR that fails trace finalization'
Assert-GameFtTest ($wrapper.IndexOf('Assert-StackWaitRecorderValidation') -lt $wrapper.IndexOf('Get-Process SkyrimVR')) 'recorder validation must precede live session access'
Assert-GameFtTest ($wrapper -match '& \(Join-Path \$GameFtDirectory ''Invoke-SaveLoadTimingV2.ps1''\)') 'must delegate to saved runner'
Assert-GameFtTest ($wrapper -notmatch 'Stop-Process|logging_startstop|action=''load''') 'wrapper duplicates or bypasses saved controls'

$run = Join-Path $root 'coverage'
$trace = Join-Path $run 'stack-wait'
New-Item -ItemType Directory -Path $trace | Out-Null
Assert-GameFtTest ((Get-StackWaitResult $run).state -eq 'pending') 'missing stop receipt must remain pending'
$receipt = @{
    schema = 'csx-stack-wait-trace-v1'; state = 'stopped'; stopReason = 'requested'; errors = @()
    startedQpc = 10; stopRequestedQpc = 100; qpcFrequency = 1000
}
$markers = @(
    @{ label = 'save-1-world-entry'; qpc = 20; qpcFrequency = 1000 },
    @{ label = 'save-1-hold-end'; qpc = 90; qpcFrequency = 1000 }
)
$markers | ForEach-Object { $_ | ConvertTo-Json -Compress } | Set-Content -LiteralPath (Join-Path $run 'markers.jsonl')
[IO.File]::WriteAllBytes((Join-Path $trace 'cpu-stack-wait.etl'), [byte[]]@(1, 2, 3))
Write-StackWaitJson (Join-Path $trace 'trace-result.json') $receipt
Assert-GameFtTest (Get-StackWaitResult $run).stackWaitComplete 'complete coverage rejected'
foreach ($change in @(
    @{ key = 'stopRequestedQpc'; value = 80 },
    @{ key = 'startedQpc'; value = 30 },
    @{ key = 'qpcFrequency'; value = 2000 },
    @{ key = 'errors'; value = @('lost status') },
    @{ key = 'stopReason'; value = 'deadline' },
    @{ key = 'state'; value = 'stop_failed' }
)) {
    $original = $receipt[$change.key]
    $receipt[$change.key] = $change.value
    Write-StackWaitJson (Join-Path $trace 'trace-result.json') $receipt
    Assert-GameFtTest (!(Get-StackWaitResult $run).stackWaitComplete) "Incomplete trace accepted: $($change.key)"
    $receipt[$change.key] = $original
}
$receipt.schema = 'future-schema'
Write-StackWaitJson (Join-Path $trace 'trace-result.json') $receipt
Assert-GameFtRejected { Get-StackWaitResult $run } 'future trace schema accepted'

# Archive the exact marker-selected raw file; never infer it from latest timestamps.
$raw = Join-Path $root 'selected.csv'
[IO.File]::WriteAllBytes($raw, [byte[]]@(239, 187, 191, 97, 13, 10, 98))
@{ label = 'fpsvr-recording-confirmed'; detail = @{ path = $raw } } | ConvertTo-Json -Compress |
    Set-Content -LiteralPath (Join-Path $run 'markers.jsonl')
Save-GameFtRawArchive $run (Join-Path $root 'archive') | Out-Null
$archive = Get-Content -LiteralPath (Join-Path $run 'fpsvr-archive.json') -Raw | ConvertFrom-Json
Assert-GameFtTest ((Get-FileHash -LiteralPath $archive.archivePath).Hash -eq (Get-FileHash -LiteralPath $raw).Hash) 'archive bytes changed'
Assert-GameFtTest ((Get-Item -LiteralPath $archive.archivePath).Length -eq 7) 'archive length changed'
$firstArchive = $archive.archivePath
Save-GameFtRawArchive $run (Join-Path $root 'archive') | Out-Null
$secondArchive = (Get-Content -LiteralPath (Join-Path $run 'fpsvr-archive.json') -Raw | ConvertFrom-Json).archivePath
Assert-GameFtTest ($firstArchive -ne $secondArchive -and (Test-Path -LiteralPath $firstArchive)) 'archive overwritten'
Write-Output "gameft-sw: $checks checks passed; fixtures: $root"
