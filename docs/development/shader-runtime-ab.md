# Shader Runtime A/B Check

This is a same-frame GPU equivalence check for shader refactors whose compiled
DXBC legitimately differs. For refactors that remain bytecode-identical, use
`tools/verify-shader-refactor.ps1` first; that is a stronger offline proof and
does not need the game.

The worked example is `ISTemporalAA.hlsl`, but the method generalizes to other
pixel shaders: capture one real frame in RenderDoc, replace just that shader with
precompiled candidate DXBC, and diff the output render target against the live
shader and a baseline DXBC built from the refactor's parent.

## Why Same-Frame Swap

TAA is temporal, so two separate launches rarely align frame-for-frame. A
two-launch screenshot comparison includes menu animation, HMD pose, timing, RNG,
and temporal warmup noise. A RenderDoc capture freezes the TAA inputs, including
history, velocity, depth, mask, alpha, and constant buffers. Replacing only the
pixel shader makes the output diff mostly reflect shader behavior.

This is the runtime companion to the offline bytecode verifier. It is useful
when the compiler legitimately emits different instructions for equivalent
algebra or reordered code.

## Prerequisites

- RenderDoc with a loaded capture and access to its embedded Python context.
- `tools/taa-renderdoc-ab.py`, executed via the RenderDoc MCP `Eval` tool.
- `fxc.exe` from the Windows SDK.
- A capture where the target shader pass is actually present.

On this branch, Community Shaders disables CS Upscaling and Frame Generation
while RenderDoc capture is enabled or injected, to avoid DLSS/FSR startup
crashes. Do not rely on saved DLSS/FSR settings to expose an upscaling path in a
RenderDoc run. For `ISTemporalAA.hlsl` refactors, capture a TAA/None path where
the TAA pass is present. If you need to validate a vendor-upscaling-only path,
use a dedicated diagnostic branch or another capture strategy instead of
removing the safety guard from normal builds.

Keep HDR and frame-generation off for this workflow unless the exact permutation
under test requires them; interop/present paths can hide the D3D11 draw stream
that the harness needs.

## Compile Baseline and Candidate

Compile both shaders to DXBC with the same defines as the captured build. Use
`VR=1` for SkyrimVR captures and `HDR_OUTPUT=1` only when the captured build used
HDR output.

Use the refactor's true base, not blindly `origin/dev`. On this branch the usual
upstream baseline is `origin/cs-1.6-PL-VR`, but stacked work should use the
immediate parent commit of the refactor so the measured floor is compiler noise,
not unrelated behavior changes.

```powershell
$fxc = (Get-Command fxc.exe).Source
$inc = "package/Shaders"
$sh = "package/Shaders/ISTemporalAA.hlsl"
$defs = @("/D", "PSHADER=1") # add "/D", "VR=1" and/or "/D", "HDR_OUTPUT=1" as needed
$base = "origin/cs-1.6-PL-VR" # or the refactor parent commit

[IO.File]::WriteAllLines("$env:TEMP\taa_A.hlsl", (git show "$base`:$sh"))
& $fxc /nologo /T ps_5_0 /E main @defs /I $inc "$env:TEMP\taa_A.hlsl" /Fo "$env:TEMP\taa_A.dxbc"

& $fxc /nologo /T ps_5_0 /E main @defs /I $inc $sh /Fo "$env:TEMP\taa_B.dxbc"
```

Confirm the current branch before compiling the candidate. Building the deployed
shader as both baseline and candidate creates a meaningless `EQUIVALENT`.

## Capture Guidance

The main menu can run TAA, but it is close to static and can miss bugs that only
flow through motion-dependent branches. Use a menu frame only for changes known
to be motion-independent.

For blend, reject, history reprojection, or clip-to-AABB changes, capture an
in-game frame with real motion. Prefer a small interior scene for VR; heavy
exteriors can produce multi-GB captures that are hard to load or replay. For
large captures, scan from the end of the frame because post-process passes are
near the end.

Verify new captures by timestamp and size on disk when possible. Capture APIs
can return stale paths after repeated runs.

## Run The Harness

Load the script and run the scan in the same RenderDoc `Eval` session if your
MCP does not preserve Python globals across calls.

```python
exec(open(r"<repo>/tools/taa-renderdoc-ab.py").read())  # replace <repo>
taa_candidates(reverse=True, stop_after=1)
```

Pick the returned `eventId`, then run:

```python
ab(<eventId>,
   candidate_dxbc=r"%TEMP%/taa_B.dxbc",
   baseline_dxbc=r"%TEMP%/taa_A.dxbc")
```

Always pass `baseline_dxbc`. The live shader and offline `fxc` output can differ
by a few least-significant bits even when the source is equivalent. The harness
uses `baseline_vs_live.mean_abs` as the noise floor and reports:

- `EQUIVALENT` when the candidate is within the baseline-relative threshold.
- `DIFFERS` when the candidate exceeds that threshold.
- `UNVERIFIED-BASELINE` when the baseline floor is too large, usually because
  the defines, HDR/VR permutation, or baseline ref are wrong.
- `NOT-COMPARABLE` when the output format is unsupported or data is missing.

## Generalizing

`ab()`, `grab_rt()`, `replace_ps_with_dxbc()`, `restore()`, and `_diff()` are
shader-agnostic. `taa_candidates()` is only a preset for the TAA SRV
fingerprint. For another pass, call `find_candidates()` with distinctive SRV
names:

```python
cands = find_candidates({"mycolortex", "mydepthtex", "myhistorytex"}, reverse=True, stop_after=1)
ab(cands[0]["eventId"], candidate_dxbc=..., baseline_dxbc=...)
```

Change only the pass fingerprint, the compile defines, and the captured scene
state. One frame is regression evidence for that frame, not a formal proof; pair
it with the offline bytecode verifier wherever possible.
