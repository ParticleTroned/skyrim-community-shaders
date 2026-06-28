<#
.SYNOPSIS
    Stage DXVK's d3d11/dxgi DLLs into Community Shaders' mod subfolder under unique base names so they can
    be loaded from Data/ instead of the game root.

.DESCRIPTION
    Skyrim statically imports one symbol from each of d3d11.dll and dxgi.dll, so the System32 copies are
    mapped at process start and the Windows loader keys modules by base name — a second "d3d11.dll"/"dxgi.dll"
    from a subfolder would just alias the already-loaded System32 module. To run DXVK from Data/ we give its
    DLLs unique base names and let CS load them explicitly + redirect the two hooked entry points.

    DXVK now BUILDS its DLLs with a 'dxvk_' name prefix (extern/dxvk/meson.build dxvk_name_prefix='dxvk_',
    with the matching LIBRARY directives in the d3d11/dxgi .def files), so it emits dxvk_d3d11.dll and
    dxvk_dxgi.dll and d3d11 already imports dxvk_dxgi.dll. No post-build import-name patch is needed — this
    script just copies the two DLLs into place (content-compared so incremental deploys stay quiet).

    Output:
      <Dst>/dxvk_d3d11.dll
      <Dst>/dxvk_dxgi.dll

.PARAMETER Src
    DXVK build "src" directory containing d3d11/dxvk_d3d11.dll and dxgi/dxvk_dxgi.dll
    (flat layout directly under Src is also accepted).

.PARAMETER Dst
    Destination directory (created if missing). Existing DLLs are overwritten only when content differs.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Src,
    [Parameter(Mandatory = $true)][string]$Dst
)

$ErrorActionPreference = 'Stop'

function Resolve-DxvkDll {
    param([string]$Base, [string]$Subdir, [string]$Name)
    $nested = Join-Path $Base (Join-Path $Subdir $Name)
    if (Test-Path -LiteralPath $nested) { return (Resolve-Path -LiteralPath $nested).Path }
    $flat = Join-Path $Base $Name
    if (Test-Path -LiteralPath $flat) { return (Resolve-Path -LiteralPath $flat).Path }
    throw "Could not find $Name under '$Base' (looked for '$nested' and '$flat')"
}

# Write bytes only if the target differs (keeps incremental deploy timestamps stable).
function Write-IfDifferent {
    param([string]$Path, [byte[]]$Bytes)
    if (Test-Path -LiteralPath $Path) {
        $existing = [System.IO.File]::ReadAllBytes($Path)
        if ($existing.Length -eq $Bytes.Length) {
            $same = $true
            for ($i = 0; $i -lt $Bytes.Length; $i++) {
                if ($existing[$i] -ne $Bytes[$i]) { $same = $false; break }
            }
            if ($same) { Write-Host "  unchanged: $Path"; return }
        }
    }
    [System.IO.File]::WriteAllBytes($Path, $Bytes)
    Write-Host "  wrote:     $Path"
}

$d3d11Src = Resolve-DxvkDll -Base $Src -Subdir 'd3d11' -Name 'dxvk_d3d11.dll'
$dxgiSrc = Resolve-DxvkDll -Base $Src -Subdir 'dxgi' -Name 'dxvk_dxgi.dll'

if (-not (Test-Path -LiteralPath $Dst)) {
    New-Item -ItemType Directory -Path $Dst -Force | Out-Null
}
$Dst = (Resolve-Path -LiteralPath $Dst).Path

Write-Host "Staging DXVK DLLs:"
Write-Host "  d3d11 src: $d3d11Src"
Write-Host "  dxgi  src: $dxgiSrc"
Write-Host "  dst:       $Dst"

# Both DLLs are copied verbatim — DXVK already built them with the dxvk_ prefix and the correct
# dxvk_d3d11 -> dxvk_dxgi import binding, so no import-name patch is required.
Write-IfDifferent -Path (Join-Path $Dst 'dxvk_dxgi.dll') -Bytes ([System.IO.File]::ReadAllBytes($dxgiSrc))
Write-IfDifferent -Path (Join-Path $Dst 'dxvk_d3d11.dll') -Bytes ([System.IO.File]::ReadAllBytes($d3d11Src))

Write-Host "Done."
