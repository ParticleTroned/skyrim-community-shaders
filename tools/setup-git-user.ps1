[CmdletBinding(SupportsShouldProcess)]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $PSCmdlet.ShouldProcess(
    "the current user's Git configuration",
    "replace the broad GitHub HTTPS rewrite with a push-only SSH rewrite"
)) {
    return
}

& git config --global --unset-all "url.git@github.com:.insteadof" 2>$null
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 5) {
    throw "Unable to remove the global GitHub HTTPS rewrite."
}

& git config --global --replace-all "url.git@github.com:.pushInsteadOf" "https://github.com/"
if ($LASTEXITCODE -ne 0) {
    throw "Unable to configure the push-only GitHub SSH rewrite."
}

if ($env:OS -eq "Windows_NT") {
    & git config --global http.sslBackend openssl
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to configure Git's OpenSSL backend."
    }
}

Write-Host "User Git transport is ready: GitHub fetches remain HTTPS and pushes use SSH."
