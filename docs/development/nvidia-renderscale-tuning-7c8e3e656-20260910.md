# NVIDIA render-scale tuning: independent PR65 repeat, 10 September 2026

This is an independent repeat for PR65 on main-VR source 7c8e3e656.
It has a separate run ID and ledger column; earlier results are retained.

The NVIDIA assay completed both 33-transition passes in one Skyrim VR
process. All 66 terminal render checks passed; Task 2 counts are
66 PASS / 0 FAIL / 0 INCONCLUSIVE. Candidate evidence and retry reporting
are COMPLETE. Full-history health contains recovered failures, so the
improvement-or-neutral assessment is **DOES_NOT_MEET_STANDARD**.

Mean strict completion was 866.009 / 833.078 ms in passes 1/2,
12.872% / 14.040% slower than the pinned main-VR reference.
These are transition timings. Scene time differs, and no versioned
tolerance policy was supplied; the deltas do not establish a causal
performance regression or a steady-state GPU/FPS change.

## Identity and scope

Run `nvidia-20260910T124329625Z`; game PID `45232`.
Compiled source, renderer base and main-VR identity: `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`.
Build ID: `9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4`; clean Release with DevBench enabled.
The reference is the preceding measured main-VR source
`348803c1831c8cd71ceb75a58143ad0bcb00abb2`, run
`nvidia-2026-09-10T07-19-50-405Z`. The intervening PR66 review merge
was not an integration build. Git confirms the reference is the direct
parent of this compiled source; there is no reporting bridge backport.

The physical DLL is 28,063,744 bytes, SHA-256
`bcd04e8c31248d5cfb02148266fbc2ead73d06b36bafa59cf8f971d79df24afe`. Its adjacent manifest, runtime producer and
AIO receipt match. The archive hash/size and retained archive/staging DLL
hash/size also match. The AIO receipt itself lacks DLL size; that value
is verified against the adjacent manifest and retained package payload.
The selected profile has one enabled loose DLL provider; Overwrite and
unmanaged Data contain no other CommunityShaders DLL.
[Deployment and compile identity](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z/raw/offline/physical-aio-verification.json).

Both runs use the RTX 5070 Ti Laptop GPU, Dragonsreach, DLSS profile K,
explicit FSR3, 1512 x 1680 native output per eye, and foveation
0.3/0.3/0.7. Adapter, foveation, toolchain, dependencies and shader
compiler match. The position and weather match; game hour is
1.958 / 9.532, making scene fingerprints unequal.
Driver version, headset refresh/power state, and full modlist/cache
equivalence are not proven. No tolerance policy is inferred.

The protocol used one positioning COC, two runtime-only baselines,
66 public-API transitions with five-second server pacing, and the
ten-second inter-pass cooldown. No recovery apply was needed.
No build, deployment, restart, persistence or third pass was performed.

## Per-pass latency and failure counts

| Metric                                  |    Pass 1 |    Pass 2 |
| --------------------------------------- | --------: | --------: |
| Strict mean (ms)                        |   866.009 |   833.078 |
| Strict median (ms)                      |   778.498 |   827.143 |
| Strict p95 (ms)                         |  1454.714 |  1450.874 |
| Strict maximum (ms)                     |  2184.558 |  1918.325 |
| Strict total (ms)                       | 28578.281 | 27491.579 |
| Presentation mean (ms)                  |   731.887 |   698.998 |
| Cleanup-tail mean (ms)                  |   108.464 |   108.446 |
| Cleanup-tail maximum (ms)               |   317.603 |   320.628 |
| Device-loss failures                    |         0 |         0 |
| OOM failures                            |         0 |         0 |
| Producer terminal failures              |         0 |         0 |
| Fidelity mismatch observations          |         4 |         4 |
| Vendor-failure stretch eye observations |         2 |         2 |
| Vendor-native qualification failures    |         0 |         0 |
| Credible liveness timeouts              |         0 |         0 |

All native-vendor destinations satisfied their strict terminal proof.
No terminal timeout, device-loss, OOM or producer failure was reported.

Pass 1: 26 routes slower and 7 faster than the reference.
Largest increase: row 29, FSR3 Ultra Performance -> DLSS Ultra Performance,
+797.474 ms.

Pass 2: 28 routes slower and 5 faster than the reference.
Largest increase: row 29, FSR3 Ultra Performance -> DLSS Ultra Performance,
+588.266 ms.

Rows 26 (DLSS Hoshipa to FSR3 Hoshipa) and 28 (None to FSR3 Ultra
Performance) each recorded two fidelity mismatches and one vendor-failure
stretch eye observation per pass before recovering to terminal PASS.
The reference has the same affected routes and counts; no new or resolved
failure routes were observed. These counts are observations, not crashes.

The raw cumulative fidelity and presentation-fallback gates remain unmet.
The fixed stretch cutoff is DIAGNOSTIC_ONLY because settling imposes
stretch; the scaled-presentation gate after proven native output is a
CONTRACT_MISMATCH. Neither excluded gate is counted as a health penalty.

## Retry detail and reporting limits

All 66 candidate rows have complete schema-v1 retry diagnostics.
Owned stress retries total 10 / 10; detailed events, per-role viewport
waits and guard/promotion intervals are retained below and in the CSV.
Overlapping full-eye and center waits are not summed. Guard, readiness,
promotion and stereo qualification overlap; their elapsed intervals are
not isolated retry overhead. Missing/inapplicable intervals remain n.d.

The historical reference has no detailed retry telemetry in any of its
66 rows. Its retained COMPLETE reporting label predates this contract.
Candidate reporting is complete; detailed baseline/candidate retry
comparison remains unavailable. The generated comparison preserves the
historical label unchanged and must be read with this limitation.

GPU profiler captures have no fresh resolved samples in either candidate
pass. Their zero totals are unavailable GPU timings, not zero GPU cost.
CPU/GPU counters and profiler freshness remain in the detailed comparison.

## Same-process repeat by transition group

Times are milliseconds. Retry cells list status; count; reasons; observed
per-role waits; ready-to-candidate time, immediately after route identity.
Timing tuples are strict / presentation / cleanup / cleanup tail. Source
and destination identities keep None, TAA, DLAA and FSR Native AA distinct.
Provider crossings also appear in destination tables; do not sum views.

### DLSS and DLAA destinations

| Row | Route                                            | P1 retry diagnostics                                                                                             | P2 retry diagnostics                                                                                             | P1 render / Task 2 | P1 timing tuple                          | P2 render / Task 2 | P2 timing tuple                          | Recovered finding |
| --- | ------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | ------------------ | ---------------------------------------- | ------------------ | ---------------------------------------- | ----------------- |
| 3   | TAA -> DLAA                                      | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                                       | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                                       | PASS / PASS        | 249.126 / 249.126 / 249.126 / 0.000      | PASS / PASS        | 286.338 / 286.338 / 286.338 / 0.000      | none observed     |
| 4   | DLAA -> DLSS Hoshipa                             | complete; 0; none; no wait observed; ready-to-candidate 112.417                                                  | complete; 0; none; no wait observed; ready-to-candidate 110.401                                                  | PASS / PASS        | 1004.374 / 790.942 / 920.929 / 129.987   | PASS / PASS        | 1032.563 / 804.481 / 948.804 / 144.323   | none observed     |
| 5   | DLSS Hoshipa -> DLSS Ultra Quality               | complete; 0; none; no wait observed; ready-to-candidate 101.687                                                  | complete; 0; none; no wait observed; ready-to-candidate 103.774                                                  | PASS / PASS        | 1266.234 / 1014.085 / 1168.706 / 154.620 | PASS / PASS        | 1027.220 / 805.305 / 939.029 / 133.724   | none observed     |
| 6   | DLSS Ultra Quality -> DLSS Quality               | complete; 1; dlss_viewport_recycle; FullEye 50.717; SubmitStageFoveatedCenter 50.665; ready-to-candidate 223.701 | complete; 1; dlss_viewport_recycle; FullEye 55.738; SubmitStageFoveatedCenter 55.694; ready-to-candidate 224.258 | PASS / PASS        | 1175.702 / 947.755 / 1089.396 / 141.641  | PASS / PASS        | 1169.543 / 953.781 / 1087.266 / 133.485  | none observed     |
| 7   | DLSS Quality -> DLSS Balanced                    | complete; 1; dlss_viewport_recycle; FullEye 58.278; SubmitStageFoveatedCenter 58.222; ready-to-candidate 223.234 | complete; 1; dlss_viewport_recycle; FullEye 73.233; SubmitStageFoveatedCenter 72.546; ready-to-candidate 255.358 | PASS / PASS        | 1393.400 / 1160.289 / 1307.700 / 147.411 | PASS / PASS        | 1390.699 / 1093.288 / 1251.222 / 157.935 | none observed     |
| 8   | DLSS Balanced -> DLSS Performance                | complete; 1; dlss_viewport_recycle; FullEye 55.275; SubmitStageFoveatedCenter 55.216; ready-to-candidate 217.921 | complete; 1; dlss_viewport_recycle; FullEye 63.761; SubmitStageFoveatedCenter 63.684; ready-to-candidate 236.254 | PASS / PASS        | 1352.171 / 1122.704 / 1271.880 / 149.177 | PASS / PASS        | 1195.918 / 968.840 / 1111.087 / 142.246  | none observed     |
| 9   | DLSS Performance -> DLSS Ultra Performance       | complete; 1; dlss_viewport_recycle; FullEye 58.733; SubmitStageFoveatedCenter 58.699; ready-to-candidate 241.486 | complete; 1; dlss_viewport_recycle; FullEye 60.639; SubmitStageFoveatedCenter 60.601; ready-to-candidate 255.334 | PASS / PASS        | 1386.511 / 1153.443 / 1299.332 / 145.889 | PASS / PASS        | 1249.407 / 971.186 / 1146.171 / 174.985  | none observed     |
| 10  | DLSS Ultra Performance -> DLAA                   | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                                       | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                                       | PASS / PASS        | 770.294 / 592.366 / 770.294 / 177.929    | PASS / PASS        | 914.287 / 692.329 / 914.287 / 221.958    | none observed     |
| 23  | NONE -> DLAA                                     | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                                       | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                                       | PASS / PASS        | 261.855 / 261.855 / 261.855 / 0.000      | PASS / PASS        | 254.963 / 254.963 / 254.963 / 0.000      | none observed     |
| 25  | FSR3 Native AA -> DLSS Hoshipa                   | complete; 0; none; no wait observed; ready-to-candidate 106.259                                                  | complete; 0; none; no wait observed; ready-to-candidate 110.569                                                  | PASS / PASS        | 976.698 / 747.831 / 895.346 / 147.514    | PASS / PASS        | 1028.504 / 804.197 / 943.123 / 138.926   | none observed     |
| 29  | FSR3 Ultra Performance -> DLSS Ultra Performance | complete; 2; render_target_relatch_requeued; no wait observed; ready-to-candidate 359.008                        | complete; 2; render_target_relatch_requeued; no wait observed; ready-to-candidate 293.171                        | PASS / PASS        | 2184.558 / 1978.717 / 2084.388 / 105.670 | PASS / PASS        | 1918.325 / 1701.862 / 1838.119 / 136.257 | none observed     |
| 33  | NONE -> DLAA                                     | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                                       | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                                       | PASS / PASS        | 296.510 / 296.510 / 296.510 / 0.000      | PASS / PASS        | 273.671 / 273.671 / 273.671 / 0.000      | none observed     |

### FSR3 destinations

| Row | Route                                      | P1 retry diagnostics                                                                                 | P2 retry diagnostics                                                                                 | P1 render / Task 2 | P1 timing tuple                          | P2 render / Task 2 | P2 timing tuple                         | Recovered finding              |
| --- | ------------------------------------------ | ---------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- | ------------------ | ---------------------------------------- | ------------------ | --------------------------------------- | ------------------------------ |
| 13  | NONE -> FSR3 Native AA                     | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                           | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                           | PASS / PASS        | 822.942 / 822.942 / 822.942 / 0.000      | PASS / PASS        | 619.982 / 619.982 / 619.982 / 0.000     | none observed                  |
| 14  | FSR3 Native AA -> FSR3 Hoshipa             | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.               | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.               | PASS / PASS        | 1377.140 / 1240.733 / 1329.442 / 88.709  | PASS / PASS        | 1408.406 / 1273.888 / 1361.411 / 87.523 | none observed                  |
| 15  | FSR3 Hoshipa -> FSR3 Ultra Quality         | complete; 0; none; no wait observed; ready-to-candidate n.d.                                         | complete; 0; none; no wait observed; ready-to-candidate n.d.                                         | PASS / PASS        | 749.789 / 749.789 / 570.716 / 0.000      | PASS / PASS        | 945.154 / 945.154 / 634.137 / 0.000     | none observed                  |
| 16  | FSR3 Ultra Quality -> FSR3 Quality         | complete; 0; none; no wait observed; ready-to-candidate n.d.                                         | complete; 0; none; no wait observed; ready-to-candidate n.d.                                         | PASS / PASS        | 929.137 / 929.137 / 662.595 / 0.000      | PASS / PASS        | 789.230 / 789.230 / 608.752 / 0.000     | none observed                  |
| 17  | FSR3 Quality -> FSR3 Balanced              | complete; 0; none; no wait observed; ready-to-candidate n.d.                                         | complete; 0; none; no wait observed; ready-to-candidate n.d.                                         | PASS / PASS        | 762.921 / 762.921 / 616.503 / 0.000      | PASS / PASS        | 831.971 / 831.971 / 680.701 / 0.000     | none observed                  |
| 18  | FSR3 Balanced -> FSR3 Performance          | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.               | complete; 0; none; no wait observed; ready-to-candidate n.d.                                         | PASS / PASS        | 1400.519 / 1400.519 / 1120.851 / 0.000   | PASS / PASS        | 757.557 / 757.557 / 620.771 / 0.000     | none observed                  |
| 19  | FSR3 Performance -> FSR3 Ultra Performance | complete; 0; none; no wait observed; ready-to-candidate n.d.                                         | complete; 0; none; no wait observed; ready-to-candidate n.d.                                         | PASS / PASS        | 778.498 / 778.498 / 635.134 / 0.000      | PASS / PASS        | 758.886 / 758.886 / 624.111 / 0.000     | none observed                  |
| 20  | FSR3 Ultra Performance -> FSR3 Native AA   | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                           | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 731.796 / 546.689 / 731.796 / 185.107    | PASS / PASS        | 1053.683 / 807.760 / 1053.683 / 245.923 | none observed                  |
| 24  | DLAA -> FSR3 Native AA                     | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.; no candidate | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 1233.573 / 1042.196 / 1233.573 / 191.377 | PASS / PASS        | 1118.329 / 935.684 / 1118.329 / 182.645 | none observed                  |
| 26  | DLSS Hoshipa -> FSR3 Hoshipa               | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.               | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.               | PASS / PASS        | 1536.005 / 1384.499 / 1484.709 / 100.210 | PASS / PASS        | 1514.576 / 1514.576 / 1459.934 / 0.000  | both passes; also in reference |
| 28  | NONE -> FSR3 Ultra Performance             | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                           | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                           | PASS / PASS        | 924.794 / 748.294 / 879.078 / 130.784    | PASS / PASS        | 939.204 / 798.736 / 887.620 / 88.884    | both passes; also in reference |
| 31  | TAA -> FSR3 Native AA                      | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                           | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate                           | PASS / PASS        | 610.701 / 610.701 / 610.701 / 0.000      | PASS / PASS        | 628.123 / 628.123 / 628.123 / 0.000     | none observed                  |

### Provider crossings

| Row | Route                                            | P1 retry diagnostics                                                                                 | P2 retry diagnostics                                                                                 | P1 render / Task 2 | P1 timing tuple                          | P2 render / Task 2 | P2 timing tuple                          | Recovered finding              |
| --- | ------------------------------------------------ | ---------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- | ------------------ | ---------------------------------------- | ------------------ | ---------------------------------------- | ------------------------------ |
| 24  | DLAA -> FSR3 Native AA                           | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.; no candidate | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 1233.573 / 1042.196 / 1233.573 / 191.377 | PASS / PASS        | 1118.329 / 935.684 / 1118.329 / 182.645  | none observed                  |
| 25  | FSR3 Native AA -> DLSS Hoshipa                   | complete; 0; none; no wait observed; ready-to-candidate 106.259                                      | complete; 0; none; no wait observed; ready-to-candidate 110.569                                      | PASS / PASS        | 976.698 / 747.831 / 895.346 / 147.514    | PASS / PASS        | 1028.504 / 804.197 / 943.123 / 138.926   | none observed                  |
| 26  | DLSS Hoshipa -> FSR3 Hoshipa                     | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.               | complete; 1; render_target_relatch_requeued; no wait observed; ready-to-candidate n.d.               | PASS / PASS        | 1536.005 / 1384.499 / 1484.709 / 100.210 | PASS / PASS        | 1514.576 / 1514.576 / 1459.934 / 0.000   | both passes; also in reference |
| 29  | FSR3 Ultra Performance -> DLSS Ultra Performance | complete; 2; render_target_relatch_requeued; no wait observed; ready-to-candidate 359.008            | complete; 2; render_target_relatch_requeued; no wait observed; ready-to-candidate 293.171            | PASS / PASS        | 2184.558 / 1978.717 / 2084.388 / 105.670 | PASS / PASS        | 1918.325 / 1701.862 / 1838.119 / 136.257 | none observed                  |

### TAA and None boundary routes

| Row | Route                          | P1 retry diagnostics                                                       | P2 retry diagnostics                                                       | P1 render / Task 2 | P1 timing tuple                       | P2 render / Task 2 | P2 timing tuple                       | Recovered finding              |
| --- | ------------------------------ | -------------------------------------------------------------------------- | -------------------------------------------------------------------------- | ------------------ | ------------------------------------- | ------------------ | ------------------------------------- | ------------------------------ |
| 1   | DLSS Hoshipa -> NONE           | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 688.545 / 466.266 / 688.545 / 222.279 | PASS / PASS        | 716.918 / 485.143 / 716.918 / 231.775 | none observed                  |
| 2   | NONE -> TAA                    | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 169.425 / 169.425 / 169.425 / 0.000   | PASS / PASS        | 170.160 / 170.160 / 170.160 / 0.000   | none observed                  |
| 3   | TAA -> DLAA                    | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 249.126 / 249.126 / 249.126 / 0.000   | PASS / PASS        | 286.338 / 286.338 / 286.338 / 0.000   | none observed                  |
| 11  | DLAA -> TAA                    | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 450.117 / 171.418 / 450.117 / 278.699 | PASS / PASS        | 480.252 / 200.500 / 480.252 / 279.753 | none observed                  |
| 12  | TAA -> NONE                    | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 190.626 / 190.626 / 190.626 / 0.000   | PASS / PASS        | 172.881 / 172.881 / 172.881 / 0.000   | none observed                  |
| 13  | NONE -> FSR3 Native AA         | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 822.942 / 822.942 / 822.942 / 0.000   | PASS / PASS        | 619.982 / 619.982 / 619.982 / 0.000   | none observed                  |
| 21  | FSR3 Native AA -> TAA          | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 572.501 / 254.898 / 572.501 / 317.603 | PASS / PASS        | 495.001 / 222.836 / 495.001 / 272.166 | none observed                  |
| 22  | TAA -> NONE                    | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 172.732 / 172.732 / 172.732 / 0.000   | PASS / PASS        | 217.047 / 217.047 / 217.047 / 0.000   | none observed                  |
| 23  | NONE -> DLAA                   | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 261.855 / 261.855 / 261.855 / 0.000   | PASS / PASS        | 254.963 / 254.963 / 254.963 / 0.000   | none observed                  |
| 27  | FSR3 Hoshipa -> NONE           | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 933.032 / 636.450 / 933.032 / 296.582 | PASS / PASS        | 774.192 / 527.127 / 774.192 / 247.065 | none observed                  |
| 28  | NONE -> FSR3 Ultra Performance | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 924.794 / 748.294 / 879.078 / 130.784 | PASS / PASS        | 939.204 / 798.736 / 887.620 / 88.884  | both passes; also in reference |
| 30  | DLSS Ultra Performance -> TAA  | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 744.487 / 560.371 / 744.487 / 184.117 | PASS / PASS        | 827.143 / 588.638 / 827.143 / 238.505 | none observed                  |
| 31  | TAA -> FSR3 Native AA          | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 610.701 / 610.701 / 610.701 / 0.000   | PASS / PASS        | 628.123 / 628.123 / 628.123 / 0.000   | none observed                  |
| 32  | FSR3 Native AA -> NONE         | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 481.567 / 197.556 / 481.567 / 284.011 | PASS / PASS        | 531.443 / 210.815 / 531.443 / 320.628 | none observed                  |
| 33  | NONE -> DLAA                   | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | complete; 0; none; no wait observed; ready-to-candidate n.d.; no candidate | PASS / PASS        | 296.510 / 296.510 / 296.510 / 0.000   | PASS / PASS        | 273.671 / 273.671 / 273.671 / 0.000   | none observed                  |

## Candidate health gates, relatch/stretch and memory

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms   | Max ms   | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | -------- | -------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 866.009        | 1454.714 | 2184.558 | 15.576             | 807.337         | 13.920              | 19               | 89             | 5719.962   | 10      | 4        | 2                   | NOT_MET         | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 833.078        | 1450.874 | 1918.325 | 15.455             | 763.765         | 13.840              | 19               | 83             | 5383.107   | 10      | 4        | 2                   | NOT_MET         | COMPLETE |

### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                     | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                    | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":89,"allowedPresentationStretchEpisodes":19,"allowedPresentationStretchEyeObservations":192,"allowedPresentationStretchMaximumFrames":13,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                                                                                                                                                                                             | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":52644,"leftPath":"NativeOriginal","referenceFrame":53015,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                            | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations                                | Receipt                                             |
| --- | --------- | ----------------------------------------------------------- | --------------------------------------------------- |
| 26  | true      | fidelityMismatches=2; vendorFailureStretchEyeObservations=1 | raw/lane-nvidia/pass-1/transitions/26/retained.json |
| 28  | true      | fidelityMismatches=2; vendorFailureStretchEyeObservations=1 | raw/lane-nvidia/pass-1/transitions/28/retained.json |

### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":83,"allowedPresentationStretchEpisodes":19,"allowedPresentationStretchEyeObservations":181,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":57130,"leftPath":"NativeOriginal","referenceFrame":57515,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                           | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations                                | Receipt                                             |
| --- | --------- | ----------------------------------------------------------- | --------------------------------------------------- |
| 26  | true      | fidelityMismatches=2; vendorFailureStretchEyeObservations=1 | raw/lane-nvidia/pass-2/transitions/26/retained.json |
| 28  | true      | fidelityMismatches=2; vendorFailureStretchEyeObservations=1 | raw/lane-nvidia/pass-2/transitions/28/retained.json |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

## Memory confirmation

### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    16894.508 |  16681.246 |     -213.262 |      17022.098 |    17024.133 |          2.035 |    17097.223 |  16617.348 |     -479.875 |                   n.d. |
| System commit MiB                  |    55422.949 |  55060.379 |      -362.57 |      55331.238 |    55373.375 |         42.137 |    55505.602 |  54815.926 |     -689.676 |                   n.d. |
| DXGI process usage MiB             |     5583.117 |   3750.016 |    -1833.102 |       4007.578 |     4007.578 |              0 |     4066.754 |   3643.645 |     -423.109 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        261 |          261 |            261 |          261 |              0 |            0 |        211 |          211 |                  0.808 |
| Estimated live tracked texture MiB |            0 |   2429.896 |     2429.896 |       2429.896 |     2429.896 |              0 |            0 |   2274.931 |     2274.931 |                  0.936 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -213.26171875,
        "systemCommitMiB": -362.5703125,
        "dxgiUsageMiB": -1833.1015625,
        "liveTextures": 261,
        "liveTextureMiB": 2429.8961448669434
    },
    "pass2": {
        "processPrivateMiB": -479.875,
        "systemCommitMiB": -689.67578125,
        "dxgiUsageMiB": -423.109375,
        "liveTextures": 211,
        "liveTextureMiB": 2274.9311332702637
    },
    "positivePass1PrivateAndCommit": false,
    "pass2ResourceGrowth": true,
    "retentionPredicate": false,
    "initializationPredicate": false
}
```

Conclusion: classification_is_not_proof_for_or_against_a_leak. Growth is relative to the freshly reset tracker in each pass; session IDs are retained.

Evidence gaps: none.

Memory classification is inconclusive. Process-private and system-commit
growth are negative in both passes, so positive-pass-1 growth ratios are
unavailable. Fresh texture trackers and Normal pressure do not prove or
disprove a leak.

## Evidence and validation

-   [Canonical ledger](vr-render-scale-ledger.md): 528 numeric timing cells for this run plus 66 full transition timing objects, including retry intervals and explicit identities/units.
-   The maintained comparison wrapper verified 1,056 reference/candidate timing cells and preserved every historical cell. Full timing and retry objects were checked against the summary after publication.
-   [Summary](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z/summary.json), [transition CSV](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z/transitions.csv), [receipt index](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z/receipt-index.json), and [full scalar export](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z/evidence-values.csv): 444 journal records, 183 raw JSON files and 6,033,651 scalar/null/empty values; 24/24 required DLSS trace windows complete.
-   [Worker status](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z/worker-status.json): COMPLETE; captures verified inactive; evidencePending=0. Maximum client dispatch gap 41.038 ms, within the 250 ms diagnostic limit.
-   [Exact comparison command](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z/comparison-command.json), [ledger audit](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z-comparison/ledger-validation.json), [comparison JSON](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z-comparison/comparison.json), and [comparison CSV](../../artifacts/renderscale-tuning/nvidia-20260910T124329625Z-comparison/comparison.csv).
-   Finalization ran once in 15.071 s; comparison ran once in 2.654 s (generation 2366.995 ms, ledger audit/update 65.616 ms). Stage receipts remain local.
-   Preparation corrected the package layout from Core/SKSE to SKSE and raised the local CSV parser field limit to retain all derived timing intervals. Both were offline preparation issues; no measurements or comparisons were replayed.
-   Focused git diff --check is recorded with the completion result. No build or regression tests were needed for this evidence-only update.

The user requested this comparison as an independent repeat on PR65.
Raw evidence stays local; the PR receives the report and comparison.

## Detailed baseline comparison

Change assessment: **DOES_NOT_MEET_STANDARD**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                     | Candidate                                                                               |
| ----------------------- | -------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Run                     | nvidia-2026-09-10T07-19-50-405Z                                                              | nvidia-20260910T124329625Z                                                              |
| Renderer base           | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                                                     | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                |
| Main-VR base/equivalent | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                                                     | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                |
| Compiled source         | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                                                     | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                |
| Build ID                | 07238b1fe36024d2c33081f023973c34887ce8a0fa4bd4743bf8a1c85b5ec78d                             | 9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4                        |
| DLL SHA-256             | 2101a4f7faa417b415ebb28dfa7b93a4ef39abf32ab9879f549f25da09bbef38                             | bcd04e8c31248d5cfb02148266fbc2ead73d06b36bafa59cf8f971d79df24afe                        |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-2026-09-10T07-19-50-405Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-20260910T124329625Z |

Assessment limits: candidate_health_standard_not_met; retained_context_not_matched:scene; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 767.246/866.009 | 12.872       | 8/10        | 4/4          | 2/2                 | none             | NOT_MET/NOT_MET     |
| nvidia | 2    | 33/33    | 730.514/833.078 | 14.040       | 10/10       | 4/4          | 2/2                 | none             | NOT_MET/NOT_MET     |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 708.580   | 807.337   | 98.756   | 13.937  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.160    | 13.920    | 0.760    | 5.775   |
| nvidia | 1    | Relatch proof total        | ms          | 17714.506 | 20183.415 | 2468.909 | 13.937  |
| nvidia | 1    | Relatch proof total        | frames      | 329       | 348       | 19       | 5.775   |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 767.246   | 866.009   | 98.763   | 12.872  |
| nvidia | 1    | Strict completion mean     | frames      | 14.970    | 15.576    | 0.606    | 4.049   |
| nvidia | 1    | Strict completion total    | ms          | 25319.115 | 28578.281 | 3259.166 | 12.872  |
| nvidia | 1    | Strict completion total    | frames      | 494       | 514       | 20       | 4.049   |
| nvidia | 1    | Stretch completed episodes | episodes    | 18        | 19        | 1        | 5.556   |
| nvidia | 1    | Stretch completed total    | frames      | 74        | 89        | 15       | 20.270  |
| nvidia | 1    | Stretch completed total    | ms          | 4578.021  | 5719.962  | 1141.941 | 24.944  |
| nvidia | 1    | Stretch longest episode    | ms          | 426.722   | 646.277   | 219.555  | 51.452  |
| nvidia | 2    | Relatch proof mean         | ms          | 682.641   | 763.765   | 81.124   | 11.884  |
| nvidia | 2    | Relatch proof mean         | frames      | 14.080    | 13.840    | -0.240   | -1.705  |
| nvidia | 2    | Relatch proof total        | ms          | 17066.032 | 19094.136 | 2028.103 | 11.884  |
| nvidia | 2    | Relatch proof total        | frames      | 352       | 346       | -6       | -1.705  |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 730.514   | 833.078   | 102.565  | 14.040  |
| nvidia | 2    | Strict completion mean     | frames      | 15.394    | 15.455    | 0.061    | 0.394   |
| nvidia | 2    | Strict completion total    | ms          | 24106.946 | 27491.579 | 3384.633 | 14.040  |
| nvidia | 2    | Strict completion total    | frames      | 508       | 510       | 2        | 0.394   |
| nvidia | 2    | Stretch completed episodes | episodes    | 17        | 19        | 2        | 11.765  |
| nvidia | 2    | Stretch completed total    | frames      | 77        | 83        | 6        | 7.792   |
| nvidia | 2    | Stretch completed total    | ms          | 4310.798  | 5383.107  | 1072.309 | 24.875  |
| nvidia | 2    | Stretch longest episode    | ms          | 417.844   | 441.441   | 23.598   | 5.647   |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 669.500 / 688.545   | 19.045   | 2.845   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 181.499 / 169.425   | -12.074  | -6.652  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 289.827 / 249.126   | -40.701  | -14.043 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 920.565 / 1004.374  | 83.809   | 9.104   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 981.793 / 1266.234  | 284.441  | 28.972  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1099.691 / 1175.702 | 76.010   | 6.912   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1392.319 / 1393.400 | 1.082    | 0.078   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1343.752 / 1352.171 | 8.418    | 0.626   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1221.973 / 1386.511 | 164.538  | 13.465  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 740.453 / 770.294   | 29.841   | 4.030   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 486.216 / 450.117   | -36.100  | -7.425  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 155.001 / 190.626   | 35.625   | 22.984  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 937.889 / 822.942   | -114.947 | -12.256 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1339.594 / 1377.140 | 37.546   | 2.803   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 802.999 / 749.789   | -53.210  | -6.626  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 803.849 / 929.137   | 125.288  | 15.586  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 732.811 / 762.921   | 30.110   | 4.109   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 840.052 / 1400.519  | 560.467  | 66.718  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 708.567 / 778.498   | 69.930   | 9.869   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 655.757 / 731.796   | 76.039   | 11.596  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 475.970 / 572.501   | 96.531   | 20.281  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 174.895 / 172.732   | -2.163   | -1.237  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 270.911 / 261.855   | -9.056   | -3.343  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1024.970 / 1233.573 | 208.603  | 20.352  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 934.134 / 976.698   | 42.563   | 4.556   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1378.302 / 1536.005 | 157.703  | 11.442  | 1/1         | 2/1 -> 2/1 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 665.093 / 933.032   | 267.939  | 40.286  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 841.685 / 924.794   | 83.110   | 9.874   | 0/0         | 2/1 -> 2/1 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1387.084 / 2184.558 | 797.474  | 57.493  | 1/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 644.207 / 744.487   | 100.280  | 15.566  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 562.726 / 610.701   | 47.975   | 8.525   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 417.950 / 481.567   | 63.618   | 15.221  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 237.079 / 296.510   | 59.430   | 25.068  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 501.294 / 466.266   | 669.500 / 688.545   | 168.206 / 222.279 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.6851,"dispatchToBlockedOrPreparationMs":327.2972,"firstNewGenerationToCleanupDrainedMs":228.3937,"firstPhysicalMutationToFirstNewGenerationMs":111.124,"presentationToStrictCompletionMs":168.2062}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1234,"dispatchToBlockedOrPreparationMs":350.2632,"firstNewGenerationToCleanupDrainedMs":222.7267,"firstPhysicalMutationToFirstNewGenerationMs":112.4315,"presentationToStrictCompletionMs":222.2791}   |
| 2   | 181.499 / 169.425   | 181.499 / 169.425   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":181.4987,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.4249,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 289.827 / 249.126   | 289.827 / 249.126   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":289.8271,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.1258,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 695.179 / 790.942   | 836.902 / 920.929   | 141.723 / 129.987 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.351,"dispatchToBlockedOrPreparationMs":338.413,"firstNewGenerationToCleanupDrainedMs":184.9359,"firstPhysicalMutationToFirstNewGenerationMs":310.2019,"presentationToStrictCompletionMs":225.3857}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2853,"dispatchToBlockedOrPreparationMs":392.9859,"firstNewGenerationToCleanupDrainedMs":172.3976,"firstPhysicalMutationToFirstNewGenerationMs":351.2604,"presentationToStrictCompletionMs":213.4314}   |
| 5   | 768.274 / 1014.085  | 896.303 / 1168.706  | 128.030 / 154.620 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3558,"dispatchToBlockedOrPreparationMs":397.8904,"firstNewGenerationToCleanupDrainedMs":172.3257,"firstPhysicalMutationToFirstNewGenerationMs":321.7316,"presentationToStrictCompletionMs":213.5189}  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2487,"dispatchToBlockedOrPreparationMs":400.2561,"firstNewGenerationToCleanupDrainedMs":207.9868,"firstPhysicalMutationToFirstNewGenerationMs":556.2139,"presentationToStrictCompletionMs":252.149}    |
| 6   | 898.584 / 947.755   | 1017.670 / 1089.396 | 119.086 / 141.641 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0717,"dispatchToBlockedOrPreparationMs":369.5131,"firstNewGenerationToCleanupDrainedMs":158.8355,"firstPhysicalMutationToFirstNewGenerationMs":486.2494,"presentationToStrictCompletionMs":201.1075}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9174,"dispatchToBlockedOrPreparationMs":408.0937,"firstNewGenerationToCleanupDrainedMs":184.7971,"firstPhysicalMutationToFirstNewGenerationMs":492.5879,"presentationToStrictCompletionMs":227.9465}   |
| 7   | 1184.415 / 1160.289 | 1309.015 / 1307.700 | 124.600 / 147.411 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1909,"dispatchToBlockedOrPreparationMs":395.1179,"firstNewGenerationToCleanupDrainedMs":167.299,"firstPhysicalMutationToFirstNewGenerationMs":742.407,"presentationToStrictCompletionMs":207.9042}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6713,"dispatchToBlockedOrPreparationMs":416.7351,"firstNewGenerationToCleanupDrainedMs":193.9378,"firstPhysicalMutationToFirstNewGenerationMs":692.3557,"presentationToStrictCompletionMs":233.111}    |
| 8   | 1159.017 / 1122.704 | 1256.318 / 1271.880 | 97.301 / 149.177  | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8169,"dispatchToBlockedOrPreparationMs":323.115,"firstNewGenerationToCleanupDrainedMs":210.1568,"firstPhysicalMutationToFirstNewGenerationMs":720.2293,"presentationToStrictCompletionMs":184.7348}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0394,"dispatchToBlockedOrPreparationMs":418.9637,"firstNewGenerationToCleanupDrainedMs":192.5665,"firstPhysicalMutationToFirstNewGenerationMs":656.3107,"presentationToStrictCompletionMs":229.4671}   |
| 9   | 1000.332 / 1153.443 | 1135.000 / 1299.332 | 134.668 / 145.889 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2676,"dispatchToBlockedOrPreparationMs":324.6854,"firstNewGenerationToCleanupDrainedMs":180.084,"firstPhysicalMutationToFirstNewGenerationMs":626.9629,"presentationToStrictCompletionMs":221.6411}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2105,"dispatchToBlockedOrPreparationMs":388.2587,"firstNewGenerationToCleanupDrainedMs":204.3861,"firstPhysicalMutationToFirstNewGenerationMs":701.4765,"presentationToStrictCompletionMs":233.0682}   |
| 10  | 566.234 / 592.366   | 740.453 / 770.294   | 174.220 / 177.929 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8785,"dispatchToBlockedOrPreparationMs":334.9253,"firstNewGenerationToCleanupDrainedMs":218.8826,"firstPhysicalMutationToFirstNewGenerationMs":183.7671,"presentationToStrictCompletionMs":174.2198}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9604,"dispatchToBlockedOrPreparationMs":355.7102,"firstNewGenerationToCleanupDrainedMs":229.2753,"firstPhysicalMutationToFirstNewGenerationMs":181.3485,"presentationToStrictCompletionMs":177.9286}   |
| 11  | 195.936 / 171.418   | 486.216 / 450.117   | 290.280 / 278.699 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1218,"dispatchToBlockedOrPreparationMs":142.8027,"firstNewGenerationToCleanupDrainedMs":291.184,"firstPhysicalMutationToFirstNewGenerationMs":48.108,"presentationToStrictCompletionMs":290.2801}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8788,"dispatchToBlockedOrPreparationMs":128.0403,"firstNewGenerationToCleanupDrainedMs":280.2042,"firstPhysicalMutationToFirstNewGenerationMs":37.9934,"presentationToStrictCompletionMs":278.6991}    |
| 12  | 155.001 / 190.626   | 155.001 / 190.626   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":155.0011,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":190.626,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 937.889 / 822.942   | 937.889 / 822.942   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":218.4206,"dispatchToBlockedOrPreparationMs":454.3682,"firstNewGenerationToCleanupDrainedMs":43.5433,"firstPhysicalMutationToFirstNewGenerationMs":221.5573,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":203.2194,"dispatchToBlockedOrPreparationMs":438.0838,"firstNewGenerationToCleanupDrainedMs":47.8224,"firstPhysicalMutationToFirstNewGenerationMs":133.8166,"presentationToStrictCompletionMs":0}         |
| 14  | 1339.594 / 1240.733 | 1292.678 / 1329.442 | 0 / 88.709        | {"blockedOrPreparationToFirstPhysicalMutationMs":269.7296,"dispatchToBlockedOrPreparationMs":416.8972,"firstNewGenerationToCleanupDrainedMs":173.5496,"firstPhysicalMutationToFirstNewGenerationMs":432.5019,"presentationToStrictCompletionMs":0}       | {"blockedOrPreparationToFirstPhysicalMutationMs":272.3075,"dispatchToBlockedOrPreparationMs":430.0498,"firstNewGenerationToCleanupDrainedMs":174.1568,"firstPhysicalMutationToFirstNewGenerationMs":452.9278,"presentationToStrictCompletionMs":136.4068} |
| 15  | 802.999 / 749.789   | 622.719 / 570.716   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7832,"dispatchToBlockedOrPreparationMs":379.7732,"firstNewGenerationToCleanupDrainedMs":42.6334,"firstPhysicalMutationToFirstNewGenerationMs":196.5295,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1103,"dispatchToBlockedOrPreparationMs":364.6701,"firstNewGenerationToCleanupDrainedMs":0.2125,"firstPhysicalMutationToFirstNewGenerationMs":201.7232,"presentationToStrictCompletionMs":0}            |
| 16  | 803.849 / 929.137   | 615.393 / 662.595   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7894,"dispatchToBlockedOrPreparationMs":359.0321,"firstNewGenerationToCleanupDrainedMs":44.8407,"firstPhysicalMutationToFirstNewGenerationMs":206.7308,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2371,"dispatchToBlockedOrPreparationMs":385.7405,"firstNewGenerationToCleanupDrainedMs":50.9281,"firstPhysicalMutationToFirstNewGenerationMs":220.6896,"presentationToStrictCompletionMs":0}           |
| 17  | 732.811 / 762.921   | 607.464 / 616.503   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3609,"dispatchToBlockedOrPreparationMs":375.1355,"firstNewGenerationToCleanupDrainedMs":41.1615,"firstPhysicalMutationToFirstNewGenerationMs":187.8058,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8981,"dispatchToBlockedOrPreparationMs":362.8132,"firstNewGenerationToCleanupDrainedMs":45.3978,"firstPhysicalMutationToFirstNewGenerationMs":203.3939,"presentationToStrictCompletionMs":0}           |
| 18  | 840.052 / 1400.519  | 676.686 / 1120.851  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":7.0764,"dispatchToBlockedOrPreparationMs":373.4732,"firstNewGenerationToCleanupDrainedMs":60.3653,"firstPhysicalMutationToFirstNewGenerationMs":235.7712,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":269.9156,"dispatchToBlockedOrPreparationMs":430.8939,"firstNewGenerationToCleanupDrainedMs":44.2763,"firstPhysicalMutationToFirstNewGenerationMs":375.7652,"presentationToStrictCompletionMs":0}         |
| 19  | 708.567 / 778.498   | 585.888 / 635.134   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0818,"dispatchToBlockedOrPreparationMs":355.2031,"firstNewGenerationToCleanupDrainedMs":39.1574,"firstPhysicalMutationToFirstNewGenerationMs":187.446,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2943,"dispatchToBlockedOrPreparationMs":377.4468,"firstNewGenerationToCleanupDrainedMs":50.6356,"firstPhysicalMutationToFirstNewGenerationMs":202.7568,"presentationToStrictCompletionMs":0}           |
| 20  | 493.734 / 546.689   | 655.757 / 731.796   | 162.023 / 185.107 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.628,"dispatchToBlockedOrPreparationMs":324.3054,"firstNewGenerationToCleanupDrainedMs":209.0784,"firstPhysicalMutationToFirstNewGenerationMs":117.7452,"presentationToStrictCompletionMs":162.0231}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0066,"dispatchToBlockedOrPreparationMs":356.7277,"firstNewGenerationToCleanupDrainedMs":246.7222,"firstPhysicalMutationToFirstNewGenerationMs":123.3394,"presentationToStrictCompletionMs":185.1067}   |
| 21  | 194.486 / 254.898   | 475.970 / 572.501   | 281.484 / 317.603 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.1194,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":281.4839}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7708,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":317.6031}             |
| 22  | 174.895 / 172.732   | 174.895 / 172.732   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":174.8952,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7317,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 270.911 / 261.855   | 270.911 / 261.855   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":270.9109,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.8554,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 866.555 / 1042.196  | 1024.970 / 1233.573 | 158.415 / 191.377 | {"blockedOrPreparationToFirstPhysicalMutationMs":256.0541,"dispatchToBlockedOrPreparationMs":436.8501,"firstNewGenerationToCleanupDrainedMs":197.0475,"firstPhysicalMutationToFirstNewGenerationMs":135.0184,"presentationToStrictCompletionMs":158.415} | {"blockedOrPreparationToFirstPhysicalMutationMs":313.41,"dispatchToBlockedOrPreparationMs":517.0402,"firstNewGenerationToCleanupDrainedMs":241.0097,"firstPhysicalMutationToFirstNewGenerationMs":162.1131,"presentationToStrictCompletionMs":191.3771}   |
| 25  | 739.348 / 747.831   | 856.854 / 895.346   | 117.506 / 147.514 | {"blockedOrPreparationToFirstPhysicalMutationMs":30.9018,"dispatchToBlockedOrPreparationMs":355.0593,"firstNewGenerationToCleanupDrainedMs":156.1911,"firstPhysicalMutationToFirstNewGenerationMs":314.7019,"presentationToStrictCompletionMs":194.7865} | {"blockedOrPreparationToFirstPhysicalMutationMs":28.8488,"dispatchToBlockedOrPreparationMs":356.1955,"firstNewGenerationToCleanupDrainedMs":191.0269,"firstPhysicalMutationToFirstNewGenerationMs":319.2743,"presentationToStrictCompletionMs":228.8665}  |
| 26  | 1378.302 / 1384.499 | 1326.476 / 1484.709 | 0 / 100.210       | {"blockedOrPreparationToFirstPhysicalMutationMs":257.0673,"dispatchToBlockedOrPreparationMs":387.3346,"firstNewGenerationToCleanupDrainedMs":189.5145,"firstPhysicalMutationToFirstNewGenerationMs":492.5595,"presentationToStrictCompletionMs":0}       | {"blockedOrPreparationToFirstPhysicalMutationMs":278.0765,"dispatchToBlockedOrPreparationMs":483.9876,"firstNewGenerationToCleanupDrainedMs":196.3728,"firstPhysicalMutationToFirstNewGenerationMs":526.2725,"presentationToStrictCompletionMs":151.5059} |
| 27  | 446.145 / 636.450   | 665.093 / 933.032   | 218.948 / 296.582 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3501,"dispatchToBlockedOrPreparationMs":313.9309,"firstNewGenerationToCleanupDrainedMs":219.6107,"firstPhysicalMutationToFirstNewGenerationMs":128.2013,"presentationToStrictCompletionMs":218.9481}  | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1024,"dispatchToBlockedOrPreparationMs":436.9625,"firstNewGenerationToCleanupDrainedMs":297.3029,"firstPhysicalMutationToFirstNewGenerationMs":193.6644,"presentationToStrictCompletionMs":296.5822}   |
| 28  | 759.193 / 748.294   | 799.619 / 879.078   | 40.425 / 130.784  | {"blockedOrPreparationToFirstPhysicalMutationMs":2.5958,"dispatchToBlockedOrPreparationMs":373.1481,"firstNewGenerationToCleanupDrainedMs":170.9405,"firstPhysicalMutationToFirstNewGenerationMs":252.9344,"presentationToStrictCompletionMs":82.4913}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6136,"dispatchToBlockedOrPreparationMs":416.804,"firstNewGenerationToCleanupDrainedMs":174.1042,"firstPhysicalMutationToFirstNewGenerationMs":284.556,"presentationToStrictCompletionMs":176.5007}     |
| 29  | 1190.855 / 1978.717 | 1310.255 / 2084.388 | 119.400 / 105.670 | {"blockedOrPreparationToFirstPhysicalMutationMs":21.0974,"dispatchToBlockedOrPreparationMs":631.0741,"firstNewGenerationToCleanupDrainedMs":160.4408,"firstPhysicalMutationToFirstNewGenerationMs":497.6428,"presentationToStrictCompletionMs":196.2287} | {"blockedOrPreparationToFirstPhysicalMutationMs":773.8797,"dispatchToBlockedOrPreparationMs":472.5051,"firstNewGenerationToCleanupDrainedMs":205.3727,"firstPhysicalMutationToFirstNewGenerationMs":632.6303,"presentationToStrictCompletionMs":205.8408} |
| 30  | 428.091 / 560.371   | 644.207 / 744.487   | 216.117 / 184.117 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9414,"dispatchToBlockedOrPreparationMs":316.259,"firstNewGenerationToCleanupDrainedMs":216.9443,"firstPhysicalMutationToFirstNewGenerationMs":108.0628,"presentationToStrictCompletionMs":216.1168}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6993,"dispatchToBlockedOrPreparationMs":388.6597,"firstNewGenerationToCleanupDrainedMs":238.0266,"firstPhysicalMutationToFirstNewGenerationMs":114.1016,"presentationToStrictCompletionMs":184.1165}   |
| 31  | 562.726 / 610.701   | 562.726 / 610.701   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":43.7834,"dispatchToBlockedOrPreparationMs":395.4822,"firstNewGenerationToCleanupDrainedMs":40.431,"firstPhysicalMutationToFirstNewGenerationMs":83.0294,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1454,"dispatchToBlockedOrPreparationMs":419.9648,"firstNewGenerationToCleanupDrainedMs":47.1311,"firstPhysicalMutationToFirstNewGenerationMs":92.4599,"presentationToStrictCompletionMs":0}           |
| 32  | 174.944 / 197.556   | 417.950 / 481.567   | 243.005 / 284.011 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":117.7198,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":243.0051}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.0718,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":284.0112}             |
| 33  | 237.079 / 296.510   | 237.079 / 296.510   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":237.079,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.5095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 9                    | 0            | 441.106 / 465.818    | 24.712   | 15 / 14           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 5 / 4             | -1           |
| 4   | 12 / 12                  | 0            | 651.966 / 748.532    | 96.566   | 17 / 17           | 0            |
| 5   | 14 / 14                  | 0            | 723.978 / 960.719    | 236.741  | 19 / 19           | 0            |
| 6   | 18 / 17                  | -1           | 858.834 / 904.599    | 45.765   | 23 / 22           | -1           |
| 7   | 17 / 16                  | -1           | 1141.716 / 1113.762  | -27.954  | 22 / 21           | -1           |
| 8   | 16 / 17                  | 1            | 1046.161 / 1079.314  | 33.153   | 21 / 22           | 1            |
| 9   | 16 / 16                  | 0            | 954.916 / 1094.946   | 140.030  | 21 / 21           | 0            |
| 10  | 9 / 9                    | 0            | 521.571 / 541.019    | 19.448   | 14 / 15           | 1            |
| 11  | 3 / 3                    | 0            | 195.032 / 169.912    | -25.120  | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 9                   | -1           | 894.346 / 775.120    | -119.226 | 11 / 10           | -1           |
| 14  | 23 / 23                  | 0            | 1119.129 / 1155.285  | 36.156   | 28 / 28           | 0            |
| 15  | 11 / 12                  | 1            | 580.086 / 570.504    | -9.582   | 16 / 16           | 0            |
| 16  | 12 / 12                  | 0            | 570.552 / 611.667    | 41.115   | 17 / 18           | 1            |
| 17  | 12 / 12                  | 0            | 566.302 / 571.105    | 4.803    | 16 / 16           | 0            |
| 18  | 12 / 22                  | 10           | 616.321 / 1076.575   | 460.254  | 16 / 29           | 13           |
| 19  | 12 / 12                  | 0            | 546.731 / 584.498    | 37.767   | 16 / 16           | 0            |
| 20  | 10 / 10                  | 0            | 446.679 / 485.074    | 38.395   | 15 / 16           | 1            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 24  | 15 / 16                  | 1            | 827.923 / 992.563    | 164.641  | 20 / 21           | 1            |
| 25  | 13 / 13                  | 0            | 700.663 / 704.319    | 3.656    | 18 / 18           | 0            |
| 26  | 23 / 24                  | 1            | 1136.961 / 1288.337  | 151.375  | 28 / 29           | 1            |
| 27  | 10 / 10                  | 0            | 445.482 / 635.729    | 190.247  | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 628.678 / 704.974    | 76.295   | 16 / 16           | 0            |
| 29  | 23 / 29                  | 6            | 1149.814 / 1879.015  | 729.201  | 28 / 34           | 6            |
| 30  | 9 / 11                   | 2            | 427.263 / 506.461    | 79.197   | 15 / 16           | 1            |
| 31  | 9 / 9                    | 0            | 522.295 / 563.570    | 41.275   | 10 / 11           | 1            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 114.697 / 116.243  | 1.545    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 201.515 / 226.448  | 24.932   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 200.370 / 225.499  | 25.128   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 380.828 / 378.865  | -1.962   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 393.856 / 379.410  | -14.446  |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 426.722 / 375.234  | -51.488  |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 371.096 / 422.382  | 51.287   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 278.286 / 293.625  | 15.339   |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 141.276 / 143.915  | 2.640    |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 149.235 / 156.516  | 7.281    |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 138.085 / 152.521  | 14.436   |
| 18  | 1 / 1                | 0     | 3 / 13             | 10    | 176.326 / 646.277  | 469.951  |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 137.756 / 151.172  | 13.416   |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 199.863 / 207.881  | 8.017    |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 588.339 / 629.135  | 40.796   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 2 / 3                | 1     | 11 / 16            | 5     | 679.770 / 1214.838 | 535.068  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 734.886 / 716.918   | -17.968  | -2.445  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 177.782 / 170.160   | -7.622   | -4.287  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 260.525 / 286.338   | 25.812   | 9.908   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 906.681 / 1032.563  | 125.883  | 13.884  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 904.298 / 1027.220  | 122.923  | 13.593  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1039.935 / 1169.543 | 129.608  | 12.463  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1064.043 / 1390.699 | 326.656  | 30.700  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1205.586 / 1195.918 | -9.667   | -0.802  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1112.825 / 1249.407 | 136.582  | 12.273  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 729.996 / 914.287   | 184.291  | 25.245  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 431.684 / 480.252   | 48.569   | 11.251  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 154.506 / 172.881   | 18.375   | 11.893  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 567.794 / 619.982   | 52.188   | 9.191   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1214.111 / 1408.406 | 194.295  | 16.003  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 722.015 / 945.154   | 223.139  | 30.905  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 623.909 / 789.230   | 165.321  | 26.498  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 699.393 / 831.971   | 132.579  | 18.956  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 670.129 / 757.557   | 87.428   | 13.046  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 698.477 / 758.886   | 60.409   | 8.649   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 643.825 / 1053.683  | 409.857  | 63.660  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 400.298 / 495.001   | 94.703   | 23.658  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 154.918 / 217.047   | 62.129   | 40.105  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 230.219 / 254.963   | 24.744   | 10.748  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1039.184 / 1118.329 | 79.145   | 7.616   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1658.352 / 1028.504 | -629.848 | -37.980 | 2/0         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1227.931 / 1514.576 | 286.645  | 23.344  | 1/1         | 2/1 -> 2/1 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 696.952 / 774.192   | 77.240   | 11.083  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 842.921 / 939.204   | 96.283   | 11.423  | 0/0         | 2/1 -> 2/1 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1330.059 / 1918.325 | 588.266  | 44.229  | 1/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 618.367 / 827.143   | 208.776  | 33.763  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 697.904 / 628.123   | -69.781  | -9.999  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 412.420 / 531.443   | 119.023  | 28.860  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 235.021 / 273.671   | 38.650   | 16.445  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 502.328 / 485.143   | 734.886 / 716.918   | 232.558 / 231.775 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1491,"dispatchToBlockedOrPreparationMs":385.6548,"firstNewGenerationToCleanupDrainedMs":233.271,"firstPhysicalMutationToFirstNewGenerationMs":112.8113,"presentationToStrictCompletionMs":232.5584}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2651,"dispatchToBlockedOrPreparationMs":366.8156,"firstNewGenerationToCleanupDrainedMs":231.9285,"firstPhysicalMutationToFirstNewGenerationMs":114.9089,"presentationToStrictCompletionMs":231.775}    |
| 2   | 177.782 / 170.160   | 177.782 / 170.160   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.7818,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.1598,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 260.525 / 286.338   | 260.525 / 286.338   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":260.5254,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.3376,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 716.662 / 804.481   | 829.553 / 948.804   | 112.891 / 144.323 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8646,"dispatchToBlockedOrPreparationMs":380.6282,"firstNewGenerationToCleanupDrainedMs":151.6239,"firstPhysicalMutationToFirstNewGenerationMs":294.4361,"presentationToStrictCompletionMs":190.0182}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1201,"dispatchToBlockedOrPreparationMs":428.4029,"firstNewGenerationToCleanupDrainedMs":187.6301,"firstPhysicalMutationToFirstNewGenerationMs":328.6511,"presentationToStrictCompletionMs":228.0825}   |
| 5   | 700.205 / 805.305   | 819.578 / 939.029   | 119.374 / 133.724 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3144,"dispatchToBlockedOrPreparationMs":356.8476,"firstNewGenerationToCleanupDrainedMs":157.9589,"firstPhysicalMutationToFirstNewGenerationMs":301.4574,"presentationToStrictCompletionMs":204.093}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4788,"dispatchToBlockedOrPreparationMs":433.4416,"firstNewGenerationToCleanupDrainedMs":177.506,"firstPhysicalMutationToFirstNewGenerationMs":324.6027,"presentationToStrictCompletionMs":221.9152}    |
| 6   | 813.263 / 953.781   | 953.048 / 1087.266  | 139.784 / 133.485 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8019,"dispatchToBlockedOrPreparationMs":323.5516,"firstNewGenerationToCleanupDrainedMs":179.079,"firstPhysicalMutationToFirstNewGenerationMs":447.6151,"presentationToStrictCompletionMs":226.6718}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9058,"dispatchToBlockedOrPreparationMs":399.6181,"firstNewGenerationToCleanupDrainedMs":177.5245,"firstPhysicalMutationToFirstNewGenerationMs":506.2179,"presentationToStrictCompletionMs":215.7622}   |
| 7   | 859.186 / 1093.288  | 980.442 / 1251.222  | 121.255 / 157.935 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6249,"dispatchToBlockedOrPreparationMs":346.5762,"firstNewGenerationToCleanupDrainedMs":161.8545,"firstPhysicalMutationToFirstNewGenerationMs":468.3859,"presentationToStrictCompletionMs":204.8563}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2632,"dispatchToBlockedOrPreparationMs":463.7802,"firstNewGenerationToCleanupDrainedMs":206.6991,"firstPhysicalMutationToFirstNewGenerationMs":576.4798,"presentationToStrictCompletionMs":297.411}    |
| 8   | 985.673 / 968.840   | 1120.593 / 1111.087 | 134.920 / 142.246 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6332,"dispatchToBlockedOrPreparationMs":408.503,"firstNewGenerationToCleanupDrainedMs":178.6233,"firstPhysicalMutationToFirstNewGenerationMs":529.8337,"presentationToStrictCompletionMs":219.9124}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0262,"dispatchToBlockedOrPreparationMs":401.594,"firstNewGenerationToCleanupDrainedMs":187.1834,"firstPhysicalMutationToFirstNewGenerationMs":518.283,"presentationToStrictCompletionMs":227.0782}     |
| 9   | 911.436 / 971.186   | 1028.647 / 1146.171 | 117.211 / 174.985 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.061,"dispatchToBlockedOrPreparationMs":366.7606,"firstNewGenerationToCleanupDrainedMs":155.644,"firstPhysicalMutationToFirstNewGenerationMs":503.1813,"presentationToStrictCompletionMs":201.3885}     | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2317,"dispatchToBlockedOrPreparationMs":361.625,"firstNewGenerationToCleanupDrainedMs":228.7108,"firstPhysicalMutationToFirstNewGenerationMs":551.6035,"presentationToStrictCompletionMs":278.2205}    |
| 10  | 576.899 / 692.329   | 729.996 / 914.287   | 153.098 / 221.958 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.7523,"dispatchToBlockedOrPreparationMs":344.4053,"firstNewGenerationToCleanupDrainedMs":198.0434,"firstPhysicalMutationToFirstNewGenerationMs":184.7953,"presentationToStrictCompletionMs":153.0977}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6875,"dispatchToBlockedOrPreparationMs":406.104,"firstNewGenerationToCleanupDrainedMs":285.7356,"firstPhysicalMutationToFirstNewGenerationMs":217.76,"presentationToStrictCompletionMs":221.9585}      |
| 11  | 180.330 / 200.500   | 431.684 / 480.252   | 251.354 / 279.753 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1532,"dispatchToBlockedOrPreparationMs":135.7273,"firstNewGenerationToCleanupDrainedMs":252.1991,"firstPhysicalMutationToFirstNewGenerationMs":40.6043,"presentationToStrictCompletionMs":251.3542}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6232,"dispatchToBlockedOrPreparationMs":153.3929,"firstNewGenerationToCleanupDrainedMs":281.5936,"firstPhysicalMutationToFirstNewGenerationMs":40.6427,"presentationToStrictCompletionMs":279.7528}    |
| 12  | 154.506 / 172.881   | 154.506 / 172.881   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.5059,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8809,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 567.794 / 619.982   | 567.794 / 619.982   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.0735,"dispatchToBlockedOrPreparationMs":380.9779,"firstNewGenerationToCleanupDrainedMs":43.6037,"firstPhysicalMutationToFirstNewGenerationMs":92.1392,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4789,"dispatchToBlockedOrPreparationMs":434.7645,"firstNewGenerationToCleanupDrainedMs":42.5815,"firstPhysicalMutationToFirstNewGenerationMs":91.1569,"presentationToStrictCompletionMs":0}           |
| 14  | 1082.938 / 1273.888 | 1166.144 / 1361.411 | 83.207 / 87.523   | {"blockedOrPreparationToFirstPhysicalMutationMs":253.1325,"dispatchToBlockedOrPreparationMs":360.6436,"firstNewGenerationToCleanupDrainedMs":164.0358,"firstPhysicalMutationToFirstNewGenerationMs":388.3325,"presentationToStrictCompletionMs":131.1736} | {"blockedOrPreparationToFirstPhysicalMutationMs":290.4089,"dispatchToBlockedOrPreparationMs":406.8997,"firstNewGenerationToCleanupDrainedMs":175.9205,"firstPhysicalMutationToFirstNewGenerationMs":488.1822,"presentationToStrictCompletionMs":134.5184} |
| 15  | 722.015 / 945.154   | 567.055 / 634.137   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.478,"dispatchToBlockedOrPreparationMs":330.129,"firstNewGenerationToCleanupDrainedMs":44.9991,"firstPhysicalMutationToFirstNewGenerationMs":187.4488,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0545,"dispatchToBlockedOrPreparationMs":364.9358,"firstNewGenerationToCleanupDrainedMs":46.2897,"firstPhysicalMutationToFirstNewGenerationMs":217.8569,"presentationToStrictCompletionMs":0}           |
| 16  | 623.909 / 789.230   | 542.328 / 608.752   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0964,"dispatchToBlockedOrPreparationMs":322.2486,"firstNewGenerationToCleanupDrainedMs":36.6532,"firstPhysicalMutationToFirstNewGenerationMs":179.3302,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2824,"dispatchToBlockedOrPreparationMs":358.6511,"firstNewGenerationToCleanupDrainedMs":44.2401,"firstPhysicalMutationToFirstNewGenerationMs":200.5787,"presentationToStrictCompletionMs":0}           |
| 17  | 699.393 / 831.971   | 557.760 / 680.701   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4787,"dispatchToBlockedOrPreparationMs":331.5922,"firstNewGenerationToCleanupDrainedMs":39.6883,"firstPhysicalMutationToFirstNewGenerationMs":183.0012,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.905,"dispatchToBlockedOrPreparationMs":402.5673,"firstNewGenerationToCleanupDrainedMs":48.1971,"firstPhysicalMutationToFirstNewGenerationMs":224.0314,"presentationToStrictCompletionMs":0}            |
| 18  | 670.129 / 757.557   | 552.224 / 620.771   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2756,"dispatchToBlockedOrPreparationMs":331.165,"firstNewGenerationToCleanupDrainedMs":37.7262,"firstPhysicalMutationToFirstNewGenerationMs":180.0575,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2012,"dispatchToBlockedOrPreparationMs":366.6054,"firstNewGenerationToCleanupDrainedMs":43.9078,"firstPhysicalMutationToFirstNewGenerationMs":205.0569,"presentationToStrictCompletionMs":0}           |
| 19  | 698.477 / 758.886   | 579.160 / 624.111   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.791,"dispatchToBlockedOrPreparationMs":355.396,"firstNewGenerationToCleanupDrainedMs":37.6891,"firstPhysicalMutationToFirstNewGenerationMs":182.2836,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2709,"dispatchToBlockedOrPreparationMs":375.4259,"firstNewGenerationToCleanupDrainedMs":42.8573,"firstPhysicalMutationToFirstNewGenerationMs":201.5567,"presentationToStrictCompletionMs":0}           |
| 20  | 486.824 / 807.760   | 643.825 / 1053.683  | 157.002 / 245.923 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5842,"dispatchToBlockedOrPreparationMs":325.8008,"firstNewGenerationToCleanupDrainedMs":201.587,"firstPhysicalMutationToFirstNewGenerationMs":112.8534,"presentationToStrictCompletionMs":157.0017}    | {"blockedOrPreparationToFirstPhysicalMutationMs":268.8283,"dispatchToBlockedOrPreparationMs":412.9697,"firstNewGenerationToCleanupDrainedMs":246.2078,"firstPhysicalMutationToFirstNewGenerationMs":125.6768,"presentationToStrictCompletionMs":245.9226} |
| 21  | 168.741 / 222.836   | 400.298 / 495.001   | 231.557 / 272.166 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":114.5782,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":231.557}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.7564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.1656}             |
| 22  | 154.918 / 217.047   | 154.918 / 217.047   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.9181,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":217.0474,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 230.219 / 254.963   | 230.219 / 254.963   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":230.2187,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":254.9632,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 861.456 / 935.684   | 1039.184 / 1118.329 | 177.728 / 182.645 | {"blockedOrPreparationToFirstPhysicalMutationMs":253.7354,"dispatchToBlockedOrPreparationMs":412.073,"firstNewGenerationToCleanupDrainedMs":218.2977,"firstPhysicalMutationToFirstNewGenerationMs":155.0781,"presentationToStrictCompletionMs":177.7282}  | {"blockedOrPreparationToFirstPhysicalMutationMs":277.6547,"dispatchToBlockedOrPreparationMs":458.7517,"firstNewGenerationToCleanupDrainedMs":229.2147,"firstPhysicalMutationToFirstNewGenerationMs":152.7076,"presentationToStrictCompletionMs":182.645}  |
| 25  | 1461.034 / 804.197  | 1578.508 / 943.123  | 117.475 / 138.926 | {"blockedOrPreparationToFirstPhysicalMutationMs":592.2031,"dispatchToBlockedOrPreparationMs":370.4295,"firstNewGenerationToCleanupDrainedMs":157.0104,"firstPhysicalMutationToFirstNewGenerationMs":458.8654,"presentationToStrictCompletionMs":197.3187} | {"blockedOrPreparationToFirstPhysicalMutationMs":27.795,"dispatchToBlockedOrPreparationMs":405.4911,"firstNewGenerationToCleanupDrainedMs":182.8934,"firstPhysicalMutationToFirstNewGenerationMs":326.9437,"presentationToStrictCompletionMs":224.3067}   |
| 26  | 1227.931 / 1514.576 | 1186.869 / 1459.934 | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":250.7248,"dispatchToBlockedOrPreparationMs":358.9416,"firstNewGenerationToCleanupDrainedMs":151.131,"firstPhysicalMutationToFirstNewGenerationMs":426.0714,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":275.8095,"dispatchToBlockedOrPreparationMs":439.8071,"firstNewGenerationToCleanupDrainedMs":227.9014,"firstPhysicalMutationToFirstNewGenerationMs":516.4164,"presentationToStrictCompletionMs":0}        |
| 27  | 529.267 / 527.127   | 696.952 / 774.192   | 167.684 / 247.065 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4841,"dispatchToBlockedOrPreparationMs":353.5913,"firstNewGenerationToCleanupDrainedMs":235.3675,"firstPhysicalMutationToFirstNewGenerationMs":104.5086,"presentationToStrictCompletionMs":167.6843}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8776,"dispatchToBlockedOrPreparationMs":372.2902,"firstNewGenerationToCleanupDrainedMs":248.1193,"firstPhysicalMutationToFirstNewGenerationMs":149.9049,"presentationToStrictCompletionMs":247.0652}   |
| 28  | 842.921 / 798.736   | 795.130 / 887.620   | 0 / 88.884        | {"blockedOrPreparationToFirstPhysicalMutationMs":2.6177,"dispatchToBlockedOrPreparationMs":359.1764,"firstNewGenerationToCleanupDrainedMs":166.7212,"firstPhysicalMutationToFirstNewGenerationMs":266.6143,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6003,"dispatchToBlockedOrPreparationMs":413.0077,"firstNewGenerationToCleanupDrainedMs":176.1734,"firstPhysicalMutationToFirstNewGenerationMs":294.8381,"presentationToStrictCompletionMs":140.4682}   |
| 29  | 1139.899 / 1701.862 | 1253.666 / 1838.119 | 113.767 / 136.257 | {"blockedOrPreparationToFirstPhysicalMutationMs":22.3652,"dispatchToBlockedOrPreparationMs":652.6467,"firstNewGenerationToCleanupDrainedMs":151.9158,"firstPhysicalMutationToFirstNewGenerationMs":426.7381,"presentationToStrictCompletionMs":190.1601}  | {"blockedOrPreparationToFirstPhysicalMutationMs":721.0348,"dispatchToBlockedOrPreparationMs":432.8313,"firstNewGenerationToCleanupDrainedMs":178.7958,"firstPhysicalMutationToFirstNewGenerationMs":505.4568,"presentationToStrictCompletionMs":216.4628} |
| 30  | 407.171 / 588.638   | 618.367 / 827.143   | 211.196 / 238.505 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.7039,"dispatchToBlockedOrPreparationMs":306.8064,"firstNewGenerationToCleanupDrainedMs":211.8148,"firstPhysicalMutationToFirstNewGenerationMs":97.0419,"presentationToStrictCompletionMs":211.1963}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3175,"dispatchToBlockedOrPreparationMs":459.0829,"firstNewGenerationToCleanupDrainedMs":239.1926,"firstPhysicalMutationToFirstNewGenerationMs":124.5504,"presentationToStrictCompletionMs":238.5052}   |
| 31  | 697.904 / 628.123   | 697.904 / 628.123   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":58.6527,"dispatchToBlockedOrPreparationMs":406.124,"firstNewGenerationToCleanupDrainedMs":38.7281,"firstPhysicalMutationToFirstNewGenerationMs":194.3997,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":52.1465,"dispatchToBlockedOrPreparationMs":435.4388,"firstNewGenerationToCleanupDrainedMs":44.028,"firstPhysicalMutationToFirstNewGenerationMs":96.5097,"presentationToStrictCompletionMs":0}            |
| 32  | 177.159 / 210.815   | 412.420 / 531.443   | 235.261 / 320.628 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.6117,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":235.2609}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":138.7934,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":320.6276}             |
| 33  | 235.021 / 273.671   | 235.021 / 273.671   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":235.0208,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":273.6711,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 11 / 10                  | -1           | 501.615 / 484.990    | -16.626  | 16 / 15           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 13 / 14                  | 1            | 677.929 / 761.174    | 83.245   | 18 / 19           | 1            |
| 5   | 13 / 13                  | 0            | 661.619 / 761.523    | 99.904   | 18 / 18           | 0            |
| 6   | 16 / 17                  | 1            | 773.969 / 909.742    | 135.773  | 21 / 22           | 1            |
| 7   | 16 / 18                  | 2            | 818.587 / 1044.523   | 225.936  | 21 / 23           | 2            |
| 8   | 18 / 17                  | -1           | 941.970 / 923.903    | -18.067  | 23 / 22           | -1           |
| 9   | 17 / 16                  | -1           | 873.003 / 917.460    | 44.457   | 22 / 21           | -1           |
| 10  | 10 / 10                  | 0            | 531.953 / 628.552    | 96.599   | 15 / 15           | 0            |
| 11  | 4 / 3                    | -1           | 179.485 / 198.659    | 19.174   | 11 / 9            | -2           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 13  | 9 / 9                    | 0            | 524.191 / 577.400    | 53.210   | 10 / 10           | 0            |
| 14  | 23 / 23                  | 0            | 1002.109 / 1185.491  | 183.382  | 28 / 28           | 0            |
| 15  | 12 / 12                  | 0            | 522.056 / 587.847    | 65.791   | 17 / 20           | 3            |
| 16  | 12 / 12                  | 0            | 505.675 / 564.512    | 58.837   | 15 / 17           | 2            |
| 17  | 12 / 12                  | 0            | 518.072 / 632.504    | 114.432  | 16 / 16           | 0            |
| 18  | 12 / 12                  | 0            | 514.498 / 576.864    | 62.365   | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 541.471 / 581.254    | 39.783   | 16 / 16           | 0            |
| 20  | 10 / 16                  | 6            | 442.238 / 807.475    | 365.236  | 15 / 21           | 6            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 16 / 15                  | -1           | 820.886 / 889.114    | 68.228   | 21 / 20           | -1           |
| 25  | 29 / 13                  | -16          | 1421.498 / 760.230   | -661.268 | 34 / 18           | -16          |
| 26  | 22 / 23                  | 1            | 1035.738 / 1232.033  | 196.295  | 27 / 28           | 1            |
| 27  | 11 / 10                  | -1           | 461.584 / 526.073    | 64.489   | 17 / 16           | -1           |
| 28  | 11 / 11                  | 0            | 628.408 / 711.446    | 83.038   | 16 / 16           | 0            |
| 29  | 24 / 29                  | 5            | 1101.750 / 1659.323  | 557.573  | 29 / 34           | 5            |
| 30  | 9 / 10                   | 1            | 406.552 / 587.951    | 181.399  | 14 / 16           | 2            |
| 31  | 10 / 9                   | -1           | 659.176 / 584.095    | -75.081  | 12 / 10           | -2           |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 193.586 / 213.825  | 20.238   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 193.446 / 210.089  | 16.643   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 350.457 / 380.853  | 30.396   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 366.733 / 441.441  | 74.708   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 417.844 / 401.860  | -15.984  |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 388.059 / 429.952  | 41.893   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 247.670 / 308.404  | 60.733   |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 132.586 / 158.148  | 25.562   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 128.082 / 143.759  | 15.677   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 133.435 / 165.514  | 32.079   |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 131.632 / 155.220  | 23.588   |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 128.674 / 149.943  | 21.268   |
| 20  | 0 / 1                | 1     | 0 / 5              | 5     | 0 / 341.305        | 341.305  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 6 / 2              | -4    | 353.264 / 210.712  | -142.552 |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 533.118 / 617.137  | 84.019   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 2 / 3                | 1     | 11 / 16            | 5     | 612.211 / 1054.946 | 442.735  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

## Cumulative gates and other health evidence

### nvidia-2026-09-10T07-19-50-405Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":74,"allowedPresentationStretchEpisodes":18,"allowedPresentationStretchEyeObservations":161,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":127406,"leftPath":"NativeOriginal","referenceFrame":127829,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                         | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### nvidia-2026-09-10T07-19-50-405Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":77,"allowedPresentationStretchEpisodes":17,"allowedPresentationStretchEyeObservations":167,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":132453,"leftPath":"NativeOriginal","referenceFrame":132888,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                         | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 17082.26953125 / 16608.77734375 / -473.4921875 | 16894.5078125 / 16681.24609375 / -213.26171875 | 260.230               |
| nvidia | 1    | systemCommitMiB   | 49931.01953125 / 49272.2421875 / -658.77734375 | 55422.94921875 / 55060.37890625 / -362.5703125 | 296.207               |
| nvidia | 1    | dxgiUsageMiB      | 4083.84375 / 3340.59375 / -743.25              | 5583.1171875 / 3750.015625 / -1833.1015625     | -1089.852             |
| nvidia | 1    | liveTextures      | 0 / 217 / 217                                  | 0 / 261 / 261                                  | 44                    |
| nvidia | 1    | liveTextureMiB    | 0 / 2281.5979194641113 / 2281.5979194641113    | 0 / 2429.8961448669434 / 2429.8961448669434    | 148.298               |
| nvidia | 2    | processPrivateMiB | 16990.12890625 / 16624.8671875 / -365.26171875 | 17097.22265625 / 16617.34765625 / -479.875     | -114.613              |
| nvidia | 2    | systemCommitMiB   | 49759.61328125 / 49175.984375 / -583.62890625  | 55505.6015625 / 54815.92578125 / -689.67578125 | -106.047              |
| nvidia | 2    | dxgiUsageMiB      | 3657.33203125 / 3244.27734375 / -413.0546875   | 4066.75390625 / 3643.64453125 / -423.109375    | -10.055               |
| nvidia | 2    | liveTextures      | 0 / 207 / 207                                  | 0 / 211 / 211                                  | 4                     |
| nvidia | 2    | liveTextureMiB    | 0 / 2265.5977058410645 / 2265.5977058410645    | 0 / 2274.9311332702637 / 2274.9311332702637    | 9.333                 |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2284        | 2168        | -116        |
| cpu/compactPresentationContract/reuses                  | 2262        | 2146        | -116        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 71          | 70          | -1          |
| cpu/generationResourceValidation/contractPublishes      | 152         | 155         | 3           |
| cpu/generationResourceValidation/fullValidations        | 559         | 581         | 22          |
| cpu/generationResourceValidation/stableChecks           | 8618        | 8236        | -382        |
| cpu/generationResourceValidation/stableHits             | 8541        | 8159        | -382        |
| cpu/generationResourceValidation/stableMisses           | 77          | 77          | 0           |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 2           | 1           | -1          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4450        | 4149        | -301        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4450        | 4149        | -301        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4408        | 4107        | -301        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4450        | 4149        | -301        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4429        | 4128        | -301        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 35          | 34          | -1          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4415        | 4115        | -300        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4360        | 4054        | -306        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 94          | 4           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4435        | 4134        | -301        |
| cpu/strongStereoPacket/captures                         | 4726        | 4492        | -234        |
| cpu/strongStereoPacket/commitAccepts                    | 4549        | 4317        | -232        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 65          | 0           |
| cpu/strongStereoPacket/commitValidations                | 4614        | 4382        | -232        |
| cpu/strongStereoPacket/cycleReuses                      | 2323        | 2205        | -118        |
| cpu/strongStereoPacket/fastSkips                        | 4174        | 3806        | -368        |
| cpu/strongStereoPacket/invalidations                    | 5002        | 4706        | -296        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 100         | 104         | 4           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2303        | 2183        | -120        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.872       | 1.867       | -0.005      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 17.200      | 22.700      | 5.500       |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.136       | 0.133       | -0.003      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.200       | 1.600       | 0.400       |
| cpu/window/currentFrame                                 | 127830      | 53015       | -74815      |
| cpu/window/elapsedFrames                                | 4450        | 4148        | -302        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 123380      | 48867       | -74513      |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 127830      | 53017       | -74813      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6418778256  | 5974703856  | -444074400  |
| gpu/item10PeripheryTAAHistory/dispatches                | 5059        | 4709        | -350        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12850669440 | 11961613440 | -889056000  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.307       | 0.297       | -0.010      |
| gpu/item5ActiveFSRCopies/activePixels                   | 12352231120 | 11119322840 | -1232908280 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27883903280 | 26297233960 | -1586669320 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15840       | 14730       | -1110       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2075        | 1971        | -104        |
| gpu/item7EarlyHAM/executedClears                        | 2134        | 1974        | -160        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2134        | 1974        | -160        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4368        | 4104        | -264        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4450        | 4150        | -300        |
| gpu/startFrame                                          | 123380      | 48867       | -74513      |
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
| texture/createdCount                                    | 3922        | 3962        | 40          |
| texture/createdEstimatedBytes                           | 38663664176 | 38815224912 | 151560736   |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3705        | 3701        | -4          |
| texture/destroyedEstimatedBytes                         | 36271235356 | 36267294132 | -3941224    |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 868         | 886         | 18          |
| texture/liveTextureRecordCount                          | 217         | 261         | 44          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 10          | 50          | 40          |
| texture/niSourceTextureMatchedEstimatedBytes            | 16777440    | 172279000   | 155501560   |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1484        | 1536        | 52          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 217         | 261         | 44          |
| texture/outstandingEstimatedBytes                       | 2392428820  | 2547930780  | 155501960   |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 2           | 1           | -1          |
| texture/supported                                       | true        | true        | n/a         |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2393        | 2143        | -250        |
| cpu/compactPresentationContract/reuses                  | 2371        | 2121        | -250        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 71          | 70          | -1          |
| cpu/generationResourceValidation/contractPublishes      | 157         | 156         | -1          |
| cpu/generationResourceValidation/fullValidations        | 578         | 570         | -8          |
| cpu/generationResourceValidation/stableChecks           | 9007        | 8160        | -847        |
| cpu/generationResourceValidation/stableHits             | 8928        | 8083        | -845        |
| cpu/generationResourceValidation/stableMisses           | 79          | 77          | -2          |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 3           | 2           | -1          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4641        | 4106        | -535        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4641        | 4106        | -535        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4599        | 4064        | -535        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4641        | 4106        | -535        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4620        | 4085        | -535        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 35          | 34          | -1          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4606        | 4072        | -534        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4547        | 4017        | -530        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 90          | -4          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4626        | 4091        | -535        |
| cpu/strongStereoPacket/captures                         | 4970        | 4444        | -526        |
| cpu/strongStereoPacket/commitAccepts                    | 4768        | 4266        | -502        |
| cpu/strongStereoPacket/commitRejects                    | 64          | 66          | 2           |
| cpu/strongStereoPacket/commitValidations                | 4832        | 4332        | -500        |
| cpu/strongStereoPacket/cycleReuses                      | 2445        | 2180        | -265        |
| cpu/strongStereoPacket/fastSkips                        | 4312        | 3768        | -544        |
| cpu/strongStereoPacket/invalidations                    | 5212        | 4653        | -559        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 104         | 1           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2422        | 2160        | -262        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.817       | 1.869       | 0.052       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 20.300      | 36.700      | 16.400      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.141       | 0.133       | -0.008      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 20.300      | 1.500       | -18.800     |
| cpu/window/currentFrame                                 | 132889      | 57516       | -75373      |
| cpu/window/elapsedFrames                                | 4641        | 4107        | -534        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 128248      | 53409       | -74839      |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 132890      | 57516       | -75374      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6700448304  | 5901114384  | -799333920  |
| gpu/item10PeripheryTAAHistory/dispatches                | 5281        | 4651        | -630        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 13414584960 | 11814284160 | -1600300800 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.307       | 0.001       |
| gpu/item5ActiveFSRCopies/activePixels                   | 13157283320 | 11542473520 | -1614809800 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 29873027080 | 26077296080 | -3795731000 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 16940       | 14810       | -2130       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2243        | 1955        | -288        |
| gpu/item7EarlyHAM/executedClears                        | 2176        | 1948        | -228        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2176        | 1948        | -228        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4580        | 4064        | -516        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4642        | 4107        | -535        |
| gpu/startFrame                                          | 128248      | 53409       | -74839      |
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
| texture/createdCount                                    | 3896        | 3914        | 18          |
| texture/createdEstimatedBytes                           | 38604943312 | 38675267808 | 70324496    |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3689        | 3703        | 14          |
| texture/destroyedEstimatedBytes                         | 36229291932 | 36289829620 | 60537688    |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 860         | 863         | 3           |
| texture/liveTextureRecordCount                          | 207         | 211         | 4           |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 0           | 4           | 4           |
| texture/niSourceTextureMatchedEstimatedBytes            | 0           | 9786808     | 9786808     |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1474        | 1509        | 35          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 207         | 211         | 4           |
| texture/outstandingEstimatedBytes                       | 2375651380  | 2385438188  | 9786808     |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 3           | 2           | -1          |
| texture/supported                                       | true        | true        | n/a         |

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
