# LLF Particle and Placed Light Optimization Plan

Date: 2026-03-10  
Branch baseline: `cs-1.4.11-PL-VR`

## 1. Baseline and Comparison Scope

This document is written against `cs-1.4.11-PL-VR`.

I compared LLF/PL-relevant files against `cs-1.4.11-PL`:

- `src/Features/LightLimitFix.cpp`
- `src/Features/LightLimitFix.h`
- `src/Features/LightLimitFix/ParticleLights.cpp`
- `src/Features/LightLimitFix/ParticleLights.h`
- `package/Shaders/Particle.hlsl`
- `package/Shaders/Lighting.hlsl`
- `features/Light Limit Fix/Shaders/LightLimitFix/ClusterCullingCS.hlsl`
- `src/Hooks.cpp`

Result: no meaningful functional delta for the particle/placed-light runtime pipeline.  
Main changes are LLF tooltip text and unrelated hook cleanup in `src/Hooks.cpp`.

## 2. Current Pipeline on `cs-1.4.11-PL-VR`

### 2.1 Particle lights (PL)

1. PL detection is invoked from multiple render-pass hooks via `CheckParticleLights(...)` in `src/Hooks.cpp`.
2. PL config source is `Data\\ParticleLights` INI data loaded at startup by `ParticleLights::GetConfigs()` in `src/Features/LightLimitFix/ParticleLights.cpp`.
3. LLF skips PL generation when the effect shader already has native `lightData` (`GetParticleLightConfigs(...)`).
4. Per-geometry lookup result and billboard base color are cached in `particleLightsReferences`.
5. PL candidates are queued in `queuedParticleLights`; they are consumed next frame after swap in `Reset()`.
6. PL build path already includes:
   - clustering control (`ParticleClusterThreshold`)
   - per-emitter sampling cap (`MaxParticlesPerEmitter`)
   - hard distance cut (`MaxParticleDistance`)
7. Generated PL lights are marked `Simple`, reducing expensive shader branches in lighting.

### 2.2 Placed/scene lights

1. LLF ingests scene lights from `activeLights` and `activeShadowLights` in `UpdateLights()`.
2. ISL adjusts falloff/radius in `InverseSquareLighting::ProcessLight(...)`.
3. LLF pushes valid lights into `lightsData` with no runtime dedupe by source JSON/NIF/form.
4. Cluster culling cost scales with total `LightCount` (`ClusterCullingCS` loops all lights per cluster thread).

## 3. What Is Already Implemented and Useful

- `UseLegacyParticleLighting` toggle for PL shading behavior.
- PL clustering and emitter cap controls.
- PL distance cutoff control.
- PL detection toggle for gameplay luminance.
- PL `Simple` flag to avoid some heavy per-light shader work.
- Skip path when shader already provides native effect `lightData`.

## 4. Main Remaining Cost/Risk Areas

1. Repeated same-frame PL detection/queueing from multiple pass hooks.
2. PL discovery still pays string/path/config lookup work on detection path.
3. Duplicated placed lights (from stacked LP/CS-light JSON content) are additive in brightness and cost.
4. When light pressure is high, `lightsData` truncation by insertion order is not quality-optimal.
5. Cluster-culling compute cost still scales heavily with `LightCount`.

## 5. Ranked Optimization Backlog (for `cs-1.4.11-PL-VR`)

Scales:

- Performance Win: 1 (small) to 5 (large)
- Implementation Risk: 1 (low) to 5 (high)
- Visual Risk: 1 (negligible) to 5 (high artifact/over-culling risk)

| Rank | Optimization | Perf Win | Impl Risk | Visual Risk | Quality Outcome |
|---|---|---:|---:|---:|---|
| 1 | Offline "last JSON wins" conflict resolution for LP duplicates | 5 | 1 | 2 | Usually better (less over-lighting) |
| 2 | Once-per-frame dedupe stamp in `CheckParticleLights()` | 4 | 2 | 2 | Neutral to better if key is robust |
| 3 | Fast material-aware PL lookup cache (reduce repeated path/string work) | 3 | 2 | 1 | Neutral |
| 4 | Adaptive PL budget by distance/influence (dynamic emitter cap) | 4 | 2 | 2 | Slight detail loss far away only |
| 5 | Pre-upload light prioritization before `MAX_LIGHTS` clamp | 3 | 3 | 2 | Better retention of important lights |
| 6 | Runtime near-duplicate scene-light merge mode (optional) | 3 | 4 | 4 | Can help badly stacked lists; risky |
| 7 | Cluster-culling CS optimization pass | 3 | 4 | 1 | Neutral if output-equivalent |
| 8 | Frame-local memoization mode (safety-first variant) | 1 | 2 | 1 | Neutral; mainly safety, not speed |

## 6. Details by Optimization

### 6.1 Offline LP conflict resolution (highest ROI)

Why:

- Duplicate JSON-placed lights are pure additive overhead and common over-lighting source.
- Runtime LLF cannot reliably know "which JSON should win" after lights are already spawned.

Implementation direction:

- Run a preprocessing step outside LLF that resolves conflicts per canonical NIF and exports one winning entry.
- Treat this as modlist build-time policy, not runtime heuristic.

Tradeoff:

- Requires user tooling/workflow support, but runtime cost is effectively zero.

### 6.2 Same-frame PL dedupe in `CheckParticleLights()`

Why:

- `CheckParticleLights()` is called from multiple pass paths.
- Same geometry can be queued multiple times in one frame.

Implementation direction:

- Add per-frame set keyed by `(geometry, shaderProperty/material, technique, frameIndex)`.
- Clear automatically each frame.

Risk control:

- Do not dedupe by geometry pointer alone.
- If collisions happen, prefer strongest contribution, not first-hit.

### 6.3 Material-aware fast lookup cache

Why:

- First-hit PL detection still does texture stem extraction, lowercase, config map lookup.

Implementation direction:

- Add cache keyed by material texture identity (source + gradient + billboard mode).
- Store resolved config/gradient and precomputed metadata.
- Keep current geometry cache for baseColor reuse.

### 6.4 Adaptive PL emitter budget

Why:

- Fixed `MaxParticlesPerEmitter` over-spends for distant effects.

Implementation direction:

- Scale per-emitter cap with camera distance and/or projected size.
- Keep near-camera cap high, far cap low.

Risk control:

- Clamp to minimum non-zero cap.
- Keep user override ceiling.

### 6.5 Pre-upload prioritization before hard cap

Why:

- Current hard cap keeps earliest inserted lights, not highest contribution lights.

Implementation direction:

- Score by contribution estimate (distance, fade, luminance, flags).
- Preserve strict/shadow-critical lights first.

### 6.6 Optional runtime near-duplicate merge

Why:

- Useful only when users cannot curate modlist conflicts offline.

Implementation direction:

- Optional mode (`Off` default).
- Require very strong spatial/color/radius similarity.
- Never merge shadow lights.
- Be conservative with portal-strict/global-critical lights.

Risk:

- False merges can remove intentional layered lighting.

### 6.7 Cluster-culling compute optimization

Why:

- `ClusterCullingCS` loops all lights for every cluster thread.

Implementation direction:

- Optimize memory and loop structure while preserving identical results.
- Measure only in high-light-count scenes; this is medium-high engineering effort.

### 6.8 Frame-local memoization mode

Why:

- Safety against stale pointer-cache edge cases.

Reality:

- Usually not faster than current persistent geometry cache.
- Good as optional correctness-oriented mode, not primary performance path.

## 7. Practical Tuning Profiles for `cs-1.4.11-PL-VR` (No Code Changes)

### Balanced

- `EnableParticleLightsOptimization = true`
- `ParticleClusterThreshold = 32-48`
- `MaxParticlesPerEmitter = 128-256`
- `MaxParticleDistance = 5000-7000`

### Performance

- `EnableParticleLightsOptimization = true`
- `ParticleClusterThreshold = 48-72`
- `MaxParticlesPerEmitter = 64-128`
- `MaxParticleDistance = 3500-5500`

### Quality

- `EnableParticleLightsOptimization = true`
- `ParticleClusterThreshold = 16-32`
- `MaxParticlesPerEmitter = 256-512`
- `MaxParticleDistance = 7000-10000`

## 8. Recommended Execution Order

1. Resolve placed-light conflicts offline first (highest win, lowest runtime risk).
2. Add same-frame PL dedupe + material-aware lookup acceleration.
3. Add adaptive PL budgeting.
4. Add optional advanced runtime duplicate controls only if needed after steps 1-3.

