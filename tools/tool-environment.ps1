Set-StrictMode -Version Latest

function Add-CsxGitCommandConfig {
    param(
        [Parameter(Mandatory = $true)][string] $Key,
        [Parameter(Mandatory = $true)][string] $Value
    )

    $count = if ($env:GIT_CONFIG_COUNT) { [int] $env:GIT_CONFIG_COUNT } else { 0 }
    [Environment]::SetEnvironmentVariable("GIT_CONFIG_KEY_$count", $Key, "Process")
    [Environment]::SetEnvironmentVariable("GIT_CONFIG_VALUE_$count", $Value, "Process")
    $env:GIT_CONFIG_COUNT = $count + 1
}

function Enable-CsxRepositoryGitSafety {
    param([Parameter(Mandatory = $true)][string] $RepositoryRoot)

    $resolvedRoot = [IO.Path]::GetFullPath($RepositoryRoot).Replace("\", "/")
    Add-CsxGitCommandConfig -Key "safe.directory" -Value $resolvedRoot

    # Repository-local transport settings do not automatically reach the fresh
    # repositories created by recursive submodule clones. Publish the effective
    # values as command-scope config so every child Git process retains the same
    # strict SSH identity and OpenSSL HTTPS behavior.
    foreach ($key in @("core.sshCommand", "http.sslBackend")) {
        [string[]] $configuredValues = @(
            & git -C $RepositoryRoot config --local --get $key 2>$null
        )
        $gitExitCode = $LASTEXITCODE
        if ($gitExitCode -ne 0) {
            continue
        }

        $value = ($configuredValues -join "`n").Trim()
        if ($value) {
            Add-CsxGitCommandConfig -Key $key -Value $value
        }
    }
}

function Get-CsxVisualStudioInstallationPaths {
    param([switch] $RequireMsvc)

    if ($env:OS -ne "Windows_NT") {
        return @()
    }

    $candidates = [System.Collections.Generic.List[string]]::new()
    $vswhereCandidates = [System.Collections.Generic.List[string]]::new()
    $vswhereCommand = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($vswhereCommand) {
        $vswhereCandidates.Add($vswhereCommand.Source)
    }

    $programFilesX86 = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::ProgramFilesX86)
    if ($programFilesX86) {
        $vswhereCandidates.Add((Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"))
    }

    foreach ($vswhere in $vswhereCandidates | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
            continue
        }

        $vswhereArguments = @("-products", "*")
        if ($RequireMsvc) {
            $vswhereArguments += @(
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
            )
        }
        $vswhereArguments += @("-property", "installationPath")

        foreach ($installationPath in @(& $vswhere @vswhereArguments 2>$null)) {
            if ($installationPath) {
                $candidates.Add($installationPath)
            }
        }
        break
    }

    $programFiles = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::ProgramFiles)
    if ($programFiles) {
        $visualStudioRoot = Join-Path $programFiles "Microsoft Visual Studio"
        if (Test-Path -LiteralPath $visualStudioRoot -PathType Container) {
            foreach ($versionDirectory in Get-ChildItem -LiteralPath $visualStudioRoot -Directory | Sort-Object Name -Descending) {
                foreach ($editionDirectory in Get-ChildItem -LiteralPath $versionDirectory.FullName -Directory) {
                    $candidates.Add($editionDirectory.FullName)
                }
            }
        }
    }

    return @(
        $candidates |
            Where-Object {
                (Test-Path -LiteralPath $_ -PathType Container) -and
                (-not $RequireMsvc -or
                    (Test-Path -LiteralPath (Join-Path $_ "VC\Tools\MSVC") -PathType Container))
            } |
            Select-Object -Unique
    )
}

function Test-CsxMsvcEnvironment {
    return -not [string]::IsNullOrWhiteSpace($env:VCToolsInstallDir) -and
           -not [string]::IsNullOrWhiteSpace($env:INCLUDE) -and
           -not [string]::IsNullOrWhiteSpace($env:LIB)
}

function Resolve-CsxVsDevCmd {
    param([switch] $Required)

    if ($env:OS -ne "Windows_NT") {
        return $null
    }

    if ($env:CSX_VSDEVCMD) {
        if (Test-Path -LiteralPath $env:CSX_VSDEVCMD -PathType Leaf) {
            return [IO.Path]::GetFullPath($env:CSX_VSDEVCMD)
        }
        throw "CSX_VSDEVCMD does not point to VsDevCmd.bat: $env:CSX_VSDEVCMD"
    }

    foreach ($installationPath in Get-CsxVisualStudioInstallationPaths -RequireMsvc) {
        $candidate = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }

    if ($Required) {
        throw "VsDevCmd.bat was not found. Install the Visual Studio C++ build tools or set CSX_VSDEVCMD."
    }
    return $null
}

function Initialize-CsxMsvcEnvironment {
    param([switch] $Required)

    if ($env:OS -ne "Windows_NT" -or (Test-CsxMsvcEnvironment)) {
        return $null
    }

    $vsDevCmd = Resolve-CsxVsDevCmd -Required:$Required
    if (-not $vsDevCmd) {
        return $null
    }

    $commandProcessor = Get-Command cmd.exe -ErrorAction SilentlyContinue
    if (-not $commandProcessor) {
        throw "cmd.exe was not found; the Visual Studio build environment cannot be initialized."
    }

    $command = "call `"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    [string[]] $environmentLines = @(& $commandProcessor.Source /d /s /c $command)
    $commandExitCode = $LASTEXITCODE
    if ($commandExitCode -ne 0) {
        throw "VsDevCmd.bat failed with exit code ${commandExitCode}: $vsDevCmd"
    }

    foreach ($line in $environmentLines) {
        if ($line -match "^([^=][^=]*)=(.*)$") {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
        }
    }

    if (-not (Test-CsxMsvcEnvironment)) {
        throw "VsDevCmd.bat completed without defining VCToolsInstallDir, INCLUDE, and LIB: $vsDevCmd"
    }

    return $vsDevCmd
}

function Test-CsxVcpkgRoot {
    param([Parameter(Mandatory = $true)][string] $Path)

    if (-not $Path) {
        return $false
    }

    $resolvedPath = [IO.Path]::GetFullPath($Path)
    return (Test-Path -LiteralPath (Join-Path $resolvedPath "scripts\buildsystems\vcpkg.cmake") -PathType Leaf) -and
           (Test-Path -LiteralPath (Join-Path $resolvedPath "vcpkg.exe") -PathType Leaf)
}

function Resolve-CsxVcpkgRoot {
    param([switch] $Required)

    if ($env:VCPKG_ROOT) {
        if (Test-CsxVcpkgRoot -Path $env:VCPKG_ROOT) {
            return [IO.Path]::GetFullPath($env:VCPKG_ROOT)
        }
        throw "VCPKG_ROOT does not contain vcpkg.exe and scripts\buildsystems\vcpkg.cmake: $env:VCPKG_ROOT"
    }

    $candidates = [System.Collections.Generic.List[string]]::new()
    $vcpkgCommand = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
    if ($vcpkgCommand) {
        $candidates.Add((Split-Path -Parent $vcpkgCommand.Source))
    }

    if ($env:OS -eq "Windows_NT") {
        foreach ($installationPath in Get-CsxVisualStudioInstallationPaths) {
            $candidates.Add((Join-Path $installationPath "VC\vcpkg"))
        }
    }

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-CsxVcpkgRoot -Path $candidate) {
            $resolvedPath = [IO.Path]::GetFullPath($candidate)
            $env:VCPKG_ROOT = $resolvedPath
            return $resolvedPath
        }
    }

    if ($Required) {
        throw "vcpkg was not found. Install it or set VCPKG_ROOT to a complete vcpkg root."
    }
    return $null
}

function Initialize-CsxVcpkgCacheEnvironment {
    param(
        [Parameter(Mandatory = $true)][string] $RepositoryRoot,
        [Parameter(Mandatory = $true)][string] $ToolRoot
    )

    $vcpkgCacheRoot = Join-Path $ToolRoot "vcpkg"
    $defaults = [ordered]@{
        VCPKG_DOWNLOADS = Join-Path $vcpkgCacheRoot "downloads"
        X_VCPKG_REGISTRIES_CACHE = Join-Path $vcpkgCacheRoot "registries"
        VCPKG_DEFAULT_BINARY_CACHE = Join-Path $vcpkgCacheRoot "archives"
    }

    foreach ($entry in $defaults.GetEnumerator()) {
        $configuredPath = [Environment]::GetEnvironmentVariable($entry.Key, "Process")
        if (-not $configuredPath) {
            $configuredPath = $entry.Value
            [Environment]::SetEnvironmentVariable($entry.Key, $configuredPath, "Process")
        }
        New-Item -ItemType Directory -Force -Path $configuredPath | Out-Null
    }

    $assetDownloader = $null
    $cmakeAssetDownloader = $null
    if ($env:CODEX_CI -eq "1") {
        $managedPython = if ($env:OS -eq "Windows_NT") {
            Join-Path $ToolRoot "venv\Scripts\python.exe"
        } else {
            Join-Path $ToolRoot "venv/bin/python"
        }

        $assetDownloader = Join-Path $RepositoryRoot "tools\vcpkg-download.py"
        if ((Test-Path -LiteralPath $managedPython -PathType Leaf) -and
            (Test-Path -LiteralPath $assetDownloader -PathType Leaf)) {
            $env:CSX_ASSET_DOWNLOADER_PYTHON = $managedPython
            $env:CSX_ASSET_DOWNLOADER_SCRIPT = $assetDownloader
            $cmakeAssetDownloader = $assetDownloader
            if (-not $env:X_VCPKG_ASSET_SOURCES) {
                $env:X_VCPKG_ASSET_SOURCES =
                    "x-script,`"$managedPython`" `"$assetDownloader`" {url} {dst}"
            } else {
                $assetDownloader = $null
            }
        } else {
            $assetDownloader = $null
        }
    }

    return [pscustomobject]@{
        Downloads = $env:VCPKG_DOWNLOADS
        Registries = $env:X_VCPKG_REGISTRIES_CACHE
        BinaryCache = $env:VCPKG_DEFAULT_BINARY_CACHE
        AssetDownloader = $assetDownloader
        CmakeAssetDownloader = $cmakeAssetDownloader
    }
}

function Initialize-CsxToolEnvironment {
    param(
        [Parameter(Mandatory = $true)][string] $RepositoryRoot,
        [Parameter(Mandatory = $true)][string] $CommonGitDirectory,
        [switch] $ProtectPublicGitHub
    )

    $toolRoot = Join-Path $CommonGitDirectory "csx-tools"
    $toolTemp = Join-Path $RepositoryRoot "build\tool-temp"
    foreach ($directory in @($toolRoot, $toolTemp)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }

    # Assign these only after PowerShell starts. Codex's Windows sandbox helper
    # initializes before the command and must retain its own temporary path.
    $env:TEMP = $toolTemp
    $env:TMP = $toolTemp
    $env:PIP_CACHE_DIR = Join-Path $toolRoot "pip-cache"
    $env:npm_config_cache = Join-Path $toolRoot "npm-cache"
    $env:PIP_DISABLE_PIP_VERSION_CHECK = "1"
    $env:GIT_TERMINAL_PROMPT = "0"

    # vcpkg otherwise stores registry and package state in the user profile.
    # That location may be unavailable to an isolated build process, producing
    # misleading "port does not exist" errors. Keep default caches shared by
    # all worktrees in the writable common Git tool root, while preserving
    # explicit caller overrides.
    $vcpkgCaches = Initialize-CsxVcpkgCacheEnvironment `
        -RepositoryRoot $RepositoryRoot `
        -ToolRoot $toolRoot

    if ($ProtectPublicGitHub) {
        $broadRewrite = @(& git config --global --get-all "url.git@github.com:.insteadof" 2>$null)
        if ($broadRewrite -contains "https://github.com/") {
            # Ignore the toxic user-level rewrite only in tool subprocesses.
            # Existing command-scope config (including Codex safe.directory
            # entries) stays intact; system and repository config still apply.
            $env:GIT_CONFIG_GLOBAL = if ($env:OS -eq "Windows_NT") { "NUL" } else { "/dev/null" }
            if ($env:OS -eq "Windows_NT") {
                Add-CsxGitCommandConfig -Key "http.sslBackend" -Value "openssl"
            }
        }
    }

    return [pscustomobject]@{
        ToolRoot = $toolRoot
        ToolTemp = $toolTemp
        VcpkgDownloads = $vcpkgCaches.Downloads
        VcpkgRegistries = $vcpkgCaches.Registries
        VcpkgBinaryCache = $vcpkgCaches.BinaryCache
        VcpkgAssetDownloader = $vcpkgCaches.AssetDownloader
        CmakeAssetDownloader = $vcpkgCaches.CmakeAssetDownloader
    }
}
