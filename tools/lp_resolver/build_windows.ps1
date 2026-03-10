param(
    [string]$PythonExe = "python",
    [string]$AppName = "LPConflictResolver",
    [string]$DistDir = "dist\lp_resolver_app",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $repoRoot

if ($Clean) {
    if (Test-Path "build\$AppName") { Remove-Item -Recurse -Force "build\$AppName" }
    if (Test-Path "$DistDir\$AppName") { Remove-Item -Recurse -Force "$DistDir\$AppName" }
}

& $PythonExe -m pip install --upgrade pip
& $PythonExe -m pip install -r "tools\lp_resolver\requirements.txt"

& $PythonExe -m PyInstaller `
    --noconfirm `
    --onedir `
    --name $AppName `
    --distpath $DistDir `
    --workpath "build\$AppName" `
    --specpath "build\$AppName" `
    --collect-all PySide6 `
    "tools\lp_resolver\app.py"

Write-Host ""
Write-Host "Build complete:"
Write-Host "  $repoRoot\$DistDir\$AppName"

