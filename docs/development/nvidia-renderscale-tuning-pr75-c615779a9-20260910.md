# NVIDIA tuning interruption: PR75 c615779a9, 10 September 2026

Execution is **INTERRUPTED** before the first measured transition. The
first DLSS Hoshipa baseline timed out after **20000.9316 ms / 525 frames**.
All 33 transitions in pass 1 and all 33 in pass 2 are **NOT RUN**.
The matrix terminal result, Task 2 and improvement assessment are
inconclusive; there are no candidate performance measurements.

Run `renderscale-tuning-nvidia-2026-09-10T14-36-07-046Z` used game PID 42844, clean Release source
`c615779a903d7e3f8f95c7edcf303a02ad08dced`, Build ID
`cbef73dfc7a11e26e01ad8ecdca03c8d45d4c6549ddfd3a51b8a0c537499014d`.
Its exact renderer source is c615779a9, with main-VR base
`7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`.

The public API accepted operation 1 but it remained WaitingForSafePoint.
The stable physical profile remained DLSS Quality while the request was
DLSS Hoshipa, preset K, with the 0.3/0.3/0.7 fixture. No physical
replacement began. The waiter recorded profile/active-contract mismatch,
active API operation, blocking conditions, unsettled controller and pending
relatch. Device-loss, OOM and lifecycle failure deltas were zero at the
baseline boundary; these are not evidence of completed matrix health.

Qualification closed and the baseline stress capture stopped at frame 44939. Read-only verification at frames 47829 onward confirmed all eight
capture/qualification states inactive against the exact PID and Build ID.
The worker retained cleanupVerified=false and its endpoint ownership lock;
those diagnostics remain unchanged. Operation 1 was still pending during
verification. No reset, restart or measurement replay was issued.

The physical 28060672-byte DLL has SHA-256
`7ec4ccdb57f42183343fd9b5fe60aab643b86ee38489309ce6ad674589a904a9`.
Its adjacent manifest, AIO build receipt and archive match. The selected
profile has exactly one enabled loose DLL provider; Overwrite and unmanaged
Data contain no competing CommunityShaders DLL. All six journal records
are flushed and the full scalar evidence export is retained. Matrix,
pass-end memory, cooldown and profiler evidence are unavailable because
the first baseline failed; generated reporting remains INCOMPLETE.

The reference is the previous relevant measured main-VR run
`nvidia-20260910T124329625Z`, source 7c8e3e656. Candidate timings and paired
deltas remain unavailable. The complete side-by-side tables follow.

The [canonical ledger](vr-render-scale-ledger.md) retains every
field of the finalized summary and comparison, including the baseline
timeout and all missing segments. Field-for-field reconstruction passed;
528 reference numeric timing cells were audited and every
historical cell was preserved. Local completion took 2195.0 ms.
Runtime feedback: `AUTO-20260910-144334201-99CFFE42`.

Evidence: [summary](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T14-36-07-046Z/summary.json),
[cleanup verification](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T14-36-07-046Z/raw/guarded-cleanup/verification.json),
[deployment](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T14-36-07-046Z/raw/offline/physical-aio-verification.json),
[ledger coverage](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T14-36-07-046Z/complete-ledger-validation.json).

# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / INTERRUPTED** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                | Candidate                                                                                                       |
| ----------------------- | --------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | nvidia-20260910T124329625Z                                                              | renderscale-tuning-nvidia-2026-09-10T14-36-07-046Z                                                              |
| Renderer base           | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | c615779a903d7e3f8f95c7edcf303a02ad08dced                                                                        |
| Main-VR base/equivalent | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                                        |
| Compiled source         | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | c615779a903d7e3f8f95c7edcf303a02ad08dced                                                                        |
| Build ID                | 9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4                        | cbef73dfc7a11e26e01ad8ecdca03c8d45d4c6549ddfd3a51b8a0c537499014d                                                |
| DLL SHA-256             | bcd04e8c31248d5cfb02148266fbc2ead73d06b36bafa59cf8f971d79df24afe                        | 7ec4ccdb57f42183343fd9b5fe60aab643b86ee38489309ce6ad674589a904a9                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-20260910T124329625Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-10T14-36-07-046Z |

Assessment limits: retained_context_not_matched:scene; unmatched_or_missing_transitions; matching_fixture_fingerprint_unavailable; incomplete_execution_coverage; incomplete_reporting; incomplete_health_evidence; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane | Pass | Rows B/C | Mean ms B/C | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ---- | ---- | -------- | ----------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane | Pass | Metric | Unit | Baseline | Candidate | Delta | Delta % |
| ---- | ---- | ------ | ---- | -------- | --------- | ----- | ------- |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C     | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair      |
| --- | --------------------------- | -------------- | -------- | ------- | ----------- | ---------- | --------- |
| 1   | DLSS Hoshipa -> NONE        | 688.545 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 2   | NONE -> TAA                 | 169.425 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 3   | TAA -> DLAA                 | 249.126 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1004.374 / n/a | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1266.234 / n/a | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1175.702 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1393.400 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1352.171 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1386.511 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 10  | DLSS UP -> DLAA             | 770.294 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 11  | DLAA -> TAA                 | 450.117 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 12  | TAA -> NONE                 | 190.626 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 13  | NONE -> FSR AA              | 822.942 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.140 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 749.789 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 16  | FSR UQ -> FSR Q             | 929.137 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 17  | FSR Q -> FSR Bal            | 762.921 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 18  | FSR Bal -> FSR Perf         | 1400.519 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 19  | FSR Perf -> FSR UP          | 778.498 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 20  | FSR UP -> FSR AA            | 731.796 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 21  | FSR AA -> TAA               | 572.501 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 22  | TAA -> NONE                 | 172.732 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 23  | NONE -> DLAA                | 261.855 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 24  | DLAA -> FSR AA              | 1233.573 / n/a | n/a      | n/a     | 1/n/a       | 0/0 -> n/a | UNMATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 976.698 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1536.005 / n/a | n/a      | n/a     | 1/n/a       | 2/1 -> n/a | UNMATCHED |
| 27  | FSR Hoshipa -> NONE         | 933.032 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 28  | NONE -> FSR UP              | 924.794 / n/a  | n/a      | n/a     | 0/n/a       | 2/1 -> n/a | UNMATCHED |
| 29  | FSR UP -> DLSS UP           | 2184.558 / n/a | n/a      | n/a     | 2/n/a       | 0/0 -> n/a | UNMATCHED |
| 30  | DLSS UP -> TAA              | 744.487 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 31  | TAA -> FSR AA               | 610.701 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 32  | FSR AA -> NONE              | 481.567 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |
| 33  | NONE -> DLAA                | 296.510 / n/a  | n/a      | n/a     | 0/n/a       | 0/0 -> n/a | UNMATCHED |

| Row | Presentation B/C | Cleanup B/C    | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                         | Phase durations C |
| --- | ---------------- | -------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------- |
| 1   | 466.266 / n/a    | 688.545 / n/a  | 222.279 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1234,"dispatchToBlockedOrPreparationMs":350.2632,"firstNewGenerationToCleanupDrainedMs":222.7267,"firstPhysicalMutationToFirstNewGenerationMs":112.4315,"presentationToStrictCompletionMs":222.2791}   | null              |
| 2   | 169.425 / n/a    | 169.425 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.4249,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |
| 3   | 249.126 / n/a    | 249.126 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.1258,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |
| 4   | 790.942 / n/a    | 920.929 / n/a  | 129.987 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2853,"dispatchToBlockedOrPreparationMs":392.9859,"firstNewGenerationToCleanupDrainedMs":172.3976,"firstPhysicalMutationToFirstNewGenerationMs":351.2604,"presentationToStrictCompletionMs":213.4314}   | null              |
| 5   | 1014.085 / n/a   | 1168.706 / n/a | 154.620 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2487,"dispatchToBlockedOrPreparationMs":400.2561,"firstNewGenerationToCleanupDrainedMs":207.9868,"firstPhysicalMutationToFirstNewGenerationMs":556.2139,"presentationToStrictCompletionMs":252.149}    | null              |
| 6   | 947.755 / n/a    | 1089.396 / n/a | 141.641 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9174,"dispatchToBlockedOrPreparationMs":408.0937,"firstNewGenerationToCleanupDrainedMs":184.7971,"firstPhysicalMutationToFirstNewGenerationMs":492.5879,"presentationToStrictCompletionMs":227.9465}   | null              |
| 7   | 1160.289 / n/a   | 1307.700 / n/a | 147.411 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6713,"dispatchToBlockedOrPreparationMs":416.7351,"firstNewGenerationToCleanupDrainedMs":193.9378,"firstPhysicalMutationToFirstNewGenerationMs":692.3557,"presentationToStrictCompletionMs":233.111}    | null              |
| 8   | 1122.704 / n/a   | 1271.880 / n/a | 149.177 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0394,"dispatchToBlockedOrPreparationMs":418.9637,"firstNewGenerationToCleanupDrainedMs":192.5665,"firstPhysicalMutationToFirstNewGenerationMs":656.3107,"presentationToStrictCompletionMs":229.4671}   | null              |
| 9   | 1153.443 / n/a   | 1299.332 / n/a | 145.889 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2105,"dispatchToBlockedOrPreparationMs":388.2587,"firstNewGenerationToCleanupDrainedMs":204.3861,"firstPhysicalMutationToFirstNewGenerationMs":701.4765,"presentationToStrictCompletionMs":233.0682}   | null              |
| 10  | 592.366 / n/a    | 770.294 / n/a  | 177.929 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9604,"dispatchToBlockedOrPreparationMs":355.7102,"firstNewGenerationToCleanupDrainedMs":229.2753,"firstPhysicalMutationToFirstNewGenerationMs":181.3485,"presentationToStrictCompletionMs":177.9286}   | null              |
| 11  | 171.418 / n/a    | 450.117 / n/a  | 278.699 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8788,"dispatchToBlockedOrPreparationMs":128.0403,"firstNewGenerationToCleanupDrainedMs":280.2042,"firstPhysicalMutationToFirstNewGenerationMs":37.9934,"presentationToStrictCompletionMs":278.6991}    | null              |
| 12  | 190.626 / n/a    | 190.626 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":190.626,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | null              |
| 13  | 822.942 / n/a    | 822.942 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":203.2194,"dispatchToBlockedOrPreparationMs":438.0838,"firstNewGenerationToCleanupDrainedMs":47.8224,"firstPhysicalMutationToFirstNewGenerationMs":133.8166,"presentationToStrictCompletionMs":0}         | null              |
| 14  | 1240.733 / n/a   | 1329.442 / n/a | 88.709 / n/a     | {"blockedOrPreparationToFirstPhysicalMutationMs":272.3075,"dispatchToBlockedOrPreparationMs":430.0498,"firstNewGenerationToCleanupDrainedMs":174.1568,"firstPhysicalMutationToFirstNewGenerationMs":452.9278,"presentationToStrictCompletionMs":136.4068} | null              |
| 15  | 749.789 / n/a    | 570.716 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1103,"dispatchToBlockedOrPreparationMs":364.6701,"firstNewGenerationToCleanupDrainedMs":0.2125,"firstPhysicalMutationToFirstNewGenerationMs":201.7232,"presentationToStrictCompletionMs":0}            | null              |
| 16  | 929.137 / n/a    | 662.595 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2371,"dispatchToBlockedOrPreparationMs":385.7405,"firstNewGenerationToCleanupDrainedMs":50.9281,"firstPhysicalMutationToFirstNewGenerationMs":220.6896,"presentationToStrictCompletionMs":0}           | null              |
| 17  | 762.921 / n/a    | 616.503 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8981,"dispatchToBlockedOrPreparationMs":362.8132,"firstNewGenerationToCleanupDrainedMs":45.3978,"firstPhysicalMutationToFirstNewGenerationMs":203.3939,"presentationToStrictCompletionMs":0}           | null              |
| 18  | 1400.519 / n/a   | 1120.851 / n/a | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":269.9156,"dispatchToBlockedOrPreparationMs":430.8939,"firstNewGenerationToCleanupDrainedMs":44.2763,"firstPhysicalMutationToFirstNewGenerationMs":375.7652,"presentationToStrictCompletionMs":0}         | null              |
| 19  | 778.498 / n/a    | 635.134 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2943,"dispatchToBlockedOrPreparationMs":377.4468,"firstNewGenerationToCleanupDrainedMs":50.6356,"firstPhysicalMutationToFirstNewGenerationMs":202.7568,"presentationToStrictCompletionMs":0}           | null              |
| 20  | 546.689 / n/a    | 731.796 / n/a  | 185.107 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0066,"dispatchToBlockedOrPreparationMs":356.7277,"firstNewGenerationToCleanupDrainedMs":246.7222,"firstPhysicalMutationToFirstNewGenerationMs":123.3394,"presentationToStrictCompletionMs":185.1067}   | null              |
| 21  | 254.898 / n/a    | 572.501 / n/a  | 317.603 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7708,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":317.6031}             | null              |
| 22  | 172.732 / n/a    | 172.732 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7317,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |
| 23  | 261.855 / n/a    | 261.855 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.8554,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |
| 24  | 1042.196 / n/a   | 1233.573 / n/a | 191.377 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":313.41,"dispatchToBlockedOrPreparationMs":517.0402,"firstNewGenerationToCleanupDrainedMs":241.0097,"firstPhysicalMutationToFirstNewGenerationMs":162.1131,"presentationToStrictCompletionMs":191.3771}   | null              |
| 25  | 747.831 / n/a    | 895.346 / n/a  | 147.514 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":28.8488,"dispatchToBlockedOrPreparationMs":356.1955,"firstNewGenerationToCleanupDrainedMs":191.0269,"firstPhysicalMutationToFirstNewGenerationMs":319.2743,"presentationToStrictCompletionMs":228.8665}  | null              |
| 26  | 1384.499 / n/a   | 1484.709 / n/a | 100.210 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":278.0765,"dispatchToBlockedOrPreparationMs":483.9876,"firstNewGenerationToCleanupDrainedMs":196.3728,"firstPhysicalMutationToFirstNewGenerationMs":526.2725,"presentationToStrictCompletionMs":151.5059} | null              |
| 27  | 636.450 / n/a    | 933.032 / n/a  | 296.582 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1024,"dispatchToBlockedOrPreparationMs":436.9625,"firstNewGenerationToCleanupDrainedMs":297.3029,"firstPhysicalMutationToFirstNewGenerationMs":193.6644,"presentationToStrictCompletionMs":296.5822}   | null              |
| 28  | 748.294 / n/a    | 879.078 / n/a  | 130.784 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6136,"dispatchToBlockedOrPreparationMs":416.804,"firstNewGenerationToCleanupDrainedMs":174.1042,"firstPhysicalMutationToFirstNewGenerationMs":284.556,"presentationToStrictCompletionMs":176.5007}     | null              |
| 29  | 1978.717 / n/a   | 2084.388 / n/a | 105.670 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":773.8797,"dispatchToBlockedOrPreparationMs":472.5051,"firstNewGenerationToCleanupDrainedMs":205.3727,"firstPhysicalMutationToFirstNewGenerationMs":632.6303,"presentationToStrictCompletionMs":205.8408} | null              |
| 30  | 560.371 / n/a    | 744.487 / n/a  | 184.117 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6993,"dispatchToBlockedOrPreparationMs":388.6597,"firstNewGenerationToCleanupDrainedMs":238.0266,"firstPhysicalMutationToFirstNewGenerationMs":114.1016,"presentationToStrictCompletionMs":184.1165}   | null              |
| 31  | 610.701 / n/a    | 610.701 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1454,"dispatchToBlockedOrPreparationMs":419.9648,"firstNewGenerationToCleanupDrainedMs":47.1311,"firstPhysicalMutationToFirstNewGenerationMs":92.4599,"presentationToStrictCompletionMs":0}           | null              |
| 32  | 197.556 / n/a    | 481.567 / n/a  | 284.011 / n/a    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.0718,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":284.0112}             | null              |
| 33  | 296.510 / n/a    | 296.510 / n/a  | 0 / n/a          | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.5095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | null              |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / n/a                  | n/a          | 465.818 / n/a        | n/a      | 14 / n/a          | n/a          |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / n/a           | n/a          |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / n/a           | n/a          |
| 4   | 12 / n/a                 | n/a          | 748.532 / n/a        | n/a      | 17 / n/a          | n/a          |
| 5   | 14 / n/a                 | n/a          | 960.719 / n/a        | n/a      | 19 / n/a          | n/a          |
| 6   | 17 / n/a                 | n/a          | 904.599 / n/a        | n/a      | 22 / n/a          | n/a          |
| 7   | 16 / n/a                 | n/a          | 1113.762 / n/a       | n/a      | 21 / n/a          | n/a          |
| 8   | 17 / n/a                 | n/a          | 1079.314 / n/a       | n/a      | 22 / n/a          | n/a          |
| 9   | 16 / n/a                 | n/a          | 1094.946 / n/a       | n/a      | 21 / n/a          | n/a          |
| 10  | 9 / n/a                  | n/a          | 541.019 / n/a        | n/a      | 15 / n/a          | n/a          |
| 11  | 3 / n/a                  | n/a          | 169.912 / n/a        | n/a      | 10 / n/a          | n/a          |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / n/a           | n/a          |
| 13  | 9 / n/a                  | n/a          | 775.120 / n/a        | n/a      | 10 / n/a          | n/a          |
| 14  | 23 / n/a                 | n/a          | 1155.285 / n/a       | n/a      | 28 / n/a          | n/a          |
| 15  | 12 / n/a                 | n/a          | 570.504 / n/a        | n/a      | 16 / n/a          | n/a          |
| 16  | 12 / n/a                 | n/a          | 611.667 / n/a        | n/a      | 18 / n/a          | n/a          |
| 17  | 12 / n/a                 | n/a          | 571.105 / n/a        | n/a      | 16 / n/a          | n/a          |
| 18  | 22 / n/a                 | n/a          | 1076.575 / n/a       | n/a      | 29 / n/a          | n/a          |
| 19  | 12 / n/a                 | n/a          | 584.498 / n/a        | n/a      | 16 / n/a          | n/a          |
| 20  | 10 / n/a                 | n/a          | 485.074 / n/a        | n/a      | 16 / n/a          | n/a          |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / n/a          | n/a          |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / n/a           | n/a          |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / n/a           | n/a          |
| 24  | 16 / n/a                 | n/a          | 992.563 / n/a        | n/a      | 21 / n/a          | n/a          |
| 25  | 13 / n/a                 | n/a          | 704.319 / n/a        | n/a      | 18 / n/a          | n/a          |
| 26  | 24 / n/a                 | n/a          | 1288.337 / n/a       | n/a      | 29 / n/a          | n/a          |
| 27  | 10 / n/a                 | n/a          | 635.729 / n/a        | n/a      | 16 / n/a          | n/a          |
| 28  | 11 / n/a                 | n/a          | 704.974 / n/a        | n/a      | 16 / n/a          | n/a          |
| 29  | 29 / n/a                 | n/a          | 1879.015 / n/a       | n/a      | 34 / n/a          | n/a          |
| 30  | 11 / n/a                 | n/a          | 506.461 / n/a        | n/a      | 16 / n/a          | n/a          |
| 31  | 9 / n/a                  | n/a          | 563.570 / n/a        | n/a      | 11 / n/a          | n/a          |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / n/a          | n/a          |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / n/a           | n/a          |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | -------------- | -------- |
| 1   | 1 / n/a              | n/a   | 1 / n/a            | n/a   | 116.243 / n/a  | n/a      |
| 2   | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 3   | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 4   | 1 / n/a              | n/a   | 2 / n/a            | n/a   | 226.448 / n/a  | n/a      |
| 5   | 1 / n/a              | n/a   | 2 / n/a            | n/a   | 225.499 / n/a  | n/a      |
| 6   | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 378.865 / n/a  | n/a      |
| 7   | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 379.410 / n/a  | n/a      |
| 8   | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 375.234 / n/a  | n/a      |
| 9   | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 422.382 / n/a  | n/a      |
| 10  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 11  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 12  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 13  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 14  | 1 / n/a              | n/a   | 6 / n/a            | n/a   | 293.625 / n/a  | n/a      |
| 15  | 1 / n/a              | n/a   | 3 / n/a            | n/a   | 143.915 / n/a  | n/a      |
| 16  | 1 / n/a              | n/a   | 3 / n/a            | n/a   | 156.516 / n/a  | n/a      |
| 17  | 1 / n/a              | n/a   | 3 / n/a            | n/a   | 152.521 / n/a  | n/a      |
| 18  | 1 / n/a              | n/a   | 13 / n/a           | n/a   | 646.277 / n/a  | n/a      |
| 19  | 1 / n/a              | n/a   | 3 / n/a            | n/a   | 151.172 / n/a  | n/a      |
| 20  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 21  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 22  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 23  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 24  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 25  | 1 / n/a              | n/a   | 2 / n/a            | n/a   | 207.881 / n/a  | n/a      |
| 26  | 2 / n/a              | n/a   | 11 / n/a           | n/a   | 629.135 / n/a  | n/a      |
| 27  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 28  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 29  | 3 / n/a              | n/a   | 16 / n/a           | n/a   | 1214.838 / n/a | n/a      |
| 30  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 31  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 32  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |
| 33  | 0 / n/a              | n/a   | 0 / n/a            | n/a   | 0 / n/a        | n/a      |

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

## Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane | Pass | Metric | B start/end/change | C start/end/change | Change difference C-B |
| ---- | ---- | ------ | ------------------ | ------------------ | --------------------- |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

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

## Follow-up diagnosis and cleanup correction

The preserved game log identifies menu-protection safety deferral as the
runtime blocker. A menu-context change at 16:43:11.889 local time cleared
that protection; the original request reached Active at 16:43:13.876,
frame 53460, epoch 2, generation 3. This task issued no replay. Whether the
earlier menu was stale or intentionally open is not established. The later
menu list contained only HUD Menu. A subsequent snapshot could not reach
DevBench over HTTP. This later, unmeasured completion does not qualify the
interrupted assay or replace its missing 66 matrix rows.

The cleanup defect was reproduced and fixed in automation commit
`9d87747a8f838bf9a5d5ddef27b16c60072ec101`, integrated into local `dev`.
Validated baseline owners now use shared guarded cleanup, which journals
stop and final-status receipts and verifies inactivity. The original runner
fails the regression. Timeout, recovered lost response, foreign-owner and
refused-stop fixtures pass with the fix; full runner and worker suites pass.
Source and package files match. The installed plugin remains unchanged.
Feedback `AUTO-20260910-145814354-32C64878` is resolved; the runtime feedback
retains the distinction between the menu deferral and cleanup bookkeeping.
The canonical ledger retains this complete follow-up under
`tuning_detail_followup_diagnosis_json`; original summary and comparison
cells remain unchanged. The hash-matched comparison was reused and the
528 reference timings audited again after this evidence supplement.

[Diagnosis](../../artifacts/nvidia-baseline-repair-20260910/diagnosis.json)
and [regression evidence](../../artifacts/nvidia-baseline-repair-20260910/validation.json).
