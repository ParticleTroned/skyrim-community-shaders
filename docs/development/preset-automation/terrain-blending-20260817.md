# Terrain Blending distance calibration — 2026-08-17

Status: provisional AMD null-HMD evidence

## Scope and contract

`TerrainCullDistance` controls whether multi-texture landscape bounds contribute to Terrain Blending's terrain-depth pass. A bound is skipped when the distance from the average eye to its world-bound surface exceeds the cutoff. `0` disables distance culling. The later full-screen `TerrainBlending::DepthBlend` pass remains active.

The live control exposes a bounded range of `0..8192` game units, exact effective readback, two-frame settling, retained resources, no shader-cache impact, and in-session snapshot restoration. Runtime validation rejected `9000`, applied `725` exactly, and restored `1024` exactly. With the current unit conversion:

- `725` = `10.353 m`;
- `1024` = `14.623 m`.

## Runtime lane

- GPU: current AMD calibration system;
- application API/runtime: native OpenVR through SteamVR;
- HMD: SteamVR null driver;
- scene save: newest known Honeyside save, followed by reproducible `coc` anchors;
- build: VR Release, Info logging, DevBench bridge;
- DLL SHA-256: `8CD3C4F37ED26127835DD7FDF54EC4CA45A9F16F5025D4FA32D5F14B8E1A0EA6`;
- source commit for the deployed DLL: `e9a498403101907274abe88995868ddb4258f739`.

## Timing curve

The accepted Guardian Stones clear-day run used `SkyrimClear` (`0x81A`), 14:00, the fixed cell-entry camera, separate profiler/capture lanes, and 120 unique resolved samples per phase. Campaign ID: `terrain-guardian-clear-day-curve-a-20260817`.

| Phase | Distance | Whole-frame GPU median | p95 | `RenderPasses` median | `DepthBlend` median |
| --- | ---: | ---: | ---: | ---: | ---: |
| baseline before | 1024 | 5.0079 ms | 5.7846 ms | 0.34 ms | 0.02 ms |
| uncapped | 0 | 4.9572 ms | 5.8479 ms | 0.34 ms | 0.02 ms |
| Performance candidate | 725 | 5.0598 ms | 5.8115 ms | 0.34 ms | 0.02 ms |
| baseline return | 1024 | 5.0652 ms | 5.7681 ms | 0.34 ms | 0.02 ms |

No whole-frame or feature-timer ordering separates `0`, `725`, and `1024` at this anchor. This does not prove that distance culling has zero cost in a landscape-bound-heavy moving scene; it establishes only that any saving here is below the run's noise floor.

## Visual qualification

The first clear-day sequence exposed two methodological confounds: Skyrim's default timescale advanced at approximately `20x`, and wind-driven foliage shadows moved between phases. The scalar runners now freeze time when a game-hour anchor is supplied and restore the declared prior timescale in `finally`.

An overcast storm repeat was capture-valid but interpretation-invalid for Terrain Blending because rain and Wetness activity left only about 1% of pixels temporally stable. It remains failure evidence, not a quality comparison.

The fixed Winterhold exterior pose is compositionally poor because the right eye is close to a wall, but it provides static snow/stone surfaces and useful contact edges. A simple A/B/A curve still showed broad material/exposure drift. The drift-resistant accepted run therefore alternated `725` and `1024` three times, using three exact BMP stereo pairs per phase and comparing each `725` phase with the mean of its immediately adjacent `1024` phases. Campaign ID: `terrain-winterhold-clear-interleaved-725-1024-a-20260817`.

| Eye | Cycle MAE values | Cross-cycle correlation range | Three-cycle mean MAE | Mean p95 | Pixels over 3 luma |
| --- | --- | ---: | ---: | ---: | ---: |
| left | 0.3519, 0.3500, 0.3234 | 0.0248–0.0579 | 0.2154 | 0.6072 | 0 |
| right | 0.3489, 0.3506, 0.3216 | 0.0272–0.0568 | 0.2148 | 0.6072 | 0 |

The low correlation means the small residual is not spatially repeatable across cycles. Left/right agreement rules out an eye-specific response. At this anchor, `725` and `1024` are visually indistinguishable at the accepted evidence threshold.

## Capture/storage calibration learned during the run

Direct-to-L PNG capture produced 41 backpressure drops at a ten-cycle interval and one at 45 cycles; 60 cycles was valid. Writing the same PNG workload to D: produced 42 drops at ten cycles, isolating compression rather than HDD throughput as the short-cadence bottleneck. Lossless BMP written to the independent D: staging root, separate eyes only, captured four 12-pair phases at ten-cycle spacing with zero drops in about 27 seconds. Each phase occupied about 232.6 MiB.

The accepted fast visual recipe is therefore lossless BMP, raw eyes only, independent NVMe staging, optional derivatives after capture, and stopped-runtime finalisation to the HDD archive.

## Provisional preset implication

- Performance: retain `725` provisionally. Architecture predicts less landscape-bound work, and the accepted interleaved visual run found no repeatable loss versus `1024`.
- Balanced: retain `1024`.
- Quality: retain `1024`.

This is not yet a measured Pareto win: the expected Performance saving was below the Guardian timing noise floor. Confidence is medium for visual equivalence at the tested Winterhold pose and low for performance benefit or portability.

Revalidate with a deliberately chosen visible landscape/static-object seam at distances spanning 10.35–14.62 m, a moving or physical HMD, a second exterior without foliage/rain/exposure drift, other runtime lanes, and NVIDIA hardware.
