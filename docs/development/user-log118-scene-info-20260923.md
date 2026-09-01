# User log 118: native scene workload at the slow viewpoint

The slowdown is present in the measured native world/shadow scopes and is
accompanied by substantially more render-pass and material submissions. The
native culler reaches its recorded 4096-candidate capacity in the bad section.
This identifies a useful target; it does not establish incorrect visibility,
a capacity-overflow policy defect, or a regression against RC166 on its own.

## Preserved evidence and build

-   [Original byte-preserved log](<D:/Coding/GitHub/CS logs/SceneInfo-6131c9532-USER-UNVERIFIED__20260923T002405256Z__87ee50d1__CommunityShaders(118).log>).
-   Source supplied by user: `D:/FireFox-downloads/CommunityShaders(118).log`.
-   Preserved length: 671656 bytes; SHA-256: `2B0B86984B4B810B47505A9F0C7585E3F9ABF6D8E9580B201A2462BA5AAEEA40`.
-   Runtime embedded Build ID: `022c8cb34a1a6226ba07f16be123270166d4fbb1beef69b69a82411b8e141068`; exact match to the
    [Info AIO receipt](D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-main-VR-6131c9532-SceneInfo-Info-NoDevBench-FOMOD.receipt.json).
-   Receipt source base: `6131c953232b67cfb6587ee27dc88d6db36fbac2`, with the
    temporary SceneInfo instrumentation patch (dirty digest
    `7b11f17c16737194887c6572bcf6498c9672b80b76a536f5773d9bda5a3e1572`).
-   Expected packaged DLL: SHA-256 `71cbf981159924489c4a69e0ecaeea52025d9812e63ae52913c00778256776bb`,
    23516672 bytes. The tester's physical DLL hash
    was not independently obtained; matching embedded identity is the evidence.
-   Release; `CSX_VR_SCENE_INFO=ON`, `DEVBENCH_BRIDGE=OFF`.
-   Logged Streamline runtime 2.14.1; DLSS runtime 310.9.1.

## Window selection and method

All timestamps below are the log's local wall clock; its timezone is not
established. The user's approximate 20s good/20s bad description corresponds
to longer recorded plateaus. Loading, relatch, movement and shutdown are not
included in the primary comparison. These are selected comparison windows,
not exact user-marked intervals.

-   Good: 02:16:51.650 to 02:17:11.942, seq 237–256,
    20.2918s, 900 frames.
-   Bad: 02:17:32.204 to 02:17:52.429, seq 277–296,
    20.2244s, 627 frames.
-   Both have the CSX menu open; culling enabled/Balanced, extent 10/10,
    render scale Active, recorded size 3456×1696.
-   CPU scope means are weighted by recorded frame count. Counters are summed
    before division by total frames. CPU scopes are inclusive elapsed wall time,
    not CPU execution-only measurements, and are not added together here.
    They include CSX callbacks invoked beneath the native rendering entry points;
    the labels do not attribute all elapsed time exclusively to Skyrim.exe.
-   Submissions are intercepted render-pass calls, not unique scene objects
    or a complete D3D draw-call inventory. Material setup calls are not counts
    or timings of the malformed-material guard.
-   Native visibility reads sample every 16th readback before CSX recovery.
    They describe candidate-buffer entries, not all objects in the scene.

| Metric                                            |    Good |     Bad |   Change |
| ------------------------------------------------- | ------: | ------: | -------: |
| Present interval (ms; includes pacing/waiting)    |  22.546 |  32.256 |   +9.709 |
| Native world scope (CPU wall ms/frame)            |   2.989 |   7.417 |   +4.428 |
| Native shadow scope (CPU wall ms/frame)           |   1.527 |   4.160 |   +2.633 |
| Render-pass submissions/frame                     | 2,573.8 | 7,325.2 | +4,751.4 |
| Lighting geometry setup calls/frame               |   567.2 | 1,979.2 | +1,412.1 |
| PBR material setup calls/frame                    |    98.3 | 1,324.4 | +1,226.1 |
| Object-LOD geometry setup calls/frame             |   382.1 |   477.7 |    +95.6 |
| Terrain-LOD geometry setup calls/frame            |    49.7 |    63.7 |    +14.0 |
| PBR object-LOD material setup calls/frame         |     0.0 |     0.0 |     +0.0 |
| Candidates per sampled native culler readback     | 2,219.0 | 4,096.0 | +1,877.0 |
| Sampled native results marked visible (%)         |    7.98 |   33.37 |   +25.39 |
| Native depth-render wrapper (CPU wall ms/frame)   |  0.0070 |  0.0051 |  -0.0019 |
| Native depth-readback wrapper (CPU wall ms/frame) |  0.0064 |  0.0076 |  +0.0012 |
| Recovery wrapper (CPU wall ms/frame)              |  0.0010 |  0.0010 |  +0.0000 |
| Light snapshot capture (CPU wall ms/frame)        |  0.0203 |  0.0177 |  -0.0026 |
| Native Present scope (CPU wall ms/frame)          |  0.1062 |  0.1053 |  -0.0009 |
| Balanced recovery attempts (window total)         |       0 |       0 |       +0 |
| Balanced visibility promotions (window total)     |       0 |       0 |       +0 |

## What the change supports

1. Native world time rises by 4.428 ms/frame,
   and the shadow scope separately rises by
   2.633 ms/frame. Each is called once per
   recorded frame in both windows. Submissions increase
   2.85×, lighting
   setup 3.49× and PBR
   material setup 13.47×.
   More downstream scene work is directly observed; there are no stacks in
   this log to split traversal, material setup, driver work and waits inside
   those inclusive scopes.
2. Good readbacks contain about 2219 candidates
   and 177 visible
   entries per sample. All 40 bad-window samples contain
   exactly 4096 candidates, about
   1367 visible.
   Diagnostics reject counts above 4096 rather than clamping them, so this
   plateau is the actual recorded count. The native layout/capacity is backed
   by the retained live-snapshot evidence in
   `docs/development/vr-depth-culling-temporal-evidence.md`.
   Candidate pressure/capacity is the strongest new culling clue. The log
   does not count candidates rejected at capacity or show what the engine
   does with them; overflow-related extra rendering remains a hypothesis.
3. The steady slowdown occurs with zero recovery attempts and promotions.
   This does not reproduce the old continuous all-visible temporal fallback.
   Motion produces brief bounded promotions (64 per attempt), but they stop
   while the large native workload persists.
4. The directly timed depth-render/readback/recovery wrappers and light
   snapshot capture are much too small to account for the measured rise
   directly. This does not exclude culling's downstream workload effects,
   other native traversal, or costs outside those scopes.
5. All recorded gameplay samples have zero PBR object-LOD material setup
   calls. The newly enabled PBR object-LOD setup path is therefore not an
   observed explanation in this session. Ordinary PBR setup increases, and
   object-LOD geometry itself increases about 25%; neither is a measured
   per-call cost or proof of a specific material/shader regression.
6. Render scale remains Active at the same dimensions across the selected
   windows. There is no observed relatch loop. GPU local usage falls from
   7292 MiB to 7028–7031 MiB, below the reported budget; private process usage
   changes only from 20259–20272 to 20271–20284 MiB. These snapshots do not
   support a large growing allocation as the cause of this transition.

## Transition and controls

The major change occurs in seq 258, ending 02:17:13.960: interval 30.517 ms,
world 8.758 ms/frame, 6378 submissions/frame. The next sample reaches 4096
candidates, and that candidate count persists through the end of the log.
Recovery stops by seq 265, well before the primary bad window.

The slowdown persists with the CSX menu closed: good seq 202–210 has world
3.554 / shadows 2.030 ms/frame and 2660 submissions/frame; bad seq 318–323
has world 7.377 / shadows 4.268 and 7365 submissions/frame. These shorter
windows contain some motion/recovery, so they are a qualitative cross-check,
not the primary matched-menu comparison.

The good and bad camera poses differ, as expected for a viewpoint test.
Representative seq 245 versus 285 differs by 335.6 world units and
121.9 degrees of full relative rotation. Thus this capture establishes
the expensive viewpoint's workload, but cannot by itself prove worsening
over time at the exact same pose or quantify the RC166-to-current regression.

## Timing limitations

The logger's `runtimeCpuMs` and `runtimeGpuMs` are rolling compositor timing
values, not independent one-second window means. Source inspection confirms
a 60-sample cache; the once-per-second diagnostic caller and any UI callers
share it. The CPU field retains loading contamination during much of the
good plateau, making its full-window average misleading. Late good samples
248–256 settle to about 5.958 ms, and the bad samples to about
11.952 ms; this corroborates the direction only.
The scope timings and counted frame intervals above do not use that cache.

The compositor GPU value is exactly 15.556 ms throughout both selected
windows. This log cannot establish a GPU increase or distinguish true GPU
work from runtime reporting/pacing effects. A GPU timestamp capture or
independently suitable frame-time trace is needed for that question.

One-second reporting itself records roughly 0.17–0.18 ms per report, not
per frame. This excludes added per-call timers and atomic counters; the
instrumented build has not been proven performance-neutral.

## Next decisive test

Use the same Info build and an unchanged bad viewpoint. Record settled
exterior depth culling ON → OFF → ON (about 20s each), then return to the
known good viewpoint and back. Keep camera pose, render scale, preset and
other settings fixed within each toggle comparison, and close the CSX menu
for every measured hold. Existing logging is sufficient for this control.

An unexpected recovery after OFF/ON at the same pose would implicate state
or selection history; normal worsening with culling OFF would demonstrate
that culling still saves work, without clearing the whole native pipeline.
Neither outcome alone proves a particular fix. For exact attribution inside
the expensive world/shadow scopes, use the user's DevBench/WPR capture.
For a release-regression claim, also compare RC166/current at the same pose
and settings. Do not increase native capacity or remove lifetime/material
protections based only on this log.

## Follow-up: composition of the additional submissions

The recorded lighting categories can be decomposed without another run:

| Lighting geometry setup calls/frame      |  Good |    Bad | Increase |
| ---------------------------------------- | ----: | -----: | -------: |
| Object LOD (`LODObjects`, `LODObjectHD`) | 382.1 |  477.7 |     95.6 |
| Terrain LOD (`LODLand`, `LODLandNoise`)  |  49.7 |   63.7 |     14.0 |
| Other lighting techniques                | 135.4 | 1437.9 |   1302.5 |

Other lighting techniques account for 92.24% of the increase
within this hook's lighting-geometry population. They are not proven to be
92.24% of the extra total submissions or CPU cost. The residual
includes ordinary objects, animated trees, character/skin techniques and
landscape techniques, but this log does not separate them. It also includes
`MTLandLODBlend`; calling the entire residual strictly non-LOD would overstate
the evidence. Grass/distant-tree shader families outside BSLightingShader
are not classified by this lighting hook.

`pbrMaterials` increments at entry to `TruePBR::BSLightingShader_SetupMaterial`
when `UsesCustomMaterialSetup` accepts the current technique. It counts
invocations, not unique materials, meshes, PBR configuration conversions,
or GPU cost. The function prepares material constant groups, binds relevant
textures/constants, and flushes/applies the groups. More calls add setup
work; the current log does not time this function separately.

The 13.47x PBR call increase can arise from additional visible PBR geometry,
repeated rendering of that geometry in different passes, or more frequent
material changes/rebinding. The increased native visible-result population
supports increased scene workload, but there is no object-to-submission
mapping or per-pass breakdown to establish which mechanism dominates.
Unchanged culling settings/algorithm do not imply unchanged culling inputs,
selected candidates, visible results, or the objects reaching the capacity.

The existing total submission hook has access to `BSRenderPass`, including
geometry, shader/property, technique, render flags, LOD mode and light counts.
A focused diagnostics-only extension could therefore record:

1. A fixed-size submission histogram by rendering phase and shader family,
   with lighting technique, PBR flag and important render flags separated.
2. Bounded per-frame geometry/material identity tracking or sampling, with
   overflow coverage reported, to separate more objects from repeated passes
   and material switches. Repeat identity alone is not proof of redundancy:
   phase, eye, cascade and technique must match too.
3. Bounded representative geometry names/material texture paths for the
   dominant buckets, copied only while valid; no retained raw pointer walks.
4. Sampled PBR setup timing, separately labeled for instrumentation overhead.

Output should remain aggregated at Info once per interval in the temporary
diagnostic build, with the entire instrumentation absent from normal builds.
This can identify the workload without adding a new native interception
point. It will not identify native culler overflow behavior without separate
evidence from its selection path. No such extension is implemented by this
analysis-only follow-up.

## Validation and artifacts

`python build/user-log118-scene-info-20260923/analyze.py` verifies the
preserved archive hash/size, all 323 extracted six-line SceneInfo groups,
all recorded phase/counter values, selected window invariants, recomputed
weighted timing/count metrics and the runtime Build ID against the receipt.
The complete numeric window summaries are preserved in
[user-log118-scene-info-20260923.summary.json](user-log118-scene-info-20260923.summary.json).
The audit script and extracted samples remain in the local repository-root
`build/user-log118-scene-info-20260923` evidence directory.
This documentation records the findings only; rendering source and diagnostic
instrumentation are unchanged, and no new capture was started.
