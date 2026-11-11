param(
    [Parameter(Mandatory = $true)][string]$Path,
    [switch]$AllowMissing,
    [string]$SevenZip = '7z'
)

$ErrorActionPreference = 'Stop'
$archiveExtensions = @('.7z', '.zip', '.rar', '.tar', '.gz', '.bz2', '.xz', '.zst', '.cab', '.iso', '.wim', '.tgz', '.tbz2')
$maximumArchiveDepth = 8

function Assert-AllowedName([string]$Name, [string]$Container) {
    $leaf = [IO.Path]::GetFileName($Name.Replace('\', '/')).TrimEnd(' ', '.')
    if ($leaf -imatch '^nvngx_dlssnr.*\.dll$') {
        throw "NR runtime redistribution is forbidden: $Container -> $Name"
    }
}

function Test-ArchiveName([string]$Name) {
    return [IO.Path]::GetExtension($Name).ToLowerInvariant() -in $archiveExtensions
}

function Assert-Archive([string]$Archive, [int]$Depth) {
    if ($Depth -gt $maximumArchiveDepth) { throw "Archive nesting exceeds the inspection limit: $Archive" }
    $listing = & $SevenZip l -slt -ba -sccUTF-8 -pNR-runtime-guard -- $Archive 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect archive: $Archive`n$($listing -join [Environment]::NewLine)" }
    if ($listing -match '^Encrypted = \+$') { throw "Cannot verify encrypted archive: $Archive" }
    $entries = @($listing | ForEach-Object {
        if ($_ -match '^Path = (.*)$') { $Matches[1] }
    })
    foreach ($entry in $entries) {
        Assert-AllowedName $entry $Archive
        if (-not (Test-ArchiveName $entry)) { continue }
        $destination = Join-Path $inspectionRoot ([guid]::NewGuid().ToString())
        New-Item -ItemType Directory -Path $destination | Out-Null
        & $SevenZip e -y -bd -spd -pNR-runtime-guard "-o$destination" -- $Archive $entry | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect nested archive: $Archive -> $entry" }
        $nested = @(Get-ChildItem -LiteralPath $destination -File -Recurse -Force)
        if ($nested.Count -ne 1 -or ($nested[0].Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Unexpected nested archive payload: $Archive -> $entry"
        }
        Assert-Archive $nested[0].FullName ($Depth + 1)
    }
}

if (-not (Test-Path -LiteralPath $Path)) {
    if ($AllowMissing) { return }
    throw "Artifact path does not exist: $Path"
}
$inspectionRoot = Join-Path ([IO.Path]::GetTempPath()) ('nr-runtime-guard-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $inspectionRoot | Out-Null
try {
    $root = Get-Item -LiteralPath $Path -Force
    $items = @($root)
    if ($root.PSIsContainer) { $items += @(Get-ChildItem -LiteralPath $Path -Recurse -Force) }
    foreach ($item in $items) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Cannot verify linked artifact: $($item.FullName)" }
        if ($item.PSIsContainer) { continue }
        Assert-AllowedName $item.Name $Path
        if (Test-ArchiveName $item.Name) { Assert-Archive $item.FullName 0 }
    }
    Write-Host "Verified artifact payload contains no NR runtime: $Path"
} finally {
    Remove-Item -LiteralPath $inspectionRoot -Recurse -Force
}
