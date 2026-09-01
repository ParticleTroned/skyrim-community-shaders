# NVIDIA render-scale tuning: PR73 exact parent, September 11

Run `renderscale-tuning-nvidia-20260911T153415138Z` completed the 33-transition NVIDIA matrix twice in
one game process, PID `11992`. Execution is COMPLETE;
terminal render counts are 66 PASS / 0 FAIL, scoped
only to the terminal condition. Task 2 retains per-transition counts
66 PASS / 0 FAIL / 0 INCONCLUSIVE,
with no aggregate Task 2 verdict. Full-history switch health is
`NO_COUNTED_FAILURES` and both passes meet applicable health
checks. Reporting is COMPLETE; formal change assessment is separately
INCONCLUSIVE.

The user-pinned reference is measured PR66
`renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z`, compiled source and renderer base
`a09e1cc77de098f85e74e6d5bb341dc184f83640`, main-VR base
`bf4ae54a7d49620c41cb32ee9ecfd44657688ead`. The candidate is the clean
PR73 parent source and renderer base `bc077786db08637eec8b4c3f718e971e58700a60`,
main-VR base `ef7c366dd73989b2b87751c0ef975db7c6fd310f`, with no reporting backport.
The exact identities, Build IDs and DLL hashes are compared below.

Mean strict completion changed from 793.336 to
942.965 ms in pass 1 (+18.861%) and from
807.126 to 807.993 ms in pass 2
(+0.107%). The five-second server wait before each dispatch
is excluded. Rows 1, 2, 3, 5, 6, 8, 9, 10, 11, 15, 16, 18, 19, 22, 28, 30, 31, 32 are slower in both
passes. The route and pass tables retain each observed delta; the two
ordered passes do not establish statistical significance.

Scene and toolchain context differ, a matching complete fixture fingerprint
is unavailable, and no explicit versioned tolerance policy was supplied.
These limits keep improvement-or-neutral assessment INCONCLUSIVE despite
the observed timing changes. Memory evidence is complete; the memory
verdict is `inconclusive`. The retained boundary values and predicates
do not establish leak freedom.

All counted device-loss, OOM, producer-terminal, DLSS/FSR lifecycle,
fidelity, vendor-failure, bounds-fallback, memory-trim and retirement-fence
failure observations are zero in both passes. Retry counts are
10 / 9; reasons and observed wait
intervals remain alongside every transition below. All
32 selected stretch
transitions recovered, with
0 unrecovered.
Full-pass stretch totals are 18 /
18 episodes, 84 /
80 frames and
5988.474 /
5198.280 ms, with no active tail.

Raw cumulative acceptance remains false in both passes. The fixed
two-frame stretch cutoff is retained as DIAGNOSTIC_ONLY because settling
imposes stretch; the scaled-presentation gate at the proven native terminal
target remains a CONTRACT_MISMATCH. Their exact observations and limits
are retained below. There are no new applicable failed gates or new
counted failure rows relative to PR66.

| Failure category                    | Recorded count |
| ----------------------------------- | -------------: |
| Device loss                         |              0 |
| Out of memory                       |              0 |
| Producer terminal failure           |              0 |
| Vendor-native qualification failure |              0 |
| Credible liveness timeout           |              0 |

## Physical runtime identity

The 28,173,824-byte physical DLL, adjacent manifest and AIO
build receipt match runtime Build ID `0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1`,
clean Release source `bc077786db08637eec8b4c3f718e971e58700a60` and SHA-256
`47fe6e5f0283ad05d5bd8b9c8ddfe4e22b801dd091bca81050600b9fd2a8e116`. The selected profile has exactly one enabled
loose DLL provider; direct checks find no competing DLL in Overwrite or
unmanaged Data. The [verification receipt](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z/runtime-dll-verification.json)
retains the exact physical paths, source audit, complete compile identity,
manifest, receipt and archive identities.

Producer metadata still has `manifestVerified: false` and an empty
`artifactSha256`, with verification delegated to the deployment harness.
The separate physical audit supplies DLL/manifest/AIO proof correlated by
Build ID and source commit; it does not claim an in-memory module hash.
The original AIO archive integrity result is retained from its build
receipt; this verification did not repeat archive extraction or testing.

## Evidence, diagnostics and validation

Owned captures are verified inactive and the journal is flushed with zero
pending evidence. Client pacing is VALID; the maximum
extra dispatch gap was 43.339 ms
against the 250 ms diagnostic budget.
No measured transition was replayed.

A separate [client orchestration note](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z/client-orchestration-note.json)
records a startup sequence deviation: the client enumerated local tool
metadata in the first skill-read cell before preparation. It introduced
zero extra DevBench calls, mutations or measured dispatch gaps. This
client diagnostic leaves the recorded runtime results unchanged.

Both pass profiler summaries have zero timers, zero captured frames and
no resolved samples. Their zero totals are unavailable CPU/GPU timings;
they cannot support GPU-cost or FPS claims. Feedback
`AUTO-20260911-154621563-951D86E2` retains the profiler reporting defect.
No owned provider-drain attempts were derived; each transition retains its
diagnostic status. The complete evidence journal and all available values
remain preserved; missing telemetry is not replaced with zero cost.

The [canonical comparison ledger](vr-render-scale-ledger.md)
retains every finalized summary and comparison field, both passes, all
66 transitions and supplemental provenance
and diagnostics. Exact field-for-field reconstruction passed for summary,
comparison and supplemental fields. All
1,056 paired numeric timing cells were
audited, including 528 candidate
cells, and every historical cell was preserved. The append increased the
ledger from 28 to
29 columns and from
1,211 to
1,213 metric rows.

The maintained comparison wrapper ran once in
4.431 s. Its stages were tool identity
0.099 s, comparison
2.607 s and timing audit
1.610 s. Complete ledger
preparation, comparison, reconstruction and atomic append took
17.037 s in total, including the wrapper.
Finalization elapsed time was not separately instrumented with a monotonic
clock and is unavailable; it was neither inferred nor replayed.

-   [Complete ledger coverage and timing validation](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z/complete-ledger-validation.json)
-   [Finalized summary](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z/summary.json), [transition CSV](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z/transitions.csv), [receipt index](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z/receipt-index.json)
-   [Full scalar evidence export](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z/evidence-values.csv)
-   [Comparison JSON](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z-comparison/comparison.json), [CSV](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z-comparison/comparison.csv), [stage timings](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z-comparison/reporting-performance.json)
-   [Exact comparison command](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z/comparison-command.json) and [result](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T153415138Z/comparison-command-result.json)

Raw evidence remains local. PR inclusion remains the user's decision.

## Strict completion distributions and affected routes

B/C means the pinned PR66 baseline / PR73-parent candidate. All values are
milliseconds; deltas are candidate minus baseline. Full precision remains
in the ledger and JSON.

| Pass | Statistic | Baseline ms | Candidate ms |  Delta ms | Delta % |
| ---- | --------- | ----------: | -----------: | --------: | ------: |
| 1    | total     |   26180.089 |    31117.846 | +4937.757 | +18.861 |
| 1    | mean      |     793.336 |      942.965 |  +149.629 | +18.861 |
| 1    | median    |     751.644 |      877.031 |  +125.387 | +16.682 |
| 1    | p95       |    1389.267 |     1857.869 |  +468.602 | +33.730 |
| 1    | maximum   |    1856.757 |     2176.467 |  +319.710 | +17.219 |
| 2    | total     |   26635.148 |    26663.773 |   +28.625 |  +0.107 |
| 2    | mean      |     807.126 |      807.993 |    +0.867 |  +0.107 |
| 2    | median    |     771.931 |      788.261 |   +16.330 |  +2.115 |
| 2    | p95       |    1444.193 |     1307.828 |  -136.365 |  -9.442 |
| 2    | maximum   |    1932.147 |     1871.062 |   -61.084 |  -3.161 |

The three largest strict-time increases in each pass are:

| Pass | Row | Route                             | Delta ms | Delta % |
| ---- | --- | --------------------------------- | -------: | ------: |
| 1    | 14  | FSR3 Native AA -> FSR3 Hoshipa    | +825.483 | +61.102 |
| 1    | 18  | FSR3 Balanced -> FSR3 Performance | +656.759 | +86.317 |
| 1    | 15  | FSR3 Hoshipa -> FSR3 UQ           | +444.392 | +49.177 |
| 2    | 15  | FSR3 Hoshipa -> FSR3 UQ           | +444.418 | +49.591 |
| 2    | 30  | DLSS UP -> TAA                    | +154.089 | +22.120 |
| 2    | 26  | DLSS Hoshipa -> FSR3 Hoshipa      | +137.273 | +15.230 |

## Presentation and cleanup deltas for every paired transition

Every row is MATCHED by pass and route. The complete comparison below
contains the corresponding B/C endpoint values, phase durations, request
and epoch ownership, retry evidence, actual relatch/strict frames and
milliseconds, stretch episodes, counters and gates. Here each delta is
candidate minus baseline; n.d. percentage preserves an unavailable
comparison such as a zero baseline and does not imply zero change.

| Pass | Row | Presentation delta ms | Delta % | Cleanup delta ms |  Delta % | Cleanup tail delta ms |  Delta % |
| ---- | --- | --------------------: | ------: | ---------------: | -------: | --------------------: | -------: |
| 1    | 1   |               -30.184 |  -6.017 |          +27.727 |   +4.076 |               +57.911 |  +32.421 |
| 1    | 2   |               +28.608 | +16.881 |          +28.608 |  +16.881 |                +0.000 |     n.d. |
| 1    | 3   |              +131.547 | +52.571 |         +131.547 |  +52.571 |                +0.000 |     n.d. |
| 1    | 4   |              +146.271 | +18.528 |         +252.832 |  +30.472 |              +106.562 | +264.552 |
| 1    | 5   |              +186.069 | +19.686 |         +218.240 |  +20.426 |               +32.171 |  +26.101 |
| 1    | 6   |              +243.592 | +27.758 |         +274.772 |  +27.336 |               +31.180 |  +24.435 |
| 1    | 7   |               +53.800 |  +4.924 |          +62.059 |   +5.055 |                +8.259 |   +6.119 |
| 1    | 8   |              +176.483 | +16.834 |         +192.485 |  +16.217 |               +16.001 |  +11.548 |
| 1    | 9   |              +102.890 |  +9.972 |          +96.992 |   +8.257 |                -5.898 |   -4.130 |
| 1    | 10  |              +135.826 | +22.999 |         +211.581 |  +27.809 |               +75.755 |  +44.492 |
| 1    | 11  |               +20.324 | +10.307 |          +67.666 |  +14.607 |               +47.342 |  +17.795 |
| 1    | 12  |               +14.335 |  +8.371 |          +14.335 |   +8.371 |                +0.000 |     n.d. |
| 1    | 13  |              +308.309 | +41.264 |         +308.309 |  +41.264 |                +0.000 |     n.d. |
| 1    | 14  |              +954.024 | +78.042 |         +817.364 |  +62.574 |               -83.792 | -100.000 |
| 1    | 15  |              +444.392 | +49.177 |         +529.817 |  +94.280 |                +0.000 |     n.d. |
| 1    | 16  |              +103.590 | +14.155 |           +1.066 |   +0.177 |                +0.000 |     n.d. |
| 1    | 17  |               +71.981 | +10.005 |          +65.527 |  +11.155 |                +0.000 |     n.d. |
| 1    | 18  |              +656.759 | +86.317 |         +652.102 | +103.405 |                +0.000 |     n.d. |
| 1    | 19  |               +95.673 | +12.244 |          +88.070 |  +13.540 |                +0.000 |     n.d. |
| 1    | 20  |               -26.762 |  -5.041 |           +2.948 |   +0.399 |               +29.710 |  +14.222 |
| 1    | 21  |                +8.815 |  +4.289 |          +27.560 |   +5.946 |               +18.744 |   +7.267 |
| 1    | 22  |                +4.126 |  +2.419 |           +4.126 |   +2.419 |                +0.000 |     n.d. |
| 1    | 23  |                -4.768 |  -1.812 |           -4.768 |   -1.812 |                +0.000 |     n.d. |
| 1    | 24  |              -169.297 | -17.048 |         -154.268 |  -13.022 |               +15.029 |   +7.843 |
| 1    | 25  |               -14.698 |  -0.893 |          +24.736 |   +1.394 |               +39.434 |  +30.660 |
| 1    | 26  |               -12.416 |  -1.545 |           -3.832 |   -0.431 |                +8.584 |  +10.048 |
| 1    | 27  |               +92.623 | +18.868 |         +106.084 |  +14.114 |               +13.461 |   +5.163 |
| 1    | 28  |              +201.294 | +24.838 |          +98.285 |  +11.416 |               -50.538 | -100.000 |
| 1    | 29  |              +394.830 | +32.406 |         +390.878 |  +28.826 |                -3.952 |   -2.872 |
| 1    | 30  |               -21.982 |  -4.467 |          +25.256 |   +3.709 |               +47.239 |  +25.010 |
| 1    | 31  |              +171.470 | +29.697 |         +171.470 |  +29.697 |                +0.000 |     n.d. |
| 1    | 32  |               +77.904 | +42.228 |          +99.125 |  +22.010 |               +21.221 |   +7.982 |
| 1    | 33  |               -20.659 |  -7.478 |          -20.659 |   -7.478 |                +0.000 |     n.d. |
| 2    | 1   |               +25.082 |  +4.780 |          +23.612 |   +3.088 |                -1.470 |   -0.613 |
| 2    | 2   |               +20.945 | +12.492 |          +20.945 |  +12.492 |                +0.000 |     n.d. |
| 2    | 3   |               +13.925 |  +5.629 |          +13.925 |   +5.629 |                +0.000 |     n.d. |
| 2    | 4   |               -12.143 |  -1.487 |          -20.417 |   -2.134 |                -8.274 |   -5.900 |
| 2    | 5   |                +6.807 |  +0.838 |           +1.780 |   +0.187 |                -5.027 |   -3.569 |
| 2    | 6   |               +10.188 |  +1.057 |           +1.839 |   +0.166 |                -8.349 |   -5.767 |
| 2    | 7   |               +43.149 |  +4.468 |          -13.128 |   -1.179 |               -56.278 |  -38.138 |
| 2    | 8   |               +97.417 | +10.310 |          +92.008 |   +8.380 |                -5.409 |   -3.535 |
| 2    | 9   |              +103.777 | +11.629 |         +100.464 |   +9.725 |                -3.313 |   -2.355 |
| 2    | 10  |               +27.561 |  +4.558 |          +49.960 |   +6.411 |               +22.399 |  +12.824 |
| 2    | 11  |               +12.197 |  +7.467 |          +27.836 |   +6.390 |               +15.638 |   +5.743 |
| 2    | 12  |                -5.832 |  -3.178 |           -5.832 |   -3.178 |                +0.000 |     n.d. |
| 2    | 13  |               -26.601 |  -4.148 |          -26.601 |   -4.148 |                +0.000 |     n.d. |
| 2    | 14  |              -352.855 | -30.117 |         -457.548 |  -34.565 |              -104.693 |  -68.831 |
| 2    | 15  |              +444.418 | +49.591 |         +646.557 | +115.666 |                +0.000 |     n.d. |
| 2    | 16  |               +64.378 |  +9.158 |          +19.135 |   +3.128 |                +0.000 |     n.d. |
| 2    | 17  |                -5.403 |  -0.700 |          +34.405 |   +5.779 |                +0.000 |     n.d. |
| 2    | 18  |               +21.900 |  +2.915 |          +17.301 |   +2.792 |                +0.000 |     n.d. |
| 2    | 19  |               +58.565 |  +8.052 |          +97.706 |  +17.563 |                +0.000 |     n.d. |
| 2    | 20  |               -28.054 |  -3.111 |          -20.486 |   -1.900 |                +7.567 |   +4.295 |
| 2    | 21  |                -3.092 |  -1.525 |          -25.937 |   -5.172 |               -22.845 |   -7.650 |
| 2    | 22  |               +10.820 |  +6.368 |          +10.820 |   +6.368 |                +0.000 |     n.d. |
| 2    | 23  |               +13.799 |  +5.504 |          +13.799 |   +5.504 |                +0.000 |     n.d. |
| 2    | 24  |              -269.213 | -28.176 |         -262.377 |  -23.148 |                +6.837 |   +3.841 |
| 2    | 25  |              -533.824 | -40.329 |         -501.148 |  -34.375 |               +32.676 |  +24.346 |
| 2    | 26  |              +111.385 | +14.594 |          +76.143 |   +8.448 |               -35.242 |  -25.516 |
| 2    | 27  |               -40.460 |  -7.227 |          -60.049 |   -7.329 |               -19.589 |   -7.549 |
| 2    | 28  |               +41.382 |  +4.873 |          +59.493 |   +6.341 |               +18.110 |  +20.335 |
| 2    | 29  |              -116.110 |  -6.588 |          -63.540 |   -3.434 |               +52.569 |  +59.962 |
| 2    | 30  |              +104.726 | +22.536 |         +154.089 |  +22.120 |               +49.363 |  +21.285 |
| 2    | 31  |               +28.073 |  +4.250 |          +28.073 |   +4.250 |                +0.000 |     n.d. |
| 2    | 32  |               +49.008 | +25.694 |         +117.907 |  +25.199 |               +68.899 |  +24.859 |
| 2    | 33  |                +5.984 |  +2.365 |           +5.984 |   +2.365 |                +0.000 |     n.d. |

## Retained finalizer report

-   Assay execution: **COMPLETE**
-   Transitions dispatched: **66/66**
-   Terminal render verdict: **PASS** (terminal condition only)
-   Lane qualification: **NOT_APPLICABLE**
-   Full-history switch health: **NO_COUNTED_FAILURES**
-   Change assessment: **INCONCLUSIVE**
-   Non-stable terminal notes: **0**
-   Task 2/evidence: **per transition** (66 PASS, 0 FAIL, 0 INCONCLUSIVE)
-   Reporting completeness: **COMPLETE**
-   Deployment verification: **COMPLETE**
-   Memory confirmation: **inconclusive**
-   Presentation stretch: **32 selected, 32 recovered PASS, 0 unrecovered**

Task 2 is deliberately not aggregated. Reporting failure does not rewrite the render result, and a render pass does not hide missing per-transition evidence. Every raw JSON value is available in `evidence-values.csv`.

### Per-pass switch health and performance

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms   | Max ms   | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | -------- | -------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 942.965        | 1857.869 | 2176.467 | 15.333             | 898.438         | 13.840              | 18               | 84             | 5988.474   | 10      | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 807.993        | 1307.828 | 1871.062 | 14.909             | 740.277         | 13.280              | 18               | 80             | 5198.280   | 9       | 0        | 0                   | MET             | COMPLETE |

#### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":23018,"leftPath":"NativeOriginal","referenceFrame":23405,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

#### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":27488,"leftPath":"NativeOriginal","referenceFrame":27855,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

### Memory confirmation

#### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    16677.785 |  16563.484 |     -114.301 |      16895.641 |    16898.891 |           3.25 |    16961.676 |  16603.906 |      -357.77 |                   n.d. |
| System commit MiB                  |    56653.965 |   56801.75 |      147.785 |      57152.164 |    57240.668 |         88.504 |    57297.879 |  56803.023 |     -494.855 |                 -3.348 |
| DXGI process usage MiB             |     4248.891 |   3696.676 |     -552.215 |       3954.238 |      3847.77 |       -106.469 |     3917.176 |   3556.426 |      -360.75 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        267 |          267 |            267 |          267 |              0 |            0 |        209 |          209 |                  0.783 |
| Estimated live tracked texture MiB |            0 |   2454.422 |     2454.422 |       2454.422 |     2454.422 |              0 |            0 |   2270.931 |     2270.931 |                  0.925 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -114.30078125,
        "systemCommitMiB": 147.78515625,
        "dxgiUsageMiB": -552.21484375,
        "liveTextures": 267,
        "liveTextureMiB": 2454.422275543213
    },
    "pass2": {
        "processPrivateMiB": -357.76953125,
        "systemCommitMiB": -494.85546875,
        "dxgiUsageMiB": -360.75,
        "liveTextures": 209,
        "liveTextureMiB": 2270.93111038208
    },
    "positivePass1PrivateAndCommit": false,
    "pass2ResourceGrowth": true,
    "retentionPredicate": false,
    "initializationPredicate": false
}
```

Conclusion: classification_is_not_proof_for_or_against_a_leak. Growth is relative to the freshly reset tracker in each pass; session IDs are retained.

Evidence gaps: none.

### Owned provider drain and commit intervals

Cells show frames / milliseconds from exact retained QPC endpoints. Ready means the last observed provider-ready event before the first commit; the required-provider set is not exposed. Commit-to-shared-cleanup is elapsed time to that marker, not isolated cleanup cost. Subsequent commit attempts, raw poll counts and missing identity fields remain in summary.json and CSV. The six-frame settling guard is reported separately.

No owned drain attempts were derived; see each row's diagnostic status.

### Transitions

Retry waits end at the first observed preparation-ready result. Ready-to-candidate includes stereo qualification and the settling guard; it is not isolated retry overhead. Overlapping viewport waits are not added. Unavailable results are n.d. and the affected reporting outcome is n/a. All retrieved values remain in raw evidence and evidence-values.csv; reporting provenance never aborts or replays measurements.

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                            | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery   | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | -------------------------------------------------------- | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ------------------ | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 1              | 19501/471.4447 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                            | 125.746 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 19845/935.7169 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                            | 127.714 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 19961/1131.2481 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 62.822 ms; SubmitStageFoveatedCenter: 62.782 ms | 275.044 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 20089/1121.1535 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 60.098 ms; SubmitStageFoveatedCenter: 60.041 ms | 226.511 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 20211/1146.4136 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.218 ms                                        | 307.660 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 20333/1224.8502 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 56.110 ms; SubmitStageFoveatedCenter: 56.060 ms | 229.896 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 20467/1134.7335 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 68 records / 3 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 6              | 21028/2176.4666 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 21118/1348.0426 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 21231/835.4273 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 21349/791.4178 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 13             | 21485/1417.6243 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 21613/877.0313 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 387.486 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 22359/1631.044 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 22489/791.0432 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 274.921 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 22889/1613.2047 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                            | 127.524 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 24164/804.4502 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                            | 106.248 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 2              | 24292/818.9428 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 52.225 ms; SubmitStageFoveatedCenter: 52.172 ms | 242.605 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 24427/973.8713 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 56.688 ms; SubmitStageFoveatedCenter: 56.618 ms | 238.665 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 24562/1008.9341 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 62.424 ms; SubmitStageFoveatedCenter: 62.335 ms | 256.001 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 24697/1042.2952 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 56.339 ms; SubmitStageFoveatedCenter: 56.303 ms | 233.867 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 24831/996.1836 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 25458/818.7646 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 13             | 25591/1340.5901 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 25718/767.3688 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 25848/766.5286 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 25976/773.0621 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 26097/785.8886 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 26227/873.7844 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 0       | complete                       | none observed                                            | 118.482 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 26832/789.8575 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 26962/874.6313 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 283.105 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 27360/1646.3925 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |

### Presentation stretch anomalies

| Lane   | Pass | Row | From                                                      | To                                                                           | Consecutive frames | Recovered PASS |
| ------ | ---: | --: | --------------------------------------------------------- | ---------------------------------------------------------------------------- | -----------------: | -------------- |
| nvidia |    1 |   1 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"method":"none","qualityMode":0,"renderScaleMode":false}                    |                  1 | yes            |
| nvidia |    1 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    1 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    1 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    1 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                 13 | yes            |
| nvidia |    1 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    1 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    1 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |
| nvidia |    2 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    2 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    2 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    2 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    2 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                 13 | yes            |
| nvidia |    2 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    2 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    2 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    2 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    2 |  20 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":0,"renderScaleMode":false} |                  5 | yes            |
| nvidia |    2 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |

## Complete comparison with the pinned PR66 reference

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                        | Candidate                                                                                                  |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z                                                              | renderscale-tuning-nvidia-20260911T153415138Z                                                              |
| Renderer base           | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | bc077786db08637eec8b4c3f718e971e58700a60                                                                   |
| Main-VR base/equivalent | bf4ae54a7d49620c41cb32ee9ecfd44657688ead                                                                        | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                   |
| Compiled source         | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | bc077786db08637eec8b4c3f718e971e58700a60                                                                   |
| Build ID                | 9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757                                                | 0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1                                           |
| DLL SHA-256             | aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461                                                | 47fe6e5f0283ad05d5bd8b9c8ddfe4e22b801dd091bca81050600b9fd2a8e116                                           |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-20260911T153415138Z |

Assessment limits: retained_context_not_matched:scene; retained_context_not_matched:toolchain; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

### Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 793.336/942.965 | 18.861       | 9/10        | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 807.126/807.993 | 0.107        | 10/9        | 0/0          | 0/0                 | none             | MET/MET             |

### Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 735.095   | 898.438   | 163.343  | 22.221  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.400    | 13.840    | 0.440    | 3.284   |
| nvidia | 1    | Relatch proof total        | ms          | 18377.376 | 22460.946 | 4083.570 | 22.221  |
| nvidia | 1    | Relatch proof total        | frames      | 335       | 346       | 11       | 3.284   |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 793.336   | 942.965   | 149.629  | 18.861  |
| nvidia | 1    | Strict completion mean     | frames      | 15.091    | 15.333    | 0.242    | 1.606   |
| nvidia | 1    | Strict completion total    | ms          | 26180.089 | 31117.846 | 4937.757 | 18.861  |
| nvidia | 1    | Strict completion total    | frames      | 498       | 506       | 8        | 1.606   |
| nvidia | 1    | Stretch completed episodes | episodes    | 17        | 18        | 1        | 5.882   |
| nvidia | 1    | Stretch completed total    | frames      | 69        | 84        | 15       | 21.739  |
| nvidia | 1    | Stretch completed total    | ms          | 4143.532  | 5988.474  | 1844.941 | 44.526  |
| nvidia | 1    | Stretch longest episode    | ms          | 376.746   | 726.413   | 349.667  | 92.812  |
| nvidia | 2    | Relatch proof mean         | ms          | 747.347   | 740.277   | -7.069   | -0.946  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.760    | 13.280    | -0.480   | -3.488  |
| nvidia | 2    | Relatch proof total        | ms          | 18683.664 | 18506.929 | -176.735 | -0.946  |
| nvidia | 2    | Relatch proof total        | frames      | 344       | 332       | -12      | -3.488  |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 807.126   | 807.993   | 0.867    | 0.107   |
| nvidia | 2    | Strict completion mean     | frames      | 15.303    | 14.909    | -0.394   | -2.574  |
| nvidia | 2    | Strict completion total    | ms          | 26635.148 | 26663.773 | 28.625   | 0.107   |
| nvidia | 2    | Strict completion total    | frames      | 505       | 492       | -13      | -2.574  |
| nvidia | 2    | Stretch completed episodes | episodes    | 18        | 18        | 0        | 0       |
| nvidia | 2    | Stretch completed total    | frames      | 78        | 80        | 2        | 2.564   |
| nvidia | 2    | Stretch completed total    | ms          | 4955.678  | 5198.280  | 242.602  | 4.895   |
| nvidia | 2    | Stretch longest episode    | ms          | 408.503   | 682.294   | 273.791  | 67.023  |

### nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 680.250 / 707.976   | 27.727   | 4.076   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.466 / 198.074   | 28.608   | 16.881  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 250.227 / 381.775   | 131.547  | 52.571  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 911.526 / 1179.536  | 268.010  | 29.402  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1151.036 / 1379.204 | 228.168  | 19.823  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1085.145 / 1384.794 | 299.648  | 27.614  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1311.362 / 1377.838 | 66.476   | 5.069   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1267.597 / 1472.654 | 205.057  | 16.177  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1257.584 / 1355.919 | 98.336   | 7.819   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 760.848 / 972.429   | 211.581  | 27.809  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 463.236 / 530.903   | 67.666   | 14.607  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 171.239 / 185.574   | 14.335   | 8.371   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 747.166 / 1055.475  | 308.309  | 41.264  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1350.983 / 2176.467 | 825.483  | 61.102  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 903.650 / 1348.043  | 444.392  | 49.177  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 731.838 / 835.427   | 103.590  | 14.155  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 719.437 / 791.418   | 71.981   | 10.005  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 760.866 / 1417.624  | 656.759  | 86.317  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 781.359 / 877.031   | 95.673   | 12.244  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 739.768 / 742.716   | 2.948    | 0.399   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 463.465 / 491.025   | 27.560   | 5.946   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 170.549 / 174.675   | 4.126    | 2.419   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 263.125 / 258.357   | -4.768   | -1.812  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1184.660 / 1030.392 | -154.268 | -13.022 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1856.757 / 1900.786 | 44.029   | 2.371   | 2/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 932.906 / 932.801   | -0.105   | -0.011  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 751.644 / 857.728   | 106.084  | 14.114  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 910.661 / 1011.710  | 101.049  | 11.096  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1446.692 / 1829.258 | 382.566  | 26.444  | 1/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 681.036 / 706.292   | 25.256   | 3.709   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 577.394 / 748.864   | 171.470  | 29.697  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 450.355 / 549.479   | 99.125   | 22.010  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 276.262 / 255.603   | -20.659  | -7.478  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 501.629 / 471.445   | 680.250 / 707.976   | 178.621 / 236.532 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4113,"dispatchToBlockedOrPreparationMs":331.8357,"firstNewGenerationToCleanupDrainedMs":228.8014,"firstPhysicalMutationToFirstNewGenerationMs":116.2013,"presentationToStrictCompletionMs":178.6206}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7737,"dispatchToBlockedOrPreparationMs":347.7917,"firstNewGenerationToCleanupDrainedMs":236.9509,"firstPhysicalMutationToFirstNewGenerationMs":119.46,"presentationToStrictCompletionMs":236.5316}    |
| 2   | 169.466 / 198.074   | 169.466 / 198.074   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":198.0738,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 250.227 / 381.775   | 250.227 / 381.775   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2275,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":381.7747,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 789.446 / 935.717   | 829.726 / 1082.559  | 40.280 / 146.842  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3058,"dispatchToBlockedOrPreparationMs":343.3656,"firstNewGenerationToCleanupDrainedMs":159.3035,"firstPhysicalMutationToFirstNewGenerationMs":323.7512,"presentationToStrictCompletionMs":122.0799}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2346,"dispatchToBlockedOrPreparationMs":513.2194,"firstNewGenerationToCleanupDrainedMs":195.7392,"firstPhysicalMutationToFirstNewGenerationMs":369.3653,"presentationToStrictCompletionMs":243.819}   |
| 5   | 945.179 / 1131.248  | 1068.436 / 1286.677 | 123.257 / 155.429 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4538,"dispatchToBlockedOrPreparationMs":359.0552,"firstNewGenerationToCleanupDrainedMs":163.5892,"firstPhysicalMutationToFirstNewGenerationMs":542.3382,"presentationToStrictCompletionMs":205.8572}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8716,"dispatchToBlockedOrPreparationMs":438.4427,"firstNewGenerationToCleanupDrainedMs":206.8728,"firstPhysicalMutationToFirstNewGenerationMs":637.4898,"presentationToStrictCompletionMs":247.9556}  |
| 6   | 877.562 / 1121.153  | 1005.163 / 1279.935 | 127.602 / 158.782 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9724,"dispatchToBlockedOrPreparationMs":353.8686,"firstNewGenerationToCleanupDrainedMs":174.4377,"firstPhysicalMutationToFirstNewGenerationMs":472.8845,"presentationToStrictCompletionMs":207.5839}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4136,"dispatchToBlockedOrPreparationMs":445.4328,"firstNewGenerationToCleanupDrainedMs":216.4203,"firstPhysicalMutationToFirstNewGenerationMs":613.6686,"presentationToStrictCompletionMs":263.6403}  |
| 7   | 1092.614 / 1146.414 | 1227.587 / 1289.646 | 134.973 / 143.232 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4825,"dispatchToBlockedOrPreparationMs":365.9809,"firstNewGenerationToCleanupDrainedMs":192.2705,"firstPhysicalMutationToFirstNewGenerationMs":665.8528,"presentationToStrictCompletionMs":218.7486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4567,"dispatchToBlockedOrPreparationMs":385.1714,"firstNewGenerationToCleanupDrainedMs":202.0943,"firstPhysicalMutationToFirstNewGenerationMs":698.9233,"presentationToStrictCompletionMs":231.4245}  |
| 8   | 1048.367 / 1224.850 | 1186.924 / 1379.409 | 138.557 / 154.559 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8603,"dispatchToBlockedOrPreparationMs":343.9502,"firstNewGenerationToCleanupDrainedMs":190.2907,"firstPhysicalMutationToFirstNewGenerationMs":648.823,"presentationToStrictCompletionMs":219.2301}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9449,"dispatchToBlockedOrPreparationMs":428.605,"firstNewGenerationToCleanupDrainedMs":204.3079,"firstPhysicalMutationToFirstNewGenerationMs":742.5509,"presentationToStrictCompletionMs":247.8036}   |
| 9   | 1031.843 / 1134.734 | 1174.671 / 1271.663 | 142.827 / 136.929 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3977,"dispatchToBlockedOrPreparationMs":360.4948,"firstNewGenerationToCleanupDrainedMs":184.3618,"firstPhysicalMutationToFirstNewGenerationMs":626.4163,"presentationToStrictCompletionMs":225.7405}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0818,"dispatchToBlockedOrPreparationMs":416.3309,"firstNewGenerationToCleanupDrainedMs":179.359,"firstPhysicalMutationToFirstNewGenerationMs":671.8909,"presentationToStrictCompletionMs":221.1857}   |
| 10  | 590.580 / 726.406   | 760.848 / 972.429   | 170.268 / 246.024 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5661,"dispatchToBlockedOrPreparationMs":360.7792,"firstNewGenerationToCleanupDrainedMs":214.9309,"firstPhysicalMutationToFirstNewGenerationMs":181.5719,"presentationToStrictCompletionMs":170.2681}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8964,"dispatchToBlockedOrPreparationMs":433.3119,"firstNewGenerationToCleanupDrainedMs":303.892,"firstPhysicalMutationToFirstNewGenerationMs":230.3289,"presentationToStrictCompletionMs":246.0236}   |
| 11  | 197.187 / 217.511   | 463.236 / 530.903   | 266.049 / 313.392 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6416,"dispatchToBlockedOrPreparationMs":149.8394,"firstNewGenerationToCleanupDrainedMs":267.2946,"firstPhysicalMutationToFirstNewGenerationMs":41.4606,"presentationToStrictCompletionMs":266.0491}    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0133,"dispatchToBlockedOrPreparationMs":163.4681,"firstNewGenerationToCleanupDrainedMs":314.1798,"firstPhysicalMutationToFirstNewGenerationMs":48.2413,"presentationToStrictCompletionMs":313.3916}   |
| 12  | 171.239 / 185.574   | 171.239 / 185.574   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":171.239,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":185.5742,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 747.166 / 1055.475  | 747.166 / 1055.475  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":192.6286,"dispatchToBlockedOrPreparationMs":394.74,"firstNewGenerationToCleanupDrainedMs":41.0968,"firstPhysicalMutationToFirstNewGenerationMs":118.7009,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":331.8593,"dispatchToBlockedOrPreparationMs":525.0831,"firstNewGenerationToCleanupDrainedMs":49.9863,"firstPhysicalMutationToFirstNewGenerationMs":148.5463,"presentationToStrictCompletionMs":0}        |
| 14  | 1222.442 / 2176.467 | 1306.234 / 2123.598 | 83.792 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":260.1297,"dispatchToBlockedOrPreparationMs":447.4412,"firstNewGenerationToCleanupDrainedMs":172.4308,"firstPhysicalMutationToFirstNewGenerationMs":426.2326,"presentationToStrictCompletionMs":128.541}  | {"blockedOrPreparationToFirstPhysicalMutationMs":317.6467,"dispatchToBlockedOrPreparationMs":796.3105,"firstNewGenerationToCleanupDrainedMs":203.9615,"firstPhysicalMutationToFirstNewGenerationMs":805.6797,"presentationToStrictCompletionMs":0}       |
| 15  | 903.650 / 1348.043  | 561.963 / 1091.780  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0595,"dispatchToBlockedOrPreparationMs":360.1568,"firstNewGenerationToCleanupDrainedMs":0.3022,"firstPhysicalMutationToFirstNewGenerationMs":197.4448,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":8.1734,"dispatchToBlockedOrPreparationMs":670.6568,"firstNewGenerationToCleanupDrainedMs":55.3568,"firstPhysicalMutationToFirstNewGenerationMs":357.5931,"presentationToStrictCompletionMs":0}          |
| 16  | 731.838 / 835.427   | 602.543 / 603.608   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4262,"dispatchToBlockedOrPreparationMs":343.501,"firstNewGenerationToCleanupDrainedMs":42.7009,"firstPhysicalMutationToFirstNewGenerationMs":211.9146,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4995,"dispatchToBlockedOrPreparationMs":382.8237,"firstNewGenerationToCleanupDrainedMs":0.1826,"firstPhysicalMutationToFirstNewGenerationMs":216.1024,"presentationToStrictCompletionMs":0}           |
| 17  | 719.437 / 791.418   | 587.405 / 652.932   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4142,"dispatchToBlockedOrPreparationMs":350.0745,"firstNewGenerationToCleanupDrainedMs":42.2475,"firstPhysicalMutationToFirstNewGenerationMs":190.6686,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.742,"dispatchToBlockedOrPreparationMs":382.4265,"firstNewGenerationToCleanupDrainedMs":45.3268,"firstPhysicalMutationToFirstNewGenerationMs":220.4365,"presentationToStrictCompletionMs":0}           |
| 18  | 760.866 / 1417.624  | 630.628 / 1282.730  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9396,"dispatchToBlockedOrPreparationMs":378.3542,"firstNewGenerationToCleanupDrainedMs":46.4289,"firstPhysicalMutationToFirstNewGenerationMs":200.9055,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":319.6063,"dispatchToBlockedOrPreparationMs":512.453,"firstNewGenerationToCleanupDrainedMs":44.6283,"firstPhysicalMutationToFirstNewGenerationMs":406.0426,"presentationToStrictCompletionMs":0}         |
| 19  | 781.359 / 877.031   | 650.456 / 738.526   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.493,"dispatchToBlockedOrPreparationMs":388.6542,"firstNewGenerationToCleanupDrainedMs":44.3308,"firstPhysicalMutationToFirstNewGenerationMs":211.9777,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.5759,"dispatchToBlockedOrPreparationMs":404.8708,"firstNewGenerationToCleanupDrainedMs":48.3438,"firstPhysicalMutationToFirstNewGenerationMs":279.7356,"presentationToStrictCompletionMs":0}          |
| 20  | 530.869 / 504.108   | 739.768 / 742.716   | 208.899 / 238.609 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5574,"dispatchToBlockedOrPreparationMs":341.9606,"firstNewGenerationToCleanupDrainedMs":261.9474,"firstPhysicalMutationToFirstNewGenerationMs":131.3026,"presentationToStrictCompletionMs":208.8989}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1109,"dispatchToBlockedOrPreparationMs":372.7511,"firstNewGenerationToCleanupDrainedMs":238.7998,"firstPhysicalMutationToFirstNewGenerationMs":127.0543,"presentationToStrictCompletionMs":238.6086}  |
| 21  | 205.525 / 214.340   | 463.465 / 491.025   | 257.940 / 276.685 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":141.074,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":257.9402}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":131.7941,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":276.6846}            |
| 22  | 170.549 / 174.675   | 170.549 / 174.675   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.5487,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":174.6751,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 263.125 / 258.357   | 263.125 / 258.357   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":263.1251,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":258.3567,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 993.040 / 823.743   | 1184.660 / 1030.392 | 191.620 / 206.649 | {"blockedOrPreparationToFirstPhysicalMutationMs":302.0873,"dispatchToBlockedOrPreparationMs":459.3461,"firstNewGenerationToCleanupDrainedMs":249.2883,"firstPhysicalMutationToFirstNewGenerationMs":173.9381,"presentationToStrictCompletionMs":191.6197} | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7149,"dispatchToBlockedOrPreparationMs":526.5686,"firstNewGenerationToCleanupDrainedMs":274.0909,"firstPhysicalMutationToFirstNewGenerationMs":226.0173,"presentationToStrictCompletionMs":206.6488}  |
| 25  | 1645.743 / 1631.044 | 1774.359 / 1799.095 | 128.616 / 168.051 | {"blockedOrPreparationToFirstPhysicalMutationMs":691.2409,"dispatchToBlockedOrPreparationMs":433.694,"firstNewGenerationToCleanupDrainedMs":170.4138,"firstPhysicalMutationToFirstNewGenerationMs":479.0101,"presentationToStrictCompletionMs":211.0144}  | {"blockedOrPreparationToFirstPhysicalMutationMs":42.5328,"dispatchToBlockedOrPreparationMs":778.2024,"firstNewGenerationToCleanupDrainedMs":229.0936,"firstPhysicalMutationToFirstNewGenerationMs":749.2658,"presentationToStrictCompletionMs":269.7421} |
| 26  | 803.459 / 791.043   | 888.892 / 885.060   | 85.433 / 94.017   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7511,"dispatchToBlockedOrPreparationMs":381.6439,"firstNewGenerationToCleanupDrainedMs":167.6765,"firstPhysicalMutationToFirstNewGenerationMs":334.8207,"presentationToStrictCompletionMs":129.4467}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9527,"dispatchToBlockedOrPreparationMs":377.3538,"firstNewGenerationToCleanupDrainedMs":182.6476,"firstPhysicalMutationToFirstNewGenerationMs":321.1063,"presentationToStrictCompletionMs":141.7579}  |
| 27  | 490.910 / 583.533   | 751.644 / 857.728   | 260.734 / 274.195 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.59,"dispatchToBlockedOrPreparationMs":345.411,"firstNewGenerationToCleanupDrainedMs":261.5421,"firstPhysicalMutationToFirstNewGenerationMs":140.1008,"presentationToStrictCompletionMs":260.7342}      | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8659,"dispatchToBlockedOrPreparationMs":408.2371,"firstNewGenerationToCleanupDrainedMs":275.1607,"firstPhysicalMutationToFirstNewGenerationMs":168.4642,"presentationToStrictCompletionMs":274.1947}  |
| 28  | 810.416 / 1011.710  | 860.954 / 959.239   | 50.538 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5332,"dispatchToBlockedOrPreparationMs":391.0506,"firstNewGenerationToCleanupDrainedMs":187.5037,"firstPhysicalMutationToFirstNewGenerationMs":278.8664,"presentationToStrictCompletionMs":100.2448}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6996,"dispatchToBlockedOrPreparationMs":411.5543,"firstNewGenerationToCleanupDrainedMs":193.7838,"firstPhysicalMutationToFirstNewGenerationMs":350.2012,"presentationToStrictCompletionMs":0}         |
| 29  | 1218.375 / 1613.205 | 1356.004 / 1746.882 | 137.629 / 133.677 | {"blockedOrPreparationToFirstPhysicalMutationMs":292.5704,"dispatchToBlockedOrPreparationMs":402.8201,"firstNewGenerationToCleanupDrainedMs":182.5617,"firstPhysicalMutationToFirstNewGenerationMs":478.0519,"presentationToStrictCompletionMs":228.3174} | {"blockedOrPreparationToFirstPhysicalMutationMs":651.0355,"dispatchToBlockedOrPreparationMs":428.8006,"firstNewGenerationToCleanupDrainedMs":182.667,"firstPhysicalMutationToFirstNewGenerationMs":484.3786,"presentationToStrictCompletionMs":216.0534} |
| 30  | 492.154 / 470.172   | 681.036 / 706.292   | 188.881 / 236.120 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2695,"dispatchToBlockedOrPreparationMs":336.9345,"firstNewGenerationToCleanupDrainedMs":235.4251,"firstPhysicalMutationToFirstNewGenerationMs":104.4067,"presentationToStrictCompletionMs":188.8815}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6798,"dispatchToBlockedOrPreparationMs":354.623,"firstNewGenerationToCleanupDrainedMs":236.3138,"firstPhysicalMutationToFirstNewGenerationMs":111.6756,"presentationToStrictCompletionMs":236.1203}   |
| 31  | 577.394 / 748.864   | 577.394 / 748.864   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1296,"dispatchToBlockedOrPreparationMs":401.3615,"firstNewGenerationToCleanupDrainedMs":39.3942,"firstPhysicalMutationToFirstNewGenerationMs":86.5087,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":54.4848,"dispatchToBlockedOrPreparationMs":557.0639,"firstNewGenerationToCleanupDrainedMs":45.0075,"firstPhysicalMutationToFirstNewGenerationMs":92.3075,"presentationToStrictCompletionMs":0}          |
| 32  | 184.484 / 262.388   | 450.355 / 549.479   | 265.870 / 287.091 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.8185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":265.8704}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":160.7632,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":287.0913}            |
| 33  | 276.262 / 255.603   | 276.262 / 255.603   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":276.2618,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":255.6029,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 451.448 / 471.025    | 19.577   | 15 / 15           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 12 / 13                  | 1            | 670.423 / 886.819    | 216.397  | 17 / 18           | 1            |
| 5   | 12 / 13                  | 1            | 904.847 / 1079.804   | 174.957  | 17 / 18           | 1            |
| 6   | 16 / 17                  | 1            | 830.726 / 1063.515   | 232.790  | 21 / 22           | 1            |
| 7   | 16 / 17                  | 1            | 1035.316 / 1087.551  | 52.235   | 21 / 22           | 1            |
| 8   | 16 / 17                  | 1            | 996.634 / 1175.101   | 178.467  | 21 / 22           | 1            |
| 9   | 16 / 18                  | 2            | 990.309 / 1092.304   | 101.995  | 21 / 23           | 2            |
| 10  | 10 / 11                  | 1            | 545.917 / 668.537    | 122.620  | 15 / 17           | 2            |
| 11  | 4 / 3                    | -1           | 195.942 / 216.723    | 20.781   | 11 / 9            | -2           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 706.069 / 1005.489   | 299.419  | 10 / 10           | 0            |
| 14  | 24 / 23                  | -1           | 1133.803 / 1919.637  | 785.833  | 29 / 28           | -1           |
| 15  | 11 / 12                  | 1            | 561.661 / 1036.423   | 474.762  | 19 / 17           | -2           |
| 16  | 12 / 12                  | 0            | 559.842 / 603.426    | 43.584   | 16 / 17           | 1            |
| 17  | 12 / 12                  | 0            | 545.157 / 607.605    | 62.448   | 16 / 16           | 0            |
| 18  | 12 / 22                  | 10           | 584.199 / 1238.102   | 653.903  | 16 / 26           | 10           |
| 19  | 12 / 12                  | 0            | 606.125 / 690.182    | 84.057   | 16 / 16           | 0            |
| 20  | 10 / 10                  | 0            | 477.821 / 503.916    | 26.096   | 15 / 15           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 24  | 16 / 11                  | -5           | 935.371 / 756.301    | -179.071 | 21 / 16           | -5           |
| 25  | 29 / 23                  | -6           | 1603.945 / 1570.001  | -33.944  | 34 / 28           | -6           |
| 26  | 13 / 12                  | -1           | 721.216 / 702.413    | -18.803  | 18 / 17           | -1           |
| 27  | 10 / 10                  | 0            | 490.102 / 582.567    | 92.465   | 16 / 16           | 0            |
| 28  | 11 / 12                  | 1            | 673.450 / 765.455    | 92.005   | 16 / 17           | 1            |
| 29  | 23 / 29                  | 6            | 1173.442 / 1564.215  | 390.772  | 28 / 34           | 6            |
| 30  | 10 / 9                   | -1           | 445.611 / 469.978    | 24.368   | 15 / 15           | 0            |
| 31  | 9 / 9                    | 0            | 538.000 / 703.856    | 165.856  | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 119.890 / 123.173 | 3.282    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 199.802 / 236.631 | 36.829   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 199.320 / 244.304 | 44.984   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 371.552 / 479.642 | 108.091  |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 376.746 / 389.290 | 12.544   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 366.685 / 440.955 | 74.270   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 370.248 / 407.488 | 37.240   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 268.924 / 518.001 | 249.077  |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 142.698 / 224.534 | 81.836   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 160.236 / 151.957 | -8.280   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 140.158 / 166.582 | 26.423   |
| 18  | 1 / 1                | 0     | 3 / 13             | 10    | 148.345 / 726.413 | 578.067  |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 154.845 / 207.084 | 52.239   |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 6 / 6              | 0     | 363.912 / 593.009 | 229.097  |
| 26  | 1 / 1                | 0     | 2 / 2              | 0     | 94.387 / 102.075  | 7.688    |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 3                | 1     | 11 / 16            | 5     | 665.784 / 977.337 | 311.553  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

### nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 764.649 / 788.261   | 23.612   | 3.088   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 167.665 / 188.609   | 20.945   | 12.492  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 247.366 / 261.291   | 13.925   | 5.629   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1043.867 / 1038.610 | -5.257   | -0.504  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1038.184 / 1039.456 | 1.272    | 0.122   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1196.438 / 1203.006 | 6.568    | 0.549   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1202.543 / 1186.420 | -16.123  | -1.341  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1191.568 / 1285.986 | 94.418   | 7.924   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1123.080 / 1217.829 | 94.749   | 8.437   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 779.290 / 829.250   | 49.960   | 6.411   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 435.637 / 463.473   | 27.836   | 6.390   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 183.527 / 177.695   | -5.832   | -3.178  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 641.300 / 614.699   | -26.601  | -4.148  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.913 / 915.325  | -462.588 | -33.572 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 896.172 / 1340.590  | 444.418  | 49.591  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 702.990 / 767.369   | 64.378   | 9.158   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 771.931 / 766.529   | -5.403   | -0.700  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 751.162 / 773.062   | 21.900   | 2.915   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 727.324 / 785.889   | 58.565   | 8.052   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1078.028 / 1057.541 | -20.486  | -1.900  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 501.448 / 475.511   | -25.937  | -5.172  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 169.901 / 180.721   | 10.820   | 6.368   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 250.712 / 264.511   | 13.799   | 5.504   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1133.454 / 871.077  | -262.377 | -23.148 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1543.613 / 1061.670 | -481.943 | -31.222 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 901.361 / 1038.635  | 137.273  | 15.230  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 819.338 / 759.289   | -60.049  | -7.329  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 984.498 / 1056.312  | 71.813   | 7.294   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1932.147 / 1871.062 | -61.084  | -3.161  | 2/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 696.614 / 850.703   | 154.089  | 22.120  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 660.497 / 688.570   | 28.073   | 4.250   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 467.899 / 585.806   | 117.907  | 25.199  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 253.033 / 259.017   | 5.984    | 2.365   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 524.712 / 549.794   | 764.649 / 788.261   | 239.937 / 238.467 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6788,"dispatchToBlockedOrPreparationMs":398.9273,"firstNewGenerationToCleanupDrainedMs":240.7526,"firstPhysicalMutationToFirstNewGenerationMs":121.2898,"presentationToStrictCompletionMs":239.9367}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3573,"dispatchToBlockedOrPreparationMs":428.9674,"firstNewGenerationToCleanupDrainedMs":238.6696,"firstPhysicalMutationToFirstNewGenerationMs":116.2666,"presentationToStrictCompletionMs":238.4666}   |
| 2   | 167.665 / 188.609   | 167.665 / 188.609   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.665,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":188.6095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 247.366 / 261.291   | 247.366 / 261.291   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":247.3658,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.2905,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 816.593 / 804.450   | 956.843 / 936.426   | 140.250 / 131.976 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6999,"dispatchToBlockedOrPreparationMs":408.322,"firstNewGenerationToCleanupDrainedMs":184.2716,"firstPhysicalMutationToFirstNewGenerationMs":359.55,"presentationToStrictCompletionMs":227.2739}      | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9939,"dispatchToBlockedOrPreparationMs":410.5675,"firstNewGenerationToCleanupDrainedMs":176.2961,"firstPhysicalMutationToFirstNewGenerationMs":345.5687,"presentationToStrictCompletionMs":234.1596}   |
| 5   | 812.136 / 818.943   | 952.986 / 954.767   | 140.851 / 135.824 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0876,"dispatchToBlockedOrPreparationMs":414.3874,"firstNewGenerationToCleanupDrainedMs":191.1592,"firstPhysicalMutationToFirstNewGenerationMs":343.3521,"presentationToStrictCompletionMs":226.0486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8184,"dispatchToBlockedOrPreparationMs":421.1279,"firstNewGenerationToCleanupDrainedMs":179.569,"firstPhysicalMutationToFirstNewGenerationMs":350.2513,"presentationToStrictCompletionMs":220.5132}    |
| 6   | 963.683 / 973.871   | 1108.473 / 1110.311 | 144.789 / 136.440 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1046,"dispatchToBlockedOrPreparationMs":402.0685,"firstNewGenerationToCleanupDrainedMs":193.2912,"firstPhysicalMutationToFirstNewGenerationMs":508.0083,"presentationToStrictCompletionMs":232.7553}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7294,"dispatchToBlockedOrPreparationMs":411.3054,"firstNewGenerationToCleanupDrainedMs":180.0578,"firstPhysicalMutationToFirstNewGenerationMs":515.2187,"presentationToStrictCompletionMs":229.1348}   |
| 7   | 965.785 / 1008.934  | 1113.348 / 1100.220 | 147.564 / 91.286  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1118,"dispatchToBlockedOrPreparationMs":391.181,"firstNewGenerationToCleanupDrainedMs":195.7517,"firstPhysicalMutationToFirstNewGenerationMs":522.3038,"presentationToStrictCompletionMs":236.7582}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0656,"dispatchToBlockedOrPreparationMs":403.5784,"firstNewGenerationToCleanupDrainedMs":179.5065,"firstPhysicalMutationToFirstNewGenerationMs":513.0697,"presentationToStrictCompletionMs":177.4858}   |
| 8   | 944.879 / 1042.295  | 1097.893 / 1189.901 | 153.015 / 147.606 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.849,"dispatchToBlockedOrPreparationMs":386.0721,"firstNewGenerationToCleanupDrainedMs":201.7934,"firstPhysicalMutationToFirstNewGenerationMs":506.1788,"presentationToStrictCompletionMs":246.6891}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.59,"dispatchToBlockedOrPreparationMs":423.2783,"firstNewGenerationToCleanupDrainedMs":195.9647,"firstPhysicalMutationToFirstNewGenerationMs":566.0685,"presentationToStrictCompletionMs":243.6909}     |
| 9   | 892.407 / 996.184   | 1033.092 / 1133.556 | 140.686 / 137.373 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0101,"dispatchToBlockedOrPreparationMs":350.1775,"firstNewGenerationToCleanupDrainedMs":184.5374,"firstPhysicalMutationToFirstNewGenerationMs":494.3675,"presentationToStrictCompletionMs":230.6732}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8973,"dispatchToBlockedOrPreparationMs":420.0474,"firstNewGenerationToCleanupDrainedMs":183.8236,"firstPhysicalMutationToFirstNewGenerationMs":525.7879,"presentationToStrictCompletionMs":221.6455}   |
| 10  | 604.626 / 632.187   | 779.290 / 829.250   | 174.664 / 197.063 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5596,"dispatchToBlockedOrPreparationMs":361.0118,"firstNewGenerationToCleanupDrainedMs":220.7107,"firstPhysicalMutationToFirstNewGenerationMs":193.0078,"presentationToStrictCompletionMs":174.6636}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6215,"dispatchToBlockedOrPreparationMs":360.1838,"firstNewGenerationToCleanupDrainedMs":258.9863,"firstPhysicalMutationToFirstNewGenerationMs":206.4579,"presentationToStrictCompletionMs":197.0625}   |
| 11  | 163.343 / 175.540   | 435.637 / 463.473   | 272.294 / 287.933 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6609,"dispatchToBlockedOrPreparationMs":121.2179,"firstNewGenerationToCleanupDrainedMs":273.0555,"firstPhysicalMutationToFirstNewGenerationMs":37.7029,"presentationToStrictCompletionMs":272.2943}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6074,"dispatchToBlockedOrPreparationMs":132.1942,"firstNewGenerationToCleanupDrainedMs":288.7995,"firstPhysicalMutationToFirstNewGenerationMs":38.872,"presentationToStrictCompletionMs":287.9328}     |
| 12  | 183.527 / 177.695   | 183.527 / 177.695   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":183.5274,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.6954,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 641.300 / 614.699   | 641.300 / 614.699   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0337,"dispatchToBlockedOrPreparationMs":429.1853,"firstNewGenerationToCleanupDrainedMs":47.1866,"firstPhysicalMutationToFirstNewGenerationMs":105.8945,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":53.4671,"dispatchToBlockedOrPreparationMs":423.697,"firstNewGenerationToCleanupDrainedMs":44.7161,"firstPhysicalMutationToFirstNewGenerationMs":92.8189,"presentationToStrictCompletionMs":0}            |
| 14  | 1171.620 / 818.765  | 1323.721 / 866.172  | 152.101 / 47.408  | {"blockedOrPreparationToFirstPhysicalMutationMs":262.493,"dispatchToBlockedOrPreparationMs":397.1475,"firstNewGenerationToCleanupDrainedMs":203.3643,"firstPhysicalMutationToFirstNewGenerationMs":460.7159,"presentationToStrictCompletionMs":206.2935}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9372,"dispatchToBlockedOrPreparationMs":385.2048,"firstNewGenerationToCleanupDrainedMs":189.3994,"firstPhysicalMutationToFirstNewGenerationMs":287.631,"presentationToStrictCompletionMs":96.5605}     |
| 15  | 896.172 / 1340.590  | 558.987 / 1205.543  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1702,"dispatchToBlockedOrPreparationMs":352.6839,"firstNewGenerationToCleanupDrainedMs":0.2574,"firstPhysicalMutationToFirstNewGenerationMs":201.8751,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":302.0083,"dispatchToBlockedOrPreparationMs":478.9954,"firstNewGenerationToCleanupDrainedMs":44.633,"firstPhysicalMutationToFirstNewGenerationMs":379.9068,"presentationToStrictCompletionMs":0}          |
| 16  | 702.990 / 767.369   | 611.687 / 630.822   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7991,"dispatchToBlockedOrPreparationMs":368.7131,"firstNewGenerationToCleanupDrainedMs":43.5961,"firstPhysicalMutationToFirstNewGenerationMs":194.5788,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2853,"dispatchToBlockedOrPreparationMs":369.3391,"firstNewGenerationToCleanupDrainedMs":44.1843,"firstPhysicalMutationToFirstNewGenerationMs":212.0138,"presentationToStrictCompletionMs":0}           |
| 17  | 771.931 / 766.529   | 595.296 / 629.701   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0713,"dispatchToBlockedOrPreparationMs":350.708,"firstNewGenerationToCleanupDrainedMs":42.6388,"firstPhysicalMutationToFirstNewGenerationMs":195.8778,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8678,"dispatchToBlockedOrPreparationMs":372.615,"firstNewGenerationToCleanupDrainedMs":44.854,"firstPhysicalMutationToFirstNewGenerationMs":207.364,"presentationToStrictCompletionMs":0}              |
| 18  | 751.162 / 773.062   | 619.652 / 636.953   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5809,"dispatchToBlockedOrPreparationMs":376.7105,"firstNewGenerationToCleanupDrainedMs":43.5683,"firstPhysicalMutationToFirstNewGenerationMs":194.792,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":7.6245,"dispatchToBlockedOrPreparationMs":372.8308,"firstNewGenerationToCleanupDrainedMs":44.4943,"firstPhysicalMutationToFirstNewGenerationMs":212.0034,"presentationToStrictCompletionMs":0}           |
| 19  | 727.324 / 785.889   | 556.316 / 654.022   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8053,"dispatchToBlockedOrPreparationMs":356.7758,"firstNewGenerationToCleanupDrainedMs":0.3603,"firstPhysicalMutationToFirstNewGenerationMs":194.375,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.5567,"dispatchToBlockedOrPreparationMs":404.0456,"firstNewGenerationToCleanupDrainedMs":44.696,"firstPhysicalMutationToFirstNewGenerationMs":199.7237,"presentationToStrictCompletionMs":0}            |
| 20  | 901.838 / 873.784   | 1078.028 / 1057.541 | 176.190 / 183.757 | {"blockedOrPreparationToFirstPhysicalMutationMs":267.4793,"dispatchToBlockedOrPreparationMs":463.6941,"firstNewGenerationToCleanupDrainedMs":224.7261,"firstPhysicalMutationToFirstNewGenerationMs":122.1281,"presentationToStrictCompletionMs":176.1895} | {"blockedOrPreparationToFirstPhysicalMutationMs":285.0914,"dispatchToBlockedOrPreparationMs":409.5131,"firstNewGenerationToCleanupDrainedMs":234.0775,"firstPhysicalMutationToFirstNewGenerationMs":128.8593,"presentationToStrictCompletionMs":183.7569} |
| 21  | 202.830 / 199.737   | 501.448 / 475.511   | 298.618 / 275.774 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.706,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":298.6185}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":134.8106,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":275.7737}             |
| 22  | 169.901 / 180.721   | 169.901 / 180.721   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.9006,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":180.7207,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 250.712 / 264.511   | 250.712 / 264.511   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.7117,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":264.5108,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 955.472 / 686.259   | 1133.454 / 871.077  | 177.982 / 184.818 | {"blockedOrPreparationToFirstPhysicalMutationMs":266.0538,"dispatchToBlockedOrPreparationMs":500.9016,"firstNewGenerationToCleanupDrainedMs":221.3993,"firstPhysicalMutationToFirstNewGenerationMs":145.0989,"presentationToStrictCompletionMs":177.9818} | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6459,"dispatchToBlockedOrPreparationMs":474.8769,"firstNewGenerationToCleanupDrainedMs":232.3723,"firstPhysicalMutationToFirstNewGenerationMs":160.182,"presentationToStrictCompletionMs":184.8184}    |
| 25  | 1323.681 / 789.857  | 1457.896 / 956.748  | 134.215 / 166.891 | {"blockedOrPreparationToFirstPhysicalMutationMs":321.4216,"dispatchToBlockedOrPreparationMs":449.9428,"firstNewGenerationToCleanupDrainedMs":178.1383,"firstPhysicalMutationToFirstNewGenerationMs":508.3934,"presentationToStrictCompletionMs":219.9317} | {"blockedOrPreparationToFirstPhysicalMutationMs":29.7825,"dispatchToBlockedOrPreparationMs":369.6542,"firstNewGenerationToCleanupDrainedMs":213.9043,"firstPhysicalMutationToFirstNewGenerationMs":343.407,"presentationToStrictCompletionMs":271.8125}   |
| 26  | 763.246 / 874.631   | 901.361 / 977.505   | 138.115 / 102.873 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1998,"dispatchToBlockedOrPreparationMs":367.3261,"firstNewGenerationToCleanupDrainedMs":226.1821,"firstPhysicalMutationToFirstNewGenerationMs":303.6534,"presentationToStrictCompletionMs":138.1153}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1474,"dispatchToBlockedOrPreparationMs":418.8753,"firstNewGenerationToCleanupDrainedMs":200.1484,"firstPhysicalMutationToFirstNewGenerationMs":353.3335,"presentationToStrictCompletionMs":164.0034}   |
| 27  | 559.830 / 519.370   | 819.338 / 759.289   | 259.509 / 239.919 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7194,"dispatchToBlockedOrPreparationMs":406.2385,"firstNewGenerationToCleanupDrainedMs":260.7611,"firstPhysicalMutationToFirstNewGenerationMs":147.6192,"presentationToStrictCompletionMs":259.5085}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5625,"dispatchToBlockedOrPreparationMs":361.7554,"firstNewGenerationToCleanupDrainedMs":241.0267,"firstPhysicalMutationToFirstNewGenerationMs":151.9449,"presentationToStrictCompletionMs":239.9193}   |
| 28  | 849.139 / 890.521   | 938.197 / 997.690   | 89.058 / 107.168  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3027,"dispatchToBlockedOrPreparationMs":443.3656,"firstNewGenerationToCleanupDrainedMs":175.5081,"firstPhysicalMutationToFirstNewGenerationMs":315.0207,"presentationToStrictCompletionMs":135.3592}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9094,"dispatchToBlockedOrPreparationMs":453.2262,"firstNewGenerationToCleanupDrainedMs":203.2425,"firstPhysicalMutationToFirstNewGenerationMs":337.3116,"presentationToStrictCompletionMs":165.7904}   |
| 29  | 1762.502 / 1646.392 | 1850.173 / 1786.633 | 87.671 / 140.240  | {"blockedOrPreparationToFirstPhysicalMutationMs":717.4208,"dispatchToBlockedOrPreparationMs":454.3886,"firstNewGenerationToCleanupDrainedMs":172.8374,"firstPhysicalMutationToFirstNewGenerationMs":505.5261,"presentationToStrictCompletionMs":169.6445} | {"blockedOrPreparationToFirstPhysicalMutationMs":671.9087,"dispatchToBlockedOrPreparationMs":435.9349,"firstNewGenerationToCleanupDrainedMs":186.4797,"firstPhysicalMutationToFirstNewGenerationMs":492.3093,"presentationToStrictCompletionMs":224.6697} |
| 30  | 464.694 / 569.420   | 696.614 / 850.703   | 231.920 / 281.283 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7447,"dispatchToBlockedOrPreparationMs":350.0409,"firstNewGenerationToCleanupDrainedMs":232.4266,"firstPhysicalMutationToFirstNewGenerationMs":110.402,"presentationToStrictCompletionMs":231.9199}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4877,"dispatchToBlockedOrPreparationMs":435.4832,"firstNewGenerationToCleanupDrainedMs":282.5012,"firstPhysicalMutationToFirstNewGenerationMs":128.2311,"presentationToStrictCompletionMs":281.2834}   |
| 31  | 660.497 / 688.570   | 660.497 / 688.570   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":61.1083,"dispatchToBlockedOrPreparationMs":461.9829,"firstNewGenerationToCleanupDrainedMs":42.7901,"firstPhysicalMutationToFirstNewGenerationMs":94.6154,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":60.0548,"dispatchToBlockedOrPreparationMs":464.6423,"firstNewGenerationToCleanupDrainedMs":50.5027,"firstPhysicalMutationToFirstNewGenerationMs":113.3698,"presentationToStrictCompletionMs":0}          |
| 32  | 190.736 / 239.744   | 467.899 / 585.806   | 277.163 / 346.062 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.4716,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":277.1627}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":158.2448,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":346.0616}             |
| 33  | 253.033 / 259.017   | 253.033 / 259.017   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":253.0332,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":259.0173,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 523.896 / 549.591    | 25.695   | 16 / 16           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 14                  | 0            | 772.572 / 760.130    | -12.442  | 19 / 19           | 0            |
| 5   | 14 / 14                  | 0            | 761.827 / 775.198    | 13.370   | 19 / 19           | 0            |
| 6   | 18 / 18                  | 0            | 915.181 / 930.254    | 15.072   | 23 / 23           | 0            |
| 7   | 17 / 17                  | 0            | 917.597 / 920.714    | 3.117    | 22 / 22           | 0            |
| 8   | 17 / 17                  | 0            | 896.100 / 993.937    | 97.837   | 22 / 22           | 0            |
| 9   | 16 / 17                  | 1            | 848.555 / 949.733    | 101.178  | 21 / 22           | 1            |
| 10  | 9 / 10                   | 1            | 558.579 / 570.263    | 11.684   | 14 / 15           | 1            |
| 11  | 3 / 3                    | 0            | 162.582 / 174.674    | 12.092   | 10 / 9            | -1           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 13  | 9 / 9                    | 0            | 594.114 / 569.983    | -24.131  | 10 / 10           | 0            |
| 14  | 23 / 13                  | -10          | 1120.356 / 676.773   | -443.583 | 28 / 18           | -10          |
| 15  | 12 / 22                  | 10           | 558.729 / 1160.910   | 602.181  | 19 / 26           | 7            |
| 16  | 12 / 12                  | 0            | 568.091 / 586.638    | 18.547   | 16 / 16           | 0            |
| 17  | 12 / 12                  | 0            | 552.657 / 584.847    | 32.190   | 17 / 16           | -1           |
| 18  | 12 / 12                  | 0            | 576.083 / 592.459    | 16.375   | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 555.956 / 609.326    | 53.370   | 16 / 16           | 0            |
| 20  | 16 / 16                  | 0            | 853.302 / 823.464    | -29.838  | 21 / 21           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 15 / 10                  | -5           | 912.054 / 638.705    | -273.350 | 20 / 15           | -5           |
| 25  | 23 / 12                  | -11          | 1279.758 / 742.844   | -536.914 | 28 / 17           | -11          |
| 26  | 12 / 13                  | 1            | 675.179 / 777.356    | 102.177  | 17 / 18           | 1            |
| 27  | 10 / 10                  | 0            | 558.577 / 518.263    | -40.314  | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 762.689 / 794.447    | 31.758   | 16 / 16           | 0            |
| 29  | 29 / 29                  | 0            | 1677.335 / 1600.153  | -77.183  | 34 / 34           | 0            |
| 30  | 9 / 10                   | 1            | 464.188 / 568.202    | 104.014  | 15 / 16           | 1            |
| 31  | 9 / 9                    | 0            | 617.707 / 638.067    | 20.360   | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 237.463 / 229.396  | -8.067   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 232.347 / 223.886  | -8.461   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 400.832 / 402.426  | 1.594    |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 401.917 / 401.994  | 0.077    |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 401.944 / 443.357  | 41.414   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 390.033 / 413.973  | 23.940   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 2              | -4    | 307.782 / 115.583  | -192.199 |
| 15  | 1 / 1                | 0     | 3 / 13             | 10    | 143.903 / 682.294  | 538.391  |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 141.176 / 151.037  | 9.861    |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 144.864 / 155.286  | 10.423   |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 144.350 / 153.936  | 9.586    |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 143.964 / 147.624  | 3.660    |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 334.871 / 347.497  | 12.626   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 6 / 2              | -4    | 395.523 / 222.710  | -172.813 |
| 26  | 1 / 1                | 0     | 2 / 2              | 0     | 93.461 / 108.487   | 15.026   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 3                | 0     | 16 / 16            | 0     | 1041.248 / 998.794 | -42.454  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

### Cumulative gates and other health evidence

#### renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":18078,"leftPath":"NativeOriginal","referenceFrame":18480,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

#### renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":22718,"leftPath":"NativeOriginal","referenceFrame":23111,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

#### renderscale-tuning-nvidia-20260911T153415138Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":23018,"leftPath":"NativeOriginal","referenceFrame":23405,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

#### renderscale-tuning-nvidia-20260911T153415138Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":27488,"leftPath":"NativeOriginal","referenceFrame":27855,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

### Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16521.3203125 / 16407.21484375 / -114.10546875 | 16677.78515625 / 16563.484375 / -114.30078125  | -0.195                |
| nvidia | 1    | systemCommitMiB   | 54414.625 / 54313.40234375 / -101.22265625     | 56653.96484375 / 56801.75 / 147.78515625       | 249.008               |
| nvidia | 1    | dxgiUsageMiB      | 4201.6171875 / 3527.0078125 / -674.609375      | 4248.890625 / 3696.67578125 / -552.21484375    | 122.395               |
| nvidia | 1    | liveTextures      | 0 / 256 / 256                                  | 0 / 267 / 267                                  | 11                    |
| nvidia | 1    | liveTextureMiB    | 0 / 2424.0259971618652 / 2424.0259971618652    | 0 / 2454.422275543213 / 2454.422275543213      | 30.396                |
| nvidia | 2    | processPrivateMiB | 16831.234375 / 16617.78515625 / -213.44921875  | 16961.67578125 / 16603.90625 / -357.76953125   | -144.320              |
| nvidia | 2    | systemCommitMiB   | 54507.67578125 / 54547.8203125 / 40.14453125   | 57297.87890625 / 56803.0234375 / -494.85546875 | -535                  |
| nvidia | 2    | dxgiUsageMiB      | 3889.8828125 / 3566.79296875 / -323.08984375   | 3917.17578125 / 3556.42578125 / -360.75        | -37.660               |
| nvidia | 2    | liveTextures      | 0 / 233 / 233                                  | 0 / 209 / 209                                  | -24                   |
| nvidia | 2    | liveTextureMiB    | 0 / 2349.4316596984863 / 2349.4316596984863    | 0 / 2270.93111038208 / 2270.93111038208        | -78.501               |

### Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2222        | 2024        | -198        |
| cpu/compactPresentationContract/reuses                  | 2200        | 2000        | -200        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 151         | 148         | -3          |
| cpu/generationResourceValidation/fullValidations        | 568         | 567         | -1          |
| cpu/generationResourceValidation/stableChecks           | 8397        | 7729        | -668        |
| cpu/generationResourceValidation/stableHits             | 8316        | 7650        | -666        |
| cpu/generationResourceValidation/stableMisses           | 81          | 79          | -2          |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 1           | 1           | 0           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4328        | 3914        | -414        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4328        | 3914        | -414        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4286        | 3872        | -414        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4328        | 3914        | -414        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4307        | 3893        | -414        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4294        | 3880        | -414        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4238        | 3819        | -419        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 95          | 5           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4313        | 3899        | -414        |
| cpu/strongStereoPacket/captures                         | 4628        | 4208        | -420        |
| cpu/strongStereoPacket/commitAccepts                    | 4424        | 4024        | -400        |
| cpu/strongStereoPacket/commitRejects                    | 66          | 68          | 2           |
| cpu/strongStereoPacket/commitValidations                | 4490        | 4092        | -398        |
| cpu/strongStereoPacket/cycleReuses                      | 2275        | 2063        | -212        |
| cpu/strongStereoPacket/fastSkips                        | 4028        | 3620        | -408        |
| cpu/strongStereoPacket/invalidations                    | 4882        | 4471        | -411        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 100         | 103         | 3           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2253        | 2042        | -211        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.855       | 1.841       | -0.013      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 68.100      | 150.900     | 82.800      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.147       | 0.119       | -0.029      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 11.900      | 1.500       | -10.400     |
| cpu/window/currentFrame                                 | 18480       | 23405       | 4925        |
| cpu/window/elapsedFrames                                | 4328        | 3914        | -414        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 14152       | 19491       | 5339        |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 18482       | 23407       | 4925        |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6230998224  | 5588993520  | -642004704  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4911        | 4405        | -506        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12474725760 | 11189404800 | -1285320960 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.300       | -0.006      |
| gpu/item5ActiveFSRCopies/activePixels                   | 12047940400 | 10522243280 | -1525697120 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27299138000 | 24531964720 | -2767173280 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15490       | 13800       | -1690       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2045        | 1819        | -226        |
| gpu/item7EarlyHAM/executedClears                        | 2052        | 1846        | -206        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2052        | 1846        | -206        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4256        | 3824        | -432        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4330        | 3916        | -414        |
| gpu/startFrame                                          | 14152       | 19491       | 5339        |
| profiler/available                                      | true        | true        | n/a         |
| profiler/capabilities                                   | 63          | 63          | 0           |
| profiler/capturing                                      | false       | false       | n/a         |
| profiler/enabled                                        | true        | true        | n/a         |
| profiler/frame/acquiredSlots                            | 0           | 0           | 0           |
| profiler/frame/captured                                 | 0           | 0           | 0           |
| profiler/frame/peakAcquiredSlots                        | 0           | 0           | 0           |
| profiler/frame/slotRefusals                             | 0           | 0           | 0           |
| profiler/limits/frameLatency                            | 3           | 3           | 0           |
| profiler/limits/historyCapacity                         | 300         | 300         | 0           |
| profiler/limits/maximumTimers                           | 128         | 128         | 0           |
| profiler/timerCount                                     | 0           | 0           | 0           |
| profiler/totalsMs/cpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/gpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a         | n/a         |
| texture/active                                          | false       | false       | n/a         |
| texture/attachFailures                                  | 0           | 0           | 0           |
| texture/createdCount                                    | 3974        | 4021        | 47          |
| texture/createdEstimatedBytes                           | 38786366328 | 38958703920 | 172337592   |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3718        | 3754        | 36          |
| texture/destroyedEstimatedBytes                         | 36244590844 | 36385055628 | 140464784   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 896         | 906         | 10          |
| texture/liveTextureRecordCount                          | 256         | 267         | 11          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 47          | 56          | 9           |
| texture/niSourceTextureMatchedEstimatedBytes            | 166123904   | 197996512   | 31872608    |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1473        | 1506        | 33          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 256         | 267         | 11          |
| texture/outstandingEstimatedBytes                       | 2541775484  | 2573648292  | 31872808    |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 1           | 1           | 0           |
| texture/supported                                       | true        | true        | n/a         |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2189        | 2125        | -64        |
| cpu/compactPresentationContract/reuses                  | 2167        | 2103        | -64        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 156         | 153         | -3         |
| cpu/generationResourceValidation/fullValidations        | 575         | 555         | -20        |
| cpu/generationResourceValidation/stableChecks           | 8303        | 8078        | -225       |
| cpu/generationResourceValidation/stableHits             | 8226        | 7999        | -227       |
| cpu/generationResourceValidation/stableMisses           | 77          | 79          | 2          |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 2           | 2           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4221        | 4063        | -158       |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4221        | 4063        | -158       |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4179        | 4021        | -158       |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4221        | 4063        | -158       |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4200        | 4042        | -158       |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4187        | 4029        | -158       |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4130        | 3976        | -154       |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 86          | -4         |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4206        | 4048        | -158       |
| cpu/strongStereoPacket/captures                         | 4548        | 4388        | -160       |
| cpu/strongStereoPacket/commitAccepts                    | 4359        | 4231        | -128       |
| cpu/strongStereoPacket/commitRejects                    | 65          | 65          | 0          |
| cpu/strongStereoPacket/commitValidations                | 4424        | 4296        | -128       |
| cpu/strongStereoPacket/cycleReuses                      | 2233        | 2153        | -80        |
| cpu/strongStereoPacket/fastSkips                        | 3894        | 3738        | -156       |
| cpu/strongStereoPacket/invalidations                    | 4767        | 4606        | -161       |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 102         | -1         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2212        | 2133        | -79        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.879       | 1.742       | -0.136     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 25.400      | 82.300      | 56.900     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.143       | 0.114       | -0.028     |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | 1.400       | -0.200     |
| cpu/window/currentFrame                                 | 23112       | 27855       | 4743       |
| cpu/window/elapsedFrames                                | 4220        | 4062        | -158       |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 18892       | 23793       | 4901       |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 23113       | 27856       | 4743       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6071131440  | 5878276272  | -192855168 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4785        | 4633        | -152       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12154665600 | 11768561280 | -386104320 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.309       | 0.303       | -0.006     |
| gpu/item5ActiveFSRCopies/activePixels                   | 11865705520 | 11187774560 | -677930960 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26516112080 | 25695348640 | -820763440 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15110       | 14520       | -590       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1991        | 1921        | -70        |
| gpu/item7EarlyHAM/executedClears                        | 2016        | 1954        | -62        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2016        | 1954        | -62        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4168        | 4036        | -132       |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4221        | 4063        | -158       |
| gpu/startFrame                                          | 18892       | 23793       | 4901       |
| profiler/available                                      | true        | true        | n/a        |
| profiler/capabilities                                   | 63          | 63          | 0          |
| profiler/capturing                                      | false       | false       | n/a        |
| profiler/enabled                                        | true        | true        | n/a        |
| profiler/frame/acquiredSlots                            | 0           | 0           | 0          |
| profiler/frame/captured                                 | 0           | 0           | 0          |
| profiler/frame/peakAcquiredSlots                        | 0           | 0           | 0          |
| profiler/frame/slotRefusals                             | 0           | 0           | 0          |
| profiler/limits/frameLatency                            | 3           | 3           | 0          |
| profiler/limits/historyCapacity                         | 300         | 300         | 0          |
| profiler/limits/maximumTimers                           | 128         | 128         | 0          |
| profiler/timerCount                                     | 0           | 0           | 0          |
| profiler/totalsMs/cpu                                   | n/a         | n/a         | n/a        |
| profiler/totalsMs/gpu                                   | n/a         | n/a         | n/a        |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a         | n/a        |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a         | n/a        |
| texture/active                                          | false       | false       | n/a        |
| texture/attachFailures                                  | 0           | 0           | 0          |
| texture/createdCount                                    | 3968        | 3910        | -58        |
| texture/createdEstimatedBytes                           | 38826844112 | 38640278112 | -186566000 |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3735        | 3701        | -34        |
| texture/destroyedEstimatedBytes                         | 36363286460 | 36259034252 | -104252208 |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 887         | 863         | -24        |
| texture/liveTextureRecordCount                          | 233         | 209         | -24        |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 26          | 2           | -24        |
| texture/niSourceTextureMatchedEstimatedBytes            | 87906272    | 5592480     | -82313792  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1506        | 1502        | -4         |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 233         | 209         | -24        |
| texture/outstandingEstimatedBytes                       | 2463557652  | 2381243860  | -82313792  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 2           | 2           | 0          |
| texture/supported                                       | true        | true        | n/a        |

</details>

### Context, memory, CPU/GPU and evidence

| Retained context matches | Result |
| ------------------------ | ------ |
| adapter                  | true   |
| scene                    | false  |
| foveation                | true   |
| toolchain                | false  |
| dependencies             | true   |
| shaderCompiler           | true   |

Full start/end memory evidence, CPU/GPU/resource counters, phase timings, retry details, native execution proofs, every gate and raw receipt hash are in comparison.json. comparison.csv contains every paired or missing transition with both original row payloads. Missing samples remain null/n/a. Current and baseline failures are preserved separately; a persistent gate failure is not automatically a newly introduced regression. The fixed stretch-frame cutoff is diagnostic only because settling imposes stretch. Compare its actual frames and duration, together with time to applied relatch and strict completion. Request-to-applied frames begin at the producer request, whereas strict frames/ms begin at qualification dispatch; these intervals must not be conflated.

-   Headset refresh, driver version, power state and full modlist/cache equality require retained proof.
-   One process and ordered repeats do not establish causal or statistically significant gains.
-   Missing profiler samples are unavailable time evidence, never zero cost.
