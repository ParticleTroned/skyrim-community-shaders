# NVIDIA render-scale tuning: PR73 owned release, September 11

Run `renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z` completed **66/66 transitions**
in game PID 52772, in two passes of 33. Terminal counts
are **66 PASS / 0 FAIL**, scoped to the terminal condition. Task 2 counts
are **66 PASS / 0 FAIL / 0 INCONCLUSIVE**, without an aggregate verdict.
Full-history health is **NO_COUNTED_FAILURES**; both passes meet the
applicable health standard. Reporting is **COMPLETE**. Captures are
verified inactive and the complete journal is flushed.

The existing user-pinned PR66 reference remains the primary comparison.
The additional comparison is the previous measured PR73 implementation
`269bded15`. Neither comparison averages the two ordered passes.

| Pass | PR66 strict mean ms | Previous PR73 strict mean ms | New PR73 strict mean ms | Change vs PR66 % | Change vs previous PR73 % | Cleanup-tail mean ms | Retries | Stretch episodes / frames / ms |
| ---- | ------------------: | ---------------------------: | ----------------------: | ---------------: | ------------------------: | -------------------: | ------: | ------------------------------ |
| 1    |              793.34 |                       822.55 |                  815.09 |             2.74 |                     -0.91 |                90.97 |      15 | 18 / 62 / 4080.67              |
| 2    |              807.13 |                       808.63 |                  768.53 |            -4.78 |                     -4.96 |                91.97 |      15 | 18 / 62 / 4085.36              |

All device-loss, OOM, producer-terminal, vendor-native qualification and
credible liveness-timeout counts are zero. All rows have retained terminal
receipts and passed; no reset/recovery or interruption occurred. Lifecycle,
fidelity, retirement, memory-trim and vendor/bounds-fallback counters are
zero. All 32 selected stretch transitions recovered; no active tail remains.
Raw cumulative acceptance remains false. The fixed settling-imposed stretch
cutoff is DIAGNOSTIC_ONLY; the scaled-presentation gate against the proven
native terminal target is CONTRACT_MISMATCH. Exact observed values and
limits remain below and in the complete ledger.

Both formal change assessments are INCONCLUSIVE. PR66 has differing scene
and toolchain context; previous PR73 differs in scene context. Neither
comparison has a matching fixture fingerprint or a declared versioned
tolerance policy. Memory evidence is complete, with an inconclusive
classification; its exact predicates below do not establish leak freedom.
No failure injection, SE/AE runtime test or separate render-scale release
qualification ran during this task.

## Build and evidence identity

The verified physical DLL matches clean Release source/renderer
`554e484e3957178e2d144bf35266bfdcc0948642`, main-VR base
`ef7c366dd73989b2b87751c0ef975db7c6fd310f` and runtime Build ID
`e7e9fa2d13f28c6727b9567741511a418c54113c190db2616bfb2c2ad1158d96`. Its SHA-256 is
`5bb50a859ccb62f00d20669a9ad24b6b704dc076054e86dfa8af9dda9db8bfac` and size is 28,632,576 bytes.
The adjacent manifest, AIO build receipt, exact selected profile and sole
enabled loose provider agree. Overwrite and unmanaged Data have no competing
DLL. Compile identity and dependency hashes are retained in the
[physical verification receipt](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z/runtime-dll-verification.json).
This correlates the physical artifact with producer metadata; it does not
claim an in-memory module hash. The AIO archive identity comes from its
preserved build receipt. The Git merge-base audit returned the main-VR
base above.

Worker dispatch pacing was VALID: maximum client gap
52.0227 ms against 250 ms.
Local tool metadata was enumerated in the skill-reading cell before
positioning; this added no extra live call or measured mutation. The
packaged worker executed the unchanged matrix with four-second local status
monitoring. The complete client diagnostic is retained in the ledger.

Both passes retain zero resolved profiler samples. Those totals are
unavailable GPU timings. Existing feedback
`AUTO-20260911-154621563-951D86E2` was amended with this run; no toolkit
change or measurement replay was made.

## Validation and publication

The [canonical ledger](vr-render-scale-ledger.md) retains every
finalized summary field, pass, transition and comparison field. Exact
reconstruction passed for the entire summary, both comparisons and all
supplemental records. All **528 candidate timing cells** and **1,056 cells
per paired comparison** match retained values; the two comparisons cover
**1,584 distinct numeric cells** across three runs. Every historical cell
is preserved. Full validation is recorded in
[complete-ledger-validation.json](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z/complete-ledger-validation.json).

Offline finalization took 28.110 seconds.
Complete ledger reporting took 33.558 seconds.
Both comparison outputs were reused after evidence, code and output hashes
matched, with the numeric audit rerun. Original comparison generation and
each reporting stage retain their timings in the evidence directory.

At the user's explicit request, [PR73](https://github.com/ParticleTroned/skyrim-community-shaders/pull/73)
was updated first with all 66 comparison rows and the full generated
report/comparison tables. Its published body was verified against the
prepared content. The ledger and this durable report followed afterward.
The preceding PR73 measurement block was archived in a PR comment.
Raw evidence remains local.

-   [Finalized summary](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z/summary.json)
-   [All transition columns](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z/transitions.csv)
-   [Every scalar from raw receipts and journal revisions](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z/evidence-values.csv)
-   [Receipt hashes](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z/receipt-index.json)

## Pinned PR66 comparison: per-pass distributions and endpoint deltas

B/C denotes this comparison's baseline / candidate. Passes remain
separate. Deltas are candidate minus baseline; all times are
milliseconds excluding the server wait before dispatch. Full
precision remains in the ledger and saved comparison JSON.

| Pass | Statistic | Baseline ms | Candidate ms |  Delta ms | Delta % |
| ---- | --------- | ----------: | -----------: | --------: | ------: |
| 1    | total     |   26180.089 |    26898.109 |  +718.020 |  +2.743 |
| 1    | mean      |     793.336 |      815.094 |   +21.758 |  +2.743 |
| 1    | median    |     751.644 |      798.935 |   +47.291 |  +6.292 |
| 1    | p95       |    1389.267 |     1387.633 |    -1.634 |  -0.118 |
| 1    | maximum   |    1856.757 |     1423.853 |  -432.904 | -23.315 |
| 2    | total     |   26635.148 |    25361.631 | -1273.517 |  -4.781 |
| 2    | mean      |     807.126 |      768.534 |   -38.591 |  -4.781 |
| 2    | median    |     771.931 |      817.128 |   +45.197 |  +5.855 |
| 2    | p95       |    1444.193 |     1194.946 |  -249.247 | -17.259 |
| 2    | maximum   |    1932.147 |     1205.893 |  -726.254 | -37.588 |

Rows slower in every retained pass: 2, 3, 4, 5, 7, 8, 9, 10, 11, 16, 17, 18, 19, 26, 30.

The largest observed strict-time increases in each pass are shown
below. A missing tolerance policy does not hide observed deltas.

| Pass | Row | Route                        | Delta ms | Delta % |
| ---- | --- | ---------------------------- | -------: | ------: |
| 1    | 19  | FSR3 Performance -> FSR3 UP  | +441.944 | +56.561 |
| 1    | 4   | DLAA -> DLSS Hoshipa         | +302.110 | +33.143 |
| 1    | 16  | FSR3 UQ -> FSR3 Quality      | +276.148 | +37.734 |
| 2    | 19  | FSR3 Performance -> FSR3 UP  | +212.283 | +29.187 |
| 2    | 26  | DLSS Hoshipa -> FSR3 Hoshipa | +166.704 | +18.495 |
| 2    | 5   | DLSS Hoshipa -> DLSS UQ      | +142.218 | +13.699 |

The complete comparison also retains each endpoint's B/C values,
phase durations, request/epoch ownership, retry waits, actual
relatch and strict frames/milliseconds, stretch and health gates.
Here n.d. percentage preserves an unavailable comparison such as
a zero baseline; it does not imply zero change.

| Pass | Row | Presentation delta ms | Delta % | Cleanup delta ms | Delta % | Cleanup tail delta ms |  Delta % |
| ---- | --- | --------------------: | ------: | ---------------: | ------: | --------------------: | -------: |
| 1    | 1   |               +91.688 | +18.278 |          +96.162 | +14.136 |                +4.473 |   +2.504 |
| 1    | 2   |               +10.519 |  +6.207 |          +10.519 |  +6.207 |                +0.000 |     n.d. |
| 1    | 3   |               +58.798 | +23.498 |          +58.798 | +23.498 |                +0.000 |     n.d. |
| 1    | 4   |              +194.732 | +24.667 |         +276.306 | +33.301 |               +81.575 | +202.519 |
| 1    | 5   |              +180.866 | +19.136 |         +205.742 | +19.256 |               +24.876 |  +20.182 |
| 1    | 6   |               +62.225 |  +7.091 |          +79.842 |  +7.943 |               +17.617 |  +13.806 |
| 1    | 7   |              +133.216 | +12.192 |          +94.372 |  +7.688 |               -38.844 |  -28.779 |
| 1    | 8   |              +109.243 | +10.420 |         +114.741 |  +9.667 |                +5.498 |   +3.968 |
| 1    | 9   |               +74.177 |  +7.189 |          +74.222 |  +6.319 |                +0.045 |   +0.032 |
| 1    | 10  |               +73.671 | +12.474 |          +38.087 |  +5.006 |               -35.584 |  -20.899 |
| 1    | 11  |               -20.419 | -10.355 |           +1.997 |  +0.431 |               +22.416 |   +8.425 |
| 1    | 12  |                +4.752 |  +2.775 |           +4.752 |  +2.775 |                +0.000 |     n.d. |
| 1    | 13  |              +173.912 | +23.276 |         +173.912 | +23.276 |                +0.000 |     n.d. |
| 1    | 14  |              -222.025 | -18.162 |         -199.751 | -15.292 |               +22.274 |  +26.582 |
| 1    | 15  |               +99.133 | +10.970 |         +166.174 | +29.570 |                +0.000 |     n.d. |
| 1    | 16  |              +276.148 | +37.734 |         +145.691 | +24.179 |                +0.000 |     n.d. |
| 1    | 17  |               +50.024 |  +6.953 |          +18.097 |  +3.081 |                +0.000 |     n.d. |
| 1    | 18  |               +25.266 |  +3.321 |          +30.288 |  +4.803 |                +0.000 |     n.d. |
| 1    | 19  |              +441.944 | +56.561 |          +34.600 |  +5.319 |                +0.000 |     n.d. |
| 1    | 20  |              +237.720 | +44.779 |         +213.273 | +28.830 |               -24.447 |  -11.703 |
| 1    | 21  |                +3.618 |  +1.760 |           +4.247 |  +0.916 |                +0.629 |   +0.244 |
| 1    | 22  |               -11.927 |  -6.993 |          -11.927 |  -6.993 |                +0.000 |     n.d. |
| 1    | 23  |               -22.492 |  -8.548 |          -22.492 |  -8.548 |                +0.000 |     n.d. |
| 1    | 24  |              -395.482 | -39.825 |         -459.318 | -38.772 |               -63.837 |  -33.314 |
| 1    | 25  |              -731.213 | -44.431 |         -732.634 | -41.290 |                -1.422 |   -1.105 |
| 1    | 26  |              +114.942 | +14.306 |          +76.322 |  +8.586 |               -38.620 |  -45.205 |
| 1    | 27  |               +56.075 | +11.423 |          -24.467 |  -3.255 |               -80.542 |  -30.891 |
| 1    | 28  |               +73.879 |  +9.116 |         -196.862 | -22.866 |               -50.538 | -100.000 |
| 1    | 29  |              -385.541 | -31.644 |         -386.395 | -28.495 |                -0.854 |   -0.620 |
| 1    | 30  |               +34.077 |  +6.924 |          +20.765 |  +3.049 |               -13.312 |   -7.048 |
| 1    | 31  |               +29.207 |  +5.059 |          +29.207 |  +5.059 |                +0.000 |     n.d. |
| 1    | 32  |                +4.733 |  +2.565 |           -6.888 |  -1.529 |               -11.621 |   -4.371 |
| 1    | 33  |               -10.930 |  -3.957 |          -10.930 |  -3.957 |                +0.000 |     n.d. |
| 2    | 1   |               +31.497 |  +6.003 |          -13.691 |  -1.791 |               -45.188 |  -18.833 |
| 2    | 2   |                +5.196 |  +3.099 |           +5.196 |  +3.099 |                +0.000 |     n.d. |
| 2    | 3   |               +38.912 | +15.730 |          +38.912 | +15.730 |                +0.000 |     n.d. |
| 2    | 4   |               +84.864 | +10.392 |          +36.771 |  +3.843 |               -48.093 |  -34.291 |
| 2    | 5   |              +161.950 | +19.941 |         +125.150 | +13.132 |               -36.800 |  -26.127 |
| 2    | 6   |               +51.923 |  +5.388 |           -2.327 |  -0.210 |               -54.250 |  -37.468 |
| 2    | 7   |               +15.593 |  +1.615 |           +6.553 |  +0.589 |                -9.040 |   -6.126 |
| 2    | 8   |               +26.604 |  +2.816 |           +9.169 |  +0.835 |               -17.435 |  -11.394 |
| 2    | 9   |               +65.847 |  +7.379 |          +77.218 |  +7.474 |               +11.372 |   +8.083 |
| 2    | 10  |               +67.399 | +11.147 |          +26.122 |  +3.352 |               -41.277 |  -23.632 |
| 2    | 11  |               +34.310 | +21.005 |          +49.161 | +11.285 |               +14.851 |   +5.454 |
| 2    | 12  |               -23.281 | -12.685 |          -23.281 | -12.685 |                +0.000 |     n.d. |
| 2    | 13  |                -8.704 |  -1.357 |           -8.704 |  -1.357 |                +0.000 |     n.d. |
| 2    | 14  |              -265.705 | -22.678 |         -290.146 | -21.919 |               -24.441 |  -16.069 |
| 2    | 15  |               -79.044 |  -8.820 |         +169.835 | +30.383 |                +0.000 |     n.d. |
| 2    | 16  |              +116.203 | +16.530 |          +73.339 | +11.990 |                +0.000 |     n.d. |
| 2    | 17  |               +49.488 |  +6.411 |          +97.098 | +16.311 |                +0.000 |     n.d. |
| 2    | 18  |               +66.342 |  +8.832 |          +56.068 |  +9.048 |                +0.000 |     n.d. |
| 2    | 19  |              +212.283 | +29.187 |         +132.754 | +23.863 |                +0.000 |     n.d. |
| 2    | 20  |               -69.625 |  -7.720 |          -68.310 |  -6.337 |                +1.315 |   +0.747 |
| 2    | 21  |               -13.752 |  -6.780 |          -48.377 |  -9.647 |               -34.625 |  -11.595 |
| 2    | 22  |                -7.089 |  -4.172 |           -7.089 |  -4.172 |                +0.000 |     n.d. |
| 2    | 23  |               +37.879 | +15.108 |          +37.879 | +15.108 |                +0.000 |     n.d. |
| 2    | 24  |              -292.865 | -30.651 |         -336.712 | -29.707 |               -43.846 |  -24.635 |
| 2    | 25  |              -461.869 | -34.893 |         -451.260 | -30.953 |               +10.609 |   +7.905 |
| 2    | 26  |              +304.819 | +39.937 |         +110.149 | +12.220 |              -138.115 | -100.000 |
| 2    | 27  |                +7.566 |  +1.351 |          -70.752 |  -8.635 |               -78.318 |  -30.179 |
| 2    | 28  |               +20.902 |  +2.462 |          -19.155 |  -2.042 |               -40.058 |  -44.979 |
| 2    | 29  |              -940.903 | -53.385 |         -891.843 | -48.203 |               +49.061 |  +55.960 |
| 2    | 30  |               +60.991 | +13.125 |          +24.377 |  +3.499 |               -36.614 |  -15.787 |
| 2    | 31  |               -58.930 |  -8.922 |          -58.930 |  -8.922 |                +0.000 |     n.d. |
| 2    | 32  |                -4.947 |  -2.593 |          +14.218 |  +3.039 |               +19.165 |   +6.915 |
| 2    | 33  |               +28.943 | +11.438 |          +28.943 | +11.438 |                +0.000 |     n.d. |

## Previous PR73 implementation comparison: per-pass distributions and endpoint deltas

B/C denotes this comparison's baseline / candidate. Passes remain
separate. Deltas are candidate minus baseline; all times are
milliseconds excluding the server wait before dispatch. Full
precision remains in the ledger and saved comparison JSON.

| Pass | Statistic | Baseline ms | Candidate ms |  Delta ms | Delta % |
| ---- | --------- | ----------: | -----------: | --------: | ------: |
| 1    | total     |   27144.053 |    26898.109 |  -245.944 |  -0.906 |
| 1    | mean      |     822.547 |      815.094 |    -7.453 |  -0.906 |
| 1    | median    |     920.986 |      798.935 |  -122.051 | -13.252 |
| 1    | p95       |    1301.245 |     1387.633 |   +86.388 |  +6.639 |
| 1    | maximum   |    1356.821 |     1423.853 |   +67.033 |  +4.940 |
| 2    | total     |   26684.937 |    25361.631 | -1323.305 |  -4.959 |
| 2    | mean      |     808.634 |      768.534 |   -40.100 |  -4.959 |
| 2    | median    |     883.792 |      817.128 |   -66.665 |  -7.543 |
| 2    | p95       |    1210.577 |     1194.946 |   -15.631 |  -1.291 |
| 2    | maximum   |    1260.141 |     1205.893 |   -54.248 |  -4.305 |

Rows slower in every retained pass: 1, 4, 5, 7, 8, 9, 10, 13.

The largest observed strict-time increases in each pass are shown
below. A missing tolerance policy does not hide observed deltas.

| Pass | Row | Route                         | Delta ms | Delta % |
| ---- | --- | ----------------------------- | -------: | ------: |
| 1    | 19  | FSR3 Performance -> FSR3 UP   | +247.972 | +25.424 |
| 1    | 4   | DLAA -> DLSS Hoshipa          | +204.204 | +20.230 |
| 1    | 5   | DLSS Hoshipa -> DLSS UQ       | +171.463 | +14.181 |
| 2    | 5   | DLSS Hoshipa -> DLSS UQ       | +110.643 | +10.343 |
| 2    | 28  | NONE -> FSR3 UP               |  +90.638 | +10.256 |
| 2    | 7   | DLSS Quality -> DLSS Balanced |  +88.893 |  +7.958 |

The complete comparison also retains each endpoint's B/C values,
phase durations, request/epoch ownership, retry waits, actual
relatch and strict frames/milliseconds, stretch and health gates.
Here n.d. percentage preserves an unavailable comparison such as
a zero baseline; it does not imply zero change.

| Pass | Row | Presentation delta ms | Delta % | Cleanup delta ms | Delta % | Cleanup tail delta ms |  Delta % |
| ---- | --- | --------------------: | ------: | ---------------: | ------: | --------------------: | -------: |
| 1    | 1   |               +40.585 |  +7.343 |          +32.388 |  +4.353 |                -8.196 |   -4.285 |
| 1    | 2   |               +11.816 |  +7.026 |          +11.816 |  +7.026 |                +0.000 |     n.d. |
| 1    | 3   |               +59.061 | +23.628 |          +59.061 | +23.628 |                +0.000 |     n.d. |
| 1    | 4   |              +144.357 | +17.189 |         +181.135 | +19.584 |               +36.778 |  +43.229 |
| 1    | 5   |              +130.884 | +13.152 |         +149.868 | +13.330 |               +18.984 |  +14.699 |
| 1    | 6   |               -15.494 |  -1.622 |           -2.777 |  -0.255 |               +12.716 |   +9.597 |
| 1    | 7   |              +123.596 | +11.213 |          +87.191 |  +7.061 |               -36.404 |  -27.468 |
| 1    | 8   |               +17.757 |  +1.558 |          +29.781 |  +2.341 |               +12.024 |   +9.107 |
| 1    | 9   |               +31.890 |  +2.969 |          +39.628 |  +3.277 |                +7.738 |   +5.726 |
| 1    | 10  |               +21.544 |  +3.352 |          +23.884 |  +3.082 |                +2.340 |   +1.768 |
| 1    | 11  |                +4.771 |  +2.774 |          -14.689 |  -3.061 |               -19.460 |   -6.320 |
| 1    | 12  |                +3.209 |  +1.857 |           +3.209 |  +1.857 |                +0.000 |     n.d. |
| 1    | 13  |              +111.843 | +13.821 |         +111.843 | +13.821 |                +0.000 |     n.d. |
| 1    | 14  |               +79.946 |  +8.685 |          +54.709 |  +5.202 |               -25.237 |  -19.220 |
| 1    | 15  |               +76.358 |  +8.242 |         -108.312 | -12.949 |                +0.000 |     n.d. |
| 1    | 16  |               +24.629 |  +2.505 |         -134.561 | -15.243 |                +0.000 |     n.d. |
| 1    | 17  |              -281.429 | -26.780 |         -220.354 | -26.682 |                +0.000 |     n.d. |
| 1    | 18  |              -293.035 | -27.154 |         -177.370 | -21.159 |                +0.000 |     n.d. |
| 1    | 19  |              +247.972 | +25.424 |         -156.312 | -18.578 |                +0.000 |     n.d. |
| 1    | 20  |               -80.885 |  -9.522 |          -75.827 |  -7.370 |                +5.059 |   +2.820 |
| 1    | 21  |                -5.468 |  -2.548 |          -13.810 |  -2.868 |                -8.342 |   -3.125 |
| 1    | 22  |               -12.159 |  -7.119 |          -12.159 |  -7.119 |                +0.000 |     n.d. |
| 1    | 23  |               -60.585 | -20.113 |          -60.585 | -20.113 |                +0.000 |     n.d. |
| 1    | 24  |               -71.486 | -10.685 |          -82.876 | -10.254 |               -11.390 |   -8.184 |
| 1    | 25  |               -87.840 |  -8.763 |          -97.026 |  -8.520 |                -9.186 |   -6.736 |
| 1    | 26  |               -76.838 |  -7.721 |         -134.441 | -12.226 |               -57.603 |  -55.167 |
| 1    | 27  |               -32.561 |  -5.618 |          -34.509 |  -4.531 |                -1.948 |   -1.070 |
| 1    | 28  |               -36.690 |  -3.984 |         -118.687 | -15.162 |                +0.000 |     n.d. |
| 1    | 29  |              -176.237 | -17.465 |         -180.696 | -15.709 |                -4.459 |   -3.157 |
| 1    | 30  |               -55.983 |  -9.616 |          -78.873 | -10.103 |               -22.890 |  -11.534 |
| 1    | 31  |               -30.310 |  -4.759 |          -30.310 |  -4.759 |                +0.000 |     n.d. |
| 1    | 32  |               -40.704 | -17.704 |          -72.890 | -14.116 |               -32.186 |  -11.237 |
| 1    | 33  |                +3.364 |  +1.284 |           +3.364 |  +1.284 |                +0.000 |     n.d. |
| 2    | 1   |                +1.253 |  +0.226 |           +9.954 |  +1.343 |                +8.701 |   +4.677 |
| 2    | 2   |                -9.413 |  -5.164 |           -9.413 |  -5.164 |                +0.000 |     n.d. |
| 2    | 3   |               -28.919 |  -9.175 |          -28.919 |  -9.175 |                +0.000 |     n.d. |
| 2    | 4   |               +48.484 |  +5.684 |          +51.820 |  +5.502 |                +3.336 |   +3.755 |
| 2    | 5   |              +125.444 | +14.782 |          +95.374 |  +9.705 |               -30.070 |  -22.420 |
| 2    | 6   |               +29.635 |  +3.006 |          -16.449 |  -1.465 |               -46.085 |  -33.731 |
| 2    | 7   |               +32.198 |  +3.392 |          +83.805 |  +8.089 |               +51.607 |  +59.376 |
| 2    | 8   |               +15.214 |  +1.591 |          +56.428 |  +5.371 |               +41.215 |  +43.675 |
| 2    | 9   |                +4.807 |  +0.504 |          +68.300 |  +6.555 |               +63.493 |  +71.691 |
| 2    | 10  |               +14.820 |  +2.255 |          +16.267 |  +2.061 |                +1.448 |   +1.097 |
| 2    | 11  |               +27.861 | +16.409 |          +37.683 |  +8.428 |                +9.822 |   +3.542 |
| 2    | 12  |               -14.422 |  -8.257 |          -14.422 |  -8.257 |                +0.000 |     n.d. |
| 2    | 13  |               +28.152 |  +4.658 |          +28.152 |  +4.658 |                +0.000 |     n.d. |
| 2    | 14  |               -10.562 |  -1.153 |          -17.384 |  -1.654 |                -6.821 |   -5.072 |
| 2    | 15  |              -159.973 | -16.372 |         -155.019 | -17.539 |                +0.000 |     n.d. |
| 2    | 16  |              -366.319 | -30.900 |         -177.305 | -20.561 |                +0.000 |     n.d. |
| 2    | 17  |              -172.216 | -17.332 |         -154.342 | -18.228 |                +0.000 |     n.d. |
| 2    | 18  |              -269.552 | -24.797 |         -180.918 | -21.120 |                +0.000 |     n.d. |
| 2    | 19  |               -41.945 |  -4.273 |         -116.199 | -14.430 |                +0.000 |     n.d. |
| 2    | 20  |               -15.539 |  -1.833 |          -28.138 |  -2.711 |               -12.599 |   -6.627 |
| 2    | 21  |               -12.596 |  -6.246 |          -20.615 |  -4.352 |                -8.019 |   -2.948 |
| 2    | 22  |               -40.043 | -19.740 |          -40.043 | -19.740 |                +0.000 |     n.d. |
| 2    | 23  |               -11.556 |  -3.850 |          -11.556 |  -3.850 |                +0.000 |     n.d. |
| 2    | 24  |                -0.142 |  -0.021 |          -16.142 |  -1.986 |               -16.000 |  -10.657 |
| 2    | 25  |              -211.040 | -19.671 |         -157.918 | -13.560 |               +53.122 |  +57.930 |
| 2    | 26  |                +4.422 |  +0.416 |         -147.465 | -12.724 |               -95.332 | -100.000 |
| 2    | 27  |               -38.564 |  -6.364 |          -45.849 |  -5.771 |                -7.285 |   -3.865 |
| 2    | 28  |               -13.751 |  -1.556 |         +181.967 | +24.688 |               +49.000 |     n.d. |
| 2    | 29  |              -175.130 | -17.571 |         -175.858 | -15.505 |                -0.727 |   -0.529 |
| 2    | 30  |               -60.330 | -10.295 |          -58.045 |  -7.451 |                +2.284 |   +1.183 |
| 2    | 31  |               -19.981 |  -3.215 |          -19.981 |  -3.215 |                +0.000 |     n.d. |
| 2    | 32  |               -29.402 | -13.663 |          -54.498 | -10.156 |               -25.095 |   -7.808 |
| 2    | 33  |               -14.086 |  -4.758 |          -14.086 |  -4.758 |                +0.000 |     n.d. |

## Complete finalized report

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
| nvidia | 1    | 33   | 33/0               | 815.094        | 1387.633 | 1423.853 | 15.182             | 736.858         | 13.200              | 18               | 62             | 4080.674   | 15      | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 768.534        | 1194.946 | 1205.893 | 14.818             | 698.557         | 13.200              | 18               | 62             | 4085.360   | 15      | 0        | 0                   | MET             | COMPLETE |

#### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":20782,"leftPath":"NativeOriginal","referenceFrame":21209,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

#### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":25566,"leftPath":"NativeOriginal","referenceFrame":25971,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

### Memory confirmation

#### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    16430.289 |  16245.953 |     -184.336 |      16581.684 |    16584.043 |          2.359 |    16646.344 |  16475.066 |     -171.277 |                   n.d. |
| System commit MiB                  |    57091.711 |  56790.004 |     -301.707 |      57177.996 |    57215.363 |         37.367 |    57301.563 |  57090.992 |      -210.57 |                   n.d. |
| DXGI process usage MiB             |      4191.02 |   3286.949 |      -904.07 |       3544.797 |     3631.461 |         86.664 |     3680.199 |   3536.848 |     -143.352 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        247 |          247 |            247 |          247 |              0 |            0 |        241 |          241 |                  0.976 |
| Estimated live tracked texture MiB |            0 |   2405.307 |     2405.307 |       2405.307 |     2405.307 |              0 |            0 |   2379.921 |     2379.921 |                  0.989 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -184.3359375,
        "systemCommitMiB": -301.70703125,
        "dxgiUsageMiB": -904.0703125,
        "liveTextures": 247,
        "liveTextureMiB": 2405.307025909424
    },
    "pass2": {
        "processPrivateMiB": -171.27734375,
        "systemCommitMiB": -210.5703125,
        "dxgiUsageMiB": -143.3515625,
        "liveTextures": 241,
        "liveTextureMiB": 2379.9213676452637
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

| Lane   | Pass | Row | Begin sequence | Status   | Begin to pending | Pending to ready | Ready to commit | Commit to cleanup | Commit to applied | Reasons |
| ------ | ---: | --: | -------------: | -------- | ---------------- | ---------------- | --------------- | ----------------- | ----------------- | ------- |
| nvidia |    1 |  14 |             93 | complete | 0 / 0.1309       | 1 / 49.0905      | 0 / 0.1615      | 0 / 0.0169        | 0 / 17.9366       | none    |
| nvidia |    1 |  15 |            113 | complete | 0 / 0.0591       | 1 / 47.1537      | 0 / 0.2445      | 0 / 0.0582        | 0 / 17.9474       | none    |
| nvidia |    1 |  16 |            131 | complete | 0 / 0.1077       | 1 / 47.6200      | 0 / 0.1749      | 0 / 0.0579        | 0 / 18.3036       | none    |
| nvidia |    1 |  17 |            149 | complete | 0 / 0.0584       | 1 / 42.3645      | 0 / 0.0968      | 0 / 0.0407        | 0 / 13.5851       | none    |
| nvidia |    1 |  18 |            167 | complete | 0 / 0.1488       | 1 / 44.0628      | 0 / 0.2469      | 0 / 0.0469        | 0 / 12.7602       | none    |
| nvidia |    1 |  19 |            185 | complete | 0 / 0.1093       | 1 / 47.2723      | 0 / 0.3075      | 0 / 0.0504        | 0 / 14.3907       | none    |
| nvidia |    1 |  25 |            219 | complete | 0 / 0.1053       | 1 / 48.1668      | 0 / 0.1622      | 0 / 0.1243        | 0 / 46.0959       | none    |
| nvidia |    1 |  26 |            240 | complete | 0 / 0.0446       | 1 / 46.0082      | 0 / 0.1112      | 0 / 0.1029        | 0 / 81.8273       | none    |
| nvidia |    1 |  29 |            266 | complete | 0 / 0.0802       | 1 / 51.2415      | 0 / 0.2001      | 0 / 0.1509        | 0 / 43.8701       | none    |
| nvidia |    2 |  14 |             93 | complete | 0 / 0.1298       | 1 / 46.3980      | 0 / 0.1701      | 0 / 0.0169        | 0 / 25.6428       | none    |
| nvidia |    2 |  15 |            113 | complete | 0 / 0.1905       | 1 / 48.8331      | 0 / 0.3013      | 0 / 0.0475        | 0 / 27.8047       | none    |
| nvidia |    2 |  16 |            131 | complete | 0 / 0.0658       | 1 / 43.5236      | 0 / 0.3757      | 0 / 0.0435        | 0 / 15.8585       | none    |
| nvidia |    2 |  17 |            149 | complete | 0 / 0.1139       | 1 / 44.9090      | 0 / 0.1612      | 0 / 0.0384        | 0 / 14.7936       | none    |
| nvidia |    2 |  18 |            167 | complete | 0 / 0.1585       | 1 / 46.1180      | 0 / 0.2778      | 0 / 0.0469        | 0 / 13.0051       | none    |
| nvidia |    2 |  19 |            185 | complete | 0 / 0.1101       | 1 / 44.6921      | 0 / 0.1679      | 0 / 0.0666        | 0 / 13.4781       | none    |
| nvidia |    2 |  25 |            219 | complete | 0 / 0.0786       | 1 / 44.5016      | 0 / 0.2065      | 0 / 0.0984        | 0 / 42.4005       | none    |
| nvidia |    2 |  26 |            240 | complete | 0 / 0.0554       | 1 / 46.6540      | 0 / 0.1726      | 0 / 0.0783        | 0 / 62.4331       | none    |
| nvidia |    2 |  29 |            266 | complete | 0 / 0.0537       | 1 / 47.1616      | 0 / 0.3476      | 0 / 0.1247        | 0 / 32.9270       | none    |

### Owned release stages

Guard exemption does not enable vendor dispatch. Provider preparation and coherent stereo still gate promotion. Denied, revoked and unproven receipts do not establish proof-driven release. Intervals use producer CPU observations; fence readiness is not the exact GPU completion time. blockingCleanupReadyQpc observes cleanup-ownership readiness, not completion of detached retirement fences. Displayed milliseconds are rounded to two decimals; exact QPC, frame endpoints and full precision remain in summary.json and transitions.csv.

| Lane   | Pass | Row | Certificate | Guard exempt | Request to admission ms | All ready to consumed ms | Consumed to published ms | Published to provider prepared ms | Provider prepared to promoted ms | Gaps |
| ------ | ---: | --: | ----------- | ------------ | ----------------------: | -----------------------: | -----------------------: | --------------------------------: | -------------------------------: | ---- |
| nvidia |    1 |  14 | revoked     | false        |                  354.34 |                     0.24 |                    17.87 |                             60.70 |                           252.66 | none |
| nvidia |    1 |  15 | complete    | true         |                  335.84 |                     0.37 |                    17.83 |                             52.04 |                           114.22 | none |
| nvidia |    1 |  16 | complete    | true         |                  326.06 |                     0.30 |                    18.19 |                             69.16 |                           125.87 | none |
| nvidia |    1 |  17 | complete    | true         |                  289.99 |                     0.24 |                    13.45 |                             50.31 |                           104.42 | none |
| nvidia |    1 |  18 | complete    | true         |                  301.29 |                     0.35 |                    12.67 |                             37.65 |                           121.51 | none |
| nvidia |    1 |  19 | complete    | true         |                  312.15 |                     0.42 |                    14.29 |                             53.23 |                           107.39 | none |
| nvidia |    1 |  25 | complete    | true         |                  333.52 |                    24.66 |                    21.60 |                             58.53 |                           130.20 | none |
| nvidia |    1 |  26 | complete    | true         |                  337.30 |                     0.72 |                    81.24 |                             40.43 |                           128.89 | none |
| nvidia |    1 |  29 | complete    | true         |                  285.83 |                    28.35 |                    15.73 |                             65.39 |                           113.45 | none |
| nvidia |    2 |  14 | revoked     | false        |                  316.91 |                     0.24 |                    25.58 |                             58.67 |                           252.63 | none |
| nvidia |    2 |  15 | complete    | true         |                  326.36 |                     0.41 |                    27.71 |                             55.19 |                           117.53 | none |
| nvidia |    2 |  16 | complete    | true         |                  305.36 |                     0.47 |                    15.77 |                             49.54 |                           113.06 | none |
| nvidia |    2 |  17 | complete    | true         |                  316.92 |                     0.25 |                    14.72 |                             54.67 |                           112.08 | none |
| nvidia |    2 |  18 | complete    | true         |                  302.40 |                     0.38 |                    12.91 |                             55.08 |                           107.30 | none |
| nvidia |    2 |  19 | complete    | true         |                  316.72 |                     0.31 |                    13.35 |                             56.24 |                           106.54 | none |
| nvidia |    2 |  25 | complete    | true         |                  304.37 |                    22.55 |                    20.07 |                             56.91 |                           116.55 | none |
| nvidia |    2 |  26 | complete    | true         |                  322.40 |                     0.65 |                    61.98 |                             58.52 |                           133.81 | none |
| nvidia |    2 |  29 | complete    | true         |                  316.15 |                    20.21 |                    13.07 |                             53.29 |                           110.03 | none |

| Lane   | Pass | Row | Fence      | Issue to observed ready ms | Gaps |
| ------ | ---: | --: | ---------- | -------------------------: | ---- |
| nvidia |    1 |  14 | FSRHost    |                      49.17 | none |
| nvidia |    1 |  14 | FSRInterop |                      49.10 | none |
| nvidia |    1 |  14 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  15 | FSRHost    |                      47.17 | none |
| nvidia |    1 |  15 | FSRInterop |                      47.14 | none |
| nvidia |    1 |  15 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  16 | FSRHost    |                      47.68 | none |
| nvidia |    1 |  16 | FSRInterop |                      47.62 | none |
| nvidia |    1 |  16 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  17 | FSRHost    |                      42.38 | none |
| nvidia |    1 |  17 | FSRInterop |                      42.37 | none |
| nvidia |    1 |  17 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  18 | FSRHost    |                      44.16 | none |
| nvidia |    1 |  18 | FSRInterop |                      44.07 | none |
| nvidia |    1 |  18 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  19 | FSRHost    |                      47.33 | none |
| nvidia |    1 |  19 | FSRInterop |                      47.31 | none |
| nvidia |    1 |  19 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  25 | FSRHost    |                      48.22 | none |
| nvidia |    1 |  25 | FSRInterop |                      48.19 | none |
| nvidia |    1 |  25 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  26 | DLSSHost   |                      46.03 | none |
| nvidia |    1 |  29 | FSRHost    |                      51.27 | none |
| nvidia |    1 |  29 | FSRInterop |                      51.23 | none |
| nvidia |    1 |  29 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  14 | FSRHost    |                      46.48 | none |
| nvidia |    2 |  14 | FSRInterop |                      46.42 | none |
| nvidia |    2 |  14 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  15 | FSRHost    |                      48.96 | none |
| nvidia |    2 |  15 | FSRInterop |                      48.87 | none |
| nvidia |    2 |  15 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  16 | FSRHost    |                      43.54 | none |
| nvidia |    2 |  16 | FSRInterop |                      43.52 | none |
| nvidia |    2 |  16 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  17 | FSRHost    |                      44.98 | none |
| nvidia |    2 |  17 | FSRInterop |                      44.95 | none |
| nvidia |    2 |  17 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  18 | FSRHost    |                      46.23 | none |
| nvidia |    2 |  18 | FSRInterop |                      46.14 | none |
| nvidia |    2 |  18 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  19 | FSRHost    |                      44.76 | none |
| nvidia |    2 |  19 | FSRInterop |                      44.72 | none |
| nvidia |    2 |  19 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  25 | FSRHost    |                      44.54 | none |
| nvidia |    2 |  25 | FSRInterop |                      44.51 | none |
| nvidia |    2 |  25 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  26 | DLSSHost   |                      46.69 | none |
| nvidia |    2 |  29 | FSRHost    |                      47.16 | none |
| nvidia |    2 |  29 | FSRInterop |                      47.14 | none |
| nvidia |    2 |  29 | FSRRuntime |                       0.00 | none |

### Transitions

Retry waits end at the first observed preparation-ready result. Ready-to-candidate includes stereo qualification and the settling guard; it is not isolated retry overhead. Overlapping viewport waits are not added. Unavailable results are n.d. and the affected reporting outcome is n/a. All retrieved values remain in raw evidence and evidence-values.csv; reporting provenance never aborts or replays measurements.

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                            | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery   | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | -------------------------------------------------------- | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ------------------ | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                            | 112.312 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 17211/984.1777 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                            | 101.049 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 2              | 17339/1126.045 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 49.348 ms; SubmitStageFoveatedCenter: 49.068 ms | 171.987 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 17473/939.7868 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 52.487 ms; SubmitStageFoveatedCenter: 51.765 ms | 179.175 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 17608/1225.8297 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 52.382 ms; SubmitStageFoveatedCenter: 52.146 ms | 173.257 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 17743/1157.6102 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 48.850 ms; SubmitStageFoveatedCenter: 48.691 ms | 176.779 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 17879/1106.0199 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 18525/1000.4174 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 18661/1002.783 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 18798/1007.9856 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 18942/769.4603 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 19084/786.1315 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 19233/1223.3026 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 19376/768.5887 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 107.886 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 20073/914.53 ms    | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 20218/918.4013 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 91.443 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 28 records / 1 pages | not_exposed    | 20643/832.834 ms   | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                            | 105.701 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 22025/901.4569 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                            | 116.582 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 2              | 22157/974.0853 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 57.218 ms; SubmitStageFoveatedCenter: 57.071 ms | 182.557 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 5              | 22296/1015.6059 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 52.661 ms; SubmitStageFoveatedCenter: 52.248 ms | 187.629 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 5              | 22431/981.3778 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 51.165 ms; SubmitStageFoveatedCenter: 50.914 ms | 178.511 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 5              | 22567/971.4826 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.407 ms; SubmitStageFoveatedCenter: 1.169 ms   | 219.881 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 5              | 22705/958.2535 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 23365/905.9145 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 23506/817.1278 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 23644/819.1933 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 23781/821.419 ms   | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 23921/817.5041 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 24066/939.6062 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 24208/832.2128 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 97.125 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 24882/861.8121 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 25027/1068.0652 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 89.001 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 25434/821.5988 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |

### Presentation stretch anomalies

| Lane   | Pass | Row | From                                                      | To                                                                           | Consecutive frames | Recovered PASS |
| ------ | ---: | --: | --------------------------------------------------------- | ---------------------------------------------------------------------------- | -----------------: | -------------- |
| nvidia |    1 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    1 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    1 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    1 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    1 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  5 | yes            |
| nvidia |    1 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  20 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":0,"renderScaleMode":false} |                  5 | yes            |
| nvidia |    1 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |        not_exposed | yes            |
| nvidia |    1 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |
| nvidia |    2 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  5 | yes            |
| nvidia |    2 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  20 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":0,"renderScaleMode":false} |                  5 | yes            |
| nvidia |    2 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |        not_exposed | yes            |
| nvidia |    2 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |

## Complete comparison with pinned PR66

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                        | Candidate                                                                                                       |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z                                                              | renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z                                                              |
| Renderer base           | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | 554e484e3957178e2d144bf35266bfdcc0948642                                                                        |
| Main-VR base/equivalent | bf4ae54a7d49620c41cb32ee9ecfd44657688ead                                                                        | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                        |
| Compiled source         | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | 554e484e3957178e2d144bf35266bfdcc0948642                                                                        |
| Build ID                | 9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757                                                | e7e9fa2d13f28c6727b9567741511a418c54113c190db2616bfb2c2ad1158d96                                                |
| DLL SHA-256             | aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461                                                | 5bb50a859ccb62f00d20669a9ad24b6b704dc076054e86dfa8af9dda9db8bfac                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z |

Assessment limits: retained_context_not_matched:scene; retained_context_not_matched:toolchain; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

### Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 793.336/815.094 | 2.743        | 9/15        | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 807.126/768.534 | -4.781       | 10/15       | 0/0          | 0/0                 | none             | MET/MET             |

### Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta     | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | --------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 735.095   | 736.858   | 1.763     | 0.240   |
| nvidia | 1    | Relatch proof mean         | frames      | 13.400    | 13.200    | -0.200    | -1.493  |
| nvidia | 1    | Relatch proof total        | ms          | 18377.376 | 18421.450 | 44.074    | 0.240   |
| nvidia | 1    | Relatch proof total        | frames      | 335       | 330       | -5        | -1.493  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 793.336   | 815.094   | 21.758    | 2.743   |
| nvidia | 1    | Strict completion mean     | frames      | 15.091    | 15.182    | 0.091     | 0.602   |
| nvidia | 1    | Strict completion total    | ms          | 26180.089 | 26898.109 | 718.020   | 2.743   |
| nvidia | 1    | Strict completion total    | frames      | 498       | 501       | 3         | 0.602   |
| nvidia | 1    | Stretch completed episodes | episodes    | 17        | 18        | 1         | 5.882   |
| nvidia | 1    | Stretch completed total    | frames      | 69        | 62        | -7        | -10.145 |
| nvidia | 1    | Stretch completed total    | ms          | 4143.532  | 4080.674  | -62.858   | -1.517  |
| nvidia | 1    | Stretch longest episode    | ms          | 376.746   | 345.476   | -31.270   | -8.300  |
| nvidia | 2    | Relatch proof mean         | ms          | 747.347   | 698.557   | -48.790   | -6.528  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.760    | 13.200    | -0.560    | -4.070  |
| nvidia | 2    | Relatch proof total        | ms          | 18683.664 | 17463.914 | -1219.750 | -6.528  |
| nvidia | 2    | Relatch proof total        | frames      | 344       | 330       | -14       | -4.070  |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 807.126   | 768.534   | -38.591   | -4.781  |
| nvidia | 2    | Strict completion mean     | frames      | 15.303    | 14.818    | -0.485    | -3.168  |
| nvidia | 2    | Strict completion total    | ms          | 26635.148 | 25361.631 | -1273.517 | -4.781  |
| nvidia | 2    | Strict completion total    | frames      | 505       | 489       | -16       | -3.168  |
| nvidia | 2    | Stretch completed episodes | episodes    | 18        | 18        | 0         | 0       |
| nvidia | 2    | Stretch completed total    | frames      | 78        | 62        | -16       | -20.513 |
| nvidia | 2    | Stretch completed total    | ms          | 4955.678  | 4085.360  | -870.318  | -17.562 |
| nvidia | 2    | Stretch longest episode    | ms          | 408.503   | 354.159   | -54.344   | -13.303 |

### nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 680.250 / 776.411   | 96.162   | 14.136  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.466 / 179.985   | 10.519   | 6.207   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 250.227 / 309.025   | 58.798   | 23.498  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 911.526 / 1213.636  | 302.110  | 33.143  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1151.036 / 1380.573 | 229.537  | 19.942  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1085.145 / 1177.831 | 92.686   | 8.541   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1311.362 / 1423.853 | 112.491  | 8.578   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1267.597 / 1398.222 | 130.625  | 10.305  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1257.584 / 1367.283 | 109.700  | 8.723   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 760.848 / 798.935   | 38.087   | 5.006   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 463.236 / 465.233   | 1.997    | 0.431   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 171.239 / 175.991   | 4.752    | 2.775   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 747.166 / 921.078   | 173.912  | 23.276  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1350.983 / 1155.429 | -195.554 | -14.475 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 903.650 / 1002.783  | 99.133   | 10.970  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 731.838 / 1007.986  | 276.148  | 37.734  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 719.437 / 769.460   | 50.024   | 6.953   | 0/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 760.866 / 786.131   | 25.266   | 3.321   | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 781.359 / 1223.303  | 441.944  | 56.561  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 739.768 / 953.041   | 213.273  | 28.830  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 463.465 / 467.713   | 4.247    | 0.916   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 170.549 / 158.622   | -11.927  | -6.993  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 263.125 / 240.633   | -22.492  | -8.548  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1184.660 / 725.341  | -459.318 | -38.772 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1856.757 / 1127.028 | -729.729 | -39.301 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 932.906 / 1009.984  | 77.078   | 8.262   | 0/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 751.644 / 727.177   | -24.467  | -3.255  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 910.661 / 884.295   | -26.366  | -2.895  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1446.692 / 1053.923 | -392.769 | -27.149 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 681.036 / 701.801   | 20.765   | 3.049   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 577.394 / 606.601   | 29.207   | 5.059   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 450.355 / 443.467   | -6.888   | -1.529  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 276.262 / 265.332   | -10.930  | -3.957  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 501.629 / 593.317   | 680.250 / 776.411   | 178.621 / 183.094 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4113,"dispatchToBlockedOrPreparationMs":331.8357,"firstNewGenerationToCleanupDrainedMs":228.8014,"firstPhysicalMutationToFirstNewGenerationMs":116.2013,"presentationToStrictCompletionMs":178.6206}   | {"blockedOrPreparationToFirstPhysicalMutationMs":340.9232,"dispatchToBlockedOrPreparationMs":91.2217,"firstNewGenerationToCleanupDrainedMs":249.0554,"firstPhysicalMutationToFirstNewGenerationMs":95.2109,"presentationToStrictCompletionMs":183.094}   |
| 2   | 169.466 / 179.985   | 169.466 / 179.985   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":90.3612,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 250.227 / 309.025   | 250.227 / 309.025   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2275,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":96.4899,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 789.446 / 984.178   | 829.726 / 1106.032  | 40.280 / 121.855  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3058,"dispatchToBlockedOrPreparationMs":343.3656,"firstNewGenerationToCleanupDrainedMs":159.3035,"firstPhysicalMutationToFirstNewGenerationMs":323.7512,"presentationToStrictCompletionMs":122.0799}   | {"blockedOrPreparationToFirstPhysicalMutationMs":52.7617,"dispatchToBlockedOrPreparationMs":414.2681,"firstNewGenerationToCleanupDrainedMs":228.2539,"firstPhysicalMutationToFirstNewGenerationMs":410.7485,"presentationToStrictCompletionMs":229.4586} |
| 5   | 945.179 / 1126.045  | 1068.436 / 1274.179 | 123.257 / 148.134 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4538,"dispatchToBlockedOrPreparationMs":359.0552,"firstNewGenerationToCleanupDrainedMs":163.5892,"firstPhysicalMutationToFirstNewGenerationMs":542.3382,"presentationToStrictCompletionMs":205.8572}   | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7797,"dispatchToBlockedOrPreparationMs":390.0022,"firstNewGenerationToCleanupDrainedMs":192.3497,"firstPhysicalMutationToFirstNewGenerationMs":636.047,"presentationToStrictCompletionMs":254.5278}  |
| 6   | 877.562 / 939.787   | 1005.163 / 1085.005 | 127.602 / 145.218 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9724,"dispatchToBlockedOrPreparationMs":353.8686,"firstNewGenerationToCleanupDrainedMs":174.4377,"firstPhysicalMutationToFirstNewGenerationMs":472.8845,"presentationToStrictCompletionMs":207.5839}   | {"blockedOrPreparationToFirstPhysicalMutationMs":49.9869,"dispatchToBlockedOrPreparationMs":380.9207,"firstNewGenerationToCleanupDrainedMs":193.4445,"firstPhysicalMutationToFirstNewGenerationMs":460.653,"presentationToStrictCompletionMs":238.0445}  |
| 7   | 1092.614 / 1225.830 | 1227.587 / 1321.959 | 134.973 / 96.129  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4825,"dispatchToBlockedOrPreparationMs":365.9809,"firstNewGenerationToCleanupDrainedMs":192.2705,"firstPhysicalMutationToFirstNewGenerationMs":665.8528,"presentationToStrictCompletionMs":218.7486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":51.0685,"dispatchToBlockedOrPreparationMs":379.0517,"firstNewGenerationToCleanupDrainedMs":185.0477,"firstPhysicalMutationToFirstNewGenerationMs":706.7908,"presentationToStrictCompletionMs":198.0234} |
| 8   | 1048.367 / 1157.610 | 1186.924 / 1301.666 | 138.557 / 144.055 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8603,"dispatchToBlockedOrPreparationMs":343.9502,"firstNewGenerationToCleanupDrainedMs":190.2907,"firstPhysicalMutationToFirstNewGenerationMs":648.823,"presentationToStrictCompletionMs":219.2301}    | {"blockedOrPreparationToFirstPhysicalMutationMs":51.3844,"dispatchToBlockedOrPreparationMs":374.9823,"firstNewGenerationToCleanupDrainedMs":187.6664,"firstPhysicalMutationToFirstNewGenerationMs":687.6324,"presentationToStrictCompletionMs":240.6121} |
| 9   | 1031.843 / 1106.020 | 1174.671 / 1248.893 | 142.827 / 142.873 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3977,"dispatchToBlockedOrPreparationMs":360.4948,"firstNewGenerationToCleanupDrainedMs":184.3618,"firstPhysicalMutationToFirstNewGenerationMs":626.4163,"presentationToStrictCompletionMs":225.7405}   | {"blockedOrPreparationToFirstPhysicalMutationMs":0.5993,"dispatchToBlockedOrPreparationMs":417.7949,"firstNewGenerationToCleanupDrainedMs":185.5526,"firstPhysicalMutationToFirstNewGenerationMs":644.9458,"presentationToStrictCompletionMs":261.2633}  |
| 10  | 590.580 / 664.251   | 760.848 / 798.935   | 170.268 / 134.684 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5661,"dispatchToBlockedOrPreparationMs":360.7792,"firstNewGenerationToCleanupDrainedMs":214.9309,"firstPhysicalMutationToFirstNewGenerationMs":181.5719,"presentationToStrictCompletionMs":170.2681}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3436,"dispatchToBlockedOrPreparationMs":360.4407,"firstNewGenerationToCleanupDrainedMs":182.3977,"firstPhysicalMutationToFirstNewGenerationMs":208.7527,"presentationToStrictCompletionMs":134.6836} |
| 11  | 197.187 / 176.768   | 463.236 / 465.233   | 266.049 / 288.465 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6416,"dispatchToBlockedOrPreparationMs":149.8394,"firstNewGenerationToCleanupDrainedMs":267.2946,"firstPhysicalMutationToFirstNewGenerationMs":41.4606,"presentationToStrictCompletionMs":266.0491}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4611,"dispatchToBlockedOrPreparationMs":131.4955,"firstNewGenerationToCleanupDrainedMs":289.1769,"firstPhysicalMutationToFirstNewGenerationMs":41.0993,"presentationToStrictCompletionMs":288.4649}   |
| 12  | 171.239 / 175.991   | 171.239 / 175.991   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":171.239,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":175.9914,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 747.166 / 921.078   | 747.166 / 921.078   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":192.6286,"dispatchToBlockedOrPreparationMs":394.74,"firstNewGenerationToCleanupDrainedMs":41.0968,"firstPhysicalMutationToFirstNewGenerationMs":118.7009,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":302.9725,"dispatchToBlockedOrPreparationMs":434.6174,"firstNewGenerationToCleanupDrainedMs":44.5645,"firstPhysicalMutationToFirstNewGenerationMs":138.9241,"presentationToStrictCompletionMs":0}        |
| 14  | 1222.442 / 1000.417 | 1306.234 / 1106.483 | 83.792 / 106.066  | {"blockedOrPreparationToFirstPhysicalMutationMs":260.1297,"dispatchToBlockedOrPreparationMs":447.4412,"firstNewGenerationToCleanupDrainedMs":172.4308,"firstPhysicalMutationToFirstNewGenerationMs":426.2326,"presentationToStrictCompletionMs":128.541}  | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0613,"dispatchToBlockedOrPreparationMs":449.3138,"firstNewGenerationToCleanupDrainedMs":194.3451,"firstPhysicalMutationToFirstNewGenerationMs":413.7628,"presentationToStrictCompletionMs":155.012}  |
| 15  | 903.650 / 1002.783  | 561.963 / 728.137   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0595,"dispatchToBlockedOrPreparationMs":360.1568,"firstNewGenerationToCleanupDrainedMs":0.3022,"firstPhysicalMutationToFirstNewGenerationMs":197.4448,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2954,"dispatchToBlockedOrPreparationMs":427.4852,"firstNewGenerationToCleanupDrainedMs":44.4301,"firstPhysicalMutationToFirstNewGenerationMs":207.9264,"presentationToStrictCompletionMs":0}         |
| 16  | 731.838 / 1007.986  | 602.543 / 748.234   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4262,"dispatchToBlockedOrPreparationMs":343.501,"firstNewGenerationToCleanupDrainedMs":42.7009,"firstPhysicalMutationToFirstNewGenerationMs":211.9146,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2088,"dispatchToBlockedOrPreparationMs":417.264,"firstNewGenerationToCleanupDrainedMs":43.3328,"firstPhysicalMutationToFirstNewGenerationMs":239.4284,"presentationToStrictCompletionMs":0}          |
| 17  | 719.437 / 769.460   | 587.405 / 605.502   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4142,"dispatchToBlockedOrPreparationMs":350.0745,"firstNewGenerationToCleanupDrainedMs":42.2475,"firstPhysicalMutationToFirstNewGenerationMs":190.6686,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":42.688,"dispatchToBlockedOrPreparationMs":373.0891,"firstNewGenerationToCleanupDrainedMs":0.3491,"firstPhysicalMutationToFirstNewGenerationMs":189.3757,"presentationToStrictCompletionMs":0}           |
| 18  | 760.866 / 786.131   | 630.628 / 660.916   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9396,"dispatchToBlockedOrPreparationMs":378.3542,"firstNewGenerationToCleanupDrainedMs":46.4289,"firstPhysicalMutationToFirstNewGenerationMs":200.9055,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":45.3155,"dispatchToBlockedOrPreparationMs":380.3142,"firstNewGenerationToCleanupDrainedMs":40.8901,"firstPhysicalMutationToFirstNewGenerationMs":194.3963,"presentationToStrictCompletionMs":0}         |
| 19  | 781.359 / 1223.303  | 650.456 / 685.056   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.493,"dispatchToBlockedOrPreparationMs":388.6542,"firstNewGenerationToCleanupDrainedMs":44.3308,"firstPhysicalMutationToFirstNewGenerationMs":211.9777,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":48.3383,"dispatchToBlockedOrPreparationMs":397.3388,"firstNewGenerationToCleanupDrainedMs":43.3787,"firstPhysicalMutationToFirstNewGenerationMs":195.9997,"presentationToStrictCompletionMs":0}         |
| 20  | 530.869 / 768.589   | 739.768 / 953.041   | 208.899 / 184.452 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5574,"dispatchToBlockedOrPreparationMs":341.9606,"firstNewGenerationToCleanupDrainedMs":261.9474,"firstPhysicalMutationToFirstNewGenerationMs":131.3026,"presentationToStrictCompletionMs":208.8989}   | {"blockedOrPreparationToFirstPhysicalMutationMs":211.9214,"dispatchToBlockedOrPreparationMs":428.0444,"firstNewGenerationToCleanupDrainedMs":229.1844,"firstPhysicalMutationToFirstNewGenerationMs":83.8908,"presentationToStrictCompletionMs":184.4523} |
| 21  | 205.525 / 209.143   | 463.465 / 467.713   | 257.940 / 258.569 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":141.074,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":257.9402}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.3458,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":258.5694}            |
| 22  | 170.549 / 158.622   | 170.549 / 158.622   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.5487,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":158.6221,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 263.125 / 240.633   | 263.125 / 240.633   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":263.1251,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":240.6335,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 993.040 / 597.558   | 1184.660 / 725.341  | 191.620 / 127.783 | {"blockedOrPreparationToFirstPhysicalMutationMs":302.0873,"dispatchToBlockedOrPreparationMs":459.3461,"firstNewGenerationToCleanupDrainedMs":249.2883,"firstPhysicalMutationToFirstNewGenerationMs":173.9381,"presentationToStrictCompletionMs":191.6197} | {"blockedOrPreparationToFirstPhysicalMutationMs":43.0545,"dispatchToBlockedOrPreparationMs":377.1836,"firstNewGenerationToCleanupDrainedMs":167.8687,"firstPhysicalMutationToFirstNewGenerationMs":137.2345,"presentationToStrictCompletionMs":127.7829} |
| 25  | 1645.743 / 914.530  | 1774.359 / 1041.725 | 128.616 / 127.195 | {"blockedOrPreparationToFirstPhysicalMutationMs":691.2409,"dispatchToBlockedOrPreparationMs":433.694,"firstNewGenerationToCleanupDrainedMs":170.4138,"firstPhysicalMutationToFirstNewGenerationMs":479.0101,"presentationToStrictCompletionMs":211.0144}  | {"blockedOrPreparationToFirstPhysicalMutationMs":24.1865,"dispatchToBlockedOrPreparationMs":494.0467,"firstNewGenerationToCleanupDrainedMs":168.1967,"firstPhysicalMutationToFirstNewGenerationMs":355.2946,"presentationToStrictCompletionMs":212.4982} |
| 26  | 803.459 / 918.401   | 888.892 / 965.214   | 85.433 / 46.813   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7511,"dispatchToBlockedOrPreparationMs":381.6439,"firstNewGenerationToCleanupDrainedMs":167.6765,"firstPhysicalMutationToFirstNewGenerationMs":334.8207,"presentationToStrictCompletionMs":129.4467}   | {"blockedOrPreparationToFirstPhysicalMutationMs":46.6122,"dispatchToBlockedOrPreparationMs":421.3456,"firstNewGenerationToCleanupDrainedMs":170.1694,"firstPhysicalMutationToFirstNewGenerationMs":327.0868,"presentationToStrictCompletionMs":91.5829}  |
| 27  | 490.910 / 546.985   | 751.644 / 727.177   | 260.734 / 180.192 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.59,"dispatchToBlockedOrPreparationMs":345.411,"firstNewGenerationToCleanupDrainedMs":261.5421,"firstPhysicalMutationToFirstNewGenerationMs":140.1008,"presentationToStrictCompletionMs":260.7342}      | {"blockedOrPreparationToFirstPhysicalMutationMs":0.9403,"dispatchToBlockedOrPreparationMs":389.236,"firstNewGenerationToCleanupDrainedMs":233.5112,"firstPhysicalMutationToFirstNewGenerationMs":103.489,"presentationToStrictCompletionMs":180.1917}    |
| 28  | 810.416 / 884.295   | 860.954 / 664.092   | 50.538 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5332,"dispatchToBlockedOrPreparationMs":391.0506,"firstNewGenerationToCleanupDrainedMs":187.5037,"firstPhysicalMutationToFirstNewGenerationMs":278.8664,"presentationToStrictCompletionMs":100.2448}   | {"blockedOrPreparationToFirstPhysicalMutationMs":44.6346,"dispatchToBlockedOrPreparationMs":319.5049,"firstNewGenerationToCleanupDrainedMs":41.2796,"firstPhysicalMutationToFirstNewGenerationMs":258.6731,"presentationToStrictCompletionMs":0}         |
| 29  | 1218.375 / 832.834  | 1356.004 / 969.609  | 137.629 / 136.775 | {"blockedOrPreparationToFirstPhysicalMutationMs":292.5704,"dispatchToBlockedOrPreparationMs":402.8201,"firstNewGenerationToCleanupDrainedMs":182.5617,"firstPhysicalMutationToFirstNewGenerationMs":478.0519,"presentationToStrictCompletionMs":228.3174} | {"blockedOrPreparationToFirstPhysicalMutationMs":27.2958,"dispatchToBlockedOrPreparationMs":422.2767,"firstNewGenerationToCleanupDrainedMs":177.3635,"firstPhysicalMutationToFirstNewGenerationMs":342.6734,"presentationToStrictCompletionMs":221.0887} |
| 30  | 492.154 / 526.231   | 681.036 / 701.801   | 188.881 / 175.570 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2695,"dispatchToBlockedOrPreparationMs":336.9345,"firstNewGenerationToCleanupDrainedMs":235.4251,"firstPhysicalMutationToFirstNewGenerationMs":104.4067,"presentationToStrictCompletionMs":188.8815}   | {"blockedOrPreparationToFirstPhysicalMutationMs":43.5348,"dispatchToBlockedOrPreparationMs":359.2,"firstNewGenerationToCleanupDrainedMs":231.5579,"firstPhysicalMutationToFirstNewGenerationMs":67.5083,"presentationToStrictCompletionMs":175.5695}     |
| 31  | 577.394 / 606.601   | 577.394 / 606.601   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1296,"dispatchToBlockedOrPreparationMs":401.3615,"firstNewGenerationToCleanupDrainedMs":39.3942,"firstPhysicalMutationToFirstNewGenerationMs":86.5087,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":63.0553,"dispatchToBlockedOrPreparationMs":403.6118,"firstNewGenerationToCleanupDrainedMs":43.5018,"firstPhysicalMutationToFirstNewGenerationMs":96.4326,"presentationToStrictCompletionMs":0}          |
| 32  | 184.484 / 189.217   | 450.355 / 443.467   | 265.870 / 254.250 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.8185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":265.8704}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":116.8255,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":254.2499}            |
| 33  | 276.262 / 265.332   | 276.262 / 265.332   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":276.2618,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":265.3315,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 9                   | -1           | 451.448 / 527.356    | 75.908   | 15 / 14           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 12 / 14                  | 2            | 670.423 / 877.778    | 207.356  | 17 / 19           | 2            |
| 5   | 12 / 14                  | 2            | 904.847 / 1081.829   | 176.982  | 17 / 19           | 2            |
| 6   | 16 / 17                  | 1            | 830.726 / 891.561    | 60.835   | 21 / 22           | 1            |
| 7   | 16 / 16                  | 0            | 1035.316 / 1136.911  | 101.595  | 21 / 21           | 0            |
| 8   | 16 / 17                  | 1            | 996.634 / 1113.999   | 117.366  | 21 / 22           | 1            |
| 9   | 16 / 17                  | 1            | 990.309 / 1063.340   | 73.031   | 21 / 22           | 1            |
| 10  | 10 / 11                  | 1            | 545.917 / 616.537    | 70.620   | 15 / 15           | 0            |
| 11  | 4 / 3                    | -1           | 195.942 / 176.056    | -19.886  | 11 / 9            | -2           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 10                   | 1            | 706.069 / 876.514    | 170.445  | 10 / 11           | 1            |
| 14  | 24 / 19                  | -5           | 1133.803 / 912.138   | -221.666 | 29 / 24           | -5           |
| 15  | 11 / 14                  | 3            | 561.661 / 683.707    | 122.046  | 19 / 21           | 2            |
| 16  | 12 / 14                  | 2            | 559.842 / 704.901    | 145.059  | 16 / 21           | 5            |
| 17  | 12 / 14                  | 2            | 545.157 / 605.153    | 59.995   | 16 / 18           | 2            |
| 18  | 12 / 13                  | 1            | 584.199 / 620.026    | 35.827   | 16 / 17           | 1            |
| 19  | 12 / 14                  | 2            | 606.125 / 641.677    | 35.552   | 16 / 27           | 11           |
| 20  | 10 / 16                  | 6            | 477.821 / 723.857    | 246.036  | 15 / 22           | 7            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 24  | 16 / 12                  | -4           | 935.371 / 557.473    | -377.899 | 21 / 16           | -5           |
| 25  | 29 / 15                  | -14          | 1603.945 / 873.528   | -730.417 | 34 / 20           | -14          |
| 26  | 13 / 15                  | 2            | 721.216 / 795.045    | 73.829   | 18 / 20           | 2            |
| 27  | 10 / 10                  | 0            | 490.102 / 493.665    | 3.563    | 16 / 16           | 0            |
| 28  | 11 / 10                  | -1           | 673.450 / 622.813    | -50.638  | 16 / 16           | 0            |
| 29  | 23 / 15                  | -8           | 1173.442 / 792.246   | -381.196 | 28 / 20           | -8           |
| 30  | 10 / 11                  | 1            | 445.611 / 470.243    | 24.632   | 15 / 17           | 2            |
| 31  | 9 / 10                   | 1            | 538.000 / 563.100    | 25.100   | 10 / 12           | 2            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 1 / 0                | -1    | 1 / 0              | -1    | 119.890 / 0       | -119.890 |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 199.802 / 254.095 | 54.294   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 199.320 / 217.524 | 18.204   |
| 6   | 1 / 1                | 0     | 6 / 5              | -1    | 371.552 / 337.267 | -34.285  |
| 7   | 1 / 1                | 0     | 6 / 5              | -1    | 376.746 / 345.476 | -31.270  |
| 8   | 1 / 1                | 0     | 6 / 5              | -1    | 366.685 / 337.337 | -29.348  |
| 9   | 1 / 1                | 0     | 6 / 5              | -1    | 370.248 / 343.309 | -26.938  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 6 / 5              | -1    | 268.924 / 219.261 | -49.663  |
| 15  | 1 / 1                | 0     | 3 / 4              | 1     | 142.698 / 207.808 | 65.111   |
| 16  | 1 / 1                | 0     | 3 / 4              | 1     | 160.236 / 239.806 | 79.570   |
| 17  | 1 / 1                | 0     | 3 / 4              | 1     | 140.158 / 189.400 | 49.242   |
| 18  | 1 / 1                | 0     | 3 / 4              | 1     | 148.345 / 194.840 | 46.494   |
| 19  | 1 / 1                | 0     | 3 / 4              | 1     | 154.845 / 196.377 | 41.531   |
| 20  | 0 / 1                | 1     | 0 / 5              | 5     | 0 / 253.677       | 253.677  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 6 / 2              | -4    | 363.912 / 213.478 | -150.434 |
| 26  | 1 / 2                | 1     | 2 / 3              | 1     | 94.387 / 206.467  | 112.080  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 11 / 3             | -8    | 665.784 / 324.551 | -341.232 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

### nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 764.649 / 750.957   | -13.691  | -1.791  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 167.665 / 172.861   | 5.196    | 3.099   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 247.366 / 286.278   | 38.912   | 15.730  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1043.867 / 1084.862 | 40.995   | 3.927   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1038.184 / 1180.402 | 142.218  | 13.699  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1196.438 / 1195.027 | -1.411   | -0.118  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1202.543 / 1205.893 | 3.350    | 0.279   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1191.568 / 1192.834 | 1.266    | 0.106   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1123.080 / 1194.892 | 71.812   | 6.394   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 779.290 / 805.412   | 26.122   | 3.352   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 435.637 / 484.798   | 49.161   | 11.285  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 183.527 / 160.247   | -23.281  | -12.685 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 641.300 / 632.597   | -8.704   | -1.357  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.913 / 1079.451 | -298.462 | -21.660 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 896.172 / 817.128   | -79.044  | -8.820  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 702.990 / 819.193   | 116.203  | 16.530  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 771.931 / 821.419   | 49.488   | 6.411   | 0/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 751.162 / 817.504   | 66.342   | 8.832   | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 727.324 / 939.606   | 212.283  | 29.187  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1078.028 / 1009.718 | -68.310  | -6.337  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 501.448 / 453.071   | -48.377  | -9.647  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 169.901 / 162.812   | -7.089   | -4.172  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 250.712 / 288.590   | 37.879   | 15.108  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1133.454 / 796.742  | -336.712 | -29.707 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1543.613 / 1092.223 | -451.390 | -29.242 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 901.361 / 1068.065  | 166.704  | 18.495  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 819.338 / 748.586   | -70.752  | -8.635  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 984.498 / 974.430   | -10.068  | -1.023  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1932.147 / 1039.385 | -892.762 | -46.206 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 696.614 / 720.991   | 24.377   | 3.499   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 660.497 / 601.567   | -58.930  | -8.922  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 467.899 / 482.117   | 14.218   | 3.039   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 253.033 / 281.976   | 28.943   | 11.438  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C   | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------ | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 524.712 / 556.209  | 764.649 / 750.957   | 239.937 / 194.748 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6788,"dispatchToBlockedOrPreparationMs":398.9273,"firstNewGenerationToCleanupDrainedMs":240.7526,"firstPhysicalMutationToFirstNewGenerationMs":121.2898,"presentationToStrictCompletionMs":239.9367}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.8036,"dispatchToBlockedOrPreparationMs":358.5652,"firstNewGenerationToCleanupDrainedMs":260.2503,"firstPhysicalMutationToFirstNewGenerationMs":84.3382,"presentationToStrictCompletionMs":194.7483}  |
| 2   | 167.665 / 172.861  | 167.665 / 172.861   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.665,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8606,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 247.366 / 286.278  | 247.366 / 286.278   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":247.3658,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.2776,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 816.593 / 901.457  | 956.843 / 993.615   | 140.250 / 92.158  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6999,"dispatchToBlockedOrPreparationMs":408.322,"firstNewGenerationToCleanupDrainedMs":184.2716,"firstPhysicalMutationToFirstNewGenerationMs":359.55,"presentationToStrictCompletionMs":227.2739}      | {"blockedOrPreparationToFirstPhysicalMutationMs":48.9254,"dispatchToBlockedOrPreparationMs":406.4776,"firstNewGenerationToCleanupDrainedMs":180.8965,"firstPhysicalMutationToFirstNewGenerationMs":357.315,"presentationToStrictCompletionMs":183.4047}  |
| 5   | 812.136 / 974.085  | 952.986 / 1078.136  | 140.851 / 104.051 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0876,"dispatchToBlockedOrPreparationMs":414.3874,"firstNewGenerationToCleanupDrainedMs":191.1592,"firstPhysicalMutationToFirstNewGenerationMs":343.3521,"presentationToStrictCompletionMs":226.0486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":66.4558,"dispatchToBlockedOrPreparationMs":393.7835,"firstNewGenerationToCleanupDrainedMs":216.7643,"firstPhysicalMutationToFirstNewGenerationMs":401.1323,"presentationToStrictCompletionMs":206.3166} |
| 6   | 963.683 / 1015.606 | 1108.473 / 1106.145 | 144.789 / 90.540  | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1046,"dispatchToBlockedOrPreparationMs":402.0685,"firstNewGenerationToCleanupDrainedMs":193.2912,"firstPhysicalMutationToFirstNewGenerationMs":508.0083,"presentationToStrictCompletionMs":232.7553}   | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1865,"dispatchToBlockedOrPreparationMs":391.249,"firstNewGenerationToCleanupDrainedMs":188.7139,"firstPhysicalMutationToFirstNewGenerationMs":474.996,"presentationToStrictCompletionMs":179.4213}   |
| 7   | 965.785 / 981.378  | 1113.348 / 1119.901 | 147.564 / 138.524 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1118,"dispatchToBlockedOrPreparationMs":391.181,"firstNewGenerationToCleanupDrainedMs":195.7517,"firstPhysicalMutationToFirstNewGenerationMs":522.3038,"presentationToStrictCompletionMs":236.7582}    | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4652,"dispatchToBlockedOrPreparationMs":401.2388,"firstNewGenerationToCleanupDrainedMs":183.5256,"firstPhysicalMutationToFirstNewGenerationMs":483.6717,"presentationToStrictCompletionMs":224.5152} |
| 8   | 944.879 / 971.483  | 1097.893 / 1107.063 | 153.015 / 135.580 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.849,"dispatchToBlockedOrPreparationMs":386.0721,"firstNewGenerationToCleanupDrainedMs":201.7934,"firstPhysicalMutationToFirstNewGenerationMs":506.1788,"presentationToStrictCompletionMs":246.6891}    | {"blockedOrPreparationToFirstPhysicalMutationMs":48.6324,"dispatchToBlockedOrPreparationMs":395.5498,"firstNewGenerationToCleanupDrainedMs":179.0661,"firstPhysicalMutationToFirstNewGenerationMs":483.8143,"presentationToStrictCompletionMs":221.3515} |
| 9   | 892.407 / 958.254  | 1033.092 / 1110.311 | 140.686 / 152.057 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0101,"dispatchToBlockedOrPreparationMs":350.1775,"firstNewGenerationToCleanupDrainedMs":184.5374,"firstPhysicalMutationToFirstNewGenerationMs":494.3675,"presentationToStrictCompletionMs":230.6732}   | {"blockedOrPreparationToFirstPhysicalMutationMs":50.4611,"dispatchToBlockedOrPreparationMs":401.1379,"firstNewGenerationToCleanupDrainedMs":195.1782,"firstPhysicalMutationToFirstNewGenerationMs":463.5337,"presentationToStrictCompletionMs":236.6381} |
| 10  | 604.626 / 672.026  | 779.290 / 805.412   | 174.664 / 133.387 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5596,"dispatchToBlockedOrPreparationMs":361.0118,"firstNewGenerationToCleanupDrainedMs":220.7107,"firstPhysicalMutationToFirstNewGenerationMs":193.0078,"presentationToStrictCompletionMs":174.6636}   | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1025,"dispatchToBlockedOrPreparationMs":370.241,"firstNewGenerationToCleanupDrainedMs":180.3564,"firstPhysicalMutationToFirstNewGenerationMs":204.7122,"presentationToStrictCompletionMs":133.3865}  |
| 11  | 163.343 / 197.653  | 435.637 / 484.798   | 272.294 / 287.145 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6609,"dispatchToBlockedOrPreparationMs":121.2179,"firstNewGenerationToCleanupDrainedMs":273.0555,"firstPhysicalMutationToFirstNewGenerationMs":37.7029,"presentationToStrictCompletionMs":272.2943}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6255,"dispatchToBlockedOrPreparationMs":145.0668,"firstNewGenerationToCleanupDrainedMs":287.8392,"firstPhysicalMutationToFirstNewGenerationMs":48.2665,"presentationToStrictCompletionMs":287.145}    |
| 12  | 183.527 / 160.247  | 183.527 / 160.247   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":183.5274,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":160.2466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 641.300 / 632.597  | 641.300 / 632.597   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0337,"dispatchToBlockedOrPreparationMs":429.1853,"firstNewGenerationToCleanupDrainedMs":47.1866,"firstPhysicalMutationToFirstNewGenerationMs":105.8945,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":52.8288,"dispatchToBlockedOrPreparationMs":430.0565,"firstNewGenerationToCleanupDrainedMs":44.353,"firstPhysicalMutationToFirstNewGenerationMs":105.3583,"presentationToStrictCompletionMs":0}          |
| 14  | 1171.620 / 905.914 | 1323.721 / 1033.575 | 152.101 / 127.660 | {"blockedOrPreparationToFirstPhysicalMutationMs":262.493,"dispatchToBlockedOrPreparationMs":397.1475,"firstNewGenerationToCleanupDrainedMs":203.3643,"firstPhysicalMutationToFirstNewGenerationMs":460.7159,"presentationToStrictCompletionMs":206.2935}  | {"blockedOrPreparationToFirstPhysicalMutationMs":46.5767,"dispatchToBlockedOrPreparationMs":402.3021,"firstNewGenerationToCleanupDrainedMs":170.0301,"firstPhysicalMutationToFirstNewGenerationMs":414.6658,"presentationToStrictCompletionMs":173.5363} |
| 15  | 896.172 / 817.128  | 558.987 / 728.821   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1702,"dispatchToBlockedOrPreparationMs":352.6839,"firstNewGenerationToCleanupDrainedMs":0.2574,"firstPhysicalMutationToFirstNewGenerationMs":201.8751,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":50.0628,"dispatchToBlockedOrPreparationMs":413.8539,"firstNewGenerationToCleanupDrainedMs":41.3177,"firstPhysicalMutationToFirstNewGenerationMs":223.5868,"presentationToStrictCompletionMs":0}         |
| 16  | 702.990 / 819.193  | 611.687 / 685.026   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7991,"dispatchToBlockedOrPreparationMs":368.7131,"firstNewGenerationToCleanupDrainedMs":43.5961,"firstPhysicalMutationToFirstNewGenerationMs":194.5788,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":44.7755,"dispatchToBlockedOrPreparationMs":391.2656,"firstNewGenerationToCleanupDrainedMs":43.9256,"firstPhysicalMutationToFirstNewGenerationMs":205.0593,"presentationToStrictCompletionMs":0}         |
| 17  | 771.931 / 821.419  | 595.296 / 692.394   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0713,"dispatchToBlockedOrPreparationMs":350.708,"firstNewGenerationToCleanupDrainedMs":42.6388,"firstPhysicalMutationToFirstNewGenerationMs":195.8778,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":45.9464,"dispatchToBlockedOrPreparationMs":401.6109,"firstNewGenerationToCleanupDrainedMs":40.6797,"firstPhysicalMutationToFirstNewGenerationMs":204.1569,"presentationToStrictCompletionMs":0}         |
| 18  | 751.162 / 817.504  | 619.652 / 675.719   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5809,"dispatchToBlockedOrPreparationMs":376.7105,"firstNewGenerationToCleanupDrainedMs":43.5683,"firstPhysicalMutationToFirstNewGenerationMs":194.792,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":47.1171,"dispatchToBlockedOrPreparationMs":389.3872,"firstNewGenerationToCleanupDrainedMs":41.6418,"firstPhysicalMutationToFirstNewGenerationMs":197.5732,"presentationToStrictCompletionMs":0}         |
| 19  | 727.324 / 939.606  | 556.316 / 689.071   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8053,"dispatchToBlockedOrPreparationMs":356.7758,"firstNewGenerationToCleanupDrainedMs":0.3603,"firstPhysicalMutationToFirstNewGenerationMs":194.375,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":44.9648,"dispatchToBlockedOrPreparationMs":404.2762,"firstNewGenerationToCleanupDrainedMs":41.4998,"firstPhysicalMutationToFirstNewGenerationMs":198.33,"presentationToStrictCompletionMs":0}           |
| 20  | 901.838 / 832.213  | 1078.028 / 1009.718 | 176.190 / 177.505 | {"blockedOrPreparationToFirstPhysicalMutationMs":267.4793,"dispatchToBlockedOrPreparationMs":463.6941,"firstNewGenerationToCleanupDrainedMs":224.7261,"firstPhysicalMutationToFirstNewGenerationMs":122.1281,"presentationToStrictCompletionMs":176.1895} | {"blockedOrPreparationToFirstPhysicalMutationMs":238.13,"dispatchToBlockedOrPreparationMs":453.5158,"firstNewGenerationToCleanupDrainedMs":227.8665,"firstPhysicalMutationToFirstNewGenerationMs":90.2054,"presentationToStrictCompletionMs":177.5049}   |
| 21  | 202.830 / 189.077  | 501.448 / 453.071   | 298.618 / 263.994 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.706,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":298.6185}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":126.7527,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":263.9937}            |
| 22  | 169.901 / 162.812  | 169.901 / 162.812   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.9006,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":162.8116,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 250.712 / 288.590  | 250.712 / 288.590   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.7117,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":288.5903,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 955.472 / 662.606  | 1133.454 / 796.742  | 177.982 / 134.135 | {"blockedOrPreparationToFirstPhysicalMutationMs":266.0538,"dispatchToBlockedOrPreparationMs":500.9016,"firstNewGenerationToCleanupDrainedMs":221.3993,"firstPhysicalMutationToFirstNewGenerationMs":145.0989,"presentationToStrictCompletionMs":177.9818} | {"blockedOrPreparationToFirstPhysicalMutationMs":45.811,"dispatchToBlockedOrPreparationMs":411.9901,"firstNewGenerationToCleanupDrainedMs":185.9268,"firstPhysicalMutationToFirstNewGenerationMs":153.0138,"presentationToStrictCompletionMs":134.1354}  |
| 25  | 1323.681 / 861.812 | 1457.896 / 1006.636 | 134.215 / 144.824 | {"blockedOrPreparationToFirstPhysicalMutationMs":321.4216,"dispatchToBlockedOrPreparationMs":449.9428,"firstNewGenerationToCleanupDrainedMs":178.1383,"firstPhysicalMutationToFirstNewGenerationMs":508.3934,"presentationToStrictCompletionMs":219.9317} | {"blockedOrPreparationToFirstPhysicalMutationMs":21.7457,"dispatchToBlockedOrPreparationMs":454.0392,"firstNewGenerationToCleanupDrainedMs":195.2621,"firstPhysicalMutationToFirstNewGenerationMs":335.5889,"presentationToStrictCompletionMs":230.4107} |
| 26  | 763.246 / 1068.065 | 901.361 / 1011.510  | 138.115 / 0       | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1998,"dispatchToBlockedOrPreparationMs":367.3261,"firstNewGenerationToCleanupDrainedMs":226.1821,"firstPhysicalMutationToFirstNewGenerationMs":303.6534,"presentationToStrictCompletionMs":138.1153}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3468,"dispatchToBlockedOrPreparationMs":408.0517,"firstNewGenerationToCleanupDrainedMs":216.359,"firstPhysicalMutationToFirstNewGenerationMs":339.7526,"presentationToStrictCompletionMs":0}         |
| 27  | 559.830 / 567.395  | 819.338 / 748.586   | 259.509 / 181.191 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7194,"dispatchToBlockedOrPreparationMs":406.2385,"firstNewGenerationToCleanupDrainedMs":260.7611,"firstPhysicalMutationToFirstNewGenerationMs":147.6192,"presentationToStrictCompletionMs":259.5085}   | {"blockedOrPreparationToFirstPhysicalMutationMs":50.2463,"dispatchToBlockedOrPreparationMs":347.906,"firstNewGenerationToCleanupDrainedMs":236.1733,"firstPhysicalMutationToFirstNewGenerationMs":114.2602,"presentationToStrictCompletionMs":181.1906}  |
| 28  | 849.139 / 870.041  | 938.197 / 919.042   | 89.058 / 49.000   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3027,"dispatchToBlockedOrPreparationMs":443.3656,"firstNewGenerationToCleanupDrainedMs":175.5081,"firstPhysicalMutationToFirstNewGenerationMs":315.0207,"presentationToStrictCompletionMs":135.3592}   | {"blockedOrPreparationToFirstPhysicalMutationMs":57.5457,"dispatchToBlockedOrPreparationMs":396.541,"firstNewGenerationToCleanupDrainedMs":175.0451,"firstPhysicalMutationToFirstNewGenerationMs":289.9098,"presentationToStrictCompletionMs":104.3887}  |
| 29  | 1762.502 / 821.599 | 1850.173 / 958.330  | 87.671 / 136.731  | {"blockedOrPreparationToFirstPhysicalMutationMs":717.4208,"dispatchToBlockedOrPreparationMs":454.3886,"firstNewGenerationToCleanupDrainedMs":172.8374,"firstPhysicalMutationToFirstNewGenerationMs":505.5261,"presentationToStrictCompletionMs":169.6445} | {"blockedOrPreparationToFirstPhysicalMutationMs":19.8893,"dispatchToBlockedOrPreparationMs":450.0967,"firstNewGenerationToCleanupDrainedMs":178.5163,"firstPhysicalMutationToFirstNewGenerationMs":309.8279,"presentationToStrictCompletionMs":217.7862} |
| 30  | 464.694 / 525.686  | 696.614 / 720.991   | 231.920 / 195.306 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7447,"dispatchToBlockedOrPreparationMs":350.0409,"firstNewGenerationToCleanupDrainedMs":232.4266,"firstPhysicalMutationToFirstNewGenerationMs":110.402,"presentationToStrictCompletionMs":231.9199}    | {"blockedOrPreparationToFirstPhysicalMutationMs":48.3199,"dispatchToBlockedOrPreparationMs":350.8517,"firstNewGenerationToCleanupDrainedMs":249.0011,"firstPhysicalMutationToFirstNewGenerationMs":72.8185,"presentationToStrictCompletionMs":195.3056}  |
| 31  | 660.497 / 601.567  | 660.497 / 601.567   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":61.1083,"dispatchToBlockedOrPreparationMs":461.9829,"firstNewGenerationToCleanupDrainedMs":42.7901,"firstPhysicalMutationToFirstNewGenerationMs":94.6154,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":48.0276,"dispatchToBlockedOrPreparationMs":414.816,"firstNewGenerationToCleanupDrainedMs":42.5589,"firstPhysicalMutationToFirstNewGenerationMs":96.1643,"presentationToStrictCompletionMs":0}           |
| 32  | 190.736 / 185.789  | 467.899 / 482.117   | 277.163 / 296.327 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.4716,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":277.1627}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":124.7113,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":296.3274}            |
| 33  | 253.033 / 281.976  | 253.033 / 281.976   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":253.0332,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":281.9764,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 523.896 / 490.707    | -33.189  | 16 / 15           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 5             | 1            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 14                  | 0            | 772.572 / 812.718    | 40.146   | 19 / 19           | 0            |
| 5   | 14 / 13                  | -1           | 761.827 / 861.372    | 99.544   | 19 / 18           | -1           |
| 6   | 18 / 17                  | -1           | 915.181 / 917.432    | 2.250    | 23 / 22           | -1           |
| 7   | 17 / 18                  | 1            | 917.597 / 936.376    | 18.779   | 22 / 23           | 1            |
| 8   | 17 / 18                  | 1            | 896.100 / 927.996    | 31.897   | 22 / 23           | 1            |
| 9   | 16 / 17                  | 1            | 848.555 / 915.133    | 66.578   | 21 / 22           | 1            |
| 10  | 9 / 10                   | 1            | 558.579 / 625.056    | 66.476   | 14 / 14           | 0            |
| 11  | 3 / 3                    | 0            | 162.582 / 196.959    | 34.377   | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 13  | 9 / 10                   | 1            | 594.114 / 588.244    | -5.870   | 10 / 11           | 1            |
| 14  | 23 / 18                  | -5           | 1120.356 / 863.545   | -256.812 | 28 / 23           | -5           |
| 15  | 12 / 14                  | 2            | 558.729 / 687.504    | 128.774  | 19 / 17           | -2           |
| 16  | 12 / 14                  | 2            | 568.091 / 641.100    | 73.009   | 16 / 18           | 2            |
| 17  | 12 / 14                  | 2            | 552.657 / 651.714    | 99.057   | 17 / 18           | 1            |
| 18  | 12 / 14                  | 2            | 576.083 / 634.077    | 57.994   | 16 / 18           | 2            |
| 19  | 12 / 14                  | 2            | 555.956 / 647.571    | 91.615   | 16 / 21           | 5            |
| 20  | 16 / 16                  | 0            | 853.302 / 781.851    | -71.450  | 21 / 21           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 15 / 11                  | -4           | 912.054 / 610.815    | -301.239 | 20 / 15           | -5           |
| 25  | 23 / 15                  | -8           | 1279.758 / 811.374   | -468.384 | 28 / 20           | -8           |
| 26  | 12 / 14                  | 2            | 675.179 / 795.151    | 119.972  | 17 / 19           | 2            |
| 27  | 10 / 10                  | 0            | 558.577 / 512.413    | -46.165  | 16 / 16           | 0            |
| 28  | 11 / 12                  | 1            | 762.689 / 743.996    | -18.692  | 16 / 17           | 1            |
| 29  | 29 / 15                  | -14          | 1677.335 / 779.814   | -897.522 | 34 / 20           | -14          |
| 30  | 9 / 9                    | 0            | 464.188 / 471.990    | 7.803    | 15 / 15           | 0            |
| 31  | 9 / 10                   | 1            | 617.707 / 559.008    | -58.699  | 10 / 11           | 1            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 9            | -1           |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 237.463 / 214.495  | -22.968  |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 232.347 / 245.001  | 12.654   |
| 6   | 1 / 1                | 0     | 6 / 5              | -1    | 400.832 / 345.614  | -55.218  |
| 7   | 1 / 1                | 0     | 6 / 5              | -1    | 401.917 / 354.159  | -47.758  |
| 8   | 1 / 1                | 0     | 6 / 5              | -1    | 401.944 / 351.013  | -50.931  |
| 9   | 1 / 1                | 0     | 6 / 5              | -1    | 390.033 / 332.710  | -57.323  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 5              | -1    | 307.782 / 221.958  | -85.824  |
| 15  | 1 / 1                | 0     | 3 / 4              | 1     | 143.903 / 223.742  | 79.839   |
| 16  | 1 / 1                | 0     | 3 / 4              | 1     | 141.176 / 205.174  | 63.998   |
| 17  | 1 / 1                | 0     | 3 / 4              | 1     | 144.864 / 204.045  | 59.182   |
| 18  | 1 / 1                | 0     | 3 / 4              | 1     | 144.350 / 198.058  | 53.708   |
| 19  | 1 / 1                | 0     | 3 / 4              | 1     | 143.964 / 198.300  | 54.336   |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 334.871 / 283.340  | -51.531  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 6 / 2              | -4    | 395.523 / 203.305  | -192.218 |
| 26  | 1 / 2                | 1     | 2 / 3              | 1     | 93.461 / 220.362   | 126.901  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 2                | -1    | 16 / 3             | -13   | 1041.248 / 284.083 | -757.165 |
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

#### renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":20782,"leftPath":"NativeOriginal","referenceFrame":21209,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

#### renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":25566,"leftPath":"NativeOriginal","referenceFrame":25971,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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
| nvidia | 1    | processPrivateMiB | 16521.3203125 / 16407.21484375 / -114.10546875 | 16430.2890625 / 16245.953125 / -184.3359375    | -70.230               |
| nvidia | 1    | systemCommitMiB   | 54414.625 / 54313.40234375 / -101.22265625     | 57091.7109375 / 56790.00390625 / -301.70703125 | -200.484              |
| nvidia | 1    | dxgiUsageMiB      | 4201.6171875 / 3527.0078125 / -674.609375      | 4191.01953125 / 3286.94921875 / -904.0703125   | -229.461              |
| nvidia | 1    | liveTextures      | 0 / 256 / 256                                  | 0 / 247 / 247                                  | -9                    |
| nvidia | 1    | liveTextureMiB    | 0 / 2424.0259971618652 / 2424.0259971618652    | 0 / 2405.307025909424 / 2405.307025909424      | -18.719               |
| nvidia | 2    | processPrivateMiB | 16831.234375 / 16617.78515625 / -213.44921875  | 16646.34375 / 16475.06640625 / -171.27734375   | 42.172                |
| nvidia | 2    | systemCommitMiB   | 54507.67578125 / 54547.8203125 / 40.14453125   | 57301.5625 / 57090.9921875 / -210.5703125      | -250.715              |
| nvidia | 2    | dxgiUsageMiB      | 3889.8828125 / 3566.79296875 / -323.08984375   | 3680.19921875 / 3536.84765625 / -143.3515625   | 179.738               |
| nvidia | 2    | liveTextures      | 0 / 233 / 233                                  | 0 / 241 / 241                                  | 8                     |
| nvidia | 2    | liveTextureMiB    | 0 / 2349.4316596984863 / 2349.4316596984863    | 0 / 2379.9213676452637 / 2379.9213676452637    | 30.490                |

### Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2222        | 2251        | 29         |
| cpu/compactPresentationContract/reuses                  | 2200        | 2229        | 29         |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 151         | 153         | 2          |
| cpu/generationResourceValidation/fullValidations        | 568         | 693         | 125        |
| cpu/generationResourceValidation/stableChecks           | 8397        | 8523        | 126        |
| cpu/generationResourceValidation/stableHits             | 8316        | 8508        | 192        |
| cpu/generationResourceValidation/stableMisses           | 81          | 15          | -66        |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 1           | 1           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4328        | 4360        | 32         |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4328        | 4360        | 32         |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4286        | 4318        | 32         |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4328        | 4360        | 32         |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4307        | 4337        | 30         |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 23          | 2          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 30          | -4         |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4294        | 4330        | 36         |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4238        | 4266        | 28         |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 93          | 3          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4313        | 4345        | 32         |
| cpu/strongStereoPacket/captures                         | 4628        | 4648        | 20         |
| cpu/strongStereoPacket/commitAccepts                    | 4424        | 4484        | 60         |
| cpu/strongStereoPacket/commitRejects                    | 66          | 64          | -2         |
| cpu/strongStereoPacket/commitValidations                | 4490        | 4548        | 58         |
| cpu/strongStereoPacket/cycleReuses                      | 2275        | 2286        | 11         |
| cpu/strongStereoPacket/fastSkips                        | 4028        | 4072        | 44         |
| cpu/strongStereoPacket/invalidations                    | 4882        | 4906        | 24         |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 100         | 92          | -8         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2253        | 2270        | 17         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.855       | 1.756       | -0.099     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 68.100      | 68.900      | 0.800      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.147       | 0.158       | 0.010      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 11.900      | 18.900      | 7.000      |
| cpu/window/currentFrame                                 | 18480       | 21210       | 2730       |
| cpu/window/elapsedFrames                                | 4328        | 4359        | 31         |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 14152       | 16851       | 2699       |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 18482       | 21211       | 2729       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6230998224  | 6300781344  | 69783120   |
| gpu/item10PeripheryTAAHistory/dispatches                | 4911        | 4966        | 55         |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12474725760 | 12614434560 | 139708800  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.305       | -0.001     |
| gpu/item5ActiveFSRCopies/activePixels                   | 12047940400 | 12576906800 | 528966400  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27299138000 | 28649890000 | 1350752000 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15490       | 16230       | 740        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 2045        | 2138        | 93         |
| gpu/item7EarlyHAM/executedClears                        | 2052        | 2004        | -48        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2052        | 2004        | -48        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4256        | 4328        | 72         |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4330        | 4360        | 30         |
| gpu/startFrame                                          | 14152       | 16851       | 2699       |
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
| texture/createdCount                                    | 3974        | 3977        | 3          |
| texture/createdEstimatedBytes                           | 38786366328 | 38776359208 | -10007120  |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3718        | 3730        | 12         |
| texture/destroyedEstimatedBytes                         | 36244590844 | 36254211988 | 9621144    |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 896         | 896         | 0          |
| texture/liveTextureRecordCount                          | 256         | 247         | -9         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 47          | 38          | -9         |
| texture/niSourceTextureMatchedEstimatedBytes            | 166123904   | 146495640   | -19628264  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1473        | 1450        | -23        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 256         | 247         | -9         |
| texture/outstandingEstimatedBytes                       | 2541775484  | 2522147220  | -19628264  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 1           | 1           | 0          |
| texture/supported                                       | true        | true        | n/a        |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2189        | 2244        | 55         |
| cpu/compactPresentationContract/reuses                  | 2167        | 2220        | 53         |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 156         | 156         | 0          |
| cpu/generationResourceValidation/fullValidations        | 575         | 684         | 109        |
| cpu/generationResourceValidation/stableChecks           | 8303        | 8465        | 162        |
| cpu/generationResourceValidation/stableHits             | 8226        | 8442        | 216        |
| cpu/generationResourceValidation/stableMisses           | 77          | 23          | -54        |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 2           | 2           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4221        | 4346        | 125        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4221        | 4346        | 125        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4179        | 4304        | 125        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4221        | 4346        | 125        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4200        | 4325        | 125        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 30          | -4         |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4187        | 4316        | 129        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4130        | 4252        | 122        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 94          | 4          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4206        | 4331        | 125        |
| cpu/strongStereoPacket/captures                         | 4548        | 4633        | 85         |
| cpu/strongStereoPacket/commitAccepts                    | 4359        | 4466        | 107        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 66          | 1          |
| cpu/strongStereoPacket/commitValidations                | 4424        | 4532        | 108        |
| cpu/strongStereoPacket/cycleReuses                      | 2233        | 2277        | 44         |
| cpu/strongStereoPacket/fastSkips                        | 3894        | 4059        | 165        |
| cpu/strongStereoPacket/invalidations                    | 4767        | 4893        | 126        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 93          | -10        |
| cpu/strongStereoPacket/lifetimeReuses                   | 2212        | 2263        | 51         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.879       | 1.652       | -0.226     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 25.400      | 27.400      | 2          |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.143       | 0.155       | 0.013      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | 22.100      | 20.500     |
| cpu/window/currentFrame                                 | 23112       | 25971       | 2859       |
| cpu/window/elapsedFrames                                | 4220        | 4346        | 126        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 18892       | 21625       | 2733       |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 23113       | 25972       | 2859       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6071131440  | 6258911472  | 187780032  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4785        | 4933        | 148        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12154665600 | 12530609280 | 375943680  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.309       | 0.305       | -0.004     |
| gpu/item5ActiveFSRCopies/activePixels                   | 11865705520 | 12379172080 | 513466560  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26516112080 | 28161781520 | 1645669440 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15110       | 15960       | 850        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1991        | 2103        | 112        |
| gpu/item7EarlyHAM/executedClears                        | 2016        | 2014        | -2         |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2016        | 2014        | -2         |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4168        | 4310        | 142        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4221        | 4347        | 126        |
| gpu/startFrame                                          | 18892       | 21625       | 2733       |
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
| texture/createdCount                                    | 3968        | 3958        | -10        |
| texture/createdEstimatedBytes                           | 38826844112 | 38734100840 | -92743272  |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3735        | 3717        | -18        |
| texture/destroyedEstimatedBytes                         | 36363286460 | 36238572412 | -124714048 |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 887         | 892         | 5          |
| texture/liveTextureRecordCount                          | 233         | 241         | 8          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 26          | 34          | 8          |
| texture/niSourceTextureMatchedEstimatedBytes            | 87906272    | 119877048   | 31970776   |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1506        | 1484        | -22        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 233         | 241         | 8          |
| texture/outstandingEstimatedBytes                       | 2463557652  | 2495528428  | 31970776   |
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

## Complete comparison with previous PR73

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                               | Candidate                                                                                                       |
| ----------------------- | -------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | nv-pr73-269bded1-mtwz6pez                                                              | renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z                                                              |
| Renderer base           | 269bded159c66f4d8b1a45a836078dfa6cdf3241                                               | 554e484e3957178e2d144bf35266bfdcc0948642                                                                        |
| Main-VR base/equivalent | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                               | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                        |
| Compiled source         | 269bded159c66f4d8b1a45a836078dfa6cdf3241                                               | 554e484e3957178e2d144bf35266bfdcc0948642                                                                        |
| Build ID                | 9b8b7f17af7ec7012eb194ad945b45756db313352ef6b2ec84d88a50bef9247b                       | e7e9fa2d13f28c6727b9567741511a418c54113c190db2616bfb2c2ad1158d96                                                |
| DLL SHA-256             | 086b2207bd4b615af5f7a1bb4f567bb0dba8e1c1ec5e5498cd440c623682380a                       | 5bb50a859ccb62f00d20669a9ad24b6b704dc076054e86dfa8af9dda9db8bfac                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nv-pr73-269bded1-mtwz6pez | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z |

Assessment limits: retained_context_not_matched:scene; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

### Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 822.547/815.094 | -0.906       | 15/15       | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 808.634/768.534 | -4.959       | 15/15       | 0/0          | 0/0                 | none             | MET/MET             |

### Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta     | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | --------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 774.289   | 736.858   | -37.431   | -4.834  |
| nvidia | 1    | Relatch proof mean         | frames      | 14.120    | 13.200    | -0.920    | -6.516  |
| nvidia | 1    | Relatch proof total        | ms          | 19357.219 | 18421.450 | -935.769  | -4.834  |
| nvidia | 1    | Relatch proof total        | frames      | 353       | 330       | -23       | -6.516  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 822.547   | 815.094   | -7.453    | -0.906  |
| nvidia | 1    | Strict completion mean     | frames      | 15.455    | 15.182    | -0.273    | -1.765  |
| nvidia | 1    | Strict completion total    | ms          | 27144.053 | 26898.109 | -245.944  | -0.906  |
| nvidia | 1    | Strict completion total    | frames      | 510       | 501       | -9        | -1.765  |
| nvidia | 1    | Stretch completed episodes | episodes    | 19        | 18        | -1        | -5.263  |
| nvidia | 1    | Stretch completed total    | frames      | 88        | 62        | -26       | -29.545 |
| nvidia | 1    | Stretch completed total    | ms          | 5331.101  | 4080.674  | -1250.427 | -23.455 |
| nvidia | 1    | Stretch longest episode    | ms          | 373.101   | 345.476   | -27.625   | -7.404  |
| nvidia | 2    | Relatch proof mean         | ms          | 742.529   | 698.557   | -43.973   | -5.922  |
| nvidia | 2    | Relatch proof mean         | frames      | 14.120    | 13.200    | -0.920    | -6.516  |
| nvidia | 2    | Relatch proof total        | ms          | 18563.231 | 17463.914 | -1099.316 | -5.922  |
| nvidia | 2    | Relatch proof total        | frames      | 353       | 330       | -23       | -6.516  |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 808.634   | 768.534   | -40.100   | -4.959  |
| nvidia | 2    | Strict completion mean     | frames      | 15.636    | 14.818    | -0.818    | -5.233  |
| nvidia | 2    | Strict completion total    | ms          | 26684.937 | 25361.631 | -1323.305 | -4.959  |
| nvidia | 2    | Strict completion total    | frames      | 516       | 489       | -27       | -5.233  |
| nvidia | 2    | Stretch completed episodes | episodes    | 18        | 18        | 0         | 0       |
| nvidia | 2    | Stretch completed total    | frames      | 86        | 62        | -24       | -27.907 |
| nvidia | 2    | Stretch completed total    | ms          | 5210.460  | 4085.360  | -1125.100 | -21.593 |
| nvidia | 2    | Stretch longest episode    | ms          | 371.788   | 354.159   | -17.629   | -4.742  |

### nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 744.023 / 776.411   | 32.388   | 4.353   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 168.169 / 179.985   | 11.816   | 7.026   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 249.964 / 309.025   | 59.061   | 23.628  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1009.432 / 1213.636 | 204.204  | 20.230  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1209.110 / 1380.573 | 171.463  | 14.181  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1171.551 / 1177.831 | 6.281    | 0.536   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1319.767 / 1423.853 | 104.086  | 7.887   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1356.821 / 1398.222 | 41.402   | 3.051   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1288.897 / 1367.283 | 78.387   | 6.082   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 775.051 / 798.935   | 23.884   | 3.082   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 479.922 / 465.233   | -14.689  | -3.061  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 172.782 / 175.991   | 3.209    | 1.857   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 809.235 / 921.078   | 111.843  | 13.821  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1098.561 / 1155.429 | 56.868   | 5.177   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 926.425 / 1002.783  | 76.358   | 8.242   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 983.356 / 1007.986  | 24.629   | 2.505   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 1050.889 / 769.460  | -281.429 | -26.780 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1079.167 / 786.131  | -293.035 | -27.154 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 975.330 / 1223.303  | 247.972  | 25.424  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1028.868 / 953.041  | -75.827  | -7.370  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 481.522 / 467.713   | -13.810  | -2.868  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 170.781 / 158.622   | -12.159  | -7.119  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 301.218 / 240.633   | -60.585  | -20.113 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 808.217 / 725.341   | -82.876  | -10.254 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1222.446 / 1127.028 | -95.418  | -7.806  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1150.180 / 1009.984 | -140.196 | -12.189 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 761.685 / 727.177   | -34.509  | -4.531  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 920.986 / 884.295   | -36.690  | -3.984  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1233.788 / 1053.923 | -179.866 | -14.578 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 780.674 / 701.801   | -78.873  | -10.103 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 636.911 / 606.601   | -30.310  | -4.759  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 516.357 / 443.467   | -72.890  | -14.116 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 261.967 / 265.332   | 3.364    | 1.284   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 552.732 / 593.317   | 744.023 / 776.411   | 191.290 / 183.094 | {"blockedOrPreparationToFirstPhysicalMutationMs":46.2207,"dispatchToBlockedOrPreparationMs":362.6825,"firstNewGenerationToCleanupDrainedMs":242.4389,"firstPhysicalMutationToFirstNewGenerationMs":92.6807,"presentationToStrictCompletionMs":191.2904}  | {"blockedOrPreparationToFirstPhysicalMutationMs":340.9232,"dispatchToBlockedOrPreparationMs":91.2217,"firstNewGenerationToCleanupDrainedMs":249.0554,"firstPhysicalMutationToFirstNewGenerationMs":95.2109,"presentationToStrictCompletionMs":183.094}   |
| 2   | 168.169 / 179.985   | 168.169 / 179.985   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":168.1689,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":90.3612,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 249.964 / 309.025   | 249.964 / 309.025   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.964,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":96.4899,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 839.821 / 984.178   | 924.897 / 1106.032  | 85.077 / 121.855  | {"blockedOrPreparationToFirstPhysicalMutationMs":46.1203,"dispatchToBlockedOrPreparationMs":361.1041,"firstNewGenerationToCleanupDrainedMs":172.6253,"firstPhysicalMutationToFirstNewGenerationMs":345.0476,"presentationToStrictCompletionMs":169.6118} | {"blockedOrPreparationToFirstPhysicalMutationMs":52.7617,"dispatchToBlockedOrPreparationMs":414.2681,"firstNewGenerationToCleanupDrainedMs":228.2539,"firstPhysicalMutationToFirstNewGenerationMs":410.7485,"presentationToStrictCompletionMs":229.4586} |
| 5   | 995.161 / 1126.045  | 1124.310 / 1274.179 | 129.149 / 148.134 | {"blockedOrPreparationToFirstPhysicalMutationMs":55.0599,"dispatchToBlockedOrPreparationMs":342.7084,"firstNewGenerationToCleanupDrainedMs":172.2143,"firstPhysicalMutationToFirstNewGenerationMs":554.3277,"presentationToStrictCompletionMs":213.9487} | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7797,"dispatchToBlockedOrPreparationMs":390.0022,"firstNewGenerationToCleanupDrainedMs":192.3497,"firstPhysicalMutationToFirstNewGenerationMs":636.047,"presentationToStrictCompletionMs":254.5278}  |
| 6   | 955.280 / 939.787   | 1087.782 / 1085.005 | 132.502 / 145.218 | {"blockedOrPreparationToFirstPhysicalMutationMs":47.1022,"dispatchToBlockedOrPreparationMs":382.1632,"firstNewGenerationToCleanupDrainedMs":175.3795,"firstPhysicalMutationToFirstNewGenerationMs":483.1375,"presentationToStrictCompletionMs":216.2703} | {"blockedOrPreparationToFirstPhysicalMutationMs":49.9869,"dispatchToBlockedOrPreparationMs":380.9207,"firstNewGenerationToCleanupDrainedMs":193.4445,"firstPhysicalMutationToFirstNewGenerationMs":460.653,"presentationToStrictCompletionMs":238.0445}  |
| 7   | 1102.234 / 1225.830 | 1234.767 / 1321.959 | 132.533 / 96.129  | {"blockedOrPreparationToFirstPhysicalMutationMs":47.4041,"dispatchToBlockedOrPreparationMs":349.0306,"firstNewGenerationToCleanupDrainedMs":176.0055,"firstPhysicalMutationToFirstNewGenerationMs":662.3272,"presentationToStrictCompletionMs":217.5327} | {"blockedOrPreparationToFirstPhysicalMutationMs":51.0685,"dispatchToBlockedOrPreparationMs":379.0517,"firstNewGenerationToCleanupDrainedMs":185.0477,"firstPhysicalMutationToFirstNewGenerationMs":706.7908,"presentationToStrictCompletionMs":198.0234} |
| 8   | 1139.853 / 1157.610 | 1271.884 / 1301.666 | 132.031 / 144.055 | {"blockedOrPreparationToFirstPhysicalMutationMs":49.8214,"dispatchToBlockedOrPreparationMs":344.7531,"firstNewGenerationToCleanupDrainedMs":175.8538,"firstPhysicalMutationToFirstNewGenerationMs":701.4561,"presentationToStrictCompletionMs":216.9671} | {"blockedOrPreparationToFirstPhysicalMutationMs":51.3844,"dispatchToBlockedOrPreparationMs":374.9823,"firstNewGenerationToCleanupDrainedMs":187.6664,"firstPhysicalMutationToFirstNewGenerationMs":687.6324,"presentationToStrictCompletionMs":240.6121} |
| 9   | 1074.130 / 1106.020 | 1209.265 / 1248.893 | 135.135 / 142.873 | {"blockedOrPreparationToFirstPhysicalMutationMs":47.0548,"dispatchToBlockedOrPreparationMs":349.1578,"firstNewGenerationToCleanupDrainedMs":178.2302,"firstPhysicalMutationToFirstNewGenerationMs":634.8221,"presentationToStrictCompletionMs":214.7668} | {"blockedOrPreparationToFirstPhysicalMutationMs":0.5993,"dispatchToBlockedOrPreparationMs":417.7949,"firstNewGenerationToCleanupDrainedMs":185.5526,"firstPhysicalMutationToFirstNewGenerationMs":644.9458,"presentationToStrictCompletionMs":261.2633}  |
| 10  | 642.707 / 664.251   | 775.051 / 798.935   | 132.343 / 134.684 | {"blockedOrPreparationToFirstPhysicalMutationMs":0.5962,"dispatchToBlockedOrPreparationMs":395.5892,"firstNewGenerationToCleanupDrainedMs":176.6887,"firstPhysicalMutationToFirstNewGenerationMs":202.1765,"presentationToStrictCompletionMs":132.3433}  | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3436,"dispatchToBlockedOrPreparationMs":360.4407,"firstNewGenerationToCleanupDrainedMs":182.3977,"firstPhysicalMutationToFirstNewGenerationMs":208.7527,"presentationToStrictCompletionMs":134.6836} |
| 11  | 171.997 / 176.768   | 479.922 / 465.233   | 307.925 / 288.465 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5808,"dispatchToBlockedOrPreparationMs":128.4443,"firstNewGenerationToCleanupDrainedMs":309.1395,"firstPhysicalMutationToFirstNewGenerationMs":38.7575,"presentationToStrictCompletionMs":307.925}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4611,"dispatchToBlockedOrPreparationMs":131.4955,"firstNewGenerationToCleanupDrainedMs":289.1769,"firstPhysicalMutationToFirstNewGenerationMs":41.0993,"presentationToStrictCompletionMs":288.4649}   |
| 12  | 172.782 / 175.991   | 172.782 / 175.991   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7824,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":175.9914,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 809.235 / 921.078   | 809.235 / 921.078   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":197.745,"dispatchToBlockedOrPreparationMs":438.364,"firstNewGenerationToCleanupDrainedMs":43.1553,"firstPhysicalMutationToFirstNewGenerationMs":129.9709,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":302.9725,"dispatchToBlockedOrPreparationMs":434.6174,"firstNewGenerationToCleanupDrainedMs":44.5645,"firstPhysicalMutationToFirstNewGenerationMs":138.9241,"presentationToStrictCompletionMs":0}        |
| 14  | 920.471 / 1000.417  | 1051.774 / 1106.483 | 131.303 / 106.066 | {"blockedOrPreparationToFirstPhysicalMutationMs":53.0763,"dispatchToBlockedOrPreparationMs":398.3695,"firstNewGenerationToCleanupDrainedMs":174.5609,"firstPhysicalMutationToFirstNewGenerationMs":425.7673,"presentationToStrictCompletionMs":178.0895} | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0613,"dispatchToBlockedOrPreparationMs":449.3138,"firstNewGenerationToCleanupDrainedMs":194.3451,"firstPhysicalMutationToFirstNewGenerationMs":413.7628,"presentationToStrictCompletionMs":155.012}  |
| 15  | 926.425 / 1002.783  | 836.449 / 728.137   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":46.2245,"dispatchToBlockedOrPreparationMs":404.3924,"firstNewGenerationToCleanupDrainedMs":42.9698,"firstPhysicalMutationToFirstNewGenerationMs":342.8622,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2954,"dispatchToBlockedOrPreparationMs":427.4852,"firstNewGenerationToCleanupDrainedMs":44.4301,"firstPhysicalMutationToFirstNewGenerationMs":207.9264,"presentationToStrictCompletionMs":0}         |
| 16  | 983.356 / 1007.986  | 882.795 / 748.234   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.5628,"dispatchToBlockedOrPreparationMs":415.6594,"firstNewGenerationToCleanupDrainedMs":45.9667,"firstPhysicalMutationToFirstNewGenerationMs":372.6063,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2088,"dispatchToBlockedOrPreparationMs":417.264,"firstNewGenerationToCleanupDrainedMs":43.3328,"firstPhysicalMutationToFirstNewGenerationMs":239.4284,"presentationToStrictCompletionMs":0}          |
| 17  | 1050.889 / 769.460  | 825.856 / 605.502   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":53.6722,"dispatchToBlockedOrPreparationMs":409.8709,"firstNewGenerationToCleanupDrainedMs":0.1596,"firstPhysicalMutationToFirstNewGenerationMs":362.1534,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":42.688,"dispatchToBlockedOrPreparationMs":373.0891,"firstNewGenerationToCleanupDrainedMs":0.3491,"firstPhysicalMutationToFirstNewGenerationMs":189.3757,"presentationToStrictCompletionMs":0}           |
| 18  | 1079.167 / 786.131  | 838.286 / 660.916   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":47.7944,"dispatchToBlockedOrPreparationMs":400.8995,"firstNewGenerationToCleanupDrainedMs":43.7485,"firstPhysicalMutationToFirstNewGenerationMs":345.8437,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":45.3155,"dispatchToBlockedOrPreparationMs":380.3142,"firstNewGenerationToCleanupDrainedMs":40.8901,"firstPhysicalMutationToFirstNewGenerationMs":194.3963,"presentationToStrictCompletionMs":0}         |
| 19  | 975.330 / 1223.303  | 841.368 / 685.056   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":47.6092,"dispatchToBlockedOrPreparationMs":413.012,"firstNewGenerationToCleanupDrainedMs":42.5309,"firstPhysicalMutationToFirstNewGenerationMs":338.2159,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":48.3383,"dispatchToBlockedOrPreparationMs":397.3388,"firstNewGenerationToCleanupDrainedMs":43.3787,"firstPhysicalMutationToFirstNewGenerationMs":195.9997,"presentationToStrictCompletionMs":0}         |
| 20  | 849.474 / 768.589   | 1028.868 / 953.041  | 179.394 / 184.452 | {"blockedOrPreparationToFirstPhysicalMutationMs":254.0958,"dispatchToBlockedOrPreparationMs":442.0118,"firstNewGenerationToCleanupDrainedMs":242.3936,"firstPhysicalMutationToFirstNewGenerationMs":90.3663,"presentationToStrictCompletionMs":179.3937} | {"blockedOrPreparationToFirstPhysicalMutationMs":211.9214,"dispatchToBlockedOrPreparationMs":428.0444,"firstNewGenerationToCleanupDrainedMs":229.1844,"firstPhysicalMutationToFirstNewGenerationMs":83.8908,"presentationToStrictCompletionMs":184.4523} |
| 21  | 214.611 / 209.143   | 481.522 / 467.713   | 266.911 / 258.569 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.5939,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":266.9111}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.3458,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":258.5694}            |
| 22  | 170.781 / 158.622   | 170.781 / 158.622   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7807,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":158.6221,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 301.218 / 240.633   | 301.218 / 240.633   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":301.2181,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":240.6335,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 669.044 / 597.558   | 808.217 / 725.341   | 139.173 / 127.783 | {"blockedOrPreparationToFirstPhysicalMutationMs":54.8492,"dispatchToBlockedOrPreparationMs":415.4329,"firstNewGenerationToCleanupDrainedMs":183.5175,"firstPhysicalMutationToFirstNewGenerationMs":154.4173,"presentationToStrictCompletionMs":139.1725} | {"blockedOrPreparationToFirstPhysicalMutationMs":43.0545,"dispatchToBlockedOrPreparationMs":377.1836,"firstNewGenerationToCleanupDrainedMs":167.8687,"firstPhysicalMutationToFirstNewGenerationMs":137.2345,"presentationToStrictCompletionMs":127.7829} |
| 25  | 1002.370 / 914.530  | 1138.751 / 1041.725 | 136.381 / 127.195 | {"blockedOrPreparationToFirstPhysicalMutationMs":32.0675,"dispatchToBlockedOrPreparationMs":445.7745,"firstNewGenerationToCleanupDrainedMs":181.7968,"firstPhysicalMutationToFirstNewGenerationMs":479.1118,"presentationToStrictCompletionMs":220.0764} | {"blockedOrPreparationToFirstPhysicalMutationMs":24.1865,"dispatchToBlockedOrPreparationMs":494.0467,"firstNewGenerationToCleanupDrainedMs":168.1967,"firstPhysicalMutationToFirstNewGenerationMs":355.2946,"presentationToStrictCompletionMs":212.4982} |
| 26  | 995.239 / 918.401   | 1099.655 / 965.214  | 104.416 / 46.813  | {"blockedOrPreparationToFirstPhysicalMutationMs":46.9174,"dispatchToBlockedOrPreparationMs":399.4277,"firstNewGenerationToCleanupDrainedMs":198.5605,"firstPhysicalMutationToFirstNewGenerationMs":454.749,"presentationToStrictCompletionMs":154.9414}  | {"blockedOrPreparationToFirstPhysicalMutationMs":46.6122,"dispatchToBlockedOrPreparationMs":421.3456,"firstNewGenerationToCleanupDrainedMs":170.1694,"firstPhysicalMutationToFirstNewGenerationMs":327.0868,"presentationToStrictCompletionMs":91.5829}  |
| 27  | 579.545 / 546.985   | 761.685 / 727.177   | 182.140 / 180.192 | {"blockedOrPreparationToFirstPhysicalMutationMs":0.6327,"dispatchToBlockedOrPreparationMs":411.3025,"firstNewGenerationToCleanupDrainedMs":237.287,"firstPhysicalMutationToFirstNewGenerationMs":112.4631,"presentationToStrictCompletionMs":182.1399}   | {"blockedOrPreparationToFirstPhysicalMutationMs":0.9403,"dispatchToBlockedOrPreparationMs":389.236,"firstNewGenerationToCleanupDrainedMs":233.5112,"firstPhysicalMutationToFirstNewGenerationMs":103.489,"presentationToStrictCompletionMs":180.1917}    |
| 28  | 920.986 / 884.295   | 782.779 / 664.092   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0429,"dispatchToBlockedOrPreparationMs":403.8252,"firstNewGenerationToCleanupDrainedMs":44.9347,"firstPhysicalMutationToFirstNewGenerationMs":284.9767,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":44.6346,"dispatchToBlockedOrPreparationMs":319.5049,"firstNewGenerationToCleanupDrainedMs":41.2796,"firstPhysicalMutationToFirstNewGenerationMs":258.6731,"presentationToStrictCompletionMs":0}         |
| 29  | 1009.071 / 832.834  | 1150.306 / 969.609  | 141.234 / 136.775 | {"blockedOrPreparationToFirstPhysicalMutationMs":32.4908,"dispatchToBlockedOrPreparationMs":460.9697,"firstNewGenerationToCleanupDrainedMs":196.496,"firstPhysicalMutationToFirstNewGenerationMs":460.3491,"presentationToStrictCompletionMs":224.717}   | {"blockedOrPreparationToFirstPhysicalMutationMs":27.2958,"dispatchToBlockedOrPreparationMs":422.2767,"firstNewGenerationToCleanupDrainedMs":177.3635,"firstPhysicalMutationToFirstNewGenerationMs":342.6734,"presentationToStrictCompletionMs":221.0887} |
| 30  | 582.214 / 526.231   | 780.674 / 701.801   | 198.460 / 175.570 | {"blockedOrPreparationToFirstPhysicalMutationMs":47.0268,"dispatchToBlockedOrPreparationMs":393.313,"firstNewGenerationToCleanupDrainedMs":266.9159,"firstPhysicalMutationToFirstNewGenerationMs":73.4185,"presentationToStrictCompletionMs":198.4598}   | {"blockedOrPreparationToFirstPhysicalMutationMs":43.5348,"dispatchToBlockedOrPreparationMs":359.2,"firstNewGenerationToCleanupDrainedMs":231.5579,"firstPhysicalMutationToFirstNewGenerationMs":67.5083,"presentationToStrictCompletionMs":175.5695}     |
| 31  | 636.911 / 606.601   | 636.911 / 606.601   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.0217,"dispatchToBlockedOrPreparationMs":434.0515,"firstNewGenerationToCleanupDrainedMs":44.7224,"firstPhysicalMutationToFirstNewGenerationMs":110.1155,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":63.0553,"dispatchToBlockedOrPreparationMs":403.6118,"firstNewGenerationToCleanupDrainedMs":43.5018,"firstPhysicalMutationToFirstNewGenerationMs":96.4326,"presentationToStrictCompletionMs":0}          |
| 32  | 229.921 / 189.217   | 516.357 / 443.467   | 286.436 / 254.250 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":150.4586,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":286.4359}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":116.8255,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":254.2499}            |
| 33  | 261.967 / 265.332   | 261.967 / 265.332   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.9674,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":265.3315,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 9                   | -1           | 501.584 / 527.356    | 25.772   | 15 / 14           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 13 / 14                  | 1            | 752.272 / 877.778    | 125.506  | 18 / 19           | 1            |
| 5   | 14 / 14                  | 0            | 952.096 / 1081.829   | 129.733  | 19 / 19           | 0            |
| 6   | 18 / 17                  | -1           | 912.403 / 891.561    | -20.842  | 23 / 22           | -1           |
| 7   | 16 / 16                  | 0            | 1058.762 / 1136.911  | 78.149   | 21 / 21           | 0            |
| 8   | 17 / 17                  | 0            | 1096.031 / 1113.999  | 17.968   | 22 / 22           | 0            |
| 9   | 16 / 17                  | 1            | 1031.035 / 1063.340  | 32.305   | 21 / 22           | 1            |
| 10  | 10 / 11                  | 1            | 598.362 / 616.537    | 18.175   | 15 / 15           | 0            |
| 11  | 3 / 3                    | 0            | 170.783 / 176.056    | 5.273    | 9 / 9             | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 10                  | 0            | 766.080 / 876.514    | 110.434  | 11 / 11           | 0            |
| 14  | 18 / 19                  | 1            | 877.213 / 912.138    | 34.925   | 23 / 24           | 1            |
| 15  | 17 / 14                  | -3           | 793.479 / 683.707    | -109.772 | 20 / 21           | 1            |
| 16  | 17 / 14                  | -3           | 836.828 / 704.901    | -131.927 | 20 / 21           | 1            |
| 17  | 17 / 14                  | -3           | 825.697 / 605.153    | -220.544 | 22 / 18           | -4           |
| 18  | 17 / 13                  | -4           | 794.538 / 620.026    | -174.512 | 23 / 17           | -6           |
| 19  | 17 / 14                  | -3           | 798.837 / 641.677    | -157.160 | 21 / 27           | 6            |
| 20  | 16 / 16                  | 0            | 786.474 / 723.857    | -62.617  | 21 / 22           | 1            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 10           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 24  | 11 / 12                  | 1            | 624.699 / 557.473    | -67.227  | 15 / 16           | 1            |
| 25  | 18 / 15                  | -3           | 956.954 / 873.528    | -83.426  | 23 / 20           | -3           |
| 26  | 18 / 15                  | -3           | 901.094 / 795.045    | -106.050 | 23 / 20           | -3           |
| 27  | 10 / 10                  | 0            | 524.398 / 493.665    | -30.733  | 16 / 16           | 0            |
| 28  | 11 / 10                  | -1           | 737.845 / 622.813    | -115.032 | 15 / 16           | 1            |
| 29  | 18 / 15                  | -3           | 953.810 / 792.246    | -161.564 | 23 / 20           | -3           |
| 30  | 11 / 11                  | 0            | 513.758 / 470.243    | -43.515  | 17 / 17           | 0            |
| 31  | 10 / 10                  | 0            | 592.189 / 563.100    | -29.089  | 11 / 12           | 1            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 1 / 0                | -1    | 2 / 0              | -2    | 139.188 / 0       | -139.188 |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 201.344 / 254.095 | 52.752   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 199.515 / 217.524 | 18.009   |
| 6   | 1 / 1                | 0     | 5 / 5              | 0     | 339.451 / 337.267 | -2.184   |
| 7   | 1 / 1                | 0     | 5 / 5              | 0     | 342.120 / 345.476 | 3.356    |
| 8   | 1 / 1                | 0     | 5 / 5              | 0     | 364.067 / 337.337 | -26.730  |
| 9   | 1 / 1                | 0     | 5 / 5              | 0     | 354.193 / 343.309 | -10.884  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 5 / 5              | 0     | 221.155 / 219.261 | -1.894   |
| 15  | 1 / 1                | 0     | 7 / 4              | -3    | 343.048 / 207.808 | -135.240 |
| 16  | 1 / 1                | 0     | 7 / 4              | -3    | 373.101 / 239.806 | -133.294 |
| 17  | 1 / 1                | 0     | 7 / 4              | -3    | 362.815 / 189.400 | -173.414 |
| 18  | 1 / 1                | 0     | 7 / 4              | -3    | 345.786 / 194.840 | -150.946 |
| 19  | 1 / 1                | 0     | 7 / 4              | -3    | 338.283 / 196.377 | -141.906 |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 278.875 / 253.677 | -25.198  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 5 / 2              | -3    | 347.446 / 213.478 | -133.968 |
| 26  | 2 / 2                | 0     | 6 / 3              | -3    | 336.559 / 206.467 | -130.093 |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 6 / 3              | -3    | 444.157 / 324.551 | -119.606 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

### nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 741.003 / 750.957   | 9.954    | 1.343   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 182.274 / 172.861   | -9.413   | -5.164  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 315.196 / 286.278   | -28.919  | -9.175  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1027.925 / 1084.862 | 56.936   | 5.539   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1069.759 / 1180.402 | 110.643  | 10.343  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1207.428 / 1195.027 | -12.401  | -1.027  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1117.000 / 1205.893 | 88.893   | 7.958   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1135.635 / 1192.834 | 57.199   | 5.037   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1129.170 / 1194.892 | 65.722   | 5.820   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 789.145 / 805.412   | 16.267   | 2.061   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 447.115 / 484.798   | 37.683   | 8.428   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 174.668 / 160.247   | -14.422  | -8.257  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 604.444 / 632.597   | 28.152   | 4.658   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1097.566 / 1079.451 | -18.115  | -1.650  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 977.101 / 817.128   | -159.973 | -16.372 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 1185.513 / 819.193  | -366.319 | -30.900 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 993.635 / 821.419   | -172.216 | -17.332 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1087.056 / 817.504  | -269.552 | -24.797 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 981.551 / 939.606   | -41.945  | -4.273  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1037.855 / 1009.718 | -28.138  | -2.711  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 473.686 / 453.071   | -20.615  | -4.352  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 202.855 / 162.812   | -40.043  | -19.740 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 300.146 / 288.590   | -11.556  | -3.850  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 812.884 / 796.742   | -16.142  | -1.986  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1260.141 / 1092.223 | -167.918 | -13.325 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1207.490 / 1068.065 | -139.425 | -11.547 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 794.435 / 748.586   | -45.849  | -5.771  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 883.792 / 974.430   | 90.638   | 10.256  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1215.207 / 1039.385 | -175.822 | -14.468 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 779.037 / 720.991   | -58.045  | -7.451  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 621.548 / 601.567   | -19.981  | -3.215  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 536.614 / 482.117   | -54.498  | -10.156 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 296.062 / 281.976   | -14.086  | -4.758  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 554.956 / 556.209   | 741.003 / 750.957   | 186.047 / 194.748 | {"blockedOrPreparationToFirstPhysicalMutationMs":49.8744,"dispatchToBlockedOrPreparationMs":364.4052,"firstNewGenerationToCleanupDrainedMs":239.9261,"firstPhysicalMutationToFirstNewGenerationMs":86.7972,"presentationToStrictCompletionMs":186.0469}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.8036,"dispatchToBlockedOrPreparationMs":358.5652,"firstNewGenerationToCleanupDrainedMs":260.2503,"firstPhysicalMutationToFirstNewGenerationMs":84.3382,"presentationToStrictCompletionMs":194.7483}  |
| 2   | 182.274 / 172.861   | 182.274 / 172.861   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":182.2739,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8606,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 315.196 / 286.278   | 315.196 / 286.278   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":315.1964,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.2776,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 852.973 / 901.457   | 941.795 / 993.615   | 88.822 / 92.158   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3988,"dispatchToBlockedOrPreparationMs":372.6052,"firstNewGenerationToCleanupDrainedMs":177.1048,"firstPhysicalMutationToFirstNewGenerationMs":344.6858,"presentationToStrictCompletionMs":174.9528}  | {"blockedOrPreparationToFirstPhysicalMutationMs":48.9254,"dispatchToBlockedOrPreparationMs":406.4776,"firstNewGenerationToCleanupDrainedMs":180.8965,"firstPhysicalMutationToFirstNewGenerationMs":357.315,"presentationToStrictCompletionMs":183.4047}  |
| 5   | 848.641 / 974.085   | 982.761 / 1078.136  | 134.120 / 104.051 | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0827,"dispatchToBlockedOrPreparationMs":399.5427,"firstNewGenerationToCleanupDrainedMs":178.7136,"firstPhysicalMutationToFirstNewGenerationMs":345.4225,"presentationToStrictCompletionMs":221.1181}  | {"blockedOrPreparationToFirstPhysicalMutationMs":66.4558,"dispatchToBlockedOrPreparationMs":393.7835,"firstNewGenerationToCleanupDrainedMs":216.7643,"firstPhysicalMutationToFirstNewGenerationMs":401.1323,"presentationToStrictCompletionMs":206.3166} |
| 6   | 985.970 / 1015.606  | 1122.595 / 1106.145 | 136.624 / 90.540  | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2644,"dispatchToBlockedOrPreparationMs":400.457,"firstNewGenerationToCleanupDrainedMs":180.7301,"firstPhysicalMutationToFirstNewGenerationMs":493.1433,"presentationToStrictCompletionMs":221.4575}   | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1865,"dispatchToBlockedOrPreparationMs":391.249,"firstNewGenerationToCleanupDrainedMs":188.7139,"firstPhysicalMutationToFirstNewGenerationMs":474.996,"presentationToStrictCompletionMs":179.4213}   |
| 7   | 949.180 / 981.378   | 1036.096 / 1119.901 | 86.916 / 138.524  | {"blockedOrPreparationToFirstPhysicalMutationMs":48.8175,"dispatchToBlockedOrPreparationMs":344.1407,"firstNewGenerationToCleanupDrainedMs":173.8925,"firstPhysicalMutationToFirstNewGenerationMs":469.2451,"presentationToStrictCompletionMs":167.8205}  | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4652,"dispatchToBlockedOrPreparationMs":401.2388,"firstNewGenerationToCleanupDrainedMs":183.5256,"firstPhysicalMutationToFirstNewGenerationMs":483.6717,"presentationToStrictCompletionMs":224.5152} |
| 8   | 956.269 / 971.483   | 1050.635 / 1107.063 | 94.365 / 135.580  | {"blockedOrPreparationToFirstPhysicalMutationMs":48.4996,"dispatchToBlockedOrPreparationMs":350.9687,"firstNewGenerationToCleanupDrainedMs":189.0992,"firstPhysicalMutationToFirstNewGenerationMs":462.0671,"presentationToStrictCompletionMs":179.3663}  | {"blockedOrPreparationToFirstPhysicalMutationMs":48.6324,"dispatchToBlockedOrPreparationMs":395.5498,"firstNewGenerationToCleanupDrainedMs":179.0661,"firstPhysicalMutationToFirstNewGenerationMs":483.8143,"presentationToStrictCompletionMs":221.3515} |
| 9   | 953.447 / 958.254   | 1042.011 / 1110.311 | 88.565 / 152.057  | {"blockedOrPreparationToFirstPhysicalMutationMs":46.6635,"dispatchToBlockedOrPreparationMs":354.4957,"firstNewGenerationToCleanupDrainedMs":176.6761,"firstPhysicalMutationToFirstNewGenerationMs":464.1759,"presentationToStrictCompletionMs":175.7231}  | {"blockedOrPreparationToFirstPhysicalMutationMs":50.4611,"dispatchToBlockedOrPreparationMs":401.1379,"firstNewGenerationToCleanupDrainedMs":195.1782,"firstPhysicalMutationToFirstNewGenerationMs":463.5337,"presentationToStrictCompletionMs":236.6381} |
| 10  | 657.206 / 672.026   | 789.145 / 805.412   | 131.939 / 133.387 | {"blockedOrPreparationToFirstPhysicalMutationMs":45.5912,"dispatchToBlockedOrPreparationMs":354.5912,"firstNewGenerationToCleanupDrainedMs":175.6807,"firstPhysicalMutationToFirstNewGenerationMs":213.2819,"presentationToStrictCompletionMs":131.9389}  | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1025,"dispatchToBlockedOrPreparationMs":370.241,"firstNewGenerationToCleanupDrainedMs":180.3564,"firstPhysicalMutationToFirstNewGenerationMs":204.7122,"presentationToStrictCompletionMs":133.3865}  |
| 11  | 169.792 / 197.653   | 447.115 / 484.798   | 277.323 / 287.145 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0317,"dispatchToBlockedOrPreparationMs":126.896,"firstNewGenerationToCleanupDrainedMs":278.2949,"firstPhysicalMutationToFirstNewGenerationMs":37.8926,"presentationToStrictCompletionMs":277.3232}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6255,"dispatchToBlockedOrPreparationMs":145.0668,"firstNewGenerationToCleanupDrainedMs":287.8392,"firstPhysicalMutationToFirstNewGenerationMs":48.2665,"presentationToStrictCompletionMs":287.145}    |
| 12  | 174.668 / 160.247   | 174.668 / 160.247   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":174.6685,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":160.2466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 604.444 / 632.597   | 604.444 / 632.597   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2125,"dispatchToBlockedOrPreparationMs":415.7236,"firstNewGenerationToCleanupDrainedMs":42.9494,"firstPhysicalMutationToFirstNewGenerationMs":97.5588,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":52.8288,"dispatchToBlockedOrPreparationMs":430.0565,"firstNewGenerationToCleanupDrainedMs":44.353,"firstPhysicalMutationToFirstNewGenerationMs":105.3583,"presentationToStrictCompletionMs":0}          |
| 14  | 916.477 / 905.914   | 1050.958 / 1033.575 | 134.481 / 127.660 | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3492,"dispatchToBlockedOrPreparationMs":410.0617,"firstNewGenerationToCleanupDrainedMs":177.5564,"firstPhysicalMutationToFirstNewGenerationMs":415.9909,"presentationToStrictCompletionMs":181.0888}  | {"blockedOrPreparationToFirstPhysicalMutationMs":46.5767,"dispatchToBlockedOrPreparationMs":402.3021,"firstNewGenerationToCleanupDrainedMs":170.0301,"firstPhysicalMutationToFirstNewGenerationMs":414.6658,"presentationToStrictCompletionMs":173.5363} |
| 15  | 977.101 / 817.128   | 883.840 / 728.821   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":57.7999,"dispatchToBlockedOrPreparationMs":411.0873,"firstNewGenerationToCleanupDrainedMs":43.4123,"firstPhysicalMutationToFirstNewGenerationMs":371.5409,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":50.0628,"dispatchToBlockedOrPreparationMs":413.8539,"firstNewGenerationToCleanupDrainedMs":41.3177,"firstPhysicalMutationToFirstNewGenerationMs":223.5868,"presentationToStrictCompletionMs":0}         |
| 16  | 1185.513 / 819.193  | 862.331 / 685.026   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.4907,"dispatchToBlockedOrPreparationMs":407.2276,"firstNewGenerationToCleanupDrainedMs":46.1316,"firstPhysicalMutationToFirstNewGenerationMs":360.4811,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":44.7755,"dispatchToBlockedOrPreparationMs":391.2656,"firstNewGenerationToCleanupDrainedMs":43.9256,"firstPhysicalMutationToFirstNewGenerationMs":205.0593,"presentationToStrictCompletionMs":0}         |
| 17  | 993.635 / 821.419   | 846.736 / 692.394   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0873,"dispatchToBlockedOrPreparationMs":402.1547,"firstNewGenerationToCleanupDrainedMs":44.3475,"firstPhysicalMutationToFirstNewGenerationMs":351.1463,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":45.9464,"dispatchToBlockedOrPreparationMs":401.6109,"firstNewGenerationToCleanupDrainedMs":40.6797,"firstPhysicalMutationToFirstNewGenerationMs":204.1569,"presentationToStrictCompletionMs":0}         |
| 18  | 1087.056 / 817.504  | 856.637 / 675.719   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0201,"dispatchToBlockedOrPreparationMs":417.3373,"firstNewGenerationToCleanupDrainedMs":43.4414,"firstPhysicalMutationToFirstNewGenerationMs":346.8383,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":47.1171,"dispatchToBlockedOrPreparationMs":389.3872,"firstNewGenerationToCleanupDrainedMs":41.6418,"firstPhysicalMutationToFirstNewGenerationMs":197.5732,"presentationToStrictCompletionMs":0}         |
| 19  | 981.551 / 939.606   | 805.270 / 689.071   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.3015,"dispatchToBlockedOrPreparationMs":410.8789,"firstNewGenerationToCleanupDrainedMs":0.192,"firstPhysicalMutationToFirstNewGenerationMs":342.8972,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":44.9648,"dispatchToBlockedOrPreparationMs":404.2762,"firstNewGenerationToCleanupDrainedMs":41.4998,"firstPhysicalMutationToFirstNewGenerationMs":198.33,"presentationToStrictCompletionMs":0}           |
| 20  | 847.752 / 832.213   | 1037.855 / 1009.718 | 190.104 / 177.505 | {"blockedOrPreparationToFirstPhysicalMutationMs":233.7132,"dispatchToBlockedOrPreparationMs":449.8744,"firstNewGenerationToCleanupDrainedMs":244.8492,"firstPhysicalMutationToFirstNewGenerationMs":109.4186,"presentationToStrictCompletionMs":190.1038} | {"blockedOrPreparationToFirstPhysicalMutationMs":238.13,"dispatchToBlockedOrPreparationMs":453.5158,"firstNewGenerationToCleanupDrainedMs":227.8665,"firstPhysicalMutationToFirstNewGenerationMs":90.2054,"presentationToStrictCompletionMs":177.5049}   |
| 21  | 201.673 / 189.077   | 473.686 / 453.071   | 272.013 / 263.994 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":127.2856,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.0126}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":126.7527,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":263.9937}            |
| 22  | 202.855 / 162.812   | 202.855 / 162.812   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":202.8551,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":162.8116,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 300.146 / 288.590   | 300.146 / 288.590   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":300.146,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":288.5903,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 662.748 / 662.606   | 812.884 / 796.742   | 150.135 / 134.135 | {"blockedOrPreparationToFirstPhysicalMutationMs":46.9643,"dispatchToBlockedOrPreparationMs":425.2182,"firstNewGenerationToCleanupDrainedMs":192.5732,"firstPhysicalMutationToFirstNewGenerationMs":148.128,"presentationToStrictCompletionMs":150.1354}   | {"blockedOrPreparationToFirstPhysicalMutationMs":45.811,"dispatchToBlockedOrPreparationMs":411.9901,"firstNewGenerationToCleanupDrainedMs":185.9268,"firstPhysicalMutationToFirstNewGenerationMs":153.0138,"presentationToStrictCompletionMs":134.1354}  |
| 25  | 1072.852 / 861.812  | 1164.554 / 1006.636 | 91.701 / 144.824  | {"blockedOrPreparationToFirstPhysicalMutationMs":32.6165,"dispatchToBlockedOrPreparationMs":477.7464,"firstNewGenerationToCleanupDrainedMs":178.5893,"firstPhysicalMutationToFirstNewGenerationMs":475.6018,"presentationToStrictCompletionMs":187.2885}  | {"blockedOrPreparationToFirstPhysicalMutationMs":21.7457,"dispatchToBlockedOrPreparationMs":454.0392,"firstNewGenerationToCleanupDrainedMs":195.2621,"firstPhysicalMutationToFirstNewGenerationMs":335.5889,"presentationToStrictCompletionMs":230.4107} |
| 26  | 1063.643 / 1068.065 | 1158.975 / 1011.510 | 95.332 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":48.818,"dispatchToBlockedOrPreparationMs":413.2857,"firstNewGenerationToCleanupDrainedMs":196.1877,"firstPhysicalMutationToFirstNewGenerationMs":500.6837,"presentationToStrictCompletionMs":143.8467}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3468,"dispatchToBlockedOrPreparationMs":408.0517,"firstNewGenerationToCleanupDrainedMs":216.359,"firstPhysicalMutationToFirstNewGenerationMs":339.7526,"presentationToStrictCompletionMs":0}         |
| 27  | 605.959 / 567.395   | 794.435 / 748.586   | 188.475 / 181.191 | {"blockedOrPreparationToFirstPhysicalMutationMs":0.7808,"dispatchToBlockedOrPreparationMs":427.7245,"firstNewGenerationToCleanupDrainedMs":240.4988,"firstPhysicalMutationToFirstNewGenerationMs":125.4306,"presentationToStrictCompletionMs":188.4753}   | {"blockedOrPreparationToFirstPhysicalMutationMs":50.2463,"dispatchToBlockedOrPreparationMs":347.906,"firstNewGenerationToCleanupDrainedMs":236.1733,"firstPhysicalMutationToFirstNewGenerationMs":114.2602,"presentationToStrictCompletionMs":181.1906}  |
| 28  | 883.792 / 870.041   | 737.074 / 919.042   | 0 / 49.000        | {"blockedOrPreparationToFirstPhysicalMutationMs":46.5143,"dispatchToBlockedOrPreparationMs":393.2081,"firstNewGenerationToCleanupDrainedMs":45.5102,"firstPhysicalMutationToFirstNewGenerationMs":251.8417,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":57.5457,"dispatchToBlockedOrPreparationMs":396.541,"firstNewGenerationToCleanupDrainedMs":175.0451,"firstPhysicalMutationToFirstNewGenerationMs":289.9098,"presentationToStrictCompletionMs":104.3887}  |
| 29  | 996.729 / 821.599   | 1134.188 / 958.330  | 137.458 / 136.731 | {"blockedOrPreparationToFirstPhysicalMutationMs":35.3388,"dispatchToBlockedOrPreparationMs":465.2285,"firstNewGenerationToCleanupDrainedMs":180.5548,"firstPhysicalMutationToFirstNewGenerationMs":453.0657,"presentationToStrictCompletionMs":218.4772}  | {"blockedOrPreparationToFirstPhysicalMutationMs":19.8893,"dispatchToBlockedOrPreparationMs":450.0967,"firstNewGenerationToCleanupDrainedMs":178.5163,"firstPhysicalMutationToFirstNewGenerationMs":309.8279,"presentationToStrictCompletionMs":217.7862} |
| 30  | 586.015 / 525.686   | 779.037 / 720.991   | 193.021 / 195.306 | {"blockedOrPreparationToFirstPhysicalMutationMs":46.7888,"dispatchToBlockedOrPreparationMs":407.8188,"firstNewGenerationToCleanupDrainedMs":250.6875,"firstPhysicalMutationToFirstNewGenerationMs":73.7414,"presentationToStrictCompletionMs":193.0212}   | {"blockedOrPreparationToFirstPhysicalMutationMs":48.3199,"dispatchToBlockedOrPreparationMs":350.8517,"firstNewGenerationToCleanupDrainedMs":249.0011,"firstPhysicalMutationToFirstNewGenerationMs":72.8185,"presentationToStrictCompletionMs":195.3056}  |
| 31  | 621.548 / 601.567   | 621.548 / 601.567   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4248,"dispatchToBlockedOrPreparationMs":428.7578,"firstNewGenerationToCleanupDrainedMs":43.0913,"firstPhysicalMutationToFirstNewGenerationMs":98.2738,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":48.0276,"dispatchToBlockedOrPreparationMs":414.816,"firstNewGenerationToCleanupDrainedMs":42.5589,"firstPhysicalMutationToFirstNewGenerationMs":96.1643,"presentationToStrictCompletionMs":0}           |
| 32  | 215.192 / 185.789   | 536.614 / 482.117   | 321.423 / 296.327 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":133.0564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":321.4227}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":124.7113,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":296.3274}            |
| 33  | 296.062 / 281.976   | 296.062 / 281.976   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.0622,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":281.9764,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 10                   | 1            | 501.077 / 490.707    | -10.370  | 15 / 15           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 5             | 1            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 4   | 14 / 14                  | 0            | 764.690 / 812.718    | 48.028   | 19 / 19           | 0            |
| 5   | 15 / 13                  | -2           | 804.048 / 861.372    | 57.324   | 20 / 18           | -2           |
| 6   | 18 / 17                  | -1           | 941.865 / 917.432    | -24.433  | 23 / 22           | -1           |
| 7   | 17 / 18                  | 1            | 862.203 / 936.376    | 74.172   | 22 / 23           | 1            |
| 8   | 17 / 18                  | 1            | 861.535 / 927.996    | 66.461   | 22 / 23           | 1            |
| 9   | 16 / 17                  | 1            | 865.335 / 915.133    | 49.798   | 21 / 22           | 1            |
| 10  | 10 / 10                  | 0            | 613.464 / 625.056    | 11.591   | 14 / 14           | 0            |
| 11  | 3 / 3                    | 0            | 168.820 / 196.959    | 28.138   | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 10                  | 0            | 561.495 / 588.244    | 26.749   | 11 / 11           | 0            |
| 14  | 17 / 18                  | 1            | 873.402 / 863.545    | -9.857   | 22 / 23           | 1            |
| 15  | 17 / 14                  | -3           | 840.428 / 687.504    | -152.925 | 20 / 17           | -3           |
| 16  | 17 / 14                  | -3           | 816.199 / 641.100    | -175.099 | 25 / 18           | -7           |
| 17  | 17 / 14                  | -3           | 802.388 / 651.714    | -150.674 | 21 / 18           | -3           |
| 18  | 17 / 14                  | -3           | 813.196 / 634.077    | -179.118 | 23 / 18           | -5           |
| 19  | 17 / 14                  | -3           | 805.078 / 647.571    | -157.507 | 21 / 21           | 0            |
| 20  | 16 / 16                  | 0            | 793.006 / 781.851    | -11.155  | 21 / 21           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 24  | 11 / 11                  | 0            | 620.311 / 610.815    | -9.496   | 15 / 15           | 0            |
| 25  | 18 / 15                  | -3           | 985.965 / 811.374    | -174.591 | 23 / 20           | -3           |
| 26  | 18 / 14                  | -4           | 962.787 / 795.151    | -167.636 | 23 / 19           | -4           |
| 27  | 10 / 10                  | 0            | 553.936 / 512.413    | -41.523  | 16 / 16           | 0            |
| 28  | 11 / 12                  | 1            | 691.564 / 743.996    | 52.432   | 15 / 17           | 2            |
| 29  | 18 / 15                  | -3           | 953.633 / 779.814    | -173.819 | 23 / 20           | -3           |
| 30  | 10 / 9                   | -1           | 528.349 / 471.990    | -56.359  | 16 / 15           | -1           |
| 31  | 10 / 10                  | 0            | 578.456 / 559.008    | -19.449  | 11 / 11           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 9 / 9             | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 209.818 / 214.495 | 4.677    |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 208.593 / 245.001 | 36.408   |
| 6   | 1 / 1                | 0     | 5 / 5              | 0     | 357.841 / 345.614 | -12.226  |
| 7   | 1 / 1                | 0     | 5 / 5              | 0     | 339.427 / 354.159 | 14.733   |
| 8   | 1 / 1                | 0     | 5 / 5              | 0     | 325.257 / 351.013 | 25.755   |
| 9   | 1 / 1                | 0     | 5 / 5              | 0     | 328.108 / 332.710 | 4.602    |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 5 / 5              | 0     | 215.473 / 221.958 | 6.484    |
| 15  | 1 / 1                | 0     | 7 / 4              | -3    | 371.788 / 223.742 | -148.046 |
| 16  | 1 / 1                | 0     | 7 / 4              | -3    | 360.686 / 205.174 | -155.511 |
| 17  | 1 / 1                | 0     | 7 / 4              | -3    | 351.101 / 204.045 | -147.055 |
| 18  | 1 / 1                | 0     | 7 / 4              | -3    | 346.976 / 198.058 | -148.918 |
| 19  | 1 / 1                | 0     | 7 / 4              | -3    | 343.250 / 198.300 | -144.950 |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 296.103 / 283.340 | -12.763  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 5 / 2              | -3    | 342.097 / 203.305 | -138.791 |
| 26  | 2 / 2                | 0     | 6 / 3              | -3    | 374.758 / 220.362 | -154.396 |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 6 / 3              | -3    | 439.185 / 284.083 | -155.101 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

### Cumulative gates and other health evidence

#### nv-pr73-269bded1-mtwz6pez / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":7,"maximumObservedFrames":7}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":28860,"leftPath":"NativeOriginal","referenceFrame":29241,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

#### nv-pr73-269bded1-mtwz6pez / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":7,"maximumObservedFrames":7}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":33440,"leftPath":"NativeOriginal","referenceFrame":33821,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

#### renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":20782,"leftPath":"NativeOriginal","referenceFrame":21209,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

#### renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":25566,"leftPath":"NativeOriginal","referenceFrame":25971,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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
| nvidia | 1    | processPrivateMiB | 16922.84765625 / 16564.875 / -357.97265625     | 16430.2890625 / 16245.953125 / -184.3359375    | 173.637               |
| nvidia | 1    | systemCommitMiB   | 56564.03515625 / 56345.5859375 / -218.44921875 | 57091.7109375 / 56790.00390625 / -301.70703125 | -83.258               |
| nvidia | 1    | dxgiUsageMiB      | 4479.46875 / 3604.31640625 / -875.15234375     | 4191.01953125 / 3286.94921875 / -904.0703125   | -28.918               |
| nvidia | 1    | liveTextures      | 0 / 213 / 213                                  | 0 / 247 / 247                                  | 34                    |
| nvidia | 1    | liveTextureMiB    | 0 / 2271.0563163757324 / 2271.0563163757324    | 0 / 2405.307025909424 / 2405.307025909424      | 134.251               |
| nvidia | 2    | processPrivateMiB | 16923.23046875 / 16653.37109375 / -269.859375  | 16646.34375 / 16475.06640625 / -171.27734375   | 98.582                |
| nvidia | 2    | systemCommitMiB   | 56674.76953125 / 56488.5390625 / -186.23046875 | 57301.5625 / 57090.9921875 / -210.5703125      | -24.340               |
| nvidia | 2    | dxgiUsageMiB      | 3819.49609375 / 3605.03515625 / -214.4609375   | 3680.19921875 / 3536.84765625 / -143.3515625   | 71.109                |
| nvidia | 2    | liveTextures      | 0 / 225 / 225                                  | 0 / 241 / 241                                  | 16                    |
| nvidia | 2    | liveTextureMiB    | 0 / 2288.473041534424 / 2288.473041534424      | 0 / 2379.9213676452637 / 2379.9213676452637    | 91.448                |

### Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2204        | 2251        | 47         |
| cpu/compactPresentationContract/reuses                  | 2182        | 2229        | 47         |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 153         | 153         | 0          |
| cpu/generationResourceValidation/fullValidations        | 588         | 693         | 105        |
| cpu/generationResourceValidation/stableChecks           | 8357        | 8523        | 166        |
| cpu/generationResourceValidation/stableHits             | 8342        | 8508        | 166        |
| cpu/generationResourceValidation/stableMisses           | 15          | 15          | 0          |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 1           | 1           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4210        | 4360        | 150        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4210        | 4360        | 150        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4168        | 4318        | 150        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4210        | 4360        | 150        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4189        | 4337        | 148        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 23          | 2          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 30          | 30          | 0          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4180        | 4330        | 150        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4093        | 4266        | 173        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 117         | 93          | -24        |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4195        | 4345        | 150        |
| cpu/strongStereoPacket/captures                         | 4553        | 4648        | 95         |
| cpu/strongStereoPacket/commitAccepts                    | 4391        | 4484        | 93         |
| cpu/strongStereoPacket/commitRejects                    | 63          | 64          | 1          |
| cpu/strongStereoPacket/commitValidations                | 4454        | 4548        | 94         |
| cpu/strongStereoPacket/cycleReuses                      | 2238        | 2286        | 48         |
| cpu/strongStereoPacket/fastSkips                        | 3867        | 4072        | 205        |
| cpu/strongStereoPacket/invalidations                    | 4756        | 4906        | 150        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 93          | 92          | -1         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2222        | 2270        | 48         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.629       | 1.756       | 0.127      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 19.600      | 68.900      | 49.300     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.140       | 0.158       | 0.018      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 2           | 18.900      | 16.900     |
| cpu/window/currentFrame                                 | 29241       | 21210       | -8031      |
| cpu/window/elapsedFrames                                | 4210        | 4359        | 149        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 25031       | 16851       | -8180      |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 29243       | 21211       | -8032      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6072400224  | 6300781344  | 228381120  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4786        | 4966        | 180        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12157205760 | 12614434560 | 457228800  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.307       | 0.305       | -0.002     |
| gpu/item5ActiveFSRCopies/activePixels                   | 11833391040 | 12576906800 | 743515760  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26726237760 | 28649890000 | 1923652240 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15180       | 16230       | 1050       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 2000        | 2138        | 138        |
| gpu/item7EarlyHAM/executedClears                        | 2000        | 2004        | 4          |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2000        | 2004        | 4          |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4182        | 4328        | 146        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4212        | 4360        | 148        |
| gpu/startFrame                                          | 25031       | 16851       | -8180      |
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
| texture/createdCount                                    | 3902        | 3977        | 75         |
| texture/createdEstimatedBytes                           | 38571404792 | 38776359208 | 204954416  |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3689        | 3730        | 41         |
| texture/destroyedEstimatedBytes                         | 36190029644 | 36254211988 | 64182344   |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 866         | 896         | 30         |
| texture/liveTextureRecordCount                          | 213         | 247         | 34         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 4           | 38          | 34         |
| texture/niSourceTextureMatchedEstimatedBytes            | 5723568     | 146495640   | 140772072  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1504        | 1450        | -54        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 213         | 247         | 34         |
| texture/outstandingEstimatedBytes                       | 2381375148  | 2522147220  | 140772072  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 1           | 1           | 0          |
| texture/supported                                       | true        | true        | n/a        |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2197        | 2244        | 47         |
| cpu/compactPresentationContract/reuses                  | 2175        | 2220        | 45         |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 154         | 156         | 2          |
| cpu/generationResourceValidation/fullValidations        | 590         | 684         | 94         |
| cpu/generationResourceValidation/stableChecks           | 8334        | 8465        | 131        |
| cpu/generationResourceValidation/stableHits             | 8321        | 8442        | 121        |
| cpu/generationResourceValidation/stableMisses           | 13          | 23          | 10         |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 2           | 2           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4193        | 4346        | 153        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4193        | 4346        | 153        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4151        | 4304        | 153        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4193        | 4346        | 153        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4172        | 4325        | 153        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 30          | 30          | 0          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4163        | 4316        | 153        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4075        | 4252        | 177        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 117         | 94          | -23        |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4178        | 4331        | 153        |
| cpu/strongStereoPacket/captures                         | 4538        | 4633        | 95         |
| cpu/strongStereoPacket/commitAccepts                    | 4377        | 4466        | 89         |
| cpu/strongStereoPacket/commitRejects                    | 63          | 66          | 3          |
| cpu/strongStereoPacket/commitValidations                | 4440        | 4532        | 92         |
| cpu/strongStereoPacket/cycleReuses                      | 2231        | 2277        | 46         |
| cpu/strongStereoPacket/fastSkips                        | 3848        | 4059        | 211        |
| cpu/strongStereoPacket/invalidations                    | 4739        | 4893        | 154        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 92          | 93          | 1          |
| cpu/strongStereoPacket/lifetimeReuses                   | 2215        | 2263        | 48         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.617       | 1.652       | 0.035      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 16.900      | 27.400      | 10.500     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.142       | 0.155       | 0.013      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.300       | 22.100      | 20.800     |
| cpu/window/currentFrame                                 | 33821       | 25971       | -7850      |
| cpu/window/elapsedFrames                                | 4192        | 4346        | 154        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 29629       | 21625       | -8004      |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 33823       | 25972       | -7851      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6029261568  | 6258911472  | 229649904  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4752        | 4933        | 181        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12070840320 | 12530609280 | 459768960  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.308       | 0.305       | -0.003     |
| gpu/item5ActiveFSRCopies/activePixels                   | 11871721360 | 12379172080 | 507450720  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26687907440 | 28161781520 | 1473874080 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15180       | 15960       | 780        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 2000        | 2103        | 103        |
| gpu/item7EarlyHAM/executedClears                        | 1986        | 2014        | 28         |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1986        | 2014        | 28         |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4172        | 4310        | 138        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4194        | 4347        | 153        |
| gpu/startFrame                                          | 29629       | 21625       | -8004      |
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
| texture/createdCount                                    | 3958        | 3958        | 0          |
| texture/createdEstimatedBytes                           | 38732018168 | 38734100840 | 2082672    |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3733        | 3717        | -16        |
| texture/destroyedEstimatedBytes                         | 36332380260 | 36238572412 | -93807848  |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 892         | 892         | 0          |
| texture/liveTextureRecordCount                          | 225         | 241         | 16         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 18          | 34          | 16         |
| texture/niSourceTextureMatchedEstimatedBytes            | 23986528    | 119877048   | 95890520   |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1515        | 1484        | -31        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 225         | 241         | 16         |
| texture/outstandingEstimatedBytes                       | 2399637908  | 2495528428  | 95890520   |
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
| toolchain                | true   |
| dependencies             | true   |
| shaderCompiler           | true   |

Full start/end memory evidence, CPU/GPU/resource counters, phase timings, retry details, native execution proofs, every gate and raw receipt hash are in comparison.json. comparison.csv contains every paired or missing transition with both original row payloads. Missing samples remain null/n/a. Current and baseline failures are preserved separately; a persistent gate failure is not automatically a newly introduced regression. The fixed stretch-frame cutoff is diagnostic only because settling imposes stretch. Compare its actual frames and duration, together with time to applied relatch and strict completion. Request-to-applied frames begin at the producer request, whereas strict frames/ms begin at qualification dispatch; these intervals must not be conflated.

-   Headset refresh, driver version, power state and full modlist/cache equality require retained proof.
-   One process and ordered repeats do not establish causal or statistically significant gains.
-   Missing profiler samples are unavailable time evidence, never zero cost.
