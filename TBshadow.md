# TB Rectangular Shadow Artifact (VR) - Findings and Current Debug State

## Summary of the issue
- In VR with Terrain Blending (TB) enabled, a rectangular, HMD‑moving dark “shadow” appears on the ground.
- Disabling TB removes the artifact.
- The artifact is **not** Screen Space Shadows (SSS) or Terrain Shadows; toggling those had no effect.

## Confirmed via RenderDoc
- The rectangle is visible in `kSHADOW_MASK` and is written during **Utility:Pixel** shadowmask passes (RenderShadowmask / Spot / PB / DPB).
- The shadowmask pass samples:
  - Slot 2 = **TexDepthUtilitySampler**
  - Slot 4 = **TexShadowMapSamplerComp** (kSHADOWMAPS)
  - Slot 14 (Lighting) = **TexShadowMaskSampler** (kSHADOW_MASK)
- Pixel history in RenderDoc shows the offending region being written during the shadowmask Utility passes, not later composites.

## What definitely affects the artifact
- **Force White Shadow Mask (slot‑14 override in Lighting pass)**  
  → Removes the rectangular shadow reliably.  
  This proves the artifact is in the **shadowmask output** (kSHADOW_MASK) that Lighting samples, not later stages.

## What did NOT fix the artifact
- Overriding **slot‑2 depth source** for shadowmask passes:
  - Engine Prepass depth
  - Engine Main depth (depthSRVBackup)
  - Shadowmask depth copy (copied once per frame from depthSRVBackup)
  → **No change** to the rectangular artifact.
- Forcing **ShadowVisibility=1** in `RENDER_SHADOWMASK` path (Utility.hlsl)  
  → Inconsistent; does not remove the rectangle in the clean baseline.
- DPB‑specific depth swaps / overrides (prepass/main/copy)  
  → **No effect** on the rectangle.
- “Force White Shadowmap (slot‑4 override)”  
  → **No effect** on the rectangle when tested alone.
- Scoped TB depth only for the TB pass (removing global override)  
  → Removes rectangular artifact but **breaks terrain** (transparent mesh / incorrect shadows).  
  This is **not viable**.

## Current interpretation (based on tests)
- The rectangle comes **from shadowmask generation**, and persists regardless of the depth source used in that pass.
- Since slot‑14 (Lighting’s shadowmask) override removes it, the artifact is **inside kSHADOW_MASK** rather than later lighting/composite steps.
- Depth source (slot‑2) is not the root cause; the artifact likely comes from **shadowmap sampling (slot‑4) or shadowmask math** in the Utility shadowmask shader.

## Current debug controls in TB (Developer Mode)
**These are present in the tree now.**

- `Force White Shadow Mask (Debug)`  
  Overrides Lighting slot‑14 (kSHADOW_MASK) with a 1×1 white texture.  
  **Only test that reliably removes the rectangle.**

- `Shadowmask Slot2 Source (Debug)`  
  Overrides Utility shadowmask slot‑2 depth input:
  0 = Default (no override)  
  1 = Engine Prepass Depth  
  2 = Engine Main Depth  
  3 = Shadowmask Depth Copy (copied once per frame from depthSRVBackup)

- `Shadowmask Slot4: Force White Shadowmap (Debug)`  
  Overrides Utility shadowmask slot‑4 (kSHADOWMAPS) with a 1×1 depth=1 texture array.
  **So far: no effect on artifact.**

- Logging toggles:  
  - `Log Shadowmap Passes (Debug)`  
  - `Log Shadowmask Passes (Debug)`  
  - `Log Shadowmask Slot Overrides (Debug)`

## Logging highlights (what matters)
- Shadowmask pass logs confirm the rectangle is written during Utility shadowmask passes:
  - `RENDER_SHADOWMASK`, `RENDER_SHADOWMASKDPB`, etc.
  - Geometry names logged (e.g., `WRWallStr01Stockades01`, `TanningRack_1.001`, etc.) are **not** TB blend meshes.
- Slot‑2 override logs show the override is active and the depth copy is updated, but the artifact remains.
- Slot‑14 override logs show the white mask is applied and **the artifact disappears**.

## Current code changes (baseline for further work)
These are already in the working tree:

1. **Shadowmask depth copy**
   - `shadowmaskDepth` texture created in `TerrainBlending::SetupResources()`.
   - Optional per‑frame copy from `depthSRVBackup` (main depth backup).

2. **Slot‑2 shadowmask override**
   - `PSSetShaderResources` hook in `Globals.cpp` detects Utility shadowmask passes.
   - Overrides slot‑2 using the selected source (prepass/main/copy).

3. **Slot‑14 shadowmask override**
   - `ForceWhiteShadowMask` overrides Lighting slot‑14 to a 1×1 white shadowmask texture.

4. **Slot‑4 shadowmap override**
   - `ForceWhiteShadowmapSlot4` overrides Utility shadowmask slot‑4 to a 1×1 white shadowmap array.

5. **Extensive logging** with rate‑limiter:
   - Shadowmap pass logs (tech/flags/pass/hint/lights/shadowLights/prop/propFlags/geom/name).
   - Shadowmask pass logs (maskBits/pixelDesc/propFlags/geom/name).
   - Slot override logs for slot‑2/slot‑4/slot‑14.

## Repro steps (current baseline)
1. Enable TB in VR.
2. Enable Debug UI (Developer mode/log level debug).
3. Observe rectangular moving shadow on ground.
4. Toggle **Force White Shadow Mask** → rectangle disappears.
5. Toggle any slot‑2 source / slot‑4 white shadowmap → no change.

## Next likely investigation paths
1. **Shadowmask math** in Utility shader:
   - The artifact persists even with different depth sources; could be due to shadowmap sampling / math.
2. **Shadowmap content** itself:
   - Shadowmap generation pass includes many unrelated geometries; the artifact may come from a specific mesh or incorrect projection.
3. **Isolate which shadowmask variant (DPB vs non‑DPB)** actually produces the rectangle:
   - The logs show both; need to isolate by selectively disabling/overriding *only one* variant at a time.

## Where to look in code
- `src/Globals.cpp`  
  `ID3D11DeviceContext_PSSetShaderResources::thunk`  
  (shadowmask pass detection, slot‑2/slot‑4 override, Lighting slot‑14 override)

- `src/Features/TerrainBlending.cpp`  
  `SetupResources()` (shadowmaskDepth, shadowmaskWhite, shadowmapWhite creation)  
  `DrawSettings()` (debug toggles)  
  `LogShadowmapPass`, `LogShadowmaskPass`

- `src/Features/TerrainBlending.h`  
  Settings fields and debug toggles.

