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
