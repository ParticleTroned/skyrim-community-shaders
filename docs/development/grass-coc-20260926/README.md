# Sequential COC comparison — 26 September 2026

**All 65 measured COCs completed strict qualification; no crash or freeze occurred.** No Ghidra investigation was needed. The 10-second run passed the aggregate acceptance check. The 5- and 3-second runs failed only a native-presentation predicate in that aggregate, explained below; those raw failures remain visible rather than being relabeled as passes.

Shorter pacing did not produce escalating transition latency: renderer request-to-stable means stayed at 43.30, 43.20 and 42.15 frames. Every Windhelm return at 5s and 3s incurred one recoverable relatch requeue (12/12 and 10/10), versus 3/10 at 10s. Windhelm mean COC-to-ready time increased from 1.953s to 2.028s and 2.067s (+75ms and +114ms). Dragonsreach averaged 2.627s in the first run after excluding its initial 6.384s outlier, versus 2.745s and 2.577s subsequently. These sequential, instrumented runs do not establish a general CPU/GPU performance improvement or prove absence of a rare grass lifetime failure.

All three campaigns used the same Skyrim VR process (PID 34744). The initial Windhelm positioning COC is excluded from the 65 measured transitions. No build, deployment, fixture preparation, game restart, or unmeasured positioning was repeated between campaigns.

Compiled source: `3baaf91b90416ad25d067cdb26c34ce293cf7a5e`. Build ID: `5c8dfa4c9f482d14faf6cf82e06455724d317eda840b3a59db1baba961660d25`.
Physical DLL SHA-256: `63caf373b968553243b5715b411290fb9d9636fd6fddc90303c4fd44efb7d599` (29,059,584 bytes); exact enabled AIO, manifest and build receipt verified. Source dirty flag reflects the documented noncompiled working-tree files; compiled sources match the measured commit above. This documentation amendment does not change that measured identity.

Release build, DevBench enabled, Tracy disabled, comparison toggles absent. RTX 5070 Ti Laptop GPU, per-eye output 1512×1680. VR FPS Stabilizer retained exclusive profile ownership. Runtime-only foveation/periphery-TAA fixture: 0.3/0.3/0.7. Developer/debug logging enabled.

## Completion and timing

COC-to-ready time starts at the atomic console dispatch and ends at strict qualification. The requested 10/5/3-second waits are additional to loading and qualification, not a fixed wall-clock dispatch period. Render stabilization frames below instead measure the renderer request-to-stable interval; these are different timing origins.

| Campaign | Strict qualified | Mean COC-to-ready |  Worst | Mean render stabilization | Worst render frames | Retries | Final acceptance |
| -------- | ---------------: | ----------------: | -----: | ------------------------: | ------------------: | ------: | ---------------- |
| 20 × 10s |            20/20 |            2.478s | 6.384s |              43.30 frames |                  71 |       3 | PASS             |
| 25 × 5s  |            25/25 |            2.401s | 3.191s |              43.20 frames |                  71 |      12 | FAIL             |
| 20 × 3s  |            20/20 |            2.322s | 2.660s |              42.15 frames |                  65 |      10 | FAIL             |

| Campaign | Destination          | Samples | Mean strict time | Worst strict time | Mean strict frames |
| -------- | -------------------- | ------: | ---------------: | ----------------: | -----------------: |
| run1     | WhiterunDragonsreach |      10 |           3.002s |            6.384s |              30.00 |
| run1     | WindhelmExterior01   |      10 |           1.953s |            2.046s |              70.10 |
| run2     | WhiterunDragonsreach |      13 |           2.745s |            3.191s |              31.23 |
| run2     | WindhelmExterior01   |      12 |           2.028s |            2.114s |              71.17 |
| run3     | WhiterunDragonsreach |      10 |           2.577s |            2.660s |              30.30 |
| run3     | WindhelmExterior01   |      10 |           2.067s |            2.258s |              68.80 |

## Health and resource behavior

-   **run1:** failure deltas `{}`; failed final gates `[]`; all publication checks current/complete/matched: `True`. Retries: `{"render_target_relatch_requeued": 3}`. Presentation stretch: 19 completed frames, 12986.492 ms total; maximum 1 frames. Mean cleanup tail: 0.000 ms.
-   **run2:** failure deltas `{}`; failed final gates `[{"limit": {"bothEyes": true, "maximumAgeFrames": 2, "path": "VendorEvaluated", "stableContract": true}, "name": "presentation_recovered", "observed": {"lastBothEyesVendorFrame": 66740, "leftPath": "NativeOriginal", "referenceFrame": 76528, "rightPath": "NativeOriginal", "stableVendorPresentation": true}, "passed": false}]`; all publication checks current/complete/matched: `True`. Retries: `{"render_target_relatch_requeued": 12}`. Presentation stretch: 26 completed frames, 16646.931 ms total; maximum 1 frames. Mean cleanup tail: 0.000 ms.
-   **run3:** failure deltas `{}`; failed final gates `[{"limit": {"bothEyes": true, "maximumAgeFrames": 2, "path": "VendorEvaluated", "stableContract": true}, "name": "presentation_recovered", "observed": {"lastBothEyesVendorFrame": 80985, "leftPath": "NativeOriginal", "referenceFrame": 84322, "rightPath": "NativeOriginal", "stableVendorPresentation": true}, "passed": false}]`; all publication checks current/complete/matched: `True`. Retries: `{"render_target_relatch_requeued": 10}`. Presentation stretch: 20 completed frames, 12817.349 ms total; maximum 1 frames. Mean cleanup tail: 0.000 ms.

Stretch is reported separately from rendering failure. A frame can span a loading stall, so a one-frame stretch can last substantially longer than one normal gameplay frame. The fixed frame cutoff is a diagnostic, not a substituted health verdict. Final stopped-record acceptance supersedes the expected running-capture gate failures; all original live and stopped gate receipts remain preserved.

The retry source sites identify provider-drain pending (`Upscaling.cpp:28832`) and vendor teardown retry (`Upscaling.cpp:29112`). Counts by site were 0/3, 7/5 and 7/3. These are safe-release waits in the resource transition path. The telemetry did not emit separate observed-wait milliseconds for those events, so no precise retry-time saving is inferred.

**Aggregate native-path finding (`CONTRACT_MISMATCH`):** the 5-second campaign ends in Dragonsreach with DLAA/native-sized presentation (`qualityMode=0`, `renderScaleMode=false`, both paths `NativeOriginal`). Its strict receipt separately proves same-frame DLSS execution in both eyes. The aggregate `presentation_recovered` gate nevertheless sets `stableVendorPresentation` from `stable.valid && IsVendorUpscalingMethod(stable.method)` and demands `VendorEvaluated` presentation. This predicate does not distinguish inactive render scaling/native presentation. See `src/Features/Upscaling.cpp:54991`, `55020`, and `55420`. The failed aggregate flag is preserved; it is explained by this telemetry predicate and does not demonstrate a failed strict transition. The third campaign also ends in Dragonsreach and is evaluated against the same distinction.

## CPU/GPU telemetry and memory

CPU queue times are the instrumented strong-stereo-packet queue, not grass child/group lock timing. The production grass implementation contains no comparison timers. GPU performance counters measure work and pixels, not whole-frame GPU milliseconds. Profiler times are individual instrumented scope timings from a 300-frame capture starting at the first transition; scopes can nest and must not be summed into a frame time. Run 3 starts toward Windhelm while runs 1 and 2 start toward Dragonsreach, so those profiler samples also differ in scene mix.

| Campaign | Queue wait mean/max (µs) | Queue hold mean/max (µs) | Stable validation hit rate | Private memory first→last observed (GiB) |
| -------- | -----------------------: | -----------------------: | -------------------------: | ---------------------------------------: |
| run1     |              0.143/1.400 |             1.482/18.400 |                     99.75% |                              24.39→25.41 |
| run2     |              0.133/1.500 |             1.461/39.000 |                     99.60% |                              24.91→25.12 |
| run3     |              0.130/1.400 |             1.548/29.400 |                     99.53% |                              25.16→26.81 |

The CPU table uses each campaign’s last measured transition snapshot, excluding later transcript-export idle time. Final capture counters and GPU counters include the extraction/cleanup tail and are labeled accordingly in the JSON. First/last memory observations can be in different cells; this delta alone is not a leak test. Full per-transition memory, backend-specific memory-trend gates, texture groups, CPU counters and GPU pixel counts are in the CSV/JSON.

The renderer memory-trend gate passed all runs. Its final 3-second-run same-profile consecutive-growth diagnostic was 20.26 MiB GPU, 49.82 MiB private, and 63.92 MiB system commit, below the 256 MiB gate. Memory did fluctuate and grow during portions of the run; this bounded result is not proof of leak freedom.

| Campaign | Profiler scope                | CPU mean (ms) | GPU mean (ms) | Capture        |
| -------- | ----------------------------- | ------------: | ------------: | -------------- |
| run1     | Upscaling::Upscale            |        0.6732 |        2.4253 | 300/300 frames |
| run1     | Upscaling::SubmitStageUpscale |        0.5763 |        0.5638 | 300/300 frames |
| run1     | ScreenSpaceGI::CenterGI       |        0.0008 |        0.7795 | 300/300 frames |
| run1     | Skylighting::OcclusionMask    |        0.2072 |        0.0503 | 300/300 frames |
| run2     | Upscaling::Upscale            |        0.6762 |        1.5088 | 300/300 frames |
| run2     | Upscaling::SubmitStageUpscale |        0.7010 |        1.2517 | 300/300 frames |
| run2     | ScreenSpaceGI::CenterGI       |        0.0005 |        0.4929 | 300/300 frames |
| run2     | Skylighting::OcclusionMask    |        0.1379 |        0.0292 | 300/300 frames |
| run3     | Upscaling::Upscale            |        0.3460 |        0.9317 | 300/300 frames |
| run3     | Upscaling::SubmitStageUpscale |        0.9716 |        1.9226 | 300/300 frames |
| run3     | ScreenSpaceGI::CenterGI       |        0.0013 |        0.9513 | 300/300 frames |
| run3     | Skylighting::OcclusionMask    |        0.1133 |        0.0150 | 300/300 frames |

## Preparation and evidence limits

Per-transition preparation records are matched to the observed renderer epoch. Full original rings, coalesced versions, stage summaries, retry reasons/source locations and publication receipts are preserved. For stages not entered, no timing is invented. The admission/early-exit outcome must be considered before interpreting absent prewarm/creator timings.

-   **run1:** emitted preparation stages: admission_check, early_exit, request_queued, total_preparation. Probe retained 4096 / overwritten 41888; black candidates 3742, white candidates 0; pending readbacks at stop 2. DLSS retained 256 / overwritten 85740; dropped 0. Texture tracker dropped records 0; outstanding 1091.
-   **run2:** emitted preparation stages: admission_check, early_exit, request_queued, total_preparation. Probe retained 4096 / overwritten 28655; black candidates 4555, white candidates 0; pending readbacks at stop 2. DLSS retained 256 / overwritten 57900; dropped 0. Texture tracker dropped records 0; outstanding 319.
-   **run3:** emitted preparation stages: admission_check, early_exit, request_queued, total_preparation. Probe retained 4096 / overwritten 10148; black candidates 3650, white candidates 0; pending readbacks at stop 2. DLSS retained 256 / overwritten 22480; dropped 0. Texture tracker dropped records 0; outstanding 325.

Bounded ring overwrites limit frame-by-frame retrospective inspection. Black probe candidates during COC loading are not proof of a visible gameplay defect. This is telemetry qualification, not a human stereo-image review. The separately pre-existing lifetime tracer stayed active (`performanceDistorted=true`), so all measurements are instrumented. The controller’s standalone temporal-probe guard reports neutral/not applicable; it does not negate that tracer overhead.

The first completed-transcript read exceeded the controller’s default 30-second processing deadline. The same read recovered with a larger budget; no COC or state mutation was replayed. Skyrim continued advancing. The 25-transition transcript then exposed a separate HTTP 404 after local semantic processing outlived its MCP session. A task-local copy of the bundled controller moved the existing after-guard read immediately after the live response, before offline classification; identity, both neutrality checks and failure classification remain enabled. The installed toolkit was unchanged. The exact patch and original hashes are under `controller-recovery/`. The same saved scenario was recovered without replaying mutations. Transcript processing and analysis introduced inter-campaign gaps; there was no repeated fixture/positioning or game restart. The controller itself revalidates the bound identity on every invocation. Local toolkit feedback: `AUTO-20260926-154728325-3F6DA5A3`, `AUTO-20260926-155812429-912798D2`, and native-path gate report `AUTO-20260926-161113945-D0F3AC52`.

## Historical references

These are the pinned PrePR19 and RC166-derived logging references, not the Nexus binary or a matched baseline for this run. Their per-eye output was 2468×2740 versus 1512×1680 here; build, instrumentation and measurement contracts differ. Numbers provide historical context and do not establish a controlled performance gain.

| Metric                                 | PrePR19 reference | RC166-derived reference | Run 1 |              Run 2 | Run 3 |
| -------------------------------------- | ----------------: | ----------------------: | ----: | -----------------: | ----: |
| dragonsreach_mean_stabilization_frames |              68.9 |                    24.0 |  21.3 | 22.384615384615383 |    22 |
| windhelm_mean_stabilization_frames     |             139.3 |                    20.0 |  65.3 |              65.75 |  62.3 |
| overall_mean_stabilization_frames      |             104.1 |                    22.0 |  43.3 |               43.2 | 42.15 |
| worst_stabilization_frames             |               142 |                      24 |    71 |                 71 |    65 |
| recoverable_retries                    |                10 |                      30 |     3 |                 12 |    10 |

## Evidence

[Complete numbered ledger](../vr-render-scale-ledger-0008-investigation.csv), [all 65 transition timings and route comparisons](transitions.csv), and [field coverage and shutdown receipt](coverage.json) are committed with the implementation. The original main-VR snapshot 0005 is retained byte-for-byte as snapshot 0008 to preserve the existing NR snapshots 0005 through 0007. Snapshot 0008 uses the existing investigation convention because this direct main-VR work has no assigned PR. The two historical reference columns are copied exactly from snapshot 0001; snapshots 0001–0004 are unchanged. Each detail.complete_summary cell reconstructs the entire finalized run summary, including original final and stopped records. The ledger also retains scalar transition timings, exact requests, provenance, comparisons and the clean shutdown receipt. Read structured cells as JSON and raise the CSV field limit to the ledger byte length. The tuning wrapper requires fixed-matrix worker summaries and does not consume this direct COC scenario format; offline exact reconstruction and numeric coverage were verified instead. Raw per-run evidence remains local under `build/validation/simple-coc-sequence-20260926-3baaf91b9/`.

Each `runN-analysis.json` contains the summary, all transition timings, final/stopped telemetry, profiler and health gates. `runN-latest.json` is the full measured transcript; `runN-final.json` and `runN-stop.json` preserve extraction and cleanup. `runN-request.json` preserves exact dispatch order and pacing. All controller journals are adjacent.

`receipt-audit.json` verifies every tool/action, producer, owner, transition, destination and requested wait. `stage-timings.json` retains binding/fixture, positioning, reads, resets, arms and first-dispatch timing brackets. The shared toolkit normalizers found no missing preparation or publication fields in any of the 65 transition observations.

At assay completion all owned captures were inactive, each profiler capture had completed 300/300 frames, and the profiler was restored to disabled. Skyrim was still running in the same PID with the player loaded; the pre-existing external lifetime tracer remained active and unclaimed. No runtime DLL/source change or game restart occurred.

The user subsequently requested shutdown. At 2026-09-26 16:35:33.575 UTC, the identity-bound DevBench console queued `qqq`. By 16:35:38.158 UTC the original process had exited and no Skyrim or SKSE loader process remained. No process was force-terminated. The exit code was unavailable, not reported as zero. MO2 remained open; no profile or deployment cleanup was requested. The exact shutdown receipt is retained in the ledger and coverage record.

## Per-transition comparison and assessment

Execution is COMPLETE and all 65 terminal strict receipts are satisfied. Task 2 was not run: this was the COC assay, not the render-scale tuning protocol. The production improvement-or-neutral assessment is INCONCLUSIVE because there is no matched uninstrumented baseline. The raw full-history aggregate results remain PASS / FAIL / FAIL, with both failures classified as the proven native-path contract mismatch described above.

W means WindhelmExterior01 and D means WhiterunDragonsreach. Deltas use the same route occurrence in run 1; run 2 has five additional transitions without a matching run-1 occurrence. Historical per-transition COC strict timings and render-latch values are unavailable in the selected reference columns; their aggregates are shown above. These pairings preserve differences without treating sequential scene/loading histories as controlled A/B samples. Every exact numeric value, route, measured source and Build ID is retained in the linked CSV and ledger.

| Run | Transition | Route | Strict ms | Strict frames | Render frames | Retries | Delta strict ms vs run 1 |
| --- | ---------: | ----- | --------: | ------------: | ------------: | ------: | -----------------------: |
| 1   |          1 | W → D | 6384.2829 |            26 |            17 |       0 |                  +0.0000 |
| 1   |          2 | D → W | 2045.5583 |            59 |            52 |       1 |                  +0.0000 |
| 1   |          3 | W → D | 2605.9441 |            30 |            21 |       0 |                  +0.0000 |
| 1   |          4 | D → W | 1950.7270 |            68 |            64 |       0 |                  +0.0000 |
| 1   |          5 | W → D | 2780.7291 |            30 |            21 |       0 |                  +0.0000 |
| 1   |          6 | D → W | 1918.5606 |            64 |            61 |       0 |                  +0.0000 |
| 1   |          7 | W → D | 2684.7501 |            32 |            23 |       0 |                  +0.0000 |
| 1   |          8 | D → W | 1911.2140 |            74 |            69 |       0 |                  +0.0000 |
| 1   |          9 | W → D | 2543.5342 |            31 |            22 |       0 |                  +0.0000 |
| 1   |         10 | D → W | 1984.5351 |            71 |            64 |       1 |                  +0.0000 |
| 1   |         11 | W → D | 2535.6872 |            30 |            22 |       0 |                  +0.0000 |
| 1   |         12 | D → W | 1975.9134 |            73 |            70 |       0 |                  +0.0000 |
| 1   |         13 | W → D | 2620.8157 |            29 |            20 |       0 |                  +0.0000 |
| 1   |         14 | D → W | 1967.5870 |            67 |            64 |       0 |                  +0.0000 |
| 1   |         15 | W → D | 2459.3395 |            31 |            22 |       0 |                  +0.0000 |
| 1   |         16 | D → W | 1899.6083 |            74 |            69 |       0 |                  +0.0000 |
| 1   |         17 | W → D | 2952.6132 |            30 |            22 |       0 |                  +0.0000 |
| 1   |         18 | D → W | 1902.4786 |            75 |            71 |       0 |                  +0.0000 |
| 1   |         19 | W → D | 2457.0522 |            31 |            23 |       0 |                  +0.0000 |
| 1   |         20 | D → W | 1976.2870 |            76 |            69 |       1 |                  +0.0000 |
| 2   |          1 | W → D | 2779.2032 |            31 |            23 |       0 |               -3605.0797 |
| 2   |          2 | D → W | 1994.9590 |            69 |            66 |       1 |                 -50.5993 |
| 2   |          3 | W → D | 3190.8902 |            27 |            18 |       0 |                +584.9461 |
| 2   |          4 | D → W | 2095.3816 |            73 |            65 |       1 |                +144.6546 |
| 2   |          5 | W → D | 2510.6717 |            32 |            23 |       0 |                -270.0574 |
| 2   |          6 | D → W | 2077.5036 |            73 |            65 |       1 |                +158.9430 |
| 2   |          7 | W → D | 2508.9946 |            32 |            23 |       0 |                -175.7555 |
| 2   |          8 | D → W | 2114.4214 |            74 |            69 |       1 |                +203.2074 |
| 2   |          9 | W → D | 2641.6761 |            29 |            21 |       0 |                 +98.1419 |
| 2   |         10 | D → W | 2018.2328 |            69 |            63 |       1 |                 +33.6977 |
| 2   |         11 | W → D | 2659.4221 |            32 |            23 |       0 |                +123.7349 |
| 2   |         12 | D → W | 1992.3642 |            70 |            64 |       1 |                 +16.4508 |
| 2   |         13 | W → D | 3140.0462 |            31 |            22 |       0 |                +519.2305 |
| 2   |         14 | D → W | 1978.6229 |            72 |            67 |       1 |                 +11.0359 |
| 2   |         15 | W → D | 2661.2142 |            32 |            22 |       0 |                +201.8747 |
| 2   |         16 | D → W | 2105.5785 |            74 |            67 |       1 |                +205.9702 |
| 2   |         17 | W → D | 2732.2578 |            32 |            24 |       0 |                -220.3554 |
| 2   |         18 | D → W | 1990.9851 |            75 |            71 |       1 |                 +88.5065 |
| 2   |         19 | W → D | 2551.1707 |            32 |            22 |       0 |                 +94.1185 |
| 2   |         20 | D → W | 1986.1293 |            70 |            67 |       1 |                  +9.8423 |
| 2   |         21 | W → D | 2622.3009 |            33 |            24 |       0 |                unmatched |
| 2   |         22 | D → W | 1965.3847 |            71 |            65 |       1 |                unmatched |
| 2   |         23 | W → D | 2926.3639 |            31 |            23 |       0 |                unmatched |
| 2   |         24 | D → W | 2019.2105 |            64 |            60 |       1 |                unmatched |
| 2   |         25 | W → D | 2758.4737 |            32 |            23 |       0 |                unmatched |
| 3   |          1 | D → W | 2258.2745 |            76 |            62 |       1 |                +212.7162 |
| 3   |          2 | W → D | 2655.0809 |            29 |            22 |       0 |               -3729.2020 |
| 3   |          3 | D → W | 2108.4137 |            66 |            61 |       1 |                +157.6867 |
| 3   |          4 | W → D | 2554.5010 |            29 |            21 |       0 |                 -51.4431 |
| 3   |          5 | D → W | 2076.7980 |            70 |            63 |       1 |                +158.2374 |
| 3   |          6 | W → D | 2480.6085 |            31 |            22 |       0 |                -300.1206 |
| 3   |          7 | D → W | 1980.6430 |            67 |            63 |       1 |                 +69.4290 |
| 3   |          8 | W → D | 2498.0143 |            30 |            22 |       0 |                -186.7358 |
| 3   |          9 | D → W | 2098.9033 |            69 |            61 |       1 |                +114.3682 |
| 3   |         10 | W → D | 2555.9984 |            30 |            21 |       0 |                 +12.4642 |
| 3   |         11 | D → W | 2107.2645 |            68 |            60 |       1 |                +131.3511 |
| 3   |         12 | W → D | 2644.8942 |            31 |            22 |       0 |                +109.2070 |
| 3   |         13 | D → W | 2102.0330 |            66 |            60 |       1 |                +134.4460 |
| 3   |         14 | W → D | 2597.3317 |            31 |            22 |       0 |                 -23.4840 |
| 3   |         15 | D → W | 1964.7373 |            69 |            65 |       1 |                 +65.1290 |
| 3   |         16 | W → D | 2659.9911 |            29 |            21 |       0 |                +200.6516 |
| 3   |         17 | D → W | 1980.2320 |            67 |            63 |       1 |                 +77.7534 |
| 3   |         18 | W → D | 2498.9553 |            31 |            24 |       0 |                -453.6579 |
| 3   |         19 | D → W | 1995.8987 |            70 |            65 |       1 |                 +19.6117 |
| 3   |         20 | W → D | 2625.7082 |            32 |            23 |       0 |                +168.6560 |
