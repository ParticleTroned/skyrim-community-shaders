# Unified-preset performance methodology

## Purpose

The three unified tiers remain Performance, Balanced, and Quality. Current
deferred G-buffer measurements refine how feature cost must be attributed; they
do not justify a fourth tier or immediate policy-value changes.

## Shared-topology result

On the controlled RX 7900 XT null-HMD scene, enabling the complete fourth
deferred pixel-output path increased `Deferred::GeometryInterval` from 2.7548
ms to 4.1303 ms. The 1.3756 ms difference is 9.9 percent of a 72 Hz frame
budget and 12.4 percent of a 90 Hz frame budget.

The treatment omitted an output and may therefore have removed both export work
and producer instructions. It must not be represented as the independent cost
of one shader or consumer. UNORM transport did not improve the interval, and a
simpler packer recovered only about 0.043 ms, so neither target class nor
packing ALU explains the main difference.

Raw captures and comparisons are retained authoritatively at:

```text
L:\Codex\evidence\CSX\deferred-gbuffer-performance-20260830
```

These captures predate the current mandatory context fingerprint and do not
contain a verified artifact hash. They are suitable for choosing the next
experiment, but not for qualifying preset values. The first rebuilt baseline
must use the current profiler contract and retain the exact source, DLL, shader
bytecode, settings, cache, scene, and runtime identities.

## Required measurement record

Every preset timing row should include:

-   preset tier and complete settings hash;
-   GPU, driver, runtime route, HMD mode, refresh, and render dimensions;
-   exact build/DLL, shader-cache, and compile-context identities;
-   save, cell, camera/pose, time, weather, and warm-up;
-   exact `GameHour` and `TimeScale`, with time frozen for each comparison and
    restored after the session;
-   feature state as enabled, disabled-in-settings, or unloaded where supported;
-   deferred demand mask and required/configured/active RTV counts;
-   CSX profiler total, `Deferred::GeometryInterval`, and feature-local timers;
-   sample count, interval, mean, median, p95, fresh-frame count, and failures;
-   compiler, loading/menu, limiter, reprojection, saturation, and background-load
    state; and
-   an adjacent bounded render-map capture identity for the exact pipeline state.

The CSX profiler total is not whole-frame time. Correlated timer deltas are not
independent and must not be added together.

## Experimental sequence

1. Measure the common three-tier baseline before changing individual features.
2. Use binary feature-family splits to find material cost centres.
3. For each selected feature, compare enabled, disabled-in-settings, and
   unloaded states when those states are semantically valid.
4. Measure shared topology separately from the consumer's own post-pass.
5. Sample representative interior/exterior, day/night, and weather anchors.
6. Repeat selected AMD treatments and then add equivalent NVIDIA results using
   the same record shape and vendor-neutral tier policy.
7. Validate SteamVR and OCU/VDXR routes separately; do not pool their absolute
   timings without establishing comparability.

## 2026-08-30 OCU exterior screen

The first preset-oriented screen used a physical Pico 4 Ultra through
OCU/VDXR on an RX 7900 XT. It loaded the same Whiterun save and held the
camera in `WhiterunPlainsDistrict03` under `SkyrimCloudyTU`. CSX rendered
1992 x 1992 per eye, displayed 2592 x 2592 per eye, and used FSR 4 Ultra
Quality with VR render scale enabled.

The exact DLL SHA-256 was
`3B72C74468BD9F5CCB14A2D69A6ABF0F8EDAF2C9E5A3545CDF182A881CF37013`.
The compiled shader-cache tree contained 3,606 files and retained SHA-256
`623B701118F244A3970E2CD869B6615EBED2B0D162AC2879E9BBB0205A120CCB`
before and after the run.

The first adjacent baselines exposed a protocol error: ordinary game-time
progression changed `Deferred::GeometryInterval` by 1.37 ms while the
upscaler remained nearly stable. Subsequent samples fixed `GameHour` at 8.5
and set `TimeScale` to zero. The locked 120-frame control pair was suitably
stable for feature screening:

| Timer                        | Baseline B1 | Baseline B2 |    Delta |
| ---------------------------- | ----------: | ----------: | -------: |
| `Deferred::GeometryInterval` |    10.79 ms |    10.97 ms | +0.18 ms |
| `Engine::RenderWorld`        |    13.19 ms |    13.39 ms | +0.20 ms |
| `RuntimeUpscalerDispatch`    |     7.59 ms |     7.62 ms | +0.03 ms |
| Deferred passes              |     0.65 ms |     0.64 ms | -0.01 ms |
| Deferred composite           |     0.60 ms |     0.61 ms | +0.01 ms |

Bounded A-B-A feature trials produced these whole-frame deltas. Positive
values mean the enabled state cost more than the disabled state.

| Feature             | Frame delta | Standard error | Result                       |
| ------------------- | ----------: | -------------: | ---------------------------- |
| Skylighting         |    +4.11 ms |        0.45 ms | significant tier lever       |
| Grass Collision     |    -0.47 ms |        0.65 ms | inconclusive                 |
| Volumetric Lighting |    +0.44 ms |        0.24 ms | below significance threshold |

Skylighting is therefore a justified distinction between Performance and the
higher tiers. Grass Collision remains a visual and interaction choice; its
sign and magnitude were not reproducible. This exterior screen gives no reason
to diverge the shared High volumetric-lighting setting, but an interior anchor
is still required before that question is closed.

Wetterness needs an active-rain anchor, and Subsurface Scattering and Hair
Specular need a controlled character close-up. The live baseline had AO-only
SSGI disabled, so Quality's provisional SSGI policy requires a deliberate
enabled lane rather than an on/off trial from this baseline.

The feature-cost API's `gameGpuMs` field remained zero or epsilon while named
CSX GPU timers and whole-frame measurements were non-zero. It is invalid for
this run. The conclusions above use the A-B-A whole-frame results and named
feature timers; the tooling defect is recorded separately.

The final B2 capture completed all 120 frames and retained 58 timers, but its
wrapper reported failure after a transient HTTP 503 during profiler-state
restoration. An explicit follow-up disabled the profiler and verified the
postcondition. The capture payload is usable; its failed wrapper status must
not be mistaken for an incomplete capture.

The complete evidence is retained at:

```text
L:\Codex\evidence\CSX\unified-presets-v2-sampling-20260830
```

[`unified-preset-measurements.json`](./unified-preset-measurements.json)
contains the compact machine-readable result for future AMD and NVIDIA rows.

## Significance and stopping rule

For the current bottleneck investigation, a candidate gain is significant when
two independent 120-sample null-HMD captures recover at least 0.50 ms in both
the geometry mean and the same-direction tail metric without a known visual,
stereo, material, or consumer regression. Confirm the selected change once on
a physical OCU route.

Stop pursuing immediately available G-buffer gains when each low-risk
producer, output-signature, write-mask, and compatible target-format lever
either recovers less than 0.20 ms reproducibly, fails compatibility, or requires
architectural redesign. Preserve the residual cost, then begin the preset
matrix. Differences below that threshold may still matter in aggregate, but
they are not a reason to delay the first evidence-backed three-tier rebuild.
