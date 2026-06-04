<#
.SYNOPSIS
    Check whether an HLSL refactor preserves compiled bytecode.

.DESCRIPTION
    Compiles a shader from a base git revision and from the current working tree
    across a set of preprocessor permutations, then compares the resulting DXBC.
    The base ref's entire include tree (-IncludeDir) is materialized with
    git archive, so the base compiles against base-ref headers and the working
    tree against working-tree headers.

    Tier 1: identical SHA-256 of the compiled .cso means the GPU program is
    byte-for-byte identical. fxc emits no timestamps without /Zi, so the result
    is stable for this purpose.

    Tier 2: on mismatch, the script dumps /Fc assembly for both revisions and
    lists differing lines with base/work markers.

    The default permutation sweep (VR x HDR_OUTPUT) is useful coverage, not the
    full shader-validation matrix. Pass -Permutations for feature-specific define
    combinations.

.PARAMETER Shader
    Path to the .hlsl file, repo-relative or absolute.

.PARAMETER BaseRef
    Git ref to treat as "before". Default: merge-base of HEAD and the current
    branch upstream. If no upstream is configured, falls back to
    origin/cs-1.6-PL-VR, then origin/dev, then HEAD.

.PARAMETER IncludeDir
    Shader include root passed to fxc /I. Default: package/Shaders.

.PARAMETER Permutations
    Optional explicit permutation list; each entry is a space-separated define
    set, for example -Permutations "PSHADER","PSHADER VR".

.PARAMETER Entry
    Shader entry point. Default: main.

.PARAMETER Profile
    fxc target profile. Default: auto (cs_5_0 for *CS.hlsl, otherwise ps_5_0).

.PARAMETER Fxc
    Optional explicit path to fxc.exe.

.EXAMPLE
    pwsh tools/verify-shader-refactor.ps1 package/Shaders/ISTemporalAA.hlsl

.EXAMPLE
    pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl -BaseRef HEAD~1
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Shader,
    [string]$BaseRef,
    [string]$IncludeDir = "package/Shaders",
    [string[]]$Permutations,
    [string]$Entry = "main",
    [string]$Profile,
    [string]$Fxc
)

# Native git calls can write warnings to stderr; flow control checks
# $LASTEXITCODE explicitly.
$ErrorActionPreference = "Continue"

function Resolve-Fxc {
    if ($Fxc -and (Test-Path $Fxc)) {
        return $Fxc
    }

    $cmd = Get-Command fxc.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "${env:ProgramFiles}\Windows Kits\10\bin")
    $found = foreach ($r in $roots) {
        if (Test-Path $r) {
            Get-ChildItem -Path $r -Recurse -Filter fxc.exe -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "x64" }
        }
    }

    $pick = $found | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $pick) {
        throw "fxc.exe not found. Install the Windows 10/11 SDK or pass -Fxc."
    }

    return $pick.FullName
}

function Convert-ToRepoRelativePath([string]$Path, [string]$RepoRoot, [string]$Label) {
    $full = (Resolve-Path $Path).Path
    $rootFull = (Resolve-Path $RepoRoot).Path

    if (-not $full.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label '$Path' resolves outside the repo root '$rootFull'."
    }

    return @{
        Full = $full
        Relative = $full.Substring($rootFull.Length).TrimStart('\', '/').Replace('\', '/')
    }
}

function Resolve-DefaultBaseRef {
    $upstream = (git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null)
    if ($LASTEXITCODE -eq 0 -and $upstream) {
        $base = (git merge-base HEAD $upstream 2>$null)
        if ($LASTEXITCODE -eq 0 -and $base) {
            return @{ Ref = $base; Source = $upstream }
        }
    }

    foreach ($candidate in @("origin/cs-1.6-PL-VR", "origin/dev")) {
        git rev-parse --verify $candidate 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $base = (git merge-base HEAD $candidate 2>$null)
            if ($LASTEXITCODE -eq 0 -and $base) {
                return @{ Ref = $base; Source = $candidate }
            }
        }
    }

    return @{ Ref = "HEAD"; Source = "HEAD" }
}

$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) {
    throw "Not inside a git repository."
}

$work = $null
$pushedLocation = $false
$exitCode = 1

try {
    Push-Location $repoRoot
    $pushedLocation = $true

    $fxcPath = Resolve-Fxc

    $shaderPath = Convert-ToRepoRelativePath $Shader $repoRoot "Shader"
    $includePath = Convert-ToRepoRelativePath $IncludeDir $repoRoot "IncludeDir"
    $relPath = $shaderPath.Relative
    $includeRel = $includePath.Relative.TrimEnd('/')

    if (-not $BaseRef) {
        $defaultBase = Resolve-DefaultBaseRef
        $BaseRef = $defaultBase.Ref
        $baseSource = $defaultBase.Source
    } else {
        $baseSource = $BaseRef
    }

    if (-not $Profile) {
        $Profile = if ($relPath -match 'CS\.hlsl$') { "cs_5_0" } else { "ps_5_0" }
    }

    $stageDefine = switch -Wildcard ($Profile) {
        "cs_*" { "CSHADER" }
        "vs_*" { "VSHADER" }
        default { "PSHADER" }
    }

    if (-not $Permutations -or $Permutations.Count -eq 0) {
        $Permutations = @(
            "$stageDefine",
            "$stageDefine VR",
            "$stageDefine HDR_OUTPUT",
            "$stageDefine VR HDR_OUTPUT"
        )
    }

    $work = Join-Path ([IO.Path]::GetTempPath()) ("shaderverify_" + [Guid]::NewGuid().ToString("N"))
    $baseRoot = Join-Path $work "base"
    New-Item -ItemType Directory -Force $baseRoot | Out-Null

    $tar = Join-Path $work "base.tar"
    git archive --format=tar -o $tar $BaseRef -- $includeRel $relPath 2>$null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tar)) {
        throw "git archive failed for '$BaseRef' (paths: $includeRel, $relPath)."
    }

    tar -xf $tar -C $baseRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to extract base archive."
    }

    $baseFile = Join-Path $baseRoot $relPath
    $baseInclude = Join-Path $baseRoot $includeRel
    if (-not (Test-Path $baseFile)) {
        throw "'$relPath' not found at '$BaseRef'."
    }

    function Compile-Shader([string]$Source, [string]$Include, [string]$Defines, [string]$OutFile, [switch]$Asm) {
        $defArgs = @()
        foreach ($d in ($Defines -split '\s+' | Where-Object { $_ })) {
            $defArgs += "/D"
            $defArgs += $(if ($d -like "*=*") { $d } else { "$d=1" })
        }

        $fmt = if ($Asm) { "/Fc" } else { "/Fo" }
        $out = & $fxcPath /nologo /T $Profile /E $Entry @defArgs /I $Include $Source $fmt $OutFile 2>&1
        return @{ Code = $LASTEXITCODE; Out = $out }
    }

    Write-Host "Shader   : $relPath"
    Write-Host "Base ref : $BaseRef  (from $baseSource, full include tree materialized)"
    Write-Host "Profile  : $Profile  (entry $Entry)"
    Write-Host "Include  : $includeRel"
    Write-Host ("-" * 60)

    $allIdentical = $true
    $anyError = $false

    foreach ($perm in $Permutations) {
        $baseCso = Join-Path $work "base.cso"
        $workCso = Join-Path $work "work.cso"
        $rb = Compile-Shader $baseFile $baseInclude $perm $baseCso
        $rw = Compile-Shader $shaderPath.Full $includePath.Full $perm $workCso

        if ($rb.Code -ne 0 -or $rw.Code -ne 0) {
            $anyError = $true
            $which = if ($rb.Code -ne 0) { "BASE" } else { "WORK" }
            Write-Host "[$perm] COMPILE-ERROR ($which)" -ForegroundColor Red
            ($(if ($rb.Code -ne 0) { $rb.Out } else { $rw.Out }) |
                Where-Object { $_ -match "error|warning" } |
                Select-Object -First 6) |
                ForEach-Object { Write-Host "    $_" }
            continue
        }

        $hb = (Get-FileHash $baseCso -Algorithm SHA256).Hash
        $hw = (Get-FileHash $workCso -Algorithm SHA256).Hash
        if ($hb -eq $hw) {
            Write-Host "[$perm] IDENTICAL" -ForegroundColor Green
        } else {
            $allIdentical = $false
            Write-Host "[$perm] DIFFERS  base=$($hb.Substring(0, 12)) work=$($hw.Substring(0, 12))" -ForegroundColor Yellow

            $baseAsm = Join-Path $work "base.asm"
            $workAsm = Join-Path $work "work.asm"
            Compile-Shader $baseFile $baseInclude $perm $baseAsm -Asm | Out-Null
            Compile-Shader $shaderPath.Full $includePath.Full $perm $workAsm -Asm | Out-Null
            $diff = Compare-Object (Get-Content $baseAsm) (Get-Content $workAsm)
            if ($diff) {
                $diff | Select-Object -First 40 | ForEach-Object {
                    $mark = if ($_.SideIndicator -eq "=>") { "work" } else { "base" }
                    Write-Host ("    [{0}] {1}" -f $mark, $_.InputObject)
                }
                if (@($diff).Count -gt 40) {
                    Write-Host ("    ... (+{0} more asm lines)" -f (@($diff).Count - 40))
                }
            }
        }
    }

    Write-Host ("-" * 60)
    if ($anyError) {
        Write-Host "RESULT: compile error" -ForegroundColor Red
        $exitCode = 1
    } elseif ($allIdentical) {
        Write-Host "RESULT: behavior-preserving (all permutations identical)" -ForegroundColor Green
        $exitCode = 0
    } else {
        Write-Host "RESULT: bytecode differs - inspect asm diff above" -ForegroundColor Yellow
        $exitCode = 2
    }
} catch {
    Write-Error $_
    $exitCode = 1
} finally {
    if ($work -and (Test-Path $work)) {
        Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    }
    if ($pushedLocation) {
        Pop-Location
    }
}

exit $exitCode
