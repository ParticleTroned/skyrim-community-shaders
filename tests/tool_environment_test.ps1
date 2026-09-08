Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $repositoryRoot "tools\tool-environment.ps1")

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)] $Expected,
        [Parameter(Mandatory = $true)] $Actual,
        [Parameter(Mandatory = $true)][string] $Message
    )

    if ($Expected -ne $Actual) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
}

function Invoke-Git {
    param([Parameter(Mandatory = $true)][string[]] $Arguments)

    & git @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Git failed with exit code ${LASTEXITCODE}: git $($Arguments -join ' ')"
    }
}

function Restore-TestEnvironment {
    param([Parameter(Mandatory = $true)][System.Collections.IDictionary] $Snapshot)

    foreach ($name in [Environment]::GetEnvironmentVariables("Process").Keys) {
        if (-not $Snapshot.Contains($name)) {
            [Environment]::SetEnvironmentVariable($name, $null, "Process")
        }
    }
    foreach ($entry in $Snapshot.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
}

$testParent = Join-Path $repositoryRoot "build\test-temp"
$testRoot = Join-Path `
    $testParent `
    ("csx-tool-environment-" + [Guid]::NewGuid().ToString("N"))
$sourceRepository = Join-Path $testRoot "source.git"
$standaloneClone = Join-Path $testRoot "standalone"

try {
    New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
    Invoke-Git -Arguments @("init", "--bare", $sourceRepository)
    Invoke-Git -Arguments @("clone", "--no-hardlinks", $sourceRepository, $standaloneClone)

    $initialConfigCount = if ($env:GIT_CONFIG_COUNT) {
        [int] $env:GIT_CONFIG_COUNT
    } else {
        0
    }

    Enable-CsxRepositoryGitSafety -RepositoryRoot $standaloneClone

    Assert-Equal `
        -Expected ($initialConfigCount + 1) `
        -Actual ([int] $env:GIT_CONFIG_COUNT) `
        -Message "A clone without local transport settings must add only safe.directory."
    Assert-Equal `
        -Expected "safe.directory" `
        -Actual ([Environment]::GetEnvironmentVariable("GIT_CONFIG_KEY_$initialConfigCount", "Process")) `
        -Message "The command-scoped Git key is incorrect."
    Assert-Equal `
        -Expected ([IO.Path]::GetFullPath($standaloneClone).Replace("\", "/")) `
        -Actual ([Environment]::GetEnvironmentVariable("GIT_CONFIG_VALUE_$initialConfigCount", "Process")) `
        -Message "The standalone clone was not marked safe."

    if ($env:OS -eq "Windows_NT") {
        $savedEnvironment = [Environment]::GetEnvironmentVariables("Process")

        try {
            $vsDevCmd = Join-Path $testRoot "VsDevCmd.bat"
            [IO.File]::WriteAllLines($vsDevCmd, @(
                "@echo off",
                "set `"VCToolsInstallDir=C:\csx-test\VC\Tools\`"",
                "set `"INCLUDE=C:\csx-test\include`"",
                "set `"LIB=C:\csx-test\lib`"",
                "set `"CSX_MSVC_TEST_MARKER=initialized`""
            ))

            $env:CSX_VSDEVCMD = $null
            $env:VSINSTALLDIR = $null
            $discoveredVsDevCmd = Resolve-CsxVsDevCmd -Required
            $env:VSINSTALLDIR = Join-Path $testRoot "Missing Visual Studio"
            Assert-Equal -Expected $discoveredVsDevCmd -Actual (Resolve-CsxVsDevCmd -Required) `
                -Message "An absent active installation must fall back to discovery."

            $activeInstallation = Join-Path $testRoot "Active Visual Studio"
            $activeTools = Join-Path $activeInstallation "Common7\Tools"
            New-Item -ItemType Directory -Force -Path $activeTools | Out-Null
            New-Item -ItemType Directory -Force -Path (Join-Path $activeInstallation "VC\Tools\MSVC") | Out-Null
            $activeVsDevCmd = Join-Path $activeTools "VsDevCmd.bat"
            Copy-Item -LiteralPath $vsDevCmd -Destination $activeVsDevCmd
            $env:VSINSTALLDIR = $activeInstallation
            Assert-Equal -Expected $activeVsDevCmd -Actual (Resolve-CsxVsDevCmd -Required) `
                -Message "The active installation must take precedence over discovered installations."

            foreach ($name in @("VCToolsInstallDir", "INCLUDE", "LIB")) {
                [Environment]::SetEnvironmentVariable($name, $null, "Process")
            }
            $env:CSX_VSDEVCMD = $vsDevCmd
            Assert-Equal -Expected $vsDevCmd -Actual (Resolve-CsxVsDevCmd -Required) `
                -Message "An explicit override must take precedence over the active installation."

            $cmakeLauncher = Join-Path $repositoryRoot "tools\cmake.ps1"
            [string[]] $cmakeOutput = @(
                & (Join-Path $PSHOME "pwsh.exe") -NoProfile -File $cmakeLauncher --version
            )
            if ($LASTEXITCODE -ne 0) {
                throw "The CMake launcher failed with exit code $LASTEXITCODE."
            }
            if (-not ($cmakeOutput -match "Initialized the MSVC environment")) {
                throw "The CMake launcher did not initialize the missing MSVC environment."
            }

            $initializedWith = Initialize-CsxMsvcEnvironment -Required

            Assert-Equal -Expected $vsDevCmd -Actual $initializedWith `
                -Message "The configured VsDevCmd path was not used."
            Assert-Equal -Expected "initialized" -Actual $env:CSX_MSVC_TEST_MARKER `
                -Message "The Visual Studio environment was not imported."
            if (-not (Test-CsxMsvcEnvironment)) {
                throw "The imported Visual Studio environment is incomplete."
            }

            $env:CSX_MSVC_TEST_MARKER = "retained"
            if ($null -ne (Initialize-CsxMsvcEnvironment -Required)) {
                throw "An initialized MSVC environment must be reused."
            }
            Assert-Equal -Expected "retained" -Actual $env:CSX_MSVC_TEST_MARKER `
                -Message "The active Visual Studio environment was initialized again."

            # VsDevCmd imports PATH, SDK and installation state as one environment.
            Restore-TestEnvironment -Snapshot $savedEnvironment
            Initialize-CsxMsvcEnvironment -Required | Out-Null
            $expectedVsDevCmd = Resolve-CsxVsDevCmd -Required
            foreach ($name in @("VCToolsInstallDir", "INCLUDE", "LIB")) {
                [Environment]::SetEnvironmentVariable($name, $null, "Process")
            }

            $actualVsDevCmd = Initialize-CsxMsvcEnvironment -Required
            Assert-Equal -Expected $expectedVsDevCmd -Actual $actualVsDevCmd `
                -Message "Repairing a partial environment must retain the active installation."
            $compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
            if (-not $compiler) {
                throw "cl.exe was not added to PATH by $actualVsDevCmd."
            }

            $probeSource = Join-Path $testRoot "standard-library-probe.cpp"
            $probeObject = Join-Path $testRoot "standard-library-probe.obj"
            [IO.File]::WriteAllLines($probeSource, @(
                "#include <algorithm>",
                "#include <cstdint>",
                "int main() { return std::max(std::int32_t{1}, std::int32_t{2}); }"
            ))
            & $compiler.Source /nologo /c /EHsc "/Fo$probeObject" $probeSource
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $probeObject -PathType Leaf)) {
                throw "The initialized MSVC environment could not compile standard-library headers."
            }
        } finally {
            Restore-TestEnvironment -Snapshot $savedEnvironment
        }
    }
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedParent = [IO.Path]::GetFullPath($testParent)
        $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
        if ([IO.Path]::GetDirectoryName($resolvedTestRoot) -ne $resolvedParent) {
            throw "Refusing to remove a test path outside the expected root: $resolvedTestRoot"
        }
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}

Write-Host "Tool environment tests passed."
