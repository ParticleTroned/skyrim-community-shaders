[CmdletBinding()]
param([string] $OutputDirectory)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$started = [DateTimeOffset]::UtcNow
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot (
        "build/validation/" + $started.ToString("yyyyMMddTHHmmssfffZ") + "-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
}
$recordRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $recordRoot) {
    throw "Validation evidence must use a new directory: $recordRoot"
}
New-Item -ItemType Directory -Path $recordRoot | Out-Null
$stages = [Collections.Generic.List[object]]::new()
$summary = [ordered]@{
    startedUtc = $started.ToString("o")
    repository = $repositoryRoot
    configurePreset = "ALL"
    buildPreset = "CSmain"
    configuration = "Release"
    verdict = "failed"
    failure = $null
    tests = $null
    buildId = $null
}

function Invoke-ValidationStage {
    param([string] $Name, [string] $Executable, [string[]] $Arguments)
    $logPath = Join-Path $recordRoot ($Name + ".log")
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $stageStart = [DateTimeOffset]::UtcNow
    $exitCode = 1
    try {
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logPath | Out-Host
        $exitCode = $LASTEXITCODE
    } finally {
        $watch.Stop()
        if (-not (Test-Path -LiteralPath $logPath)) { [IO.File]::WriteAllText($logPath, "") }
        $stages.Add([ordered]@{
            name = $Name
            executable = $Executable
            arguments = $Arguments
            startedUtc = $stageStart.ToString("o")
            wallClockSeconds = $watch.Elapsed.TotalSeconds
            exitCode = $exitCode
            log = $logPath
            logSha256 = (Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    if ($exitCode -ne 0) { throw "$Name failed with exit code $exitCode; see $logPath" }
}

Push-Location -LiteralPath $repositoryRoot
try {
    . (Join-Path $PSScriptRoot "tool-environment.ps1")
    Enable-CsxRepositoryGitSafety -RepositoryRoot $repositoryRoot
    $commonGitDirectory = (& git rev-parse --path-format=absolute --git-common-dir).Trim()
    Initialize-CsxToolEnvironment -RepositoryRoot $repositoryRoot -CommonGitDirectory $commonGitDirectory -ProtectPublicGitHub | Out-Null
    $python = (Get-Command python -ErrorAction Stop).Source
    $pwsh = (Get-Command pwsh -ErrorAction Stop).Source
    Invoke-ValidationStage "initial-status" $pwsh @("./tools/git.ps1", "status", "--short")
    Invoke-ValidationStage "initial-head" $pwsh @("./tools/git.ps1", "rev-parse", "HEAD")
    Invoke-ValidationStage "initial-submodules" $pwsh @("./tools/git.ps1", "submodule", "status", "--recursive")
    Invoke-ValidationStage "initial-source" $python @("./tools/build_provenance.py", "snapshot", "--source-dir", $repositoryRoot)
    Invoke-ValidationStage "cmake-version" $pwsh @("./tools/cmake.ps1", "--version")
    $cmakeVersionMatch = [regex]::Match((Get-Content -LiteralPath (Join-Path $recordRoot "cmake-version.log") -Raw), '(?m)^cmake version (\S+)')
    if (-not $cmakeVersionMatch.Success) { throw "CMake did not report its version." }
    $cmakeVersion = $cmakeVersionMatch.Groups[1].Value
    if ($cmakeVersion -match '^4\.3\.[01](?:-|$)') {
        throw "CMake 4.3.0/4.3.1 corrupts extracted declarations. Install CMake 4.3.5 or newer and select it on PATH."
    }
    Invoke-ValidationStage "configure" $pwsh @("./tools/cmake.ps1", "--preset", "ALL", "-DBUILD_CONTROLLER_TESTS=ON", "-DBUILD_SHADER_TESTS=ON")
    Copy-Item -LiteralPath "build/ALL/CMakeCache.txt" -Destination (Join-Path $recordRoot "CMakeCache.txt")
    $ctestEntry = @(Get-Content -LiteralPath (Join-Path $recordRoot "CMakeCache.txt") | Where-Object { $_ -match '^CMAKE_CTEST_COMMAND:INTERNAL=' })
    if ($ctestEntry.Count -ne 1) { throw "The configured build did not identify its CTest executable." }
    $ctest = $ctestEntry[0].Substring($ctestEntry[0].IndexOf('=') + 1)
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw "The configured CTest executable is missing: $ctest" }
    Invoke-ValidationStage "ctest-version" $ctest @("--version")
    if ((Get-Content -LiteralPath (Join-Path $recordRoot "ctest-version.log") -Raw) -notmatch ('(?m)^ctest version ' + [regex]::Escape($cmakeVersion) + '\s*$')) {
        throw "CMake and its configured CTest report different versions."
    }
    Invoke-ValidationStage "main-build" $pwsh @("./tools/cmake.ps1", "--build", "--preset", "CSmain", "--", "/m:1")
    Invoke-ValidationStage "test-build" $pwsh @("./tools/cmake.ps1", "--build", "build/ALL", "--config", "Release", "--target", "controller_tests", "shader_tests", "--", "/m:1")
    Invoke-ValidationStage "inventory" $ctest @("--test-dir", "build/ALL", "-C", "Release", "-N")
    Invoke-ValidationStage "inventory-json" $ctest @("--test-dir", "build/ALL", "-C", "Release", "--show-only=json-v1")
    $inventory = Get-Content -LiteralPath (Join-Path $recordRoot "inventory-json.log") -Raw | ConvertFrom-Json
    $required = @(
        "StreamlineFrameTokenPublication", "VRRenderScaleAuthorityPolicy", "VRRenderScaleQualificationPolicy",
        "VRRenderScaleModePolicy", "VRRelatchNativeBoundary", "VRRenderScaleLinkContract",
        "VRSubmitTemporalSnapshot", "VRSubmitStereoBatch", "VRRelatchDrainPolicy",
        "VRRelatchDrainController", "VRRelatchReleasePolicy", "VRPresentationStretchTelemetryPolicy", "VRLoadingMenuClear"
    )
    $missing = @($required | Where-Object { $_ -notin $inventory.tests.name })
    $unbuilt = @($inventory.tests | Where-Object { -not $_.PSObject.Properties["command"] -or -not $_.command })
    $disabled = @($inventory.tests | Where-Object { @($_.properties | Where-Object { $_.name -eq "DISABLED" -and $_.value }).Count -ne 0 })
    $summary.tests = [ordered]@{
        discovered = $inventory.tests.Count
        missing = @($missing)
        notBuilt = @($unbuilt | ForEach-Object { $_.name })
        disabled = @($disabled | ForEach-Object { $_.name })
    }
    if ($inventory.tests.Count -eq 0 -or $missing.Count -or $unbuilt.Count -or $disabled.Count) {
        throw "Incomplete test inventory; see summary.json and inventory.log."
    }
    $testFailure = $null
    try {
        Invoke-ValidationStage "ctest" $ctest @("--test-dir", "build/ALL", "-C", "Release", "--output-on-failure", "--no-tests=error", "--timeout", "300", "--output-junit", (Join-Path $recordRoot "ctest.xml"))
    } catch { $testFailure = $_ }
    Copy-Item -LiteralPath "build/ALL/Testing/Temporary/LastTest.log" -Destination (Join-Path $recordRoot "LastTest.log")
    if (Test-Path -LiteralPath (Join-Path $recordRoot "ctest.xml")) {
        [xml] $junit = Get-Content -LiteralPath (Join-Path $recordRoot "ctest.xml") -Raw
        $cases = @($junit.SelectNodes("/testsuite/testcase"))
        $failed = @($cases | Where-Object { $_.SelectSingleNode("failure|error") }).Count
        $skipped = @($cases | Where-Object { $_.SelectSingleNode("skipped") }).Count
        $summary.tests["executed"] = $cases.Count
        $summary.tests["failed"] = $failed
        $summary.tests["skipped"] = $skipped
        $summary.tests["passed"] = $cases.Count - $failed - $skipped
    } else { throw "CTest did not produce its test results." }
    if ($testFailure) { throw $testFailure }
    if ($cases.Count -ne $inventory.tests.Count -or $failed -or $skipped) { throw "CTest did not execute and pass the complete inventory." }

    # These commands share a publication lock and must run in sequence.
    Invoke-ValidationStage "preset-tests" $pwsh @("./tests/unified_preset_generator_test.ps1")
    Invoke-ValidationStage "preset-check" $pwsh @("./tools/generate-unified-presets.ps1", "-Check")
    Invoke-ValidationStage "diff-check" $pwsh @("./tools/git.ps1", "diff", "--check")
    Invoke-ValidationStage "manifest-verify" $python @("./tools/build_provenance.py", "verify", "--manifest", "build/ALL/Release/CSX.BuildManifest.json", "--artifact", "build/ALL/Release/CommunityShaders.dll")
    Copy-Item -LiteralPath "build/ALL/Release/CSX.BuildManifest.json" -Destination (Join-Path $recordRoot "CSX.BuildManifest.json")
    $manifest = Get-Content -LiteralPath (Join-Path $recordRoot "CSX.BuildManifest.json") -Raw | ConvertFrom-Json
    $summary.buildId = $manifest.buildId
    if ($manifest.identity.toolchain.cmakeVersion -cne $cmakeVersion) { throw "The DLL manifest names a different CMake version." }
    if ((Get-Item -LiteralPath "build/ALL/Release/CommunityShaders.dll").Length -ne $manifest.artifact.sizeBytes -or
        (Get-FileHash -LiteralPath $manifest.environment.compilerPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $manifest.identity.toolchain.compilerSha256) {
        throw "DLL size or compiler hash differs from the linked build manifest."
    }
    Invoke-ValidationStage "final-source" $python @("./tools/build_provenance.py", "snapshot", "--source-dir", $repositoryRoot)
    $before = Get-Content -LiteralPath (Join-Path $recordRoot "initial-source.log") -Raw
    $after = Get-Content -LiteralPath (Join-Path $recordRoot "final-source.log") -Raw
    $source = $after | ConvertFrom-Json
    if ($before.Trim() -cne $after.Trim() -or
        ($source.source | ConvertTo-Json -Compress) -cne ($manifest.identity.source | ConvertTo-Json -Compress) -or
        ($source.submodules | ConvertTo-Json -Depth 10 -Compress) -cne ($manifest.identity.dependencies.submodules | ConvertTo-Json -Depth 10 -Compress)) {
        throw "Source changed during validation or differs from the DLL manifest; preserve the evidence and rerun."
    }
    $summary["source"] = $source.source
    $summary["toolchain"] = $manifest.identity.toolchain
    $summary.verdict = "passed"
} catch {
    $summary.failure = $_.ToString()
    Write-Warning $summary.failure
} finally {
    try { Invoke-ValidationStage "final-status" "pwsh" @("./tools/git.ps1", "status", "--short") }
    catch { $summary.verdict = "failed"; $summary.failure = "$($summary.failure)`nFinal status failed: $_" }
    $finished = [DateTimeOffset]::UtcNow
    $summary["finishedUtc"] = $finished.ToString("o")
    $summary["totalWallClockSeconds"] = ($finished - $started).TotalSeconds
    $summary["stages"] = @($stages.ToArray())
    $summary | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $recordRoot "summary.json")
    Pop-Location
    Write-Host "Validation $($summary.verdict): $recordRoot"
}
if ($summary.verdict -ne "passed") { exit 1 }
