## Context: SSS 1.2.1 + DynamicSampleCount (no implementation yet)

Goal: keep SSS 1.2.1 performance and behavior, add dynamic sampling only to improve upscaling quality, plus a user-configurable hard cap on DynamicSampleCount.

### Prior reply (verbatim)
Got it — you want to keep SSS 1.2.1 behavior/perf, and only add dynamic sampling to handle upscaling better, plus a user‑controlled hard cap. No implementation yet.

Minimal change set (keeps 1.2.1 performance profile)
- Keep compile‑time SAMPLE_COUNT logic exactly as in 1.2.1 (SampleCount * 64), so no 2.0‑style shadow‑length increases at high res.
- Add DynamicSampleCount (and optionally DynamicReadCount) to the CB, pass from src/Features/ScreenSpaceShadows.cpp, and use it in features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/bend_sss_gpu.hlsli to early‑break the loops. If DynamicSampleCount == SAMPLE_COUNT, behavior is identical to 1.2.1.
- Compute DynamicSampleCount from dynamic resolution ratio only (e.g., base * sqrt(dynamicResX * dynamicResY)), so it only reduces samples when upscaling is active.
- Apply a max cap (toggle + slider in SSS settings) to DynamicSampleCount, so it never exceeds the user limit. This preserves 1.2.1‑like cost at high res.

Decisions I need from you before implementing
1) Should the cap apply only to DynamicSampleCount, or also clamp the compile‑time SAMPLE_COUNT (which would trigger a shader recompile when changed)?
2) Do you want to use DynamicReadCount for the READ_COUNT loops, or just use DynamicSampleCount for all early breaks (simpler, but less exact)?
3) Preferred default cap value (e.g., 64) and whether the cap toggle is off by default.

---

## Context: improve quality during upscaling without extra perf cost

### Latest reply (verbatim)
There’s no true free quality boost—if you keep sample count/passes fixed, you’re limited to stability and parameter tuning. Still, a few no‑(or near‑)cost options can help upscaling:

- Dynamic‑res–aware tuning of SurfaceThickness, BilinearThreshold, and ShadowContrast in src/Features/ScreenSpaceShadows.cpp to reduce aliasing/shimmer at low res (quality trade, no extra work).
- Keep blur radius consistent under dynamic res by scaling offsets in features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/ScreenSpaceShadows.hlsli; same taps, just better reconstruction.
- For VR, use dynamic‑res adjusted sampling (SharedData::ConvertUVToSampleCoord(uv, eyeIndex)) instead of raw screenPosition loads to avoid mismatched sampling under upscaling; same number of fetches.
- Stabilize the noise/rotation in GetScreenSpaceShadow (deterministic per‑pixel or blue‑noise) so TAA/upscalers can accumulate better; no extra samples.
- Make sure SSS is applied before any existing TAA/upscaler so the existing history filter smooths it “for free.”

If you want, I can point to exact spots in those files and suggest concrete parameter scaling formulas for dynamic res.

---

## Additional specifics (for transfer/implementation planning)

### Where DynamicSampleCount would be wired (SSS 1.2.1 baseline)
- C++: src/Features/ScreenSpaceShadows.cpp
  - Add DynamicSampleCount (and optionally DynamicReadCount) to RaymarchCB struct and fill it before dispatch.
  - Compute dynamicSampleCount using dynamic resolution ratio only; clamp to user cap.
  - Keep compile-time SAMPLE_COUNT as SampleCount * 64 (no scaling).
- HLSL: features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/RaymarchCS.hlsl
  - Add cbuffer fields DynamicSampleCount/DynamicReadCount (if not already present).
  - Pass them into DispatchParameters.
- HLSL: features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/bend_sss_gpu.hlsli
  - Add early-breaks in loops that currently run to READ_COUNT/SAMPLE_COUNT.
  - If DynamicReadCount is not used, use DynamicSampleCount as the break condition.

### Simple dynamic sample formula (only reduces with upscaling)
- baseSamples = SampleCount * 64;
- dynamicScale = sqrt(dynamicResX * dynamicResY); // 0..1
- dynamicSampleCount = round(baseSamples * dynamicScale);
- dynamicSampleCount = min(dynamicSampleCount, userCap);
- dynamicSampleCount = max(dynamicSampleCount, 1);

### Suggested settings UI additions
- Toggle: Enable Dynamic Sample Cap
- Slider: Dynamic Sample Cap (e.g., 32..128)

### Key constraint
- No shadow-length increase: keep compile-time SAMPLE_COUNT fixed (1.2.1), and only reduce samples when dynamic res < 1.0.
