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
        $value = [string](& git -C $RepositoryRoot config --local --get $key 2>$null)
        $value = $value.Trim()
        if ($LASTEXITCODE -eq 0 -and $value) {
            Add-CsxGitCommandConfig -Key $key -Value $value
        }
    }
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
        $programFilesX86 = [Environment]::GetFolderPath(
            [Environment+SpecialFolder]::ProgramFilesX86)
        $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            $installationPaths = @(& $vswhere -products * -property installationPath 2>$null)
            foreach ($installationPath in $installationPaths) {
                if ($installationPath) {
                    $candidates.Add((Join-Path $installationPath "VC\vcpkg"))
                }
            }
        }

        $visualStudioRoot = Join-Path $env:ProgramFiles "Microsoft Visual Studio"
        if (Test-Path -LiteralPath $visualStudioRoot -PathType Container) {
            foreach ($versionDirectory in Get-ChildItem -LiteralPath $visualStudioRoot -Directory) {
                foreach ($editionDirectory in Get-ChildItem -LiteralPath $versionDirectory.FullName -Directory) {
                    $candidates.Add((Join-Path $editionDirectory.FullName "VC\vcpkg"))
                }
            }
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
    }
}
