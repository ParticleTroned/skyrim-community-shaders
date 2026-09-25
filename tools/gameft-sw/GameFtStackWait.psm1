#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-StackWaitToolIdentity {
    param([string]$Path)
    $file = Get-Item -LiteralPath $Path -ErrorAction Stop
    $version = $file.VersionInfo
    return [pscustomobject]@{
        path = $file.FullName
        version = '{0}.{1}.{2}.{3}' -f $version.FileMajorPart, $version.FileMinorPart, $version.FileBuildPart, $version.FilePrivatePart
        sha256 = (Get-FileHash -LiteralPath $file.FullName).Hash
    }
}

function Resolve-StackWaitWpr {
    param([string]$Path)
    if (!$Path) {
        $Path = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Windows Performance Toolkit/wpr.exe'
    }
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw 'Install the Windows Performance Toolkit or supply -WprPath pointing to its recorder.'
    }
    $identity = Get-StackWaitToolIdentity $Path
    if ([version]$identity.version -lt [version]'10.0.19650.0') {
        throw "WPR $($identity.version) is unsupported: trace finalization can fail with 0x80010106. Select a newer WPT recorder."
    }
    return $identity
}

function Get-StackWaitRecorderContract {
    param([string]$WprPath)
    $recorder = Resolve-StackWaitWpr $WprPath
    $files = [ordered]@{}
    foreach ($name in @('CpuStackWait.wprp', 'GameFtStackWait.psm1', 'Invoke-StackWaitWorker.ps1', 'Test-GameFtStackWaitRecorder.ps1')) {
        $files[$name] = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name)).Hash
    }
    return [pscustomobject]@{
        recorder = $recorder
        control = Get-StackWaitToolIdentity (Join-Path (Split-Path $recorder.path) 'windowsperformancerecordercontrol.dll')
        analyzer = Get-StackWaitToolIdentity (Join-Path (Split-Path $recorder.path) 'xperf.exe')
        osVersion = [Environment]::OSVersion.Version.ToString()
        files = $files
    }
}

function Assert-StackWaitRecorderValidation {
    param([string]$WprPath, [string]$ValidationPath)
    if (!(Test-Path -LiteralPath $ValidationPath -PathType Leaf)) {
        throw 'Validate the recorder before the live start: pwsh ./tools/gameft-sw/Test-GameFtStackWaitRecorder.ps1'
    }
    $validation = Get-Content -LiteralPath $ValidationPath -Raw | ConvertFrom-Json
    $contract = Get-StackWaitRecorderContract $WprPath
    if ($validation.schema -cne 'csx-stack-wait-recorder-validation-v1' -or $validation.passed -isnot [bool] -or
        !$validation.passed -or ($validation.contract | ConvertTo-Json -Depth 8 -Compress) -cne ($contract | ConvertTo-Json -Depth 8 -Compress)) {
        throw 'Recorder validation is failed or stale. Run Test-GameFtStackWaitRecorder.ps1 before the live start.'
    }
    if (!(Test-Path -LiteralPath $validation.etl.path) -or (Get-Item -LiteralPath $validation.etl.path).Length -ne $validation.etl.bytes -or
        (Get-FileHash -LiteralPath $validation.statistics.path).Hash -ne $validation.statistics.sha256) {
        throw 'Recorder validation evidence is missing or changed. Revalidate before the live start.'
    }
    return $validation
}

function Test-StackWaitTraceStatistics {
    param([string]$Text)
    foreach ($kind in @('Buffers', 'Events')) {
        $loss = [regex]::Matches($Text, "(?m)^Total # Lost ${kind}\s*:\s*(\d+)\s*$")
        if ($loss.Count -ne 1 -or [long]$loss[0].Groups[1].Value -ne 0) {
            throw "Trace lost $kind or xperf statistics are incomplete."
        }
    }
    $events = [ordered]@{}
    $rows = [regex]::Matches($Text, '(?m)^\s*0x\w+\s+0x\w+\s+0x\w+\s+(\d+)\s+\d+\s+(.+?)\s+(\d+)\s*$')
    foreach ($name in @('SampledProfile', 'CSwitch', 'ReadyThread')) {
        $count = 0L
        $stacks = 0L
        $eventName = if ($name -eq 'SampledProfile') { 'Sampled Profile' } else { "Thread: $name" }
        foreach ($row in $rows) {
            if ($row.Groups[2].Value -ceq $eventName) {
                $count += [long]$row.Groups[1].Value
                $stacks += [long]$row.Groups[3].Value
            }
        }
        if ($count -le 0 -or $stacks -le 0) { throw "Trace is missing $name events or their stacks." }
        $events[$name] = @{ count = $count; stacks = $stacks }
    }
    return $events
}

function Start-StackWaitWorker {
    param([string]$Directory, [string]$ConfigurationPath)
    $workerPath = Join-Path $PSScriptRoot 'Invoke-StackWaitWorker.ps1'
    return Start-Process -FilePath (Join-Path $PSHOME 'pwsh.exe') -WindowStyle Hidden -PassThru `
        -ArgumentList @('-NoProfile', '-File', ('"{0}"' -f $workerPath), '-ConfigurationPath', ('"{0}"' -f $ConfigurationPath)) `
        -RedirectStandardOutput (Join-Path $Directory 'worker.stdout.log') `
        -RedirectStandardError (Join-Path $Directory 'worker.stderr.log')
}

function Assert-StackWaitElevation {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'gameft-sw requires an elevated PowerShell for WPR. Prepare elevation before the live start; no game controls were sent.'
        }
    } finally { $identity.Dispose() }
}

function Write-StackWaitJson {
    param([string]$Path, $Value)
    $temporary = "$Path.$([guid]::NewGuid().ToString('N')).tmp"
    $Value | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Assert-GameFtScripts {
    param([string]$Directory)
    $expected = @{
        'Invoke-SaveLoadTimingV2.ps1' = '63E515ED8335EEFBA7BFA1D07FDAD04F451F90D8B79FC4B9FB86FB8419861CC9'
        'Show-SaveLoadTimingQuickReport.ps1' = '1D08975EB1DBD027DD362E0F61FF06F67ADD1E0B63B1D8785F8D66DD9941983B'
        'Compare-GameFtRuns.ps1' = '988475D39F8FAFC39DD5E115E5C419FB517576BC1FEE2FB5C71A30F2FDFC8F17'
    }
    foreach ($name in $expected.Keys) {
        if ((Get-FileHash -LiteralPath (Join-Path $Directory $name)).Hash -ne $expected[$name]) {
            throw "Saved game-ft script changed: $name. Do not substitute another protocol."
        }
    }
    return $expected
}

function Assert-StackWaitSnapshot {
    param($Envelope, [int]$ProcessId, [string]$BuildId)
    if ($Envelope.transportOk -ne $true) { throw 'DevBench diagnostic transport failed.' }
    $payload = @($Envelope.data.content)[0]
    if ($payload.PSObject.Properties['error'] -or $payload.PSObject.Properties['errorCode']) {
        throw "DevBench diagnostic failed: $($payload | ConvertTo-Json -Compress -Depth 6)"
    }
    if ($payload.schema -cne 'csx-cpu-burst-snapshot-v1' -or
        $payload.action -cne 'cpu_burst_snapshot' -or $payload.devbenchOnly -isnot [bool] -or $payload.devbenchOnly -ne $true) {
        throw 'This build does not support the DevBench-only CPU stack/wait contract.'
    }
    if ($payload.processId -ne $ProcessId -or $payload.producer.buildId -notmatch '^[a-fA-F0-9]{64}$' -or
        ($BuildId -and $payload.producer.buildId -ne $BuildId)) {
        throw 'Diagnostic process/build identity changed.'
    }
    if (!$payload.vr -or $payload.profilerCapturing -or $payload.qpcFrequency -le 0 -or
        $payload.qpcEnd -lt $payload.qpcBegin) { throw 'Diagnostic snapshot is not a neutral Skyrim VR snapshot.' }
    return $payload
}

function Get-StackWaitResult {
    param([string]$RunDirectory)
    $traceDirectory = Join-Path $RunDirectory 'stack-wait'
    $resultPath = Join-Path $traceDirectory 'trace-result.json'
    if (!(Test-Path -LiteralPath $resultPath)) {
        return [pscustomobject]@{ state = 'pending'; stackWaitComplete = $false; resultPath = $resultPath }
    }
    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    if ($result.schema -cne 'csx-stack-wait-trace-v1') { throw 'Unknown stack/wait trace receipt schema.' }
    $etl = Join-Path $traceDirectory 'cpu-stack-wait.etl'
    $complete = $result.state -eq 'stopped' -and $result.stopReason -eq 'requested' -and
        $result.errors.Count -eq 0 -and (Test-Path -LiteralPath $etl) -and (Get-Item -LiteralPath $etl).Length -gt 0
    $coverage = @()
    if ($complete) {
        $markers = @(Get-Content -LiteralPath (Join-Path $RunDirectory 'markers.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        foreach ($entry in @($markers | Where-Object { $_.label -match '^save-\d+-world-entry$' })) {
            $label = $entry.label -replace '-world-entry$', ''
            $ends = @($markers | Where-Object { $_.label -eq "$label-hold-end" })
            $covered = $ends.Count -eq 1 -and $result.qpcFrequency -eq $entry.qpcFrequency -and
                $result.startedQpc -le $entry.qpc -and $result.stopRequestedQpc -ge $ends[0].qpc
            $coverage += [pscustomobject]@{ save = $label; covered = $covered }
        }
        $complete = $coverage.Count -gt 0 -and @($coverage | Where-Object { !$_.covered }).Count -eq 0
    }
    return [pscustomobject]@{
        state = $result.state; stackWaitComplete = $complete; coverage = $coverage; resultPath = $resultPath
        etl = $etl; errors = $result.errors
        attribution = 'pending ETL lost-event, clock and symbol review; capture coverage alone does not establish a cause'
    }
}

function Invoke-StackWaitSnapshot {
    param([string]$Controller, [string]$RuntimePath, [string]$Directory,
        [string]$Label, [int]$ProcessId, [string]$BuildId)
    $arguments = @{ action = 'cpu_burst_snapshot' }
    if ($BuildId) { $arguments.expectedBuildId = $BuildId }
    # Full DLL provenance follows the timing table, as in the saved game-ft protocol.
    $raw = & $Controller call -RuntimePath $RuntimePath -SkipRuntimeIdentityVerification `
        -EvidenceDirectory $Directory -EvidenceLabel $Label -Tool communityshaders.profiler `
        -ArgumentsJson ($arguments | ConvertTo-Json -Compress) -RequirePerformanceNeutral `
        -TimeoutSeconds 8 -RequestTimeoutSeconds 3 -MaxTransientRetries 0 -NoExit -Compact
    $raw | Set-Content -LiteralPath (Join-Path $Directory "$Label.json")
    return Assert-StackWaitSnapshot ($raw | ConvertFrom-Json -Depth 60) $ProcessId $BuildId
}

function Invoke-StackWaitWpr {
    param([string]$Executable, [string[]]$Arguments, [string]$LogPath)
    $text = & $Executable @Arguments 2>&1
    $code = $LASTEXITCODE
    $text | Set-Content -LiteralPath $LogPath
    if ($code -ne 0) { throw "WPR exited $code; see $LogPath" }
}

function Invoke-OwnedStackWaitTrace {
    param([string]$Directory, [string]$WprPath, [string]$ProfilePath,
        [string]$Instance, [int]$MaximumSeconds, [int]$OwnerProcessId, [long]$OwnerStartTicks)
    $owned = $false
    $receipt = [ordered]@{
        schema = 'csx-stack-wait-trace-v1'; instance = $Instance; state = 'starting'
        maximumSeconds = $MaximumSeconds; errors = @(); stopReason = $null
        qpcFrequency = [Diagnostics.Stopwatch]::Frequency
        etl = Join-Path $Directory 'cpu-stack-wait.etl'
    }
    try {
        Invoke-StackWaitWpr $WprPath @('-start', "${ProfilePath}!CSXCpuStackWait", '-filemode', '-instancename', $Instance) (Join-Path $Directory 'wpr-start.log')
        $owned = $true
        $receipt.state = 'recording'
        $receipt.startedUtc = [DateTime]::UtcNow.ToString('o')
        $receipt.startedQpc = [Diagnostics.Stopwatch]::GetTimestamp()
        Write-StackWaitJson (Join-Path $Directory 'trace-ready.json') $receipt
        $timer = [Diagnostics.Stopwatch]::StartNew()
        while (!(Test-Path -LiteralPath (Join-Path $Directory 'stop-trace'))) {
            if ($timer.Elapsed.TotalSeconds -ge $MaximumSeconds) { $receipt.stopReason = 'deadline'; break }
            $owner = Get-Process -Id $OwnerProcessId -ErrorAction SilentlyContinue
            if (!$owner -or $owner.StartTime.ToUniversalTime().Ticks -ne $OwnerStartTicks) {
                $receipt.stopReason = 'owner_exited'; break
            }
            Start-Sleep -Milliseconds 200
        }
        if (!$receipt.stopReason) { $receipt.stopReason = 'requested' }
    } catch {
        $receipt.errors += $_.Exception.Message
    } finally {
        if ($owned) {
            $receipt.stopRequestedQpc = [Diagnostics.Stopwatch]::GetTimestamp()
            try {
                Invoke-StackWaitWpr $WprPath @('-status', 'collectors', '-details', '-instancename', $Instance) (Join-Path $Directory 'wpr-status.log')
            } catch { $receipt.errors += $_.Exception.Message }
            try {
                Invoke-StackWaitWpr $WprPath @('-stop', $receipt.etl, 'gameft-sw CPU stacks and waits', '-skipPdbGen', '-instancename', $Instance) (Join-Path $Directory 'wpr-stop.log')
                if (!(Test-Path -LiteralPath $receipt.etl) -or (Get-Item -LiteralPath $receipt.etl).Length -eq 0) {
                    throw 'WPR stop produced no ETL.'
                }
                $receipt.state = 'stopped'
            } catch {
                $receipt.state = 'stop_failed'
                $receipt.errors += $_.Exception.Message
            }
        } else { $receipt.state = 'start_failed' }
        $receipt.finishedUtc = [DateTime]::UtcNow.ToString('o')
        Write-StackWaitJson (Join-Path $Directory 'trace-result.json') $receipt
    }
}

function Save-GameFtRawArchive {
    param([string]$RunDirectory, [string]$ArchiveDirectory = 'D:\Coding\GitHub\CS logs')
    $markers = @(Get-Content -LiteralPath (Join-Path $RunDirectory 'markers.jsonl') |
        ForEach-Object { $_ | ConvertFrom-Json })
    $confirmed = @($markers | Where-Object { $_.label -in @('fpsvr-already-recording', 'fpsvr-recording-confirmed') })
    if ($confirmed.Count -ne 1) { throw 'The run does not identify exactly one confirmed fpsVR CSV.' }
    $source = $confirmed[0].detail.path
    $tag = 'gameft-sw-UNVERIFIED'
    $name = '{0}__{1}__{2}__{3}' -f $tag, [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'),
        [guid]::NewGuid().ToString('N').Substring(0, 8), [IO.Path]::GetFileName($source)
    New-Item -ItemType Directory -Path $ArchiveDirectory -Force | Out-Null
    $archive = Join-Path $ArchiveDirectory $name
    $before = Get-Item -LiteralPath $source
    $length = $before.Length
    $hash = (Get-FileHash -LiteralPath $source).Hash
    [IO.File]::Copy($source, $archive, $false)
    if ((Get-Item -LiteralPath $source).Length -ne $length -or (Get-FileHash -LiteralPath $source).Hash -ne $hash -or
        (Get-Item -LiteralPath $archive).Length -ne $length -or (Get-FileHash -LiteralPath $archive).Hash -ne $hash) {
        throw "Raw CSV changed during preservation; archived bytes retained at $archive. Do not analyze this incomplete copy."
    }
    Write-StackWaitJson (Join-Path $RunDirectory 'fpsvr-archive.json') @{
        source = $source; archivePath = $archive; byteLength = $length; sha256 = $hash
    }
    Write-Output "Preserved fpsVR CSV: $archive (length and SHA-256 verified)"
}

Export-ModuleMember -Function *-StackWait*, Assert-GameFtScripts, Invoke-OwnedStackWaitTrace, Save-GameFtRawArchive
