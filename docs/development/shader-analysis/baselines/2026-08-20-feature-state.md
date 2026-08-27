# Feature-state shader baseline — 2026-08-20

This is the sanitized, versioned summary of the first enabled versus
resident-inactive versus startup-unloaded comparison. The machine-readable
record is [`2026-08-20-feature-state.json`](./2026-08-20-feature-state.json).
Raw captures, shader caches, screenshots, binaries, settings, and receipts are
retained in diagnostic archive `2026-08-20-null-hmd-shader-state`.

## Identity and scope

- DLL SHA-256: `2470AA816A3A7C4D827EE6EC4F6808702C4152A684DE6D1166B2EC8922CE360A`
- DLL size: 24,437,760 bytes
- Route: Skyrim VR through SteamVR's Valve null HMD
- Samples: 120 requested per state
- Measurement: resolved CSX profiler block, not whole-frame GPU time
- Feature group: Screen Space Shadows, Skylighting, Volumetric Lighting, and Upscaling

The original session recorded the exact binary but not its source commit. This
baseline therefore uses binary identity; future runs must record both.

## Compiled shader cache

| State | Files | Bytes |
|---|---:|---:|
| Enabled | 3,615 | 179,236,348 |
| Startup-unloaded | 3,606 | 162,088,968 |

The enabled cache was 17,147,380 bytes larger. Of the common paths, 2,131 were
byte-identical and 1,475 changed content; only nine files existed solely in the
enabled cache. Changed common paths were concentrated in Lighting (710), Effect
(416), and Water (344). Load state therefore changes shared shader permutations
broadly rather than merely adding nine optional binaries.

## GPU profiler results

### Breezehome interior

| State | Mean (ms) | Median | P95 | Ratio to enabled |
|---|---:|---:|---:|---:|
| Enabled | 3.7840 | 3.8387 | 3.9837 | 1.000 |
| Resident-inactive | 4.0593 | 4.0538 | 4.2560 | 1.073 |
| Startup-unloaded | 0.3385 | 0.3386 | 0.3711 | 0.089 |

### Riften exterior, fixed anchor

| State | Mean (ms) | Median | P95 | Ratio to enabled repeat |
|---|---:|---:|---:|---:|
| Enabled original | 4.4182 | 4.1752 | 5.0962 | 1.009 |
| Enabled repeat | 4.3776 | 4.1667 | 5.2341 | 1.000 |
| Resident-inactive | 4.1577 | 4.1120 | 4.5256 | 0.950 |
| Startup-unloaded | 0.5311 | 0.5252 | 0.5514 | 0.121 |

The two enabled Riften means differ by 0.0406 ms (0.93%). In the enabled
repeat, weighted named-timer means were Upscaling 3.6015 ms, Volumetric
Lighting 0.2039 ms, Skylighting 0.0578 ms, and Screen Space Shadows 0.0083 ms.

## Interpretation

- Runtime disabled is not a proxy for startup unloading, particularly when
  Upscaling changes route or resolution.
- Feature-state changes must be analysed by family and structural cache key,
  not solely by the number of added shader files.
- These are CSX profiler-block measurements. Remaining-pass cost and render
  resolution can change between states, so timer deltas are not additive
  whole-frame savings.

## Raw evidence receipt

- Archive ID: `2026-08-20-null-hmd-shader-state`
- Files: 14,798
- Bytes: 781,480,981
- File-manifest SHA-256: `50E2F79DBB07676BCDE7F0EDFAFB8112C2DB1A33A4C01A376E2EA492635DF187`
- Original session retained: yes
