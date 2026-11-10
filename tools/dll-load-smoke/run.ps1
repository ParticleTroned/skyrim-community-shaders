#requires -Version 7.4
<#
.SYNOPSIS
Runs an optional repeated real-DLL loader diagnostic in a surrogate host.
.DESCRIPTION
Builds a versioned x64 host, verifies the DLL against its build manifest,
and records bounded fresh-process loads in ignored local evidence.
This does not call SKSEPlugin_Load or validate rendering or shutdown.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Dll,
    [Parameter(Mandatory)][string] $GameRoot,
    [ValidateSet('SE', 'AE', 'VR')][string] $Runtime = 'VR',
    [string] $RuntimeVersion,
    [string] $Manifest,
    [ValidateRange(1, 1000)][int] $Runs = 30,
    [ValidateRange(16, 256)][int] $PadMB = 80,
    [ValidateRange(1, 60)][int] $TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'The DLL loader diagnostic requires Windows.' }
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repositoryRoot 'tools/tool-environment.ps1')
$runId = [DateTimeOffset]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N')
$recordRoot = Join-Path $repositoryRoot ('.local-docs/dll-load-smoke/' + $runId)
New-Item -ItemType Directory -Path $recordRoot | Out-Null
$stages = [Collections.Generic.List[object]]::new()
$summary = [ordered]@{
    schemaVersion = 1
    runId = $runId
    startedUtc = [DateTimeOffset]::UtcNow.ToString('o')
    finishedUtc = $null
    scope = 'surrogate-dll-loader-only'
    verdict = 'incomplete'
    failure = $null
    runtime = $Runtime
    runtimeVersion = $null
    gameRoot = $null
    requestedLoads = $Runs
    completedLoads = 0
    successfulLoads = 0
    failedLoads = 0
    padMB = $PadMB
    timeoutSeconds = $TimeoutSeconds
    input = $null
    stages = $stages
}
$dllLock = $null
$libraryLock = $null

function Save-Summary {
    $temporary = Join-Path $recordRoot 'summary.partial.json'
    $summary | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination (Join-Path $recordRoot 'summary.json') -Force
}

function Get-FileIdentity([string] $Path) {
    $file = Get-Item -LiteralPath $Path
    if ($file.PSIsContainer) { throw "Expected a file: $Path" }
    return [ordered]@{
        path = $file.FullName
        sizeBytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Invoke-DiagnosticProcess {
    param(
        [string] $Name,
        [string] $Executable,
        [string[]] $Arguments,
        [switch] $RequireLoadMarker
    )
    $stdoutPath = Join-Path $recordRoot ($Name + '.stdout.txt')
    $stderrPath = Join-Path $recordRoot ($Name + '.stderr.txt')
    $record = [ordered]@{
        name = $Name
        executable = $Executable
        arguments = $Arguments
        startedUtc = [DateTimeOffset]::UtcNow.ToString('o')
        finishedUtc = $null
        durationMilliseconds = 0
        processId = $null
        exited = $false
        exitCode = $null
        timedOut = $false
        logsComplete = $false
        loadMarkerPresent = $null
        error = $null
        passed = $false
        stdout = $stdoutPath
        stderr = $stderrPath
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $process.StartInfo.FileName = $Executable
    $process.StartInfo.WorkingDirectory = $recordRoot
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $process.StartInfo.ArgumentList.Add($argument) }
    $stdout = $null
    $stderr = $null
    $copies = @()
    $watch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $stdout = [IO.File]::Open($stdoutPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        $stderr = [IO.File]::Open($stderrPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        if (-not $process.Start()) { throw "Could not start $Executable" }
        $record.processId = $process.Id
        $process.StandardInput.Close()
        $copies = @(
            $process.StandardOutput.BaseStream.CopyToAsync($stdout)
            $process.StandardError.BaseStream.CopyToAsync($stderr)
        )
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $record.timedOut = $true
            $process.Kill($true)
            if (-not $process.WaitForExit(5000)) { throw 'Process did not exit after termination.' }
        }
        $record.exited = $true
        $record.exitCode = $process.ExitCode
        if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]] $copies, 5000)) {
            throw 'Process output did not close within five seconds.'
        }
        $record.logsComplete = $true
    } catch {
        $record.error = $_.Exception.Message
    } finally {
        if ($null -ne $record.processId -and -not $record.exited) {
            try {
                if (-not $process.HasExited) { $process.Kill($true) }
                $record.exited = $process.WaitForExit(5000)
                if ($record.exited) { $record.exitCode = $process.ExitCode }
            } catch {
                $record.error = [string] $record.error + '; termination: ' + $_.Exception.Message
            }
        }
        if ($stdout) { $stdout.Dispose() }
        if ($stderr) { $stderr.Dispose() }
        $watch.Stop()
        $record.durationMilliseconds = $watch.ElapsedMilliseconds
        $record.finishedUtc = [DateTimeOffset]::UtcNow.ToString('o')
        $process.Dispose()
    }
    if ($RequireLoadMarker -and $record.logsComplete) {
        $record.loadMarkerPresent = @(Get-Content -LiteralPath $stdoutPath | Where-Object { $_ -ceq 'CSX_DLL_LOAD_OK' }).Count -eq 1
    }
    $record.passed = $record.exited -and $record.exitCode -eq 0 -and
        -not $record.timedOut -and $record.logsComplete -and -not $record.error -and
        (-not $RequireLoadMarker -or $record.loadMarkerPresent)
    $stages.Add($record)
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $recordRoot ($Name + '.json')) -Encoding utf8
    Save-Summary
    return $record
}

try {
    $dllFile = Get-Item -LiteralPath $Dll
    if ($dllFile.PSIsContainer -or $dllFile.Extension -ne '.dll') { throw 'Dll must identify an existing DLL file.' }
    $Dll = $dllFile.FullName
    $gameDirectory = Get-Item -LiteralPath $GameRoot
    if (-not $gameDirectory.PSIsContainer) { throw 'GameRoot must be a directory.' }
    $GameRoot = $gameDirectory.FullName
    $summary.gameRoot = $GameRoot
    $gameExecutable = Join-Path $GameRoot $(if ($Runtime -eq 'VR') { 'SkyrimVR.exe' } else { 'SkyrimSE.exe' })
    $observedVersion = $null
    if (Test-Path -LiteralPath $gameExecutable -PathType Leaf) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($gameExecutable)
        $observedVersion = [version]::new($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart)
    }
    if (-not $RuntimeVersion) {
        if (-not $observedVersion) { throw 'RuntimeVersion is required when GameRoot contains no game executable.' }
        $RuntimeVersion = $observedVersion.ToString(4)
    }
    if ($RuntimeVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'RuntimeVersion must contain four numeric components.' }
    $version = [version] $RuntimeVersion
    if ($version.Major -ne 1 -or $version.Build -gt 65535 -or $version.Revision -gt 65535 -or
        ($Runtime -eq 'VR' -and $version -ne [version] '1.4.15.0') -or
        ($Runtime -eq 'SE' -and $version.Minor -ne 5) -or
        ($Runtime -eq 'AE' -and ($version.Minor -lt 6 -or $version.Minor -gt 65535))) {
        throw 'RuntimeVersion does not match the selected SE, AE or VR family.'
    }
    if ($observedVersion -and $version -ne $observedVersion) { throw 'RuntimeVersion differs from the game executable.' }
    $RuntimeVersion = $version.ToString(4)
    $summary.runtimeVersion = $RuntimeVersion
    $summary.runtimeVersionSource = $(if ($observedVersion) { 'game-executable' } else { 'explicit' })
    $libraryName = switch ($Runtime) {
        'VR' { 'version-' + $RuntimeVersion.Replace('.', '-') + '.csv' }
        'SE' { 'version-' + $RuntimeVersion.Replace('.', '-') + '.bin' }
        'AE' { 'versionlib-' + $RuntimeVersion.Replace('.', '-') + '.bin' }
    }
    $libraryPath = Join-Path $GameRoot ('Data/SKSE/Plugins/' + $libraryName)
    $libraryLock = [IO.File]::Open($libraryPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $dllLock = [IO.File]::Open($Dll, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    if (-not $Manifest) { $Manifest = Join-Path $dllFile.DirectoryName 'CSX.BuildManifest.json' }
    $Manifest = (Get-Item -LiteralPath $Manifest).FullName
    $manifestCopy = Join-Path $recordRoot 'CSX.BuildManifest.json'
    Copy-Item -LiteralPath $Manifest -Destination $manifestCopy
    $summary.input = [ordered]@{
        dll = Get-FileIdentity $Dll
        manifest = Get-FileIdentity $manifestCopy
        addressLibrary = Get-FileIdentity $libraryPath
        buildId = $null
        source = $null
        manifestVerified = $false
    }
    $summary.toolSources = @(
        Get-FileIdentity $PSCommandPath
        Get-FileIdentity (Join-Path $PSScriptRoot 'host.cpp')
        Get-FileIdentity (Join-Path $repositoryRoot 'tools/tool-environment.ps1')
        Get-FileIdentity (Join-Path $repositoryRoot 'tools/build_provenance.py')
    )
    $python = (Get-Command python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $verification = Invoke-DiagnosticProcess 'verify-dll' $python @(
        (Join-Path $repositoryRoot 'tools/build_provenance.py'), 'verify', '--manifest', $manifestCopy, '--artifact', $Dll
    )
    if (-not $verification.passed) { throw 'DLL manifest verification failed; see the retained verifier logs.' }
    $manifestData = Get-Content -LiteralPath $manifestCopy -Raw | ConvertFrom-Json -AsHashtable
    if ($manifestData.artifact.sizeBytes -ne $summary.input.dll.sizeBytes) { throw 'Manifest DLL size does not match the artifact.' }
    $summary.input.buildId = $manifestData.buildId
    $summary.input.source = $manifestData.identity.source
    $summary.input.manifestVerified = $true

    $summary.vsDevCmd = Initialize-CsxMsvcEnvironment -Required
    if ($env:VSCMD_ARG_TGT_ARCH -and $env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'An x64 MSVC environment is required.' }
    $compiler = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $resourceCompiler = (Get-Command rc.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $summary.compiler = Get-FileIdentity $compiler
    $summary.resourceCompiler = Get-FileIdentity $resourceCompiler
    $versionCommas = $RuntimeVersion.Replace('.', ',')
    $resourcePath = Join-Path $recordRoot 'host.rc'
@"
1 VERSIONINFO
FILEVERSION $versionCommas
PRODUCTVERSION $versionCommas
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "FileVersion", "$RuntimeVersion"
      VALUE "ProductVersion", "$RuntimeVersion"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
"@ | Set-Content -LiteralPath $resourcePath -Encoding ascii
    $resourceBuild = Invoke-DiagnosticProcess 'build-resource' $resourceCompiler @('/nologo', '/fo', 'host.res', $resourcePath)
    if (-not $resourceBuild.passed) { throw 'Host resource compilation failed.' }
    $padBytes = $PadMB * 1MB
    $hostBuild = Invoke-DiagnosticProcess 'build-host' $compiler @(
        '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/O2', '/MD', "/DPAD_BYTES=$padBytes", '/Fe:host.exe',
        (Join-Path $PSScriptRoot 'host.cpp'), 'host.res', '/link', '/MACHINE:X64', '/SUBSYSTEM:CONSOLE'
    )
    if (-not $hostBuild.passed) { throw 'Host compilation or linking failed.' }
    $hostPath = Join-Path $recordRoot 'host.exe'
    $summary.host = Get-FileIdentity $hostPath
    for ($ordinal = 1; $ordinal -le $Runs; ++$ordinal) {
        $result = Invoke-DiagnosticProcess ('load-{0:D4}' -f $ordinal) $hostPath @($Dll, $GameRoot) -RequireLoadMarker
        ++$summary.completedLoads
        if ($result.passed) { ++$summary.successfulLoads } else { ++$summary.failedLoads }
        Save-Summary
        if ($result.error) { throw "Load process supervision failed: $($result.error)" }
    }
    $summary.verdict = $(if ($summary.completedLoads -eq $Runs -and $summary.failedLoads -eq 0) { 'passed' } else { 'failed' })
} catch {
    $summary.failure = $_.Exception.Message
    $summary.verdict = 'failed'
} finally {
    if ($dllLock) { $dllLock.Dispose() }
    if ($libraryLock) { $libraryLock.Dispose() }
    $summary.finishedUtc = [DateTimeOffset]::UtcNow.ToString('o')
    Save-Summary
}
Write-Host ("{0} {1}: loaded {2}/{3}; verdict {4}" -f $Runtime, $summary.runtimeVersion, $summary.successfulLoads, $Runs, $summary.verdict)
Write-Host ("Evidence: " + $recordRoot)
if ($summary.failure) { Write-Warning $summary.failure }
if ($summary.verdict -ne 'passed') { exit 1 }
