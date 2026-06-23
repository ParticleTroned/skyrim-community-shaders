<#
.SYNOPSIS
    Stage DXVK's d3d11.dll / dxgi.dll into Community Shaders' mod subfolder under
    UNIQUE base names so they can be loaded from Data/ instead of the game root.

.DESCRIPTION
    Skyrim statically imports exactly one symbol from each of d3d11.dll and dxgi.dll
    (D3D11CreateDeviceAndSwapChain and CreateDXGIFactory). Those System32 copies are
    therefore mapped at process start, and the Windows loader keys modules by base
    name, so a second "d3d11.dll"/"dxgi.dll" loaded from a subfolder would just alias
    the already-loaded System32 module. To run DXVK from Data/ we give its DLLs unique
    base names and let CS load them explicitly + redirect the two hooked entry points.

    DXVK's d3d11.dll imports dxgi.dll by name, so renaming dxgi would otherwise bind
    d3d11 to System32's dxgi. We repoint that single import-descriptor name string in
    place: "dxgi.dll" -> "csgi.dll" (same length, same extension, unique base name).

    Output:
      <Dst>/csd3d11.dll  (DXVK d3d11.dll, import "dxgi.dll" -> "csgi.dll")
      <Dst>/csgi.dll     (DXVK dxgi.dll, unchanged bytes)

.PARAMETER Src
    DXVK build "src" directory containing d3d11/d3d11.dll and dxgi/dxgi.dll
    (flat layout d3d11.dll / dxgi.dll directly under Src is also accepted).

.PARAMETER Dst
    Destination directory (created if missing). Existing csd3d11.dll/csgi.dll are
    overwritten only when content differs, to keep incremental deploys quiet.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Src,
    [Parameter(Mandatory = $true)][string]$Dst
)

$ErrorActionPreference = 'Stop'

function Resolve-DxvkDll {
    param([string]$Base, [string]$Name)
    $nested = Join-Path $Base (Join-Path ($Name -replace '\.dll$', '') $Name)
    if (Test-Path -LiteralPath $nested) { return (Resolve-Path -LiteralPath $nested).Path }
    $flat = Join-Path $Base $Name
    if (Test-Path -LiteralPath $flat) { return (Resolve-Path -LiteralPath $flat).Path }
    throw "Could not find $Name under '$Base' (looked for '$nested' and '$flat')"
}

# Find a unique byte subsequence; returns the single start index or throws.
function Find-UniqueIndex {
    param([byte[]]$Haystack, [byte[]]$Needle)
    $indices = @()
    $limit = $Haystack.Length - $Needle.Length
    for ($i = 0; $i -le $limit; $i++) {
        $match = $true
        for ($j = 0; $j -lt $Needle.Length; $j++) {
            if ($Haystack[$i + $j] -ne $Needle[$j]) { $match = $false; break }
        }
        if ($match) { $indices += $i }
    }
    if ($indices.Count -ne 1) {
        throw "Expected exactly 1 occurrence of the import name, found $($indices.Count)."
    }
    return $indices[0]
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

$d3d11Src = Resolve-DxvkDll -Base $Src -Name 'd3d11.dll'
$dxgiSrc = Resolve-DxvkDll -Base $Src -Name 'dxgi.dll'

if (-not (Test-Path -LiteralPath $Dst)) {
    New-Item -ItemType Directory -Path $Dst -Force | Out-Null
}
$Dst = (Resolve-Path -LiteralPath $Dst).Path

Write-Host "Staging DXVK DLLs:"
Write-Host "  d3d11 src: $d3d11Src"
Write-Host "  dxgi  src: $dxgiSrc"
Write-Host "  dst:       $Dst"

# csgi.dll is DXVK's dxgi.dll verbatim.
$dxgiBytes = [System.IO.File]::ReadAllBytes($dxgiSrc)
Write-IfDifferent -Path (Join-Path $Dst 'csgi.dll') -Bytes $dxgiBytes

# csd3d11.dll is DXVK's d3d11.dll with its single "dxgi.dll" import name repointed.
$d3d11Bytes = [System.IO.File]::ReadAllBytes($d3d11Src)
$enc = [System.Text.Encoding]::ASCII
# Match the null-terminated import name so we can never clip a longer string.
$needle = $enc.GetBytes('dxgi.dll') + [byte]0
$replace = $enc.GetBytes('csgi.dll') + [byte]0
$idx = Find-UniqueIndex -Haystack $d3d11Bytes -Needle $needle
for ($j = 0; $j -lt $replace.Length; $j++) { $d3d11Bytes[$idx + $j] = $replace[$j] }
Write-Host ("  patched import 'dxgi.dll' -> 'csgi.dll' at offset 0x{0:X}" -f $idx)
Write-IfDifferent -Path (Join-Path $Dst 'csd3d11.dll') -Bytes $d3d11Bytes

Write-Host "Done."
