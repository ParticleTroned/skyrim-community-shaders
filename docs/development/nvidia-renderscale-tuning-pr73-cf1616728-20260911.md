# NVIDIA tuning interruption: PR73 cf1616728, September 11

The [commit investigation and motivating PR66 stretch comparison](pr73-pending-drain-crash-investigation-20260911.md)
trace the earlier recreation admission and retain all 66 paired stretch
measurements. The exact invalid-pointer mechanism remains unproven.

## Follow-up: transition 26 crashed inside rendering

The subsequently preserved crash log confirms SkyrimVR PID **34484**
crashed at **08:55:59 local / 06:55:59 UTC** with an execute access
violation at `0x000001BE71DC0100`. The DevBench transport loss was a
consequence of this process crash.

Renderer logs resolve the initial dispatch uncertainty: transition 26
did apply the DLSS Hoshipa to FSR3 Hoshipa request (request 27, epoch 28).
Memory relief ran, DLSS teardown waited for resources in use, and relatch
resumed from `State::Draw` at frame 29006. The log ends during engine
target and feature-resource recreation. No stable terminal receipt exists.

The first CS frame identified by the crash logger is the render-state
hook `Hooks::BSGraphics_SetDirtyStates::thunk` at DLL offset `+0753327`.
The surrounding entries involve utility-shader and shadow-map rendering.
These are predominantly stack-scan entries; they do not identify the
source of the invalid execution target or prove a specific freed object.
The last logged feature (SSGI) is context, not a proven cause.

`cf1616728` is the only runtime commit after the successful `c73bae9a7`
build. It changes pending-drain polling under memory relief, which was
active here. This is a credible suspected regression, not proven causal
attribution. The previous build passed rows 26 and 28 in both passes:
row 26 took 954.9178/1468.2977 ms; row 28 took 881.7711/905.7967 ms.
This log shows relatch attempts five frames apart and does not itself
prove that one-frame retries caused the fault.

The confirmed crash prevents this attempt from supporting a robustness
or improvement claim. The original finalized summary and generated
comparison remain preserved as receipt-only evidence. This later finding,
complete timeline, source diff, previous-row evidence and log hashes are
appended losslessly to the same canonical ledger; all 728 timing cells
remain audited and historical cells remain intact.

-   [Crash log](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/diagnosis/crash-2026-09-11-08-55-59.log)
-   [Renderer log](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/diagnosis/CommunityShaders.log)
-   [Structured findings and hashes](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/diagnosis/findings.json)
-   [Exact runtime change](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/diagnosis/runtime-change.diff)

## Initial receipt-only assessment

Execution is **INTERRUPTED: 25 of 66 transitions retained**. All 25 retained
terminal render checks passed; Task 2 counts are 25 PASS / 0 FAIL /
0 INCONCLUSIVE. The connection failed during the submitted pass-1
transition-26 scenario, DLSS Hoshipa to FSR3 Hoshipa. No scenario step or
terminal receipt returned for that row, so its apply dispatch and outcome
remain ambiguous. Pass 1 rows 27-33 and all 33 pass-2 rows are NOT RUN.

The worker reported `transition_receipt_unavailable` and `fetch failed`.
The first unreported step is `transition-pace`; that does not establish
which server step failed. The finalizer's 25 `transitionsDispatched` count
means 25 confirmed receipts and does not prove row 26 was never applied.
Local observation subsequently found no SkyrimVR process. The exact
exit cause is not established by these transport receipts.

Cleanup is BLOCKED because its connection also failed. Capture shutdown
was not verified. The positioned game PID was 34484; retained owners are
stress 2, texture 1, probe 2, CPU 1, GPU start frame 25668, trace 10, and
qualification transition 100166. The ownership lock remains retained.
All queued evidence was flushed (`evidencePending: 0`). No scenario was
replayed and no replacement run or game restart was attempted.

Full-history health, memory and the improvement-or-neutral assessment are
INCONCLUSIVE. Runtime reporting remains INCOMPLETE because terminal,
pass-end, cooldown and repeat evidence are missing. Ledger coverage of
every available finalized field is COMPLETE; those are separate results.

## Build and reference

Run `nvidia-2026-09-11T06-52-05-183Z` used clean Release source
`cf16167283cf70b74aedab348e740b23f0e6a49d`, main-VR base
`ef7c366dd73989b2b87751c0ef975db7c6fd310f`, and Build ID
`a8d2e6a5f759ff30246fe77851f7cf73d01880d431019284482b0f05ee12afc0`. The physical 28,173,824-byte DLL,
adjacent manifest and preserved AIO build receipt agree on identity,
size and SHA-256 `402ffc0bb96e83fb29726ac5c016e07400846aaae9fecbd5035387825fc21701`. The exact enabled loose provider
was verified; Overwrite and unmanaged Data contain no competing DLL.
Full compile identity is retained in the ledger and deployment receipt.

The reference is the previous relevant measured main-VR run
`nvidia-20260910T124329625Z`, compiled from `7c8e3e656`.
Later PR66, PR73 and PR75 results are separate candidate builds.

## Comparison of the same 25 routes

Candidate strict completion averages **805.170 ms**
against **834.665 ms**, a descriptive
**-3.534%** change for pass-1 rows 1-25 only.
The generated full-pass table below compares unequal 33/25 coverage;
its -7.025% mean change must not be interpreted as a matched speedup.
Context differs, the complete fixture fingerprint is unavailable, the
repeat is missing and no versioned tolerance policy was supplied.

| Metric             | Common samples | Baseline mean | Candidate mean |   Delta |  Delta % |
| ------------------ | -------------: | ------------: | -------------: | ------: | -------: |
| strictMs           |             25 |       834.665 |        805.170 | -29.495 |  -3.534% |
| presentationMs     |             25 |       709.567 |        690.141 | -19.427 |  -2.738% |
| cleanupMs          |             25 |       768.082 |        744.222 | -23.860 |  -3.106% |
| cleanupTailMs      |             25 |        99.118 |         88.157 | -10.960 | -11.058% |
| strictFrames       |             25 |        15.120 |         15.000 |  -0.120 |  -0.794% |
| relatchProofMs     |             19 |       768.702 |        748.203 | -20.498 |  -2.667% |
| relatchProofFrames |             19 |        13.368 |         13.474 |  +0.105 |  +0.787% |

Rows 25, 15 and 10 are the largest retained increases in strict latency;
rows 18, 24 and 14 are the largest decreases. Every route, original
timing and delta remains below and in the canonical ledger. The missing
rows include reference failure routes 26 and 28, so their resolution
cannot be assessed in this run.

The retained window contains nine retries and 14 selected-stretch
transitions, all recovered. Its recorded device-loss, OOM, producer,
lifecycle, fidelity, vendor/bounds fallback, trim and retirement-fence
failure counters are zero. Missing crash-window and end-of-pass receipts
prevent extrapolating those observations to the entire attempt. Full-pass
stretch totals, final profiler totals and memory deltas are unavailable.
The fixed stretch cutoff remains diagnostic under imposed settling.

## Evidence and validation

The [canonical ledger](vr-render-scale-ledger.md) preserves the
complete saved summary, every retained transition and pass, complete
comparison, common-route inputs and deltas, worker ownership and process
observation. Exact reconstruction passed, all historical cells remain
intact, and 728 numeric timing cells passed audit: 528 reference and
200 candidate values. Missing measurements carry explicit reasons.

-   [Finalized summary](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/summary.json)
-   [Complete ledger audit](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/complete-ledger-validation.json)
-   [Receipt index](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/receipt-index.json)
-   [Full scalar evidence](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/evidence-values.csv)
-   [Deployment verification](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/raw/offline/physical-aio-verification.json)
-   [Worker state and retained owners](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/worker-status.json)
-   [Local process observation](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/process-observation.json)
-   [Comparison JSON](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z-comparison/comparison.json)
-   [Common-route comparison](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/common-route-comparison.json)

Finalization took 4.019 s. The single maintained
comparison-wrapper invocation took 2.111 s;
ledger preparation, comparison and first complete coverage audit took
13.524 s. Commands and stage receipts are
preserved in the run directory. Raw evidence remains local.

Local automation feedback `AUTO-20260911-065924716-C321093F` records the
missing underlying fetch-error cause. Startup bookkeeping deviation:
local tool metadata was listed alongside the first skill read before
prepare_tuning; it added no live DevBench call. Dispatch pacing stayed
within its diagnostic budget: maximum 46.6942 ms against 250 ms.

## Detailed baseline and candidate comparison

# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / INTERRUPTED** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                | Candidate                                                                                    |
| ----------------------- | --------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| Run                     | nvidia-20260910T124329625Z                                                              | nvidia-2026-09-11T06-52-05-183Z                                                              |
| Renderer base           | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | cf16167283cf70b74aedab348e740b23f0e6a49d                                                     |
| Main-VR base/equivalent | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                     |
| Compiled source         | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | cf16167283cf70b74aedab348e740b23f0e6a49d                                                     |
| Build ID                | 9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4                        | a8d2e6a5f759ff30246fe77851f7cf73d01880d431019284482b0f05ee12afc0                             |
| DLL SHA-256             | bcd04e8c31248d5cfb02148266fbc2ead73d06b36bafa59cf8f971d79df24afe                        | 402ffc0bb96e83fb29726ac5c016e07400846aaae9fecbd5035387825fc21701                             |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-20260910T124329625Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-2026-09-11T06-52-05-183Z |

Assessment limits: retained_context_not_matched:scene; unmatched_or_missing_transitions; matching_fixture_fingerprint_unavailable; incomplete_execution_coverage; incomplete_reporting; incomplete_health_evidence; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C  |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | -------------------- |
| nvidia | 1    | 33/25    | 866.009/805.170 | -7.025       | 10/9        | 4/0          | 2/0                 | none             | NOT_MET/INCONCLUSIVE |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta     | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | --------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 807.337   | 748.203   | -59.133   | -7.325  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.920    | 13.474    | -0.446    | -3.206  |
| nvidia | 1    | Relatch proof total        | ms          | 20183.415 | 14215.860 | -5967.555 | -29.567 |
| nvidia | 1    | Relatch proof total        | frames      | 348       | 256       | -92       | -26.437 |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 19        | -6        | -24     |
| nvidia | 1    | Strict completion mean     | ms          | 866.009   | 805.170   | -60.838   | -7.025  |
| nvidia | 1    | Strict completion mean     | frames      | 15.576    | 15        | -0.576    | -3.696  |
| nvidia | 1    | Strict completion total    | ms          | 28578.281 | 20129.251 | -8449.030 | -29.565 |
| nvidia | 1    | Strict completion total    | frames      | 514       | 375       | -139      | -27.043 |
| nvidia | 1    | Stretch completed episodes | episodes    | 19        | n/a       | n/a       | n/a     |
| nvidia | 1    | Stretch completed total    | frames      | 89        | n/a       | n/a       | n/a     |
| nvidia | 1    | Stretch completed total    | ms          | 5719.962  | n/a       | n/a       | n/a     |
| nvidia | 1    | Stretch longest episode    | ms          | 646.277   | n/a       | n/a       | n/a     |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair      |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | --------- |
| 1   | DLSS Hoshipa -> NONE        | 688.545 / 716.538   | 27.993   | 4.066   | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 2   | NONE -> TAA                 | 169.425 / 160.554   | -8.871   | -5.236  | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 3   | TAA -> DLAA                 | 249.126 / 261.711   | 12.585   | 5.052   | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1004.374 / 1048.268 | 43.895   | 4.370   | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1266.234 / 1257.805 | -8.430   | -0.666  | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1175.702 / 1192.412 | 16.711   | 1.421   | 1/1         | 0/0 -> 0/0 | UNMATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1393.400 / 1329.951 | -63.449  | -4.554  | 1/1         | 0/0 -> 0/0 | UNMATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1352.171 / 1233.276 | -118.894 | -8.793  | 1/1         | 0/0 -> 0/0 | UNMATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1386.511 / 1292.053 | -94.458  | -6.813  | 1/1         | 0/0 -> 0/0 | UNMATCHED |
| 10  | DLSS UP -> DLAA             | 770.294 / 920.055   | 149.761  | 19.442  | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 11  | DLAA -> TAA                 | 450.117 / 506.769   | 56.653   | 12.586  | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 12  | TAA -> NONE                 | 190.626 / 155.855   | -34.771  | -18.241 | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 13  | NONE -> FSR AA              | 822.942 / 816.206   | -6.736   | -0.819  | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.140 / 1018.480 | -358.660 | -26.044 | 1/1         | 0/0 -> 0/0 | UNMATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 749.789 / 1056.596  | 306.807  | 40.919  | 0/1         | 0/0 -> 0/0 | UNMATCHED |
| 16  | FSR UQ -> FSR Q             | 929.137 / 795.817   | -133.321 | -14.349 | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 17  | FSR Q -> FSR Bal            | 762.921 / 802.068   | 39.147   | 5.131   | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 18  | FSR Bal -> FSR Perf         | 1400.519 / 819.604  | -580.915 | -41.479 | 1/0         | 0/0 -> 0/0 | UNMATCHED |
| 19  | FSR Perf -> FSR UP          | 778.498 / 788.875   | 10.377   | 1.333   | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 20  | FSR UP -> FSR AA            | 731.796 / 754.819   | 23.023   | 3.146   | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 21  | FSR AA -> TAA               | 572.501 / 475.850   | -96.651  | -16.882 | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 22  | TAA -> NONE                 | 172.732 / 202.804   | 30.073   | 17.410  | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 23  | NONE -> DLAA                | 261.855 / 253.842   | -8.013   | -3.060  | 0/0         | 0/0 -> 0/0 | UNMATCHED |
| 24  | DLAA -> FSR AA              | 1233.573 / 844.976  | -388.597 | -31.502 | 1/1         | 0/0 -> 0/0 | UNMATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 976.698 / 1424.065  | 447.368  | 45.804  | 0/2         | 0/0 -> 0/0 | UNMATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1536.005 / n/a      | n/a      | n/a     | 1/n/a       | 2/1 -> n/a | UNMATCHED |
| 27  | FSR Hoshipa -> NONE         | 933.032 / n/a       | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 28  | NONE -> FSR UP              | 924.794 / n/a       | n/a      | n/a     | 0/n/a       | 2/1 -> n/a | UNMATCHED |
| 29  | FSR UP -> DLSS UP           | 2184.558 / n/a      | n/a      | n/a     | 2/n/a       | 0/0 -> n/a | UNMATCHED |
| 30  | DLSS UP -> TAA              | 744.487 / n/a       | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 31  | TAA -> FSR AA               | 610.701 / n/a       | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 32  | FSR AA -> NONE              | 481.567 / n/a       | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 33  | NONE -> DLAA                | 296.510 / n/a       | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 466.266 / 556.650   | 688.545 / 716.538   | 222.279 / 159.887 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1234,"dispatchToBlockedOrPreparationMs":350.2632,"firstNewGenerationToCleanupDrainedMs":222.7267,"firstPhysicalMutationToFirstNewGenerationMs":112.4315,"presentationToStrictCompletionMs":222.2791}   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8592,"dispatchToBlockedOrPreparationMs":379.8417,"firstNewGenerationToCleanupDrainedMs":206.8789,"firstPhysicalMutationToFirstNewGenerationMs":126.9582,"presentationToStrictCompletionMs":159.8875}   |
| 2   | 169.425 / 160.554   | 169.425 / 160.554   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.4249,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":160.5543,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 249.126 / 261.711   | 249.126 / 261.711   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.1258,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.7107,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 790.942 / 799.348   | 920.929 / 953.322   | 129.987 / 153.974 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2853,"dispatchToBlockedOrPreparationMs":392.9859,"firstNewGenerationToCleanupDrainedMs":172.3976,"firstPhysicalMutationToFirstNewGenerationMs":351.2604,"presentationToStrictCompletionMs":213.4314}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7309,"dispatchToBlockedOrPreparationMs":369.7268,"firstNewGenerationToCleanupDrainedMs":203.4277,"firstPhysicalMutationToFirstNewGenerationMs":376.4367,"presentationToStrictCompletionMs":248.9198}   |
| 5   | 1014.085 / 1117.832 | 1168.706 / 1159.562 | 154.620 / 41.730  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2487,"dispatchToBlockedOrPreparationMs":400.2561,"firstNewGenerationToCleanupDrainedMs":207.9868,"firstPhysicalMutationToFirstNewGenerationMs":556.2139,"presentationToStrictCompletionMs":252.149}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5763,"dispatchToBlockedOrPreparationMs":387.1775,"firstNewGenerationToCleanupDrainedMs":171.6944,"firstPhysicalMutationToFirstNewGenerationMs":597.1134,"presentationToStrictCompletionMs":139.9726}   |
| 6   | 947.755 / 968.636   | 1089.396 / 1100.151 | 141.641 / 131.514 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9174,"dispatchToBlockedOrPreparationMs":408.0937,"firstNewGenerationToCleanupDrainedMs":184.7971,"firstPhysicalMutationToFirstNewGenerationMs":492.5879,"presentationToStrictCompletionMs":227.9465}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4405,"dispatchToBlockedOrPreparationMs":395.042,"firstNewGenerationToCleanupDrainedMs":175.2552,"firstPhysicalMutationToFirstNewGenerationMs":526.4129,"presentationToStrictCompletionMs":223.7759}    |
| 7   | 1160.289 / 1113.010 | 1307.700 / 1238.894 | 147.411 / 125.884 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6713,"dispatchToBlockedOrPreparationMs":416.7351,"firstNewGenerationToCleanupDrainedMs":193.9378,"firstPhysicalMutationToFirstNewGenerationMs":692.3557,"presentationToStrictCompletionMs":233.111}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8962,"dispatchToBlockedOrPreparationMs":391.3582,"firstNewGenerationToCleanupDrainedMs":165.2079,"firstPhysicalMutationToFirstNewGenerationMs":678.432,"presentationToStrictCompletionMs":216.9408}    |
| 8   | 1122.704 / 1067.299 | 1271.880 / 1153.603 | 149.177 / 86.304  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0394,"dispatchToBlockedOrPreparationMs":418.9637,"firstNewGenerationToCleanupDrainedMs":192.5665,"firstPhysicalMutationToFirstNewGenerationMs":656.3107,"presentationToStrictCompletionMs":229.4671}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4058,"dispatchToBlockedOrPreparationMs":340.0112,"firstNewGenerationToCleanupDrainedMs":163.3085,"firstPhysicalMutationToFirstNewGenerationMs":646.8777,"presentationToStrictCompletionMs":165.977}    |
| 9   | 1153.443 / 1084.424 | 1299.332 / 1203.066 | 145.889 / 118.642 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2105,"dispatchToBlockedOrPreparationMs":388.2587,"firstNewGenerationToCleanupDrainedMs":204.3861,"firstPhysicalMutationToFirstNewGenerationMs":701.4765,"presentationToStrictCompletionMs":233.0682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5685,"dispatchToBlockedOrPreparationMs":365.8139,"firstNewGenerationToCleanupDrainedMs":157.6598,"firstPhysicalMutationToFirstNewGenerationMs":676.0235,"presentationToStrictCompletionMs":207.6296}   |
| 10  | 592.366 / 739.194   | 770.294 / 920.055   | 177.929 / 180.861 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9604,"dispatchToBlockedOrPreparationMs":355.7102,"firstNewGenerationToCleanupDrainedMs":229.2753,"firstPhysicalMutationToFirstNewGenerationMs":181.3485,"presentationToStrictCompletionMs":177.9286}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4299,"dispatchToBlockedOrPreparationMs":434.4461,"firstNewGenerationToCleanupDrainedMs":247.6028,"firstPhysicalMutationToFirstNewGenerationMs":234.5764,"presentationToStrictCompletionMs":180.8611}   |
| 11  | 171.418 / 180.814   | 450.117 / 506.769   | 278.699 / 325.955 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8788,"dispatchToBlockedOrPreparationMs":128.0403,"firstNewGenerationToCleanupDrainedMs":280.2042,"firstPhysicalMutationToFirstNewGenerationMs":37.9934,"presentationToStrictCompletionMs":278.6991}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3107,"dispatchToBlockedOrPreparationMs":135.7595,"firstNewGenerationToCleanupDrainedMs":327.3186,"firstPhysicalMutationToFirstNewGenerationMs":40.3805,"presentationToStrictCompletionMs":325.9554}    |
| 12  | 190.626 / 155.855   | 190.626 / 155.855   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":190.626,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":155.8548,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 822.942 / 816.206   | 822.942 / 816.206   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":203.2194,"dispatchToBlockedOrPreparationMs":438.0838,"firstNewGenerationToCleanupDrainedMs":47.8224,"firstPhysicalMutationToFirstNewGenerationMs":133.8166,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":239.7171,"dispatchToBlockedOrPreparationMs":399.0595,"firstNewGenerationToCleanupDrainedMs":42.6255,"firstPhysicalMutationToFirstNewGenerationMs":134.8043,"presentationToStrictCompletionMs":0}         |
| 14  | 1240.733 / 856.437  | 1329.442 / 973.750  | 88.709 / 117.313  | {"blockedOrPreparationToFirstPhysicalMutationMs":272.3075,"dispatchToBlockedOrPreparationMs":430.0498,"firstNewGenerationToCleanupDrainedMs":174.1568,"firstPhysicalMutationToFirstNewGenerationMs":452.9278,"presentationToStrictCompletionMs":136.4068} | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8635,"dispatchToBlockedOrPreparationMs":393.7443,"firstNewGenerationToCleanupDrainedMs":156.9688,"firstPhysicalMutationToFirstNewGenerationMs":420.1735,"presentationToStrictCompletionMs":162.0426}   |
| 15  | 749.789 / 1056.596  | 570.716 / 852.111   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1103,"dispatchToBlockedOrPreparationMs":364.6701,"firstNewGenerationToCleanupDrainedMs":0.2125,"firstPhysicalMutationToFirstNewGenerationMs":201.7232,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8929,"dispatchToBlockedOrPreparationMs":400.9014,"firstNewGenerationToCleanupDrainedMs":42.1041,"firstPhysicalMutationToFirstNewGenerationMs":406.2123,"presentationToStrictCompletionMs":0}           |
| 16  | 929.137 / 795.817   | 662.595 / 564.189   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2371,"dispatchToBlockedOrPreparationMs":385.7405,"firstNewGenerationToCleanupDrainedMs":50.9281,"firstPhysicalMutationToFirstNewGenerationMs":220.6896,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.154,"dispatchToBlockedOrPreparationMs":328.4233,"firstNewGenerationToCleanupDrainedMs":40.5265,"firstPhysicalMutationToFirstNewGenerationMs":191.0848,"presentationToStrictCompletionMs":0}            |
| 17  | 762.921 / 802.068   | 616.503 / 669.812   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8981,"dispatchToBlockedOrPreparationMs":362.8132,"firstNewGenerationToCleanupDrainedMs":45.3978,"firstPhysicalMutationToFirstNewGenerationMs":203.3939,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.855,"dispatchToBlockedOrPreparationMs":390.0592,"firstNewGenerationToCleanupDrainedMs":44.1423,"firstPhysicalMutationToFirstNewGenerationMs":230.7552,"presentationToStrictCompletionMs":0}            |
| 18  | 1400.519 / 819.604  | 1120.851 / 664.193  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":269.9156,"dispatchToBlockedOrPreparationMs":430.8939,"firstNewGenerationToCleanupDrainedMs":44.2763,"firstPhysicalMutationToFirstNewGenerationMs":375.7652,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1251,"dispatchToBlockedOrPreparationMs":368.3474,"firstNewGenerationToCleanupDrainedMs":57.0777,"firstPhysicalMutationToFirstNewGenerationMs":233.6424,"presentationToStrictCompletionMs":0}           |
| 19  | 778.498 / 788.875   | 635.134 / 660.759   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2943,"dispatchToBlockedOrPreparationMs":377.4468,"firstNewGenerationToCleanupDrainedMs":50.6356,"firstPhysicalMutationToFirstNewGenerationMs":202.7568,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.389,"dispatchToBlockedOrPreparationMs":388.6397,"firstNewGenerationToCleanupDrainedMs":41.1702,"firstPhysicalMutationToFirstNewGenerationMs":226.5601,"presentationToStrictCompletionMs":0}            |
| 20  | 546.689 / 574.860   | 731.796 / 754.819   | 185.107 / 179.959 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0066,"dispatchToBlockedOrPreparationMs":356.7277,"firstNewGenerationToCleanupDrainedMs":246.7222,"firstPhysicalMutationToFirstNewGenerationMs":123.3394,"presentationToStrictCompletionMs":185.1067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":6.6942,"dispatchToBlockedOrPreparationMs":367.9678,"firstNewGenerationToCleanupDrainedMs":244.5338,"firstPhysicalMutationToFirstNewGenerationMs":135.6234,"presentationToStrictCompletionMs":179.9589}   |
| 21  | 254.898 / 201.015   | 572.501 / 475.850   | 317.603 / 274.835 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7708,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":317.6031}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.2831,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":274.8347}             |
| 22  | 172.732 / 202.804   | 172.732 / 202.804   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7317,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":202.8045,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 261.855 / 253.842   | 261.855 / 253.842   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.8554,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":253.8423,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 1042.196 / 662.545  | 1233.573 / 844.976  | 191.377 / 182.432 | {"blockedOrPreparationToFirstPhysicalMutationMs":313.41,"dispatchToBlockedOrPreparationMs":517.0402,"firstNewGenerationToCleanupDrainedMs":241.0097,"firstPhysicalMutationToFirstNewGenerationMs":162.1131,"presentationToStrictCompletionMs":191.3771}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3796,"dispatchToBlockedOrPreparationMs":467.1429,"firstNewGenerationToCleanupDrainedMs":225.8786,"firstPhysicalMutationToFirstNewGenerationMs":148.5752,"presentationToStrictCompletionMs":182.4317}   |
| 25  | 747.831 / 1217.518  | 895.346 / 1342.163  | 147.514 / 124.645 | {"blockedOrPreparationToFirstPhysicalMutationMs":28.8488,"dispatchToBlockedOrPreparationMs":356.1955,"firstNewGenerationToCleanupDrainedMs":191.0269,"firstPhysicalMutationToFirstNewGenerationMs":319.2743,"presentationToStrictCompletionMs":228.8665}  | {"blockedOrPreparationToFirstPhysicalMutationMs":291.6381,"dispatchToBlockedOrPreparationMs":390.6191,"firstNewGenerationToCleanupDrainedMs":165.6966,"firstPhysicalMutationToFirstNewGenerationMs":494.2093,"presentationToStrictCompletionMs":206.5472} |
| 26  | 1384.499 / n/a      | 1484.709 / n/a      | 100.210 / n/a     | {"blockedOrPreparationToFirstPhysicalMutationMs":278.0765,"dispatchToBlockedOrPreparationMs":483.9876,"firstNewGenerationToCleanupDrainedMs":196.3728,"firstPhysicalMutationToFirstNewGenerationMs":526.2725,"presentationToStrictCompletionMs":151.5059} | null                                                                                                                                                                                                                                                      |
| 27  | 636.450 / n/a       | 933.032 / n/a       | 296.582 / n/a     | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1024,"dispatchToBlockedOrPreparationMs":436.9625,"firstNewGenerationToCleanupDrainedMs":297.3029,"firstPhysicalMutationToFirstNewGenerationMs":193.6644,"presentationToStrictCompletionMs":296.5822}   | null                                                                                                                                                                                                                                                      |
| 28  | 748.294 / n/a       | 879.078 / n/a       | 130.784 / n/a     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6136,"dispatchToBlockedOrPreparationMs":416.804,"firstNewGenerationToCleanupDrainedMs":174.1042,"firstPhysicalMutationToFirstNewGenerationMs":284.556,"presentationToStrictCompletionMs":176.5007}     | null                                                                                                                                                                                                                                                      |
| 29  | 1978.717 / n/a      | 2084.388 / n/a      | 105.670 / n/a     | {"blockedOrPreparationToFirstPhysicalMutationMs":773.8797,"dispatchToBlockedOrPreparationMs":472.5051,"firstNewGenerationToCleanupDrainedMs":205.3727,"firstPhysicalMutationToFirstNewGenerationMs":632.6303,"presentationToStrictCompletionMs":205.8408} | null                                                                                                                                                                                                                                                      |
| 30  | 560.371 / n/a       | 744.487 / n/a       | 184.117 / n/a     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6993,"dispatchToBlockedOrPreparationMs":388.6597,"firstNewGenerationToCleanupDrainedMs":238.0266,"firstPhysicalMutationToFirstNewGenerationMs":114.1016,"presentationToStrictCompletionMs":184.1165}   | null                                                                                                                                                                                                                                                      |
| 31  | 610.701 / n/a       | 610.701 / n/a       | 0 / n/a           | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1454,"dispatchToBlockedOrPreparationMs":419.9648,"firstNewGenerationToCleanupDrainedMs":47.1311,"firstPhysicalMutationToFirstNewGenerationMs":92.4599,"presentationToStrictCompletionMs":0}           | null                                                                                                                                                                                                                                                      |
| 32  | 197.556 / n/a       | 481.567 / n/a       | 284.011 / n/a     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.0718,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":284.0112}             | null                                                                                                                                                                                                                                                      |
| 33  | 296.510 / n/a       | 296.510 / n/a       | 0 / n/a           | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.5095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null                                                                                                                                                                                                                                                      |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 11                   | 2            | 465.818 / 509.659    | 43.841   | 14 / 16           | 2            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 4   | 12 / 13                  | 1            | 748.532 / 749.894    | 1.363    | 17 / 18           | 1            |
| 5   | 14 / 13                  | -1           | 960.719 / 987.867    | 27.149   | 19 / 18           | -1           |
| 6   | 17 / 17                  | 0            | 904.599 / 924.895    | 20.296   | 22 / 22           | 0            |
| 7   | 16 / 18                  | 2            | 1113.762 / 1073.686  | -40.076  | 21 / 23           | 2            |
| 8   | 17 / 17                  | 0            | 1079.314 / 990.295   | -89.019  | 22 / 22           | 0            |
| 9   | 16 / 17                  | 1            | 1094.946 / 1045.406  | -49.540  | 21 / 22           | 1            |
| 10  | 9 / 11                   | 2            | 541.019 / 672.452    | 131.433  | 15 / 16           | 1            |
| 11  | 3 / 3                    | 0            | 169.912 / 179.451    | 9.538    | 10 / 9            | -1           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 13  | 9 / 9                    | 0            | 775.120 / 773.581    | -1.539   | 10 / 10           | 0            |
| 14  | 23 / 18                  | -5           | 1155.285 / 816.781   | -338.504 | 28 / 23           | -5           |
| 15  | 12 / 17                  | 5            | 570.504 / 810.007    | 239.503  | 16 / 23           | 7            |
| 16  | 12 / 12                  | 0            | 611.667 / 523.662    | -88.005  | 18 / 18           | 0            |
| 17  | 12 / 12                  | 0            | 571.105 / 625.669    | 54.564   | 16 / 16           | 0            |
| 18  | 22 / 12                  | -10          | 1076.575 / 607.115   | -469.460 | 29 / 16           | -13          |
| 19  | 12 / 12                  | 0            | 584.498 / 619.589    | 35.091   | 16 / 16           | 0            |
| 20  | 10 / 10                  | 0            | 485.074 / 510.285    | 25.212   | 16 / 16           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 10           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 16 / 11                  | -5           | 992.563 / 619.098    | -373.466 | 21 / 16           | -5           |
| 25  | 13 / 23                  | 10           | 704.319 / 1176.466   | 472.148  | 18 / 28           | 10           |
| 26  | 24 / n/a                 | n/a          | 1288.337 / n/a       | n/a      | 29 / n/a          | n/a          |
| 27  | 10 / n/a                 | n/a          | 635.729 / n/a        | n/a      | 16 / n/a          | n/a          |
| 28  | 11 / n/a                 | n/a          | 704.974 / n/a        | n/a      | 16 / n/a          | n/a          |
| 29  | 29 / n/a                 | n/a          | 1879.015 / n/a       | n/a      | 34 / n/a          | n/a          |
| 30  | 11 / n/a                 | n/a          | 506.461 / n/a        | n/a      | 16 / n/a          | n/a          |
| 31  | 9 / n/a                  | n/a          | 563.570 / n/a        | n/a      | 11 / n/a          | n/a          |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / n/a          | n/a          |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / n/a           | n/a          |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 116.243 / 129.890 | 13.648   |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 226.448 / 254.003 | 27.555   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 225.499 / 233.278 | 7.779    |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 378.865 / 390.955 | 12.089   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 379.410 / 373.640 | -5.771   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 375.234 / 355.479 | -19.755  |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 422.382 / 386.003 | -36.379  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 293.625 / 258.203 | -35.422  |
| 15  | 1 / 1                | 0     | 3 / 8              | 5     | 143.915 / 409.359 | 265.444  |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 156.516 / 137.356 | -19.160  |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 152.521 / 174.798 | 22.278   |
| 18  | 1 / 1                | 0     | 13 / 3             | -10   | 646.277 / 176.157 | -470.120 |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 151.172 / 171.453 | 20.282   |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 207.881 / 363.149 | 155.268  |
| 26  | 2 / n/a              | n/a   | 11 / n/a           | n/a   | 629.135 / n/a     | n/a      |
| 27  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a           | n/a      |
| 28  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a           | n/a      |
| 29  | 3 / n/a              | n/a   | 16 / n/a           | n/a   | 1214.838 / n/a    | n/a      |
| 30  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a           | n/a      |
| 31  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a           | n/a      |
| 32  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a           | n/a      |
| 33  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a           | n/a      |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C     | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair      |
| --- | --------------------------- | -------------- | -------- | ------- | ----------- | ---------- | --------- |
| 1   | DLSS Hoshipa -> NONE        | 716.918 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 2   | NONE -> TAA                 | 170.160 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 3   | TAA -> DLAA                 | 286.338 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1032.563 / n/a | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1027.220 / n/a | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1169.543 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1390.699 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1195.918 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1249.407 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 10  | DLSS UP -> DLAA             | 914.287 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 11  | DLAA -> TAA                 | 480.252 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 12  | TAA -> NONE                 | 172.881 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 13  | NONE -> FSR AA              | 619.982 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1408.406 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 945.154 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 16  | FSR UQ -> FSR Q             | 789.230 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 17  | FSR Q -> FSR Bal            | 831.971 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 18  | FSR Bal -> FSR Perf         | 757.557 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 19  | FSR Perf -> FSR UP          | 758.886 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 20  | FSR UP -> FSR AA            | 1053.683 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 21  | FSR AA -> TAA               | 495.001 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 22  | TAA -> NONE                 | 217.047 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 23  | NONE -> DLAA                | 254.963 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 24  | DLAA -> FSR AA              | 1118.329 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1028.504 / n/a | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1514.576 / n/a | n/a      | n/a     | 1/n/a       | 2/1 -> n/a | UNMATCHED |
| 27  | FSR Hoshipa -> NONE         | 774.192 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 28  | NONE -> FSR UP              | 939.204 / n/a  | n/a      | n/a     | 0/n/a       | 2/1 -> n/a | UNMATCHED |
| 29  | FSR UP -> DLSS UP           | 1918.325 / n/a | n/a      | n/a     | 2/n/a       | 0/0 -> n/a | UNMATCHED |
| 30  | DLSS UP -> TAA              | 827.143 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 31  | TAA -> FSR AA               | 628.123 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 32  | FSR AA -> NONE              | 531.443 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 33  | NONE -> DLAA                | 273.671 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |

| Row | Presentation B/C | Cleanup B/C    | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                         | Phase durations C |
| --- | ---------------- | -------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------- |
| 1   | 485.143 / n/a    | 716.918 / n/a  | 231.775 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2651,"dispatchToBlockedOrPreparationMs":366.8156,"firstNewGenerationToCleanupDrainedMs":231.9285,"firstPhysicalMutationToFirstNewGenerationMs":114.9089,"presentationToStrictCompletionMs":231.775}    | null              |
| 2   | 170.160 / n/a    | 170.160 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.1598,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |
| 3   | 286.338 / n/a    | 286.338 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.3376,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |
| 4   | 804.481 / n/a    | 948.804 / n/a  | 144.323 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1201,"dispatchToBlockedOrPreparationMs":428.4029,"firstNewGenerationToCleanupDrainedMs":187.6301,"firstPhysicalMutationToFirstNewGenerationMs":328.6511,"presentationToStrictCompletionMs":228.0825}   | null              |
| 5   | 805.305 / n/a    | 939.029 / n/a  | 133.724 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4788,"dispatchToBlockedOrPreparationMs":433.4416,"firstNewGenerationToCleanupDrainedMs":177.506,"firstPhysicalMutationToFirstNewGenerationMs":324.6027,"presentationToStrictCompletionMs":221.9152}    | null              |
| 6   | 953.781 / n/a    | 1087.266 / n/a | 133.485 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9058,"dispatchToBlockedOrPreparationMs":399.6181,"firstNewGenerationToCleanupDrainedMs":177.5245,"firstPhysicalMutationToFirstNewGenerationMs":506.2179,"presentationToStrictCompletionMs":215.7622}   | null              |
| 7   | 1093.288 / n/a   | 1251.222 / n/a | 157.935 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2632,"dispatchToBlockedOrPreparationMs":463.7802,"firstNewGenerationToCleanupDrainedMs":206.6991,"firstPhysicalMutationToFirstNewGenerationMs":576.4798,"presentationToStrictCompletionMs":297.411}    | null              |
| 8   | 968.840 / n/a    | 1111.087 / n/a | 142.246 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0262,"dispatchToBlockedOrPreparationMs":401.594,"firstNewGenerationToCleanupDrainedMs":187.1834,"firstPhysicalMutationToFirstNewGenerationMs":518.283,"presentationToStrictCompletionMs":227.0782}     | null              |
| 9   | 971.186 / n/a    | 1146.171 / n/a | 174.985 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2317,"dispatchToBlockedOrPreparationMs":361.625,"firstNewGenerationToCleanupDrainedMs":228.7108,"firstPhysicalMutationToFirstNewGenerationMs":551.6035,"presentationToStrictCompletionMs":278.2205}    | null              |
| 10  | 692.329 / n/a    | 914.287 / n/a  | 221.958 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6875,"dispatchToBlockedOrPreparationMs":406.104,"firstNewGenerationToCleanupDrainedMs":285.7356,"firstPhysicalMutationToFirstNewGenerationMs":217.76,"presentationToStrictCompletionMs":221.9585}      | null              |
| 11  | 200.500 / n/a    | 480.252 / n/a  | 279.753 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6232,"dispatchToBlockedOrPreparationMs":153.3929,"firstNewGenerationToCleanupDrainedMs":281.5936,"firstPhysicalMutationToFirstNewGenerationMs":40.6427,"presentationToStrictCompletionMs":279.7528}    | null              |
| 12  | 172.881 / n/a    | 172.881 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8809,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |
| 13  | 619.982 / n/a    | 619.982 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4789,"dispatchToBlockedOrPreparationMs":434.7645,"firstNewGenerationToCleanupDrainedMs":42.5815,"firstPhysicalMutationToFirstNewGenerationMs":91.1569,"presentationToStrictCompletionMs":0}           | null              |
| 14  | 1273.888 / n/a   | 1361.411 / n/a | 87.523 / n/a     | {"blockedOrPreparationToFirstPhysicalMutationMs":290.4089,"dispatchToBlockedOrPreparationMs":406.8997,"firstNewGenerationToCleanupDrainedMs":175.9205,"firstPhysicalMutationToFirstNewGenerationMs":488.1822,"presentationToStrictCompletionMs":134.5184} | null              |
| 15  | 945.154 / n/a    | 634.137 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0545,"dispatchToBlockedOrPreparationMs":364.9358,"firstNewGenerationToCleanupDrainedMs":46.2897,"firstPhysicalMutationToFirstNewGenerationMs":217.8569,"presentationToStrictCompletionMs":0}           | null              |
| 16  | 789.230 / n/a    | 608.752 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2824,"dispatchToBlockedOrPreparationMs":358.6511,"firstNewGenerationToCleanupDrainedMs":44.2401,"firstPhysicalMutationToFirstNewGenerationMs":200.5787,"presentationToStrictCompletionMs":0}           | null              |
| 17  | 831.971 / n/a    | 680.701 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":5.905,"dispatchToBlockedOrPreparationMs":402.5673,"firstNewGenerationToCleanupDrainedMs":48.1971,"firstPhysicalMutationToFirstNewGenerationMs":224.0314,"presentationToStrictCompletionMs":0}            | null              |
| 18  | 757.557 / n/a    | 620.771 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2012,"dispatchToBlockedOrPreparationMs":366.6054,"firstNewGenerationToCleanupDrainedMs":43.9078,"firstPhysicalMutationToFirstNewGenerationMs":205.0569,"presentationToStrictCompletionMs":0}           | null              |
| 19  | 758.886 / n/a    | 624.111 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2709,"dispatchToBlockedOrPreparationMs":375.4259,"firstNewGenerationToCleanupDrainedMs":42.8573,"firstPhysicalMutationToFirstNewGenerationMs":201.5567,"presentationToStrictCompletionMs":0}           | null              |
| 20  | 807.760 / n/a    | 1053.683 / n/a | 245.923 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":268.8283,"dispatchToBlockedOrPreparationMs":412.9697,"firstNewGenerationToCleanupDrainedMs":246.2078,"firstPhysicalMutationToFirstNewGenerationMs":125.6768,"presentationToStrictCompletionMs":245.9226} | null              |
| 21  | 222.836 / n/a    | 495.001 / n/a  | 272.166 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.7564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.1656}             | null              |
| 22  | 217.047 / n/a    | 217.047 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":217.0474,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |
| 23  | 254.963 / n/a    | 254.963 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":254.9632,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |
| 24  | 935.684 / n/a    | 1118.329 / n/a | 182.645 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":277.6547,"dispatchToBlockedOrPreparationMs":458.7517,"firstNewGenerationToCleanupDrainedMs":229.2147,"firstPhysicalMutationToFirstNewGenerationMs":152.7076,"presentationToStrictCompletionMs":182.645}  | null              |
| 25  | 804.197 / n/a    | 943.123 / n/a  | 138.926 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":27.795,"dispatchToBlockedOrPreparationMs":405.4911,"firstNewGenerationToCleanupDrainedMs":182.8934,"firstPhysicalMutationToFirstNewGenerationMs":326.9437,"presentationToStrictCompletionMs":224.3067}   | null              |
| 26  | 1514.576 / n/a   | 1459.934 / n/a | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":275.8095,"dispatchToBlockedOrPreparationMs":439.8071,"firstNewGenerationToCleanupDrainedMs":227.9014,"firstPhysicalMutationToFirstNewGenerationMs":516.4164,"presentationToStrictCompletionMs":0}        | null              |
| 27  | 527.127 / n/a    | 774.192 / n/a  | 247.065 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8776,"dispatchToBlockedOrPreparationMs":372.2902,"firstNewGenerationToCleanupDrainedMs":248.1193,"firstPhysicalMutationToFirstNewGenerationMs":149.9049,"presentationToStrictCompletionMs":247.0652}   | null              |
| 28  | 798.736 / n/a    | 887.620 / n/a  | 88.884 / n/a     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6003,"dispatchToBlockedOrPreparationMs":413.0077,"firstNewGenerationToCleanupDrainedMs":176.1734,"firstPhysicalMutationToFirstNewGenerationMs":294.8381,"presentationToStrictCompletionMs":140.4682}   | null              |
| 29  | 1701.862 / n/a   | 1838.119 / n/a | 136.257 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":721.0348,"dispatchToBlockedOrPreparationMs":432.8313,"firstNewGenerationToCleanupDrainedMs":178.7958,"firstPhysicalMutationToFirstNewGenerationMs":505.4568,"presentationToStrictCompletionMs":216.4628} | null              |
| 30  | 588.638 / n/a    | 827.143 / n/a  | 238.505 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3175,"dispatchToBlockedOrPreparationMs":459.0829,"firstNewGenerationToCleanupDrainedMs":239.1926,"firstPhysicalMutationToFirstNewGenerationMs":124.5504,"presentationToStrictCompletionMs":238.5052}   | null              |
| 31  | 628.123 / n/a    | 628.123 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":52.1465,"dispatchToBlockedOrPreparationMs":435.4388,"firstNewGenerationToCleanupDrainedMs":44.028,"firstPhysicalMutationToFirstNewGenerationMs":96.5097,"presentationToStrictCompletionMs":0}            | null              |
| 32  | 210.815 / n/a    | 531.443 / n/a  | 320.628 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":138.7934,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":320.6276}             | null              |
| 33  | 273.671 / n/a    | 273.671 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":273.6711,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / n/a                 | n/a          | 484.990 / n/a        | n/a      | 15 / n/a          | n/a          |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / n/a           | n/a          |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / n/a           | n/a          |
| 4   | 14 / n/a                 | n/a          | 761.174 / n/a        | n/a      | 19 / n/a          | n/a          |
| 5   | 13 / n/a                 | n/a          | 761.523 / n/a        | n/a      | 18 / n/a          | n/a          |
| 6   | 17 / n/a                 | n/a          | 909.742 / n/a        | n/a      | 22 / n/a          | n/a          |
| 7   | 18 / n/a                 | n/a          | 1044.523 / n/a       | n/a      | 23 / n/a          | n/a          |
| 8   | 17 / n/a                 | n/a          | 923.903 / n/a        | n/a      | 22 / n/a          | n/a          |
| 9   | 16 / n/a                 | n/a          | 917.460 / n/a        | n/a      | 21 / n/a          | n/a          |
| 10  | 10 / n/a                 | n/a          | 628.552 / n/a        | n/a      | 15 / n/a          | n/a          |
| 11  | 3 / n/a                  | n/a          | 198.659 / n/a        | n/a      | 9 / n/a           | n/a          |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / n/a           | n/a          |
| 13  | 9 / n/a                  | n/a          | 577.400 / n/a        | n/a      | 10 / n/a          | n/a          |
| 14  | 23 / n/a                 | n/a          | 1185.491 / n/a       | n/a      | 28 / n/a          | n/a          |
| 15  | 12 / n/a                 | n/a          | 587.847 / n/a        | n/a      | 20 / n/a          | n/a          |
| 16  | 12 / n/a                 | n/a          | 564.512 / n/a        | n/a      | 17 / n/a          | n/a          |
| 17  | 12 / n/a                 | n/a          | 632.504 / n/a        | n/a      | 16 / n/a          | n/a          |
| 18  | 12 / n/a                 | n/a          | 576.864 / n/a        | n/a      | 16 / n/a          | n/a          |
| 19  | 12 / n/a                 | n/a          | 581.254 / n/a        | n/a      | 16 / n/a          | n/a          |
| 20  | 16 / n/a                 | n/a          | 807.475 / n/a        | n/a      | 21 / n/a          | n/a          |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / n/a          | n/a          |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / n/a           | n/a          |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / n/a           | n/a          |
| 24  | 15 / n/a                 | n/a          | 889.114 / n/a        | n/a      | 20 / n/a          | n/a          |
| 25  | 13 / n/a                 | n/a          | 760.230 / n/a        | n/a      | 18 / n/a          | n/a          |
| 26  | 23 / n/a                 | n/a          | 1232.033 / n/a       | n/a      | 28 / n/a          | n/a          |
| 27  | 10 / n/a                 | n/a          | 526.073 / n/a        | n/a      | 16 / n/a          | n/a          |
| 28  | 11 / n/a                 | n/a          | 711.446 / n/a        | n/a      | 16 / n/a          | n/a          |
| 29  | 29 / n/a                 | n/a          | 1659.323 / n/a       | n/a      | 34 / n/a          | n/a          |
| 30  | 10 / n/a                 | n/a          | 587.951 / n/a        | n/a      | 16 / n/a          | n/a          |
| 31  | 9 / n/a                  | n/a          | 584.095 / n/a        | n/a      | 10 / n/a          | n/a          |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / n/a          | n/a          |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / n/a           | n/a          |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | -------------- | -------- |
| 1   | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 2   | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 3   | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 4   | 1 / n/a              | n/a   | 2 / n/a            | n/a   | 213.825 / n/a  | n/a      |
| 5   | 1 / n/a              | n/a   | 2 / n/a            | n/a   | 210.089 / n/a  | n/a      |
| 6   | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 380.853 / n/a  | n/a      |
| 7   | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 441.441 / n/a  | n/a      |
| 8   | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 401.860 / n/a  | n/a      |
| 9   | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 429.952 / n/a  | n/a      |
| 10  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 11  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 12  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 13  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 14  | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 308.404 / n/a  | n/a      |
| 15  | 1 / n/a              | n/a   | 3 / n/a            | n/a   | 158.148 / n/a  | n/a      |
| 16  | 1 / n/a              | n/a   | 3 / n/a            | n/a   | 143.759 / n/a  | n/a      |
| 17  | 1 / n/a              | n/a   | 3 / n/a            | n/a   | 165.514 / n/a  | n/a      |
| 18  | 1 / n/a              | n/a   | 3 / n/a            | n/a   | 155.220 / n/a  | n/a      |
| 19  | 1 / n/a              | n/a   | 3 / n/a            | n/a   | 149.943 / n/a  | n/a      |
| 20  | 1 / n/a              | n/a   | 5 / n/a            | n/a   | 341.305 / n/a  | n/a      |
| 21  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 22  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 23  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 24  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 25  | 1 / n/a              | n/a   | 2 / n/a            | n/a   | 210.712 / n/a  | n/a      |
| 26  | 2 / n/a              | n/a   | 11 / n/a           | n/a   | 617.137 / n/a  | n/a      |
| 27  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 28  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 29  | 3 / n/a              | n/a   | 16 / n/a           | n/a   | 1054.946 / n/a | n/a      |
| 30  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 31  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 32  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 33  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |

## Cumulative gates and other health evidence

### nvidia-20260910T124329625Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                     | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                    | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":89,"allowedPresentationStretchEpisodes":19,"allowedPresentationStretchEyeObservations":192,"allowedPresentationStretchMaximumFrames":13,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                                                                                                                                                                                             | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":52644,"leftPath":"NativeOriginal","referenceFrame":53015,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                            | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 4            |
| vendorFailureStretchEyeObservations   | 2            |
| boundsMismatchFallbackEyeObservations | 0            |

### nvidia-20260910T124329625Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":83,"allowedPresentationStretchEpisodes":19,"allowedPresentationStretchEyeObservations":181,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":57130,"leftPath":"NativeOriginal","referenceFrame":57515,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                           | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 4            |
| vendorFailureStretchEyeObservations   | 2            |
| boundsMismatchFallbackEyeObservations | 0            |

### nvidia-2026-09-11T06-52-05-183Z / nvidia / pass 1

Accepted: n/a. Health evidence: INCOMPLETE.

| Raw unmet gate | Observed | Limit | Assessment role | Reason |
| -------------- | -------- | ----- | --------------- | ------ |

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

## Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change         | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | -------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16894.5078125 / 16681.24609375 / -213.26171875 | 16571.8046875 / n/a / n/a  | n/a                   |
| nvidia | 1    | systemCommitMiB   | 55422.94921875 / 55060.37890625 / -362.5703125 | 56002.18359375 / n/a / n/a | n/a                   |
| nvidia | 1    | dxgiUsageMiB      | 5583.1171875 / 3750.015625 / -1833.1015625     | 4154.43359375 / n/a / n/a  | n/a                   |
| nvidia | 1    | liveTextures      | 0 / 261 / 261                                  | 0 / n/a / n/a              | n/a                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2429.8961448669434 / 2429.8961448669434    | 0 / n/a / n/a              | n/a                   |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate | Delta |
| ------------------------------------------------------- | ----------- | --------- | ----- |
| cpu                                                     | n/a         | n/a       | n/a   |
| cpu/active                                              | false       | n/a       | n/a   |
| cpu/compactPresentationContract/publishes               | 2168        | n/a       | n/a   |
| cpu/compactPresentationContract/reuses                  | 2146        | n/a       | n/a   |
| cpu/devBenchOnly                                        | true        | n/a       | n/a   |
| cpu/generationResourceValidation/contractInvalidations  | 70          | n/a       | n/a   |
| cpu/generationResourceValidation/contractPublishes      | 155         | n/a       | n/a   |
| cpu/generationResourceValidation/fullValidations        | 581         | n/a       | n/a   |
| cpu/generationResourceValidation/stableChecks           | 8236        | n/a       | n/a   |
| cpu/generationResourceValidation/stableHits             | 8159        | n/a       | n/a   |
| cpu/generationResourceValidation/stableMisses           | 77          | n/a       | n/a   |
| cpu/schemaVersion                                       | 1           | n/a       | n/a   |
| cpu/sessionId                                           | 1           | n/a       | n/a   |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | n/a       | n/a   |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4149        | n/a       | n/a   |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | n/a       | n/a   |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4149        | n/a       | n/a   |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4107        | n/a       | n/a   |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | n/a       | n/a   |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4149        | n/a       | n/a   |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | n/a       | n/a   |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4128        | n/a       | n/a   |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | n/a       | n/a   |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | n/a       | n/a   |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4115        | n/a       | n/a   |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4054        | n/a       | n/a   |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | n/a       | n/a   |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | n/a       | n/a   |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4134        | n/a       | n/a   |
| cpu/strongStereoPacket/captures                         | 4492        | n/a       | n/a   |
| cpu/strongStereoPacket/commitAccepts                    | 4317        | n/a       | n/a   |
| cpu/strongStereoPacket/commitRejects                    | 65          | n/a       | n/a   |
| cpu/strongStereoPacket/commitValidations                | 4382        | n/a       | n/a   |
| cpu/strongStereoPacket/cycleReuses                      | 2205        | n/a       | n/a   |
| cpu/strongStereoPacket/fastSkips                        | 3806        | n/a       | n/a   |
| cpu/strongStereoPacket/invalidations                    | 4706        | n/a       | n/a   |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | n/a       | n/a   |
| cpu/strongStereoPacket/lifetimeReuses                   | 2183        | n/a       | n/a   |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.867       | n/a       | n/a   |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 22.700      | n/a       | n/a   |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.133       | n/a       | n/a   |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | n/a       | n/a   |
| cpu/window/currentFrame                                 | 53015       | n/a       | n/a   |
| cpu/window/elapsedFrames                                | 4148        | n/a       | n/a   |
| cpu/window/initialized                                  | true        | n/a       | n/a   |
| cpu/window/startFrame                                   | 48867       | n/a       | n/a   |
| gpu                                                     | n/a         | n/a       | n/a   |
| gpu/active                                              | false       | n/a       | n/a   |
| gpu/currentFrame                                        | 53017       | n/a       | n/a   |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | n/a       | n/a   |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5974703856  | n/a       | n/a   |
| gpu/item10PeripheryTAAHistory/dispatches                | 4709        | n/a       | n/a   |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11961613440 | n/a       | n/a   |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | n/a       | n/a   |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.297       | n/a       | n/a   |
| gpu/item5ActiveFSRCopies/activePixels                   | 11119322840 | n/a       | n/a   |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26297233960 | n/a       | n/a   |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14730       | n/a       | n/a   |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | n/a       | n/a   |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | n/a       | n/a   |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | n/a       | n/a   |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | n/a       | n/a   |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | n/a       | n/a   |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | n/a       | n/a   |
| gpu/item7EarlyHAM/directOutputSkips                     | 1971        | n/a       | n/a   |
| gpu/item7EarlyHAM/executedClears                        | 1974        | n/a       | n/a   |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1974        | n/a       | n/a   |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | n/a       | n/a   |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | n/a       | n/a   |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | n/a       | n/a   |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4104        | n/a       | n/a   |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | n/a       | n/a   |
| gpu/item9SpatialComposite/dispatches                    | 0           | n/a       | n/a   |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | n/a       | n/a   |
| gpu/observedFrames                                      | 4150        | n/a       | n/a   |
| gpu/startFrame                                          | 48867       | n/a       | n/a   |
| profiler                                                | n/a         | n/a       | n/a   |
| profiler/available                                      | true        | n/a       | n/a   |
| profiler/capabilities                                   | 63          | n/a       | n/a   |
| profiler/capturing                                      | false       | n/a       | n/a   |
| profiler/enabled                                        | true        | n/a       | n/a   |
| profiler/frame/acquiredSlots                            | 0           | n/a       | n/a   |
| profiler/frame/captured                                 | 0           | n/a       | n/a   |
| profiler/frame/peakAcquiredSlots                        | 0           | n/a       | n/a   |
| profiler/frame/slotRefusals                             | 0           | n/a       | n/a   |
| profiler/limits/frameLatency                            | 3           | n/a       | n/a   |
| profiler/limits/historyCapacity                         | 300         | n/a       | n/a   |
| profiler/limits/maximumTimers                           | 128         | n/a       | n/a   |
| profiler/timerCount                                     | 0           | n/a       | n/a   |
| profiler/totalsMs/cpu                                   | n/a         | n/a       | n/a   |
| profiler/totalsMs/gpu                                   | n/a         | n/a       | n/a   |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a       | n/a   |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a       | n/a   |
| texture                                                 | n/a         | n/a       | n/a   |
| texture/active                                          | false       | n/a       | n/a   |
| texture/attachFailures                                  | 0           | n/a       | n/a   |
| texture/createdCount                                    | 3962        | n/a       | n/a   |
| texture/createdEstimatedBytes                           | 38815224912 | n/a       | n/a   |
| texture/currentCohort                                   | 0           | n/a       | n/a   |
| texture/destroyedCount                                  | 3701        | n/a       | n/a   |
| texture/destroyedEstimatedBytes                         | 36267294132 | n/a       | n/a   |
| texture/droppedTextureRecords                           | 0           | n/a       | n/a   |
| texture/faceGenAssignmentFailures                       | 0           | n/a       | n/a   |
| texture/faceGenTintAssignmentCount                      | 0           | n/a       | n/a   |
| texture/groupCount                                      | 886         | n/a       | n/a   |
| texture/liveTextureRecordCount                          | 261         | n/a       | n/a   |
| texture/maxTrackedLiveTextures                          | 16384       | n/a       | n/a   |
| texture/maxTrackedTextureGroups                         | 4096        | n/a       | n/a   |
| texture/niSourceTextureMatchedCount                     | 50          | n/a       | n/a   |
| texture/niSourceTextureMatchedEstimatedBytes            | 172279000   | n/a       | n/a   |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | n/a       | n/a   |
| texture/niSourceTextureResourceCount                    | 1536        | n/a       | n/a   |
| texture/niSourceTextureTraversalLimitReached            | false       | n/a       | n/a   |
| texture/outstandingCount                                | 261         | n/a       | n/a   |
| texture/outstandingEstimatedBytes                       | 2547930780  | n/a       | n/a   |
| texture/outstandingUnknownEstimateCount                 | 0           | n/a       | n/a   |
| texture/recordingFailures                               | 0           | n/a       | n/a   |
| texture/sentinelAllocationFailures                      | 0           | n/a       | n/a   |
| texture/sessionID                                       | 1           | n/a       | n/a   |
| texture/supported                                       | true        | n/a       | n/a   |

</details>

## Context, memory, CPU/GPU and evidence

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

## Retained transition, retry and memory evidence

# renderscale-tuning-nvidia final report

-   Assay execution: **INTERRUPTED**
-   Transitions dispatched: **25/66**
-   Terminal render verdict: **PASS** (terminal condition only)
-   Lane qualification: **NOT_APPLICABLE**
-   Full-history switch health: **INCONCLUSIVE**
-   Change assessment: **INCONCLUSIVE**
-   Non-stable terminal notes: **0**
-   Task 2/evidence: **per transition** (25 PASS, 0 FAIL, 0 INCONCLUSIVE)
-   Reporting completeness: **INCOMPLETE**
-   Deployment verification: **COMPLETE**
-   Interruption: **transition_receipt_unavailable**
-   Failed scenario step: **not_exposed** (first unreported: transition-pace; receipt: nvidia-2026-09-11T06-52-05-183Z:nvidia:pass-1:transition-26:scenario)
-   Memory confirmation: **repeat_not_completed**
-   Presentation stretch: **14 selected, 14 recovered PASS, 0 unrecovered**

Task 2 is deliberately not aggregated. Reporting failure does not rewrite the render result, and a render pass does not hide missing per-transition evidence. Every raw JSON value is available in `evidence-values.csv`.

## Per-pass switch health and performance

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms   | Max ms   | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence   |
| ------ | ---- | ---- | ------------------ | -------------- | -------- | -------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | ---------- |
| nvidia | 1    | 25   | 25/0               | 805.170        | 1322.371 | 1424.065 | 15                 | 748.203         | 13.474              | n/a              | n/a            | n/a        | 9       | 0        | 0                   | INCONCLUSIVE    | INCOMPLETE |

### nvidia pass 1

Cumulative accepted: **n/a**. Evidence gaps: owned_stopped_stress_record_missing_or_mismatched; stress_acceptance_missing.

| Raw unmet gate | Observed | Limit | Assessment role | Reason |
| -------------- | -------- | ----- | --------------- | ------ |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

## Memory confirmation

### nvidia

Memory outcome: **n/a** (repeat_not_completed); completed passes: 0/2; cooldown: n.d. ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    16571.805 |       n.d. |         n.d. |           n.d. |         n.d. |           n.d. |         n.d. |       n.d. |         n.d. |                   n.d. |
| System commit MiB                  |    56002.184 |       n.d. |         n.d. |           n.d. |         n.d. |           n.d. |         n.d. |       n.d. |         n.d. |                   n.d. |
| DXGI process usage MiB             |     4154.434 |       n.d. |         n.d. |           n.d. |         n.d. |           n.d. |         n.d. |       n.d. |         n.d. |                   n.d. |
| Memory pressure                    |       Normal |       n.d. |         n.d. |           n.d. |         n.d. |           n.d. |         n.d. |       n.d. |         n.d. |                   n.d. |
| Live tracked textures              |            0 |       n.d. |         n.d. |           n.d. |         n.d. |           n.d. |         n.d. |       n.d. |         n.d. |                   n.d. |
| Estimated live tracked texture MiB |            0 |       n.d. |         n.d. |           n.d. |         n.d. |           n.d. |         n.d. |       n.d. |         n.d. |                   n.d. |

Predicate inputs (unrounded):

```json
{
    "available": false,
    "reason": "repeat_not_completed",
    "pass1": {
        "processPrivateMiB": null,
        "systemCommitMiB": null,
        "dxgiUsageMiB": null,
        "liveTextures": null,
        "liveTextureMiB": null
    },
    "pass2": {
        "processPrivateMiB": null,
        "systemCommitMiB": null,
        "dxgiUsageMiB": null,
        "liveTextures": null,
        "liveTextureMiB": null
    },
    "positivePass1PrivateAndCommit": null,
    "pass2ResourceGrowth": null,
    "retentionPredicate": null,
    "initializationPredicate": null
}
```

Conclusion: no_leak_or_retention_conclusion_possible. Growth is relative to the freshly reset tracker in each pass; session IDs are retained.

Evidence gaps: pass1_end:boundary_receipt_missing; pass1_end:processPrivateMiB:unavailable; pass1_end:systemCommitMiB:unavailable; pass1_end:dxgiUsageMiB:unavailable; pass1_end:memoryPressure:unavailable; pass1_end:liveTextures:unavailable; pass1_end:liveTextureMiB:unavailable; cooldown_start:boundary_receipt_missing; cooldown_start:processPrivateMiB:unavailable; cooldown_start:systemCommitMiB:unavailable; cooldown_start:dxgiUsageMiB:unavailable; cooldown_start:memoryPressure:unavailable; cooldown_start:liveTextures:unavailable; cooldown_start:liveTextureMiB:unavailable; cooldown_end:boundary_receipt_missing; cooldown_end:processPrivateMiB:unavailable; cooldown_end:systemCommitMiB:unavailable; cooldown_end:dxgiUsageMiB:unavailable; cooldown_end:memoryPressure:unavailable; cooldown_end:liveTextures:unavailable; cooldown_end:liveTextureMiB:unavailable; pass2_start:boundary_receipt_missing; pass2_start:processPrivateMiB:unavailable; pass2_start:systemCommitMiB:unavailable; pass2_start:dxgiUsageMiB:unavailable; pass2_start:memoryPressure:unavailable; pass2_start:liveTextures:unavailable; pass2_start:liveTextureMiB:unavailable; pass2_end:boundary_receipt_missing; pass2_end:processPrivateMiB:unavailable; pass2_end:systemCommitMiB:unavailable; pass2_end:dxgiUsageMiB:unavailable; pass2_end:memoryPressure:unavailable; pass2_end:liveTextures:unavailable; pass2_end:liveTextureMiB:unavailable; pass1:matrix_receipts_incomplete_or_duplicated; pass2:matrix_receipts_incomplete_or_duplicated; cooldown_wait_missing_or_invalid.

## Transitions

Retry waits end at the first observed preparation-ready result. Ready-to-candidate includes stereo qualification and the settling guard; it is not isolated retry overhead. Overlapping viewport waits are not added. Unavailable results are n.d. and the affected reporting outcome is n/a. All retrieved values remain in raw evidence and evidence-values.csv; reporting provenance never aborts or replays measurements.

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                            | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery   | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | -------------------------------------------------------- | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ------------------ | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 1              | 25681/556.6505 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                            | 135.888 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 26059/799.3485 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                            | 101.122 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 26193/1117.832 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 54.395 ms; SubmitStageFoveatedCenter: 54.338 ms | 227.282 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 26329/968.6364 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 58.734 ms; SubmitStageFoveatedCenter: 58.713 ms | 225.255 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 26467/1113.0101 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 51.413 ms; SubmitStageFoveatedCenter: 51.342 ms | 207.562 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 26601/1067.2991 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 59.189 ms; SubmitStageFoveatedCenter: 59.049 ms | 230.907 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 26741/1084.4236 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 68 records / 3 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 6              | 27394/856.4371 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 8              | 27539/1056.5962 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 27678/795.8168 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 27806/802.0684 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 27937/819.6036 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 28071/788.8749 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 266.641 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 28865/1217.518 ms  | not_needed | MATCHED   | none                | none             | none                      |

## Presentation stretch anomalies

| Lane   | Pass | Row | From                                                      | To                                                                          | Consecutive frames | Recovered PASS |
| ------ | ---: | --: | --------------------------------------------------------- | --------------------------------------------------------------------------- | -----------------: | -------------- |
| nvidia |    1 |   1 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"method":"none","qualityMode":0,"renderScaleMode":false}                   |                  1 | yes            |
| nvidia |    1 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    1 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    1 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |                  6 | yes            |
| nvidia |    1 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true} |                  8 | yes            |
| nvidia |    1 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  6 | yes            |

## Final file check

`pwsh ./tools/git.ps1 diff --check -- docs/development/vr-render-scale-comparison-ledger.csv docs/development/vr-render-scale-iteration.md docs/development/nvidia-renderscale-tuning-pr73-cf1616728-20260911.md`
exited 0. Git warned that the iteration document will normalize CRLF to
LF on its next write. This whitespace check does not validate runtime
behavior. Report evidence links resolve to preserved local files.
