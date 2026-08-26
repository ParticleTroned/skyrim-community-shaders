[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $GhidraInstallDir,
    [string] $GhidraUserExtensionsDir,
    [string] $JavaHome,
    [switch] $RefreshSource,
    [switch] $RegisterCodex,
    [ValidatePattern("^[A-Za-z0-9_-]+$")]
    [string] $CodexServerName = "ghidra",
    [ValidateRange(1, 65535)]
    [int] $Port = 8080
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$onWindows = $env:OS -eq "Windows_NT"

. (Join-Path $PSScriptRoot "tool-environment.ps1")

$sourceUrl = "https://github.com/alandtse/GhidrAssistMCP.git"
$pinnedRevision = "2f6b10410081ea3691a4c2a73f8e8e7f24b72fcd"
$repositoryRoot = Split-Path -Parent $PSScriptRoot

Enable-CsxRepositoryGitSafety -RepositoryRoot $repositoryRoot

function Resolve-ExistingDirectory {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Description
    )

    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if (-not $item.PSIsContainer) {
        throw "$Description is not a directory: $Path"
    }
    return $item.FullName
}

function Get-UnresolvedFullPath([string] $Path) {
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Assert-ChildPath {
    param(
        [Parameter(Mandatory = $true)][string] $Parent,
        [Parameter(Mandatory = $true)][string] $Candidate
    )

    $parentPrefix = [IO.Path]::GetFullPath($Parent).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    $candidatePath = [IO.Path]::GetFullPath($Candidate)
    if (-not $candidatePath.StartsWith($parentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Tool path escapes its cache root: $candidatePath"
    }
}

function Invoke-Git {
    param([Parameter(Mandatory = $true)][string[]] $Arguments)

    & $script:gitExecutable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: git $($Arguments -join ' ')"
    }
}

function Invoke-GitText {
    param([Parameter(Mandatory = $true)][string[]] $Arguments)

    $value = (& $script:gitExecutable @Arguments 2>$null).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: git $($Arguments -join ' ')"
    }
    return $value
}

function Resolve-JavaInstallation {
    param([string] $RequestedJavaHome)

    $candidateRoot = $RequestedJavaHome
    if (-not $candidateRoot) {
        $candidateRoot = $env:GHIDRA_JAVA_HOME
    }
    if (-not $candidateRoot) {
        $candidateRoot = $env:JAVA_HOME
    }

    if ($candidateRoot) {
        $resolvedRoot = Resolve-ExistingDirectory `
            -Path $candidateRoot `
            -Description "Java home"
        $javaName = if ($script:onWindows) { "java.exe" } else { "java" }
        $javaExecutable = Join-Path $resolvedRoot "bin\$javaName"
    } else {
        $javaCommand = Get-Command java -ErrorAction Stop
        $javaExecutable = $javaCommand.Source
        $resolvedRoot = Split-Path -Parent (Split-Path -Parent $javaExecutable)
    }

    if (-not (Test-Path -LiteralPath $javaExecutable -PathType Leaf)) {
        throw "Java executable was not found under $resolvedRoot."
    }

    $versionOutput = (& $javaExecutable -version 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to query Java at $javaExecutable."
    }

    $versionMatch = [regex]::Match(
        $versionOutput,
        'version\s+"(?<major>\d+)(?:\.(?<minor>\d+))?'
    )
    if (-not $versionMatch.Success) {
        throw "Unable to parse the Java version reported by $javaExecutable."
    }

    $major = [int] $versionMatch.Groups["major"].Value
    if ($major -eq 1 -and $versionMatch.Groups["minor"].Success) {
        $major = [int] $versionMatch.Groups["minor"].Value
    }
    if ($major -lt 25) {
        throw "GhidrAssistMCP requires Java 25 or newer; found Java $major."
    }

    return [pscustomobject]@{
        Home = $resolvedRoot
        Executable = $javaExecutable
        MajorVersion = $major
    }
}

function Restore-EnvironmentValue {
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [AllowNull()][string] $Value
    )

    if ($null -eq $Value) {
        Remove-Item -LiteralPath "Env:$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item -LiteralPath "Env:$Name" -Value $Value
    }
}

function Get-EnvironmentValue([string] $Name) {
    $item = Get-Item -LiteralPath "Env:$Name" -ErrorAction SilentlyContinue
    if ($null -eq $item) {
        return $null
    }
    return $item.Value
}

$gitCommand = Get-Command $(if ($onWindows) { "git.exe" } else { "git" }) -ErrorAction Stop
$script:gitExecutable = $gitCommand.Source
$script:onWindows = $onWindows

$resolvedGhidraRoot = Resolve-ExistingDirectory `
    -Path $GhidraInstallDir `
    -Description "Ghidra installation"
$ghidraBuildScript = Join-Path $resolvedGhidraRoot "support\buildExtension.gradle"
if (-not (Test-Path -LiteralPath $ghidraBuildScript -PathType Leaf)) {
    throw "The selected directory is not a Ghidra installation: $resolvedGhidraRoot"
}

$resolvedUserExtensions = $null
if ($GhidraUserExtensionsDir) {
    $resolvedUserExtensions = Get-UnresolvedFullPath $GhidraUserExtensionsDir
}

$java = Resolve-JavaInstallation -RequestedJavaHome $JavaHome

$commonGitDirectory = Invoke-GitText -Arguments @(
    "-C",
    $repositoryRoot,
    "rev-parse",
    "--path-format=absolute",
    "--git-common-dir"
)
$toolRoot = Join-Path $commonGitDirectory "csx-tools"
$sourceParent = Join-Path $toolRoot "ghidrassist-mcp"
$sourceRoot = Join-Path $sourceParent $pinnedRevision

Initialize-CsxToolEnvironment `
    -RepositoryRoot $repositoryRoot `
    -CommonGitDirectory $commonGitDirectory `
    -ProtectPublicGitHub | Out-Null

Assert-ChildPath -Parent $toolRoot -Candidate $sourceRoot
if ($RefreshSource -and (Test-Path -LiteralPath $sourceRoot)) {
    Remove-Item -LiteralPath $sourceRoot -Recurse -Force
}

if (-not (Test-Path -LiteralPath $sourceRoot)) {
    New-Item -ItemType Directory -Path $sourceParent -Force | Out-Null
    $stagingRoot = "$sourceRoot.partial-$PID"
    Assert-ChildPath -Parent $toolRoot -Candidate $stagingRoot
    if (Test-Path -LiteralPath $stagingRoot) {
        throw "Incomplete GhidrAssistMCP staging directory already exists: $stagingRoot"
    }

    try {
        Write-Host "Fetching pinned GhidrAssistMCP source..."
        Invoke-Git -Arguments @(
            "-c",
            "http.sslBackend=openssl",
            "clone",
            "--filter=blob:none",
            "--no-checkout",
            $sourceUrl,
            $stagingRoot
        )
        Invoke-Git -Arguments @(
            "-C",
            $stagingRoot,
            "checkout",
            "--detach",
            $pinnedRevision
        )

        $stagedRevision = Invoke-GitText -Arguments @(
            "-C",
            $stagingRoot,
            "rev-parse",
            "HEAD"
        )
        if ($stagedRevision -ne $pinnedRevision) {
            throw "Fetched GhidrAssistMCP revision $stagedRevision, expected $pinnedRevision."
        }

        Move-Item -LiteralPath $stagingRoot -Destination $sourceRoot
    } catch {
        if (Test-Path -LiteralPath $stagingRoot) {
            Assert-ChildPath -Parent $toolRoot -Candidate $stagingRoot
            Remove-Item -LiteralPath $stagingRoot -Recurse -Force
        }
        throw
    }
}

$sourceRevision = Invoke-GitText -Arguments @(
    "-C",
    $sourceRoot,
    "rev-parse",
    "HEAD"
)
$sourceRemote = Invoke-GitText -Arguments @(
    "-C",
    $sourceRoot,
    "remote",
    "get-url",
    "origin"
)
if ($sourceRevision -ne $pinnedRevision -or $sourceRemote -ne $sourceUrl) {
    throw "The cached GhidrAssistMCP source does not match the repository pin. Use -RefreshSource to replace it."
}

$gradleWrapperName = if ($onWindows) { "gradlew.bat" } else { "gradlew" }
$gradleWrapper = Join-Path $sourceRoot $gradleWrapperName
if (-not (Test-Path -LiteralPath $gradleWrapper -PathType Leaf)) {
    throw "The pinned GhidrAssistMCP source does not contain $gradleWrapperName."
}

$gradleArguments = @(
    "--no-daemon",
    "--console=plain",
    "installExtension",
    "-PGHIDRA_INSTALL_DIR=$resolvedGhidraRoot"
)
if ($resolvedUserExtensions) {
    $gradleArguments += "-PGHIDRA_USER_EXTENSIONS_DIR=$resolvedUserExtensions"
}

$previousJavaHome = Get-EnvironmentValue -Name "JAVA_HOME"
$previousGhidraJavaHome = Get-EnvironmentValue -Name "GHIDRA_JAVA_HOME"
$env:JAVA_HOME = $java.Home
$env:GHIDRA_JAVA_HOME = $java.Home

Push-Location $sourceRoot
try {
    Write-Host "Building and installing GhidrAssistMCP $pinnedRevision..."
    & $gradleWrapper @gradleArguments
    if ($LASTEXITCODE -ne 0) {
        throw "GhidrAssistMCP installation failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
    Restore-EnvironmentValue -Name "JAVA_HOME" -Value $previousJavaHome
    Restore-EnvironmentValue -Name "GHIDRA_JAVA_HOME" -Value $previousGhidraJavaHome
}

$endpoint = "http://127.0.0.1:$Port/mcp"
if ($RegisterCodex) {
    $codexCommand = Get-Command codex -ErrorAction Stop
    $existingJson = & $codexCommand.Source mcp get $CodexServerName --json 2>$null
    if ($LASTEXITCODE -eq 0) {
        $existing = $existingJson | ConvertFrom-Json
        if (
            $existing.transport.type -ne "streamable_http" -or
            $existing.transport.url -ne $endpoint
        ) {
            throw (
                "Codex MCP server '$CodexServerName' already exists with " +
                "different settings. Remove or rename it explicitly before " +
                "registering $endpoint."
            )
        }
        Write-Host "Codex MCP server '$CodexServerName' already targets $endpoint."
    } else {
        & $codexCommand.Source mcp add $CodexServerName --url $endpoint
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to register the Ghidra MCP endpoint with Codex."
        }
    }
}

Write-Host "GhidrAssistMCP is installed from pinned revision $pinnedRevision."
Write-Host "Start a managed or GUI Ghidra MCP session bound to $endpoint."
Write-Host "Managed command: pwsh ./tools/ghidra-mcp-control.ps1 start -GhidraInstallDir <path> -ProgramPath <binary>"
