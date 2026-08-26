# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $LaunchFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-AtomicJson {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)] $Value
    )

    $temporaryPath = "$Path.partial-$PID"
    $Value |
        ConvertTo-Json -Depth 20 |
        Set-Content -LiteralPath $temporaryPath -Encoding utf8
    Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
}

$resolvedLaunchFile = [IO.Path]::GetFullPath($LaunchFile)
$launch = Get-Content -LiteralPath $resolvedLaunchFile -Raw | ConvertFrom-Json
$startedUtc = [DateTime]::UtcNow
$exitCode = 1
$failure = $null

try {
    $env:JAVA_HOME = [string] $launch.javaHome
    $env:GHIDRA_JAVA_HOME = [string] $launch.javaHome
    Set-Location -LiteralPath ([string] $launch.workingDirectory)

    & ([string] $launch.analyzeHeadless) @($launch.arguments) `
        1>> ([string] $launch.stdoutLog) `
        2>> ([string] $launch.stderrLog)
    $exitCode = $LASTEXITCODE
} catch {
    $failure = $_.Exception.Message
    $failure | Add-Content -LiteralPath ([string] $launch.stderrLog) -Encoding utf8
} finally {
    Write-AtomicJson `
        -Path ([string] $launch.exitReceipt) `
        -Value ([ordered]@{
            schemaVersion = 1
            processId = $PID
            startedUtc = $startedUtc.ToString("o")
            finishedUtc = [DateTime]::UtcNow.ToString("o")
            exitCode = $exitCode
            error = $failure
        })
}

exit $exitCode
