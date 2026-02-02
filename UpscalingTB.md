# Upscaling + Terrain Blending + Depth Culling (VR) - Findings

This is a consolidated record of what was tried, what worked, and what failed,
so we do not repeat dead ends in a future chat.

## Problem statement
- In VR, depth culling breaks when upscaling is enabled because depth is not
  upscaled. Objects pop in/out (incorrectly culled).
- The same issue occurs when Terrain Blending (TB) is enabled. Even after the
  upscaling fix, meshes still pop with TB active.

## What DID work (upscaling without TB)
### Conservative depth output in the existing upscale pass
- Change: `DepthRefractionUpscalePS.hlsl` outputs conservative depth.
  - Compute min depth from a 2x2 neighborhood (optionally 3x3 for aggressive
    scales).
  - Preserve VR-safe UV mapping + jitter removal:
    `FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(...)`.
  - Preserve the stencil discard path.
- Why it works: writes conservative (nearer) depth into the upscaled DSV,
  so occlusion never under-estimates visibility.
- Result: depth culling works when upscaling is on if TB is disabled.

### Allow depth culling in VR when conservative depth is active
- Relaxed force-disable logic in:
  - `src/Features/VR.cpp` (depth culling lock-out)
  - `src/Features/Upscaling.cpp` (upscaling lock-out)
- Result: depth culling can be enabled when upscaling is on (non-TB path).

## What DID NOT work (TB path)
### 1) Forcing occlusion to use engine/main depth (t17 guard / SRV routing)
Goal: prevent TB blended depth from being used for occlusion by forcing `t17`
to engine depth (kMAIN or kMAIN_COPY).

Attempts:
- Guard in `State.cpp` and/or `TerrainBlending.cpp` to keep `t17` as engine
  depth.
- Re-route TB to a different SRV slot, leaving `t17` as engine depth.
- Bypass TB blended depth entirely.

Result:
- Depth culling improved, but the ground became transparent (terrain breaks).
- Same failure when giving TB its own slot and sampling it in TB shaders.

Conclusion:
- TB expects its blended depth bound at `t17`. Touching that binding breaks the
  terrain composite (transparent ground). Avoid modifying `t17` routing for TB.

### 2) Using TB blended depth SRV for upscaling / occlusion
Goal: feed occlusion with TB depth or an upscaled version of it.

Attempts:
- Route TB blended depth into the upscaling path.
- Use TB blended depth SRV as the occlusion input.
- Try R16 vs R32 TB blended depth variants.

Result: still popping, and several variants caused transparent ground or
stereo mismatch.

Conclusion: swapping occlusion to TB depth does not fix the issue, and often
breaks terrain.

## What DID work (TB path)
### Localized draw-call SRV overrides (OBB + Shadowmask only)
Goal: fix occlusion and shadowmask sampling without touching TB’s global `t17`
binding.

Implementation (current):
- Hook D3D11 Draw* calls and apply scoped overrides in `src/Globals.cpp`.
- **OBB occlusion only**: override `t17` for the `OBBOcclusionTesting` shader.
  - Current code uses TB blended **R32** if available, else `kMAIN_COPY`.
- **Shadowmask only**: override `t2` (TexDepthUtilitySampler) in Utility
  shadowmask passes.
  - Current code prefers TB blended **R16**, then TB R32, then `kMAIN_COPY`.

Result:
- **Mesh popping is gone** with TB + upscaling + depth culling.
- **Stereo mismatch** and rectangular artifacts were eliminated when
  shadowmask uses **TB R16** (not R32, not `kMAIN_COPY`).

## Shadowmask artifacts (ground shadows) + OFFSET_DEPTH tuning
There are still rare, rectangular ground-shadow artifacts in some cases.
We tested different depth offsets in `package/Shaders/Utility.hlsl`:
- `OFFSET_DEPTH = 10.0`: removed darkening but caused large rectangular artifacts.
- `OFFSET_DEPTH = 2.5`: partial improvement; still artifacts.
- `OFFSET_DEPTH = 1.25`: best compromise for most cases.
- `OFFSET_DEPTH = 1.0 / 0.75`: did not meaningfully improve artifacts.
- `OFFSET_DEPTH = 0.25`: reduced some ground shadows but broke TB when close.

**Current**: hardcoded depth-based lerp instead of a fixed offset.
```
bias = lerp(1.25, 0.1, saturate((abs(w) - 256) / (2048 - 256)));
```
This is no longer tied to TerrainCullDistance (previous attempt was).

Artifacts appear most under **DLSS + TB + depth culling**.

## Upscaler matrix (latest observations)
- **DLAA + TB + depth culling** = OK (no ground shadows).
- **DLSS + depth culling (no TB)** = OK.
- **DLSS + TB + depth culling** = ground-shadow artifacts.

Attempted DLSS-specific shadowmask routing:
- Forcing shadowmask `t2` to `kMAIN_COPY` under DLSS reintroduced stereo mismatch
  (similar to TB R32). Reverted.

### 3) Depth-only upscale pass before HZB (new pass)
Goal: upscale depth into `kMAIN_COPY` before HZB/occlusion downsample.

Implementation:
- New `DepthOnlyUpscalePS` shader.
  - Conservative min depth (2x2 / optional 3x3).
  - VR jitter removal and stencil discard.
  - Writes depth only to `kMAIN_COPY` DSV.
- Added `UpscaleDepthForOcclusion()` in `src/Features/Upscaling.cpp`.
- Hooked into `src/FrameAnnotations.cpp` before HZB compute passes.
- DSV clear to 1.0/0xFF before the pass so discarded pixels remain far.

Result:
- Still mesh popping, plus stereo misalignment artifacts.
- Added a full-screen pass; performance cost felt non-trivial.

Conclusion: depth-only pre-pass is not sufficient and introduces stereo issues.

### 4) Global SRV override hook (PS/CS SetShaderResources)
Goal: force occlusion/HZB shaders to sample upscaled depth (`kMAIN_COPY`)
without touching TB shaders.

Implementation:
- Hooked `ID3D11DeviceContext::PSSetShaderResources` and
  `CSSetShaderResources` in `src/Globals.cpp`.
- Tagging and logging depth SRVs:
  `kMAIN`, `kMAIN_COPY`, `kPOST_ZPREPASS_COPY`, `TB.blendedDepth(R16/R32)`,
  `TB.depthSRVBackup`, `TB.prepassSRVBackup`, `TB.terrainDepth`.
- Override replaced any "non-upscaled depth" with `kMAIN_COPY` when
  VR + upscaling + depth culling + TB are active.

Result:
- Overrides fired too broadly (CS slots 0/1/3, PS slot 17), not just occlusion.
- Stereo mismatch and still popping.

Conclusion: global SRV override is too invasive. Any override must be localized
to the occlusion binding site only (if at all).

## Key observations from logs
- When TB is enabled, t17 in PS is typically TB.blendedDepth (R16).
- The occlusion path (OBB) samples t17 in PS.
- HZB compute uses kMAIN / kPOST_ZPREPASS_COPY in CS slots 0/1/3.
- kMAIN_COPY is the upscaled depth (VR path).
- Forcing t17 away from TB depth **globally** breaks terrain (transparent ground).
- Shadowmask pass uses `t2` (TexDepthUtilitySampler). TB R16 is the only
  source that avoided stereo mismatch so far.

Typical log patterns:
- `stage=PS slot=17 tag=TB.blendedDepth(R16)`
- `stage=PS [OBB] t17=TB.blendedDepth(R16)`
- `stage=CS slot=0/1/3 tag=kMAIN.depthSRV` or `kPOST_ZPREPASS_COPY`
- Overrides show `TB.blendedDepth -> kMAIN_COPY` and `kMAIN -> kMAIN_COPY`
  in CS, which caused stereo artifacts.

## Constraints (do not repeat these)
1. Do not change TB's `t17` binding (causes transparent ground).
2. Avoid global SRV overrides (too broad, stereo mismatch).
3. Depth-only pre-pass before HZB did not fix popping and caused stereo issues.
4. Routing TB depth to a different SRV slot and using it in TB shaders still
   broke terrain.
5. For shadowmask, **TB R16** works; **TB R32** or **kMAIN_COPY** reintroduces
   stereo mismatch in DLSS.

## Current working state (non-TB)
- Conservative depth in `DepthRefractionUpscalePS` fixes depth culling with
  upscaling when TB is OFF.
- Depth culling can be enabled in VR once that conservative path is active.

## Current working state (TB path)
- Scoped draw-call overrides in `src/Globals.cpp`:
  - OBB occlusion overrides `t17`.
  - Shadowmask overrides `t2` to TB R16.
- Depth culling works with TB + upscaling, but **DLSS + TB** still shows
  occasional rectangular ground-shadow artifacts.
- TB depth bias is now depth-based (lerp 1.25→0.1 over 256–2048).
- Default TB culling distance is now **2048** (Settings default).

## Files touched during experiments (reference only)
- `src/Features/Upscaling.cpp` / `src/Features/Upscaling.h`
  - `UpscaleDepthForOcclusion()`, `DepthOnlyUpscalePS` (failed approach).
- `features/Upscaling/Shaders/Upscaling/DepthOnlyUpscalePS.hlsl`
  - Depth-only conservative upscale shader (failed approach).
- `src/FrameAnnotations.cpp`
  - Hook to run depth-only upscale before HZB (failed approach).
- `src/Globals.cpp`
  - Global SRV logging/override (failed approach).
- `src/State.cpp`, `src/Features/TerrainBlending.cpp`
  - SRV guard experiments (transparent ground).
- `package/Shaders/Utility.hlsl`
  - OFFSET_DEPTH now uses hardcoded depth-based lerp (256–2048).
- `src/Features/TerrainBlending.h/.cpp`
  - Added DepthBiasParams CB + binding (currently unused after hardcoding).
- `src/State.h`
  - Default refractionScale (heatwarp) set to 0.5 (unrelated to TB, but noted).

## Open hypotheses (still unresolved)
- TB blended depth is likely not upscaled (or not aligned per-eye), so
  occlusion sees a depth buffer that does not match the actual view.
- Fix must not alter TB's `t17` binding, but may need a way to:
  - Provide an occlusion-only SRV that is upscaled and stereo-correct without
    changing TB's SRV layout, or
  - Produce a TB-compatible upscaled depth for occlusion only (localized).
 - Ground-shadow artifacts under DLSS may stem from depth bias or shadowmask
   depth mismatch even with TB R16. OFFSET_DEPTH may not be the right lever.

## Notes
- `TBshadow.md` exists but focuses on a separate shadowmask artifact, not this
  depth culling/upscaling issue.
