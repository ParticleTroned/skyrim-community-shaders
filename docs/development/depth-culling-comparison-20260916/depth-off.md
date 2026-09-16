# Depth culling off: controlled gameft-sw run

**Disabling native depth culling did not improve overall performance in this run. GPU frame time increased in all six saves against the preceding Legacy run, and recorded FPS fell. Keep depth culling enabled while investigating its implementation and the separate main-VR CPU/DLSS-GPU regressions.** One off run does not establish repeat variance or isolate every source of CPU variation.

The comparison is Legacy with depth culling ON versus the same Legacy selector with its depth-culling master switch OFF. Before/after saved feature settings differ only in EnableDepthBufferCullingExterior=true to false. Despite that historical field name, it is the global master in VRDepthCullingEnablePolicy::IsEnabled: false disables both exterior and interior depth culling. Effective culling status is false before and after; recovery counters remain zero. No source, defaults, shaders, rendering scale or saved measurement formulas were modified.

[All eight runs: complete saved timing, tails, noise and health comparison](tables.md) | [48-window WPR metrics](wpr-metrics.csv) | [Recorded FPS and sample cadence](tail-cadence.csv)

## CPU timing, final ten seconds

| Save / mode | On mean ms | Off mean ms | Delta   | On P95 / P99    | Off P95 / P99   | On / off single spikes/s | On / off spike events/s |
| ----------- | ---------- | ----------- | ------- | --------------- | --------------- | ------------------------ | ----------------------- |
| 08 / DLAA   | 5.768      | 16.635      | +10.867 | 6.200 / 7.413   | 24.540 / 25.500 | 0.0 / 0.4                | 0.1 / 2.9               |
| 09 / DLAA   | 5.919      | 6.539       | +0.620  | 9.800 / 10.200  | 10.200 / 10.700 | 0.3 / 6.1                | 10.3 / 14.5             |
| 10 / DLAA   | 5.935      | 5.421       | -0.514  | 9.800 / 10.100  | 5.800 / 6.300   | 6.7 / 0.1                | 15.8 / 0.2              |
| 11 / DLSS   | 8.238      | 9.880       | +1.642  | 12.100 / 14.380 | 15.400 / 17.700 | 4.0 / 7.8                | 8.9 / 13.2              |
| 12 / DLSS   | 9.048      | 9.797       | +0.749  | 12.700 / 14.300 | 13.300 / 16.020 | 7.9 / 3.7                | 13.6 / 5.2              |
| 13 / DLSS   | 18.174     | 23.058      | +4.884  | 26.100 / 28.200 | 30.700 / 34.200 | 0.4 / 6.9                | 3.4 / 12.8              |

## GPU timing, final ten seconds

| Save / mode | On mean ms | Off mean ms | Delta  | On P95 / P99    | Off P95 / P99   | On / off single spikes/s | On / off spike events/s |
| ----------- | ---------- | ----------- | ------ | --------------- | --------------- | ------------------------ | ----------------------- |
| 08 / DLAA   | 9.828      | 11.687      | +1.859 | 9.900 / 10.000  | 12.800 / 13.728 | 0.0 / 0.4                | 0.0 / 0.5               |
| 09 / DLAA   | 7.868      | 8.248       | +0.380 | 8.000 / 8.100   | 8.400 / 8.600   | 0.0 / 0.0                | 0.0 / 0.0               |
| 10 / DLAA   | 8.055      | 9.525       | +1.470 | 8.200 / 8.300   | 9.600 / 9.700   | 0.0 / 0.0                | 0.0 / 0.0               |
| 11 / DLSS   | 8.082      | 9.670       | +1.588 | 8.500 / 10.420  | 11.180 / 15.316 | 1.4 / 3.2                | 1.4 / 3.3               |
| 12 / DLSS   | 8.866      | 10.503      | +1.637 | 9.100 / 9.400   | 11.020 / 13.200 | 0.2 / 0.9                | 0.2 / 1.0               |
| 13 / DLSS   | 12.429     | 16.670      | +4.241 | 15.700 / 17.629 | 19.800 / 21.487 | 6.1 / 5.7                | 6.2 / 6.1               |

Positive mean deltas are slower. Saved spike events include one-sample events and consecutive groups at tail median +2 ms; single spikes require lower neighbors. Rates must not be added. Thresholds follow each run's median, so lower spike count alone does not prove improvement. All averages and percentiles use exactly the unchanged saved 50,60) windows.

## Frame pacing explains why a smoother CPU trace can still be slower

| Save | On recorded FPS median | Off recorded FPS median | On 8–9 ms cadence % | Off 8–9 ms cadence % | On 16–17.5 ms cadence % | Off 16–17.5 ms cadence % |
| ---- | ---------------------- | ----------------------- | ------------------- | -------------------- | ----------------------- | ------------------------ |
| 08   | 60.000                 | 58.500                  | 0.000               | 0.000                | 100.000                 | 95.105                   |
| 09   | 109.000                | 98.000                  | 90.625              | 84.985               | 8.548                   | 7.559                    |
| 10   | 104.000                | 60.000                  | 84.586              | 0.000                | 15.318                  | 100.000                  |
| 11   | 111.000                | 68.600                  | 91.944              | 26.462               | 5.093                   | 72.076                   |
| 12   | 91.000                 | 60.000                  | 82.018              | 0.000                | 4.715                   | 99.666                   |
| 13   | 57.000                 | 46.800                  | 0.000               | 0.000                | 94.921                  | 67.320                   |

Save 10 CPU mean/P99 fall from 5.935/10.100 to 5.421/6.300 ms and spike-event frequency falls from 15.8/s to 0.2/s. However, GPU mean rises from 8.055 to 9.525 ms and recorded FPS falls from 104 to 60. Its sample cadence changes from mostly ~8.33 ms intervals with some ~16.67 ms intervals to entirely ~16.67 ms intervals. This is a concrete pacing confound: smoothness improved while throughput worsened. The CSV has no reprojection-state field, so these intervals do not prove the compositor's exact mode. They also do not alone prove the cause of each CPU spike.

## Identified native frustum routines

| Save | On render CPU ms/frame | Off render CPU ms/frame | Delta  | On all-thread CPU ms/frame | Off all-thread CPU ms/frame | Delta  |
| ---- | ---------------------- | ----------------------- | ------ | -------------------------- | --------------------------- | ------ |
| 08   | 0.035                  | 0.054                   | +0.019 | 0.072                      | 0.098                       | +0.026 |
| 09   | 0.000                  | 0.000                   | +0.000 | 0.009                      | 0.010                       | +0.001 |
| 10   | 0.000                  | 0.000                   | +0.000 | 0.000                      | 0.000                       | +0.000 |
| 11   | 0.075                  | 0.018                   | -0.057 | 0.634                      | 0.454                       | -0.180 |
| 12   | 0.029                  | 0.132                   | +0.103 | 0.823                      | 1.845                       | +1.022 |
| 13   | 0.772                  | 0.720                   | -0.052 | 4.529                      | 4.106                       | -0.423 |

Only the three native sphere/compound-frustum regions identified from the preserved live Ghidra snapshot are counted. They are not the complete depth-culling implementation or a GPU timer. All-thread values sum CPU work across threads; they are not elapsed frame time. Normalization uses actual recorded frames, whose count changes with pacing. Historical/live patch-byte and missing-private-symbol limitations from the seven-run report still apply.

**The native frustum hotspot remains with depth-buffer culling disabled:** Save 13 retains about 4.106 summed CPU ms/frame versus 4.529 with it enabled; Save 12 retains 1.845 versus 0.823. Therefore those general scene/frustum samples cannot be equated with the overhead of the depth-buffer culling feature. They remain an independent native-visibility investigation target. Disabling occlusion may change the work reaching other traversal/draw paths; the trace does not count objects or prove a particular caller caused that change.

## Render-thread execution and waiting

| Save | On / off running ms/frame | On / off ready ms/frame | On / off waiting ms/frame | On / off running % of window |
| ---- | ------------------------- | ----------------------- | ------------------------- | ---------------------------- |
| 08   | 14.130 / 17.008           | 0.119 / 0.063           | 2.419 / 0.382             | 84.78 / 97.46                |
| 09   | 7.856 / 9.329             | 0.053 / 0.050           | 1.275 / 0.826             | 85.55 / 91.42                |
| 10   | 9.182 / 12.343            | 0.037 / 0.048           | 0.405 / 4.276             | 95.41 / 74.06                |
| 11   | 8.387 / 11.937            | 0.127 / 0.254           | 0.739 / 2.410             | 90.67 / 81.77                |
| 12   | 10.475 / 14.244           | 0.064 / 0.054           | 0.419 / 2.398             | 95.64 / 85.32                |
| 13   | 17.039 / 21.256           | 0.095 / 0.047           | 0.350 / 0.437             | 97.46 / 97.78                |

WPR execution/ready/wait quantities differ from fpsVR CPU frametime. Both per-frame and wall-window running share are shown so a lower delivered frame rate is not mistaken for lower work. Interval reconstruction deviations remain in the evidence, without changing saved tolerances or timing values.

Rendering-thread running CPU per recorded frame increases in all six saves. Save 10 rises from 9.182 to 12.343 ms/frame while waiting rises from 0.405 to 4.276 ms/frame. Its wall-window running share falls because it delivers fewer frames. This supports a pacing/workload explanation for its smoother fpsVR CPU trace, not a proven CPU optimization. The large Save 08 fpsVR CPU delta is not fully decomposed by WPR running time, and should not be assigned to one function.

## Exclusive rendering-thread CPU module deltas, off minus on

| Save | SkyrimVR.exe | CommunityShaders.dll | d3d11.dll | nvwgf2umx.dll | ntoskrnl.exe |
| ---- | ------------ | -------------------- | --------- | ------------- | ------------ |
| 08   | +1.361       | +0.602               | +0.562    | +0.128        | +0.015       |
| 09   | +0.556       | +0.247               | +0.256    | +0.146        | +0.027       |
| 10   | +1.950       | +0.555               | +0.587    | -0.675        | +0.114       |
| 11   | +1.560       | +0.814               | +0.363    | -0.103        | +0.083       |
| 12   | +2.214       | +0.673               | +0.472    | -0.172        | -0.037       |
| 13   | +1.309       | +1.031               | +0.715    | +0.311        | +0.014       |

These are sampled exclusive leaf-module CPU ms per recorded frame, not inclusive hook duration or GPU time. They locate where executing CPU changed; they do not identify a safe source optimization by themselves.

## Inclusive render contexts: off-minus-on CPU ms per recorded frame

| Save | render_pass_immediate | main_render_depth | begin_technique | lighting_geometry | dlss_evaluate |
| ---- | --------------------- | ----------------- | --------------- | ----------------- | ------------- |
| 08   | +2.543                | +0.418            | +0.947          | +0.572            | -0.008        |
| 09   | +0.839                | +0.083            | +0.210          | +0.182            | -0.021        |
| 10   | +2.606                | +0.575            | +0.852          | +0.652            | +0.015        |
| 11   | +2.793                | +0.753            | +0.729          | +0.599            | -0.000        |
| 12   | +2.982                | +1.010            | +0.904          | +0.620            | -0.018        |
| 13   | +3.316                | +0.957            | +0.982          | +0.682            | -0.008        |

These contexts overlap and include their descendants. They must not be summed or labeled exclusive hook overhead. More sampled work in draw submission/state/material contexts is consistent with the cost of less occlusion, but it does not establish draw-count changes. CPU time inside DLSS evaluation is not a GPU inference measurement.

## Load health and saved settling

| Save | Completed latch metric           | Stretch                            | CPU median-baseline settling     | GPU settling | Final health                                                                                |
| ---- | -------------------------------- | ---------------------------------- | -------------------------------- | ------------ | ------------------------------------------------------------------------------------------- |
| 08   | 266 frame(s), 5986.5 ms, None/q0 | none observed                      | Not reached                      | 46s          | Successful: correct AA/RS mode, stretch inactive, stereo complete, no owner/retirement debt |
| 09   | 335 frame(s), 3360.8 ms, None/q0 | 360f / 6503.8 ms; completed by +5s | First 5s window near final level | 1s           | Successful: correct AA/RS mode, stretch inactive, stereo complete, no owner/retirement debt |
| 10   | 198 frame(s), 3268.8 ms, None/q0 | 336f / 6353.6 ms; completed by +5s | First 5s window near final level | 0s           | Successful: correct AA/RS mode, stretch inactive, stereo complete, no owner/retirement debt |
| 11   | 128 frame(s), 2712.4 ms, DLSS/q1 | 1f / 122.1 ms; completed by +5s    | 4s                               | 3s           | Successful: correct AA/RS mode, stretch inactive, stereo complete, no owner/retirement debt |
| 12   | 153 frame(s), 3305.3 ms, DLSS/q1 | none observed                      | 32s                              | 33s          | Successful: correct AA/RS mode, stretch inactive, stereo complete, no owner/retirement debt |
| 13   | 21 frame(s), 1231.6 ms, DLSS/q1  | 1f / 142.0 ms; completed by +5s    | 50s                              | 5s           | Successful: correct AA/RS mode, stretch inactive, stereo complete, no owner/retirement debt |

The CPU baseline rule is unchanged: scan the full hold using five-second medians against the final baseline. A stored zero is described rather than presented as instantaneous settling; early stability does not imply no startup spikes. GPU settling is the unchanged saved gpuSettlingS. Producer latch/stretch durations can include loading; the +Ns observations are relative to world entry. These end-state checks are not canonical release qualification.

## Evidence and scope

Run: gameft-sw-20260916T190211584Z-812d171f. Exact source: 503fbfc92b5a9a58f5ba14a2343c3b0b119c40dc. Build ID: caf9feac1bce8608281b02ddeadbc5b507c68fb739a04044d5f57a6c18b905dd. DLL SHA-256: C8D41C226BF900B2032C8AE505DAB512476F4B37CCD44E5C22E2BC588695D8D0. PDB SHA-256: E82DA0EC720EC6B55BD0C10877B12EECD69FAC6A36F1C03004BFE8FEA0A441F8. All physical artifact, manifest, AIO receipt, enabled-provider, save and process-lifetime checks passed after results were shown.

The WPR profile is unchanged; all six holds were covered. Trace-quality checks require zero lost events/buffers and sampled/context-switch/ready events with stacks. [Verified raw archives (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T190211584Z-812d171f/verified-archives.json`; retained results and hashes are in the ledger); Build provenance (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T190211584Z-812d171f/provenance.json`; retained results and hashes are in the ledger); Only intended setting difference (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T190211584Z-812d171f/settings-delta-vs-legacy2.json`; retained results and hashes are in the ledger). No additional live calls or offline analysis were made during measured holds. This CPU-oriented trace does not contain GPU pass attribution; total GPU deltas cannot yet be assigned between avoided geometry, depth-culling overhead and downstream passes.

The practical next target is to preserve the observed culling benefit while understanding CPU variability and the separate DLSS GPU residual. Use the normal and slow intervals across saves; Save 13 remains an example, not the target. Do not disable culling globally or trade away visibility/ownership safety based on one frame-time metric.
