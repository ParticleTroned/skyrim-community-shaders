# PR73 readiness measurement comparison, September 11

<!-- pr73-readiness-nvidia-comparison-20260911 -->

### September 11: readiness correction versus the previous PR73 measurement

**The new PR73 build averaged 790.202 ms per switch versus
870.905 ms previously: 9.27% lower.**
Mean full-pass stretch duration changed from 5744.173 to
5138.086 ms (-10.55%). The prior
PR73 measurement and its PR66 comparison remain unchanged below.

Previous source: `d9780bb743134d975a561618f8b1cf89f8d20304`.
New source: `c73bae9a776e67614bba84ee0cecf3d076de259f`.
Both have main-VR base `ef7c366dd73989b2b87751c0ef975db7c6fd310f`.
The new run is `renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z`;
the previous run is `nvidia-20260910T214350263Z`.

Both builds completed 33 + 33 transitions in one process per build.
Each retains terminal counts 66 PASS / 0 FAIL and Task 2 counts
66 PASS / 0 FAIL / 0 INCONCLUSIVE. Both passes meet applicable health
checks, with zero device-loss, OOM, producer-terminal, lifecycle,
fidelity, vendor-fallback, bounds-fallback, memory-trim and
retirement-fence failure observations. No recovery apply or replay was used.

Formal improvement-or-neutral assessment remains **INCONCLUSIVE**:
scene conditions differ, a complete matching fixture fingerprint is
unavailable, and no versioned tolerance policy was supplied. These are
descriptive timings, excluding five-second pre-dispatch waits; they do
not establish significance, steady-state FPS, or a causal patch effect.
Separate `csx-render-scale-pr-v1` qualification and live SE/AE testing
remain pending.

#### Three retained measurements

PR66 is retained for continuity with the previous comparison. Its main-VR
base differs from both PR73 builds. Each entry is mean ± sample standard
error across two ordered pass summaries, not independent replicates.

| Measurement                  |    PR66 a09e1cc77 | Previous PR73 d9780bb74 | New PR73 c73bae9a7 | New vs previous |
| ---------------------------- | ----------------: | ----------------------: | -----------------: | --------------: |
| Strict completion ms         |    800.231 ±6.895 |          870.905 ±7.792 |    790.202 ±10.254 |         -9.267% |
| Presentation ready ms        |    673.511 ±0.953 |          737.070 ±3.536 |    666.183 ±10.440 |         -9.617% |
| Cleanup tail ms              |    102.409 ±5.973 |         108.160 ±10.364 |     100.125 ±0.188 |         -7.429% |
| Relatch proof ms             |    741.221 ±6.126 |          811.129 ±3.247 |     729.748 ±5.026 |        -10.033% |
| Strict frames                |     15.197 ±0.106 |           15.288 ±0.167 |      15.288 ±0.258 |         +0.000% |
| Relatch proof frames         |     13.580 ±0.180 |           13.700 ±0.140 |      13.620 ±0.260 |         -0.584% |
| Stretch episodes per pass    |     17.500 ±0.500 |           18.500 ±0.500 |      17.000 ±0.000 |         -8.108% |
| Stretch frames per pass      |     73.500 ±4.500 |           85.500 ±2.500 |      86.000 ±7.000 |         +0.585% |
| Stretch duration per pass ms | 4549.605 ±406.073 |       5744.173 ±347.342 |  5138.086 ±462.688 |        -10.551% |

#### Both passes and stretch history

| Pass | Previous strict mean ms | New strict mean ms |  Change | Previous/new p95 ms | Previous/new max ms | Retries previous/new | Stretch frames previous/new | Stretch ms previous/new |
| ---- | ----------------------: | -----------------: | ------: | ------------------- | ------------------- | -------------------- | --------------------------- | ----------------------- |
| 1    |                 863.112 |            779.947 | -9.636% | 1571.075/1368.052   | 1978.488/1469.693   | 10/9                 | 83/79                       | 5396.831/4675.398       |
| 2    |                 878.697 |            800.456 | -8.904% | 1616.172/1478.497   | 2045.208/1520.555   | 9/10                 | 88/93                       | 6091.515/5600.774       |

Row 16 is slower in both new passes; all routes
remain visible below. Stretch milliseconds decrease in both passes, but
pass 2 stretch frames increase from 88 to 93 and mean strict frames
increase from 15.121 to 15.545. The longest stretch episode changes
from 419.555 to 620.308 ms in pass 1, and from 809.540 to 648.801 ms
in pass 2; exact producer values in the tables and ledger are authoritative.

Both builds have 31 selected stretch transitions, all recovered. Full-pass
episode counts and selected-transition counts cover different windows.
No active stretch tail remains. Raw fixed-cutoff failures remain
DIAGNOSTIC_ONLY under imposed settling; the scaled-presentation gate
against proven native output remains CONTRACT_MISMATCH. Other gates
remain applicable. Raw failures, reasons, observations and limits are retained.

<details>
<summary>Every transition: previous and new PR73 in both passes</summary>

All timings are milliseconds. Retry counts follow route identity.
All shown terminal render and Task 2 classifications are PASS in both runs.

| Row | Route                             | Retries P1 old/new | Retries P2 old/new |     P1 old/new ms | P1 delta |     P2 old/new ms | P2 delta |      Old mean ±SE |      New mean ±SE |
| --- | --------------------------------- | ------------------ | ------------------ | ----------------: | -------: | ----------------: | -------: | ----------------: | ----------------: |
| 1   | DLSS Hoshipa -> NONE              | 0/0                | 0/0                |   802.940/656.586 | -146.354 |   771.853/697.747 |  -74.106 |   787.397 ±15.543 |   677.167 ±20.580 |
| 2   | NONE -> TAA                       | 0/0                | 0/0                |   182.563/153.417 |  -29.146 |   197.624/177.483 |  -20.141 |    190.093 ±7.530 |   165.450 ±12.033 |
| 3   | TAA -> DLAA                       | 0/0                | 0/0                |   245.994/279.702 |  +33.709 |   259.645/257.493 |   -2.152 |    252.820 ±6.826 |   268.598 ±11.105 |
| 4   | DLAA -> DLSS Hoshipa              | 0/0                | 0/0                |   972.176/865.768 | -106.408 |  1038.109/990.084 |  -48.025 |  1005.142 ±32.967 |   927.926 ±62.158 |
| 5   | DLSS Hoshipa -> DLSS UQ           | 0/0                | 0/0                | 1248.969/1120.619 | -128.350 |   972.278/987.027 |  +14.749 | 1110.623 ±138.345 |  1053.823 ±66.796 |
| 6   | DLSS UQ -> DLSS Quality           | 1/1                | 1/1                | 1152.159/1091.192 |  -60.967 | 1257.361/1125.321 | -132.040 |  1204.760 ±52.601 |  1108.257 ±17.064 |
| 7   | DLSS Quality -> DLSS Balanced     | 1/1                | 1/1                | 1445.305/1271.037 | -174.268 | 1254.924/1148.391 | -106.532 |  1350.114 ±95.191 |  1209.714 ±61.323 |
| 8   | DLSS Balanced -> DLSS Performance | 1/1                | 1/1                | 1318.676/1301.079 |  -17.596 | 1228.255/1195.793 |  -32.463 |  1273.465 ±45.210 |  1248.436 ±52.643 |
| 9   | DLSS Performance -> DLSS UP       | 1/1                | 1/1                | 1263.565/1209.841 |  -53.724 | 1432.332/1128.613 | -303.719 |  1347.948 ±84.383 |  1169.227 ±40.614 |
| 10  | DLSS UP -> DLAA                   | 0/0                | 0/0                |   762.358/734.508 |  -27.849 |   957.865/760.687 | -197.178 |   860.111 ±97.754 |   747.598 ±13.089 |
| 11  | DLAA -> TAA                       | 0/0                | 0/0                |   474.974/457.950 |  -17.023 |   502.720/425.129 |  -77.591 |   488.847 ±13.873 |   441.540 ±16.411 |
| 12  | TAA -> NONE                       | 0/0                | 0/0                |   177.775/173.229 |   -4.546 |   217.142/165.307 |  -51.835 |   197.459 ±19.683 |    169.268 ±3.961 |
| 13  | NONE -> FSR3 Native AA            | 0/0                | 0/0                |   844.424/815.505 |  -28.919 |   643.345/582.542 |  -60.803 |  743.884 ±100.539 |  699.023 ±116.482 |
| 14  | FSR3 Native AA -> FSR3 Hoshipa    | 1/1                | 1/1                | 1467.444/1279.581 | -187.863 | 1553.553/1370.679 | -182.874 |  1510.499 ±43.054 |  1325.130 ±45.549 |
| 15  | FSR3 Hoshipa -> FSR3 UQ           | 0/0                | 0/1                |  1137.621/727.243 | -410.378 |  817.940/1256.793 | +438.854 |  977.780 ±159.840 |  992.018 ±264.775 |
| 16  | FSR3 UQ -> FSR3 Quality           | 0/0                | 0/0                |   816.388/923.933 | +107.545 |   724.718/795.099 |  +70.381 |   770.553 ±45.835 |   859.516 ±64.417 |
| 17  | FSR3 Quality -> FSR3 Balanced     | 0/0                | 0/0                |   795.401/689.066 | -106.334 |   899.652/844.197 |  -55.455 |   847.526 ±52.125 |   766.632 ±77.565 |
| 18  | FSR3 Balanced -> FSR3 Performance | 0/0                | 1/1                |   861.596/735.192 | -126.403 | 1570.412/1493.796 |  -76.616 | 1216.004 ±354.408 | 1114.494 ±379.302 |
| 19  | FSR3 Performance -> FSR3 UP       | 0/1                | 0/0                |  734.869/1220.774 | +485.906 |   828.547/771.313 |  -57.233 |   781.708 ±46.839 |  996.044 ±224.731 |
| 20  | FSR3 UP -> FSR3 Native AA         | 0/0                | 0/0                |   727.042/713.693 |  -13.349 |   750.567/763.533 |  +12.966 |   738.804 ±11.762 |   738.613 ±24.920 |
| 21  | FSR3 Native AA -> TAA             | 0/0                | 0/0                |   476.993/481.173 |   +4.180 |   548.025/519.992 |  -28.033 |   512.509 ±35.516 |   500.582 ±19.409 |
| 22  | TAA -> NONE                       | 0/0                | 0/0                |   167.992/167.496 |   -0.496 |   173.281/182.075 |   +8.794 |    170.637 ±2.645 |    174.786 ±7.290 |
| 23  | NONE -> DLAA                      | 0/0                | 0/0                |   247.137/246.583 |   -0.553 |   333.987/258.243 |  -75.744 |   290.562 ±43.425 |    252.413 ±5.830 |
| 24  | DLAA -> FSR3 Native AA            | 1/1                | 0/1                |  1100.556/871.391 | -229.165 |   883.114/830.242 |  -52.871 |  991.835 ±108.721 |   850.817 ±20.574 |
| 25  | FSR3 Native AA -> DLSS Hoshipa    | 1/1                | 0/0                | 1560.185/1469.693 |  -90.492 |  1198.841/957.409 | -241.432 | 1379.513 ±180.672 | 1213.551 ±256.142 |
| 26  | DLSS Hoshipa -> FSR3 Hoshipa      | 1/0                | 1/1                |  1587.410/954.918 | -632.492 | 1684.811/1468.298 | -216.514 |  1636.111 ±48.701 | 1211.608 ±256.690 |
| 27  | FSR3 Hoshipa -> NONE              | 0/0                | 0/0                |   770.143/702.902 |  -67.241 |   879.623/781.304 |  -98.319 |   824.883 ±54.740 |   742.103 ±39.201 |
| 28  | NONE -> FSR3 UP                   | 0/0                | 0/0                |  1002.200/881.771 | -120.429 |  1077.337/905.797 | -171.540 |  1039.768 ±37.568 |   893.784 ±12.013 |
| 29  | FSR3 UP -> DLSS UP                | 2/1                | 2/1                | 1978.488/1468.512 | -509.976 | 2045.208/1520.555 | -524.653 |  2011.848 ±33.360 |  1494.534 ±26.021 |
| 30  | DLSS UP -> TAA                    | 0/0                | 0/0                |   724.710/702.745 |  -21.965 |   782.914/730.674 |  -52.241 |   753.812 ±29.102 |   716.709 ±13.964 |
| 31  | TAA -> FSR3 Native AA             | 0/0                | 0/0                |   648.718/685.023 |  +36.305 |   661.378/594.050 |  -67.328 |    655.048 ±6.330 |   639.536 ±45.487 |
| 32  | FSR3 Native AA -> NONE            | 0/0                | 0/0                |   512.205/442.075 |  -70.130 |   538.073/479.112 |  -58.961 |   525.139 ±12.934 |   460.593 ±18.519 |
| 33  | NONE -> DLAA                      | 0/0                | 0/0                |   273.734/244.053 |  -29.681 |   311.572/250.271 |  -61.300 |   292.653 ±18.919 |    247.162 ±3.109 |

</details>

#### Identity, memory and complete ledger

The new physical 28,173,824-byte DLL, adjacent manifest and AIO receipt
match runtime Build ID
`d098079db31d3f43d3433ba4023e500774ff1895ee85ea648c726731bc68a31f`.
Its SHA-256 is
`9a777935e69bbd3c4d7b6400c7291727d6bec15c65708495fbed6719d0b19581`.
The previous Build ID remains
`ac724629b68fbaaceadd64f217d48ad54269165031b984c2c9bb1deb24165f35`.
Both retain clean Release source and their full compile identities.
New-run captures are verified inactive and all journal evidence is flushed.

Memory remains separately inconclusive for both runs. New pass private
memory deltas are -237.355 / -219.543 MiB; system commit deltas are
-347.191 / -416.984 MiB; DXGI deltas are -750.688 / -174.313 MiB.
Tracked texture deltas are +263 / +236 and +2402.766 / +2358.765 MiB,
relative to freshly reset trackers. All six boundaries, pressure,
ratios and unrounded predicates remain in the complete ledger; these
measurements do not prove or disprove a leak.

The canonical ledger preserves prior measurements and adds every new
summary field, transition/pass result, retry reason/wait, health gate,
memory/resource/profiler field, provenance and comparison delta. Both
complete summaries/comparisons reconstruct exactly from detail cells;
each comparison audits all 1,056 paired numeric timing cells. The pinned
main-VR comparison also remains retained. Raw receipt trees stay local.

<!-- end pr73-readiness-nvidia-comparison-20260911 -->

# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                | Candidate                                                                                                       |
| ----------------------- | --------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | nvidia-20260910T214350263Z                                                              | renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z                                                              |
| Renderer base           | d9780bb743134d975a561618f8b1cf89f8d20304                                                | c73bae9a776e67614bba84ee0cecf3d076de259f                                                                        |
| Main-VR base/equivalent | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                        |
| Compiled source         | d9780bb743134d975a561618f8b1cf89f8d20304                                                | c73bae9a776e67614bba84ee0cecf3d076de259f                                                                        |
| Build ID                | ac724629b68fbaaceadd64f217d48ad54269165031b984c2c9bb1deb24165f35                        | d098079db31d3f43d3433ba4023e500774ff1895ee85ea648c726731bc68a31f                                                |
| DLL SHA-256             | 53adff3245c00856fa8cf18bfb99ad5c36a832f465d604c96980476198a3c89f                        | 9a777935e69bbd3c4d7b6400c7291727d6bec15c65708495fbed6719d0b19581                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-20260910T214350263Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z |

Assessment limits: retained_context_not_matched:scene; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 863.112/779.947 | -9.636       | 10/9        | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 878.697/800.456 | -8.904       | 9/10        | 0/0          | 0/0                 | none             | MET/MET             |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta     | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | --------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 814.377   | 724.722   | -89.654   | -11.009 |
| nvidia | 1    | Relatch proof mean         | frames      | 13.840    | 13.360    | -0.480    | -3.468  |
| nvidia | 1    | Relatch proof total        | ms          | 20359.413 | 18118.055 | -2241.359 | -11.009 |
| nvidia | 1    | Relatch proof total        | frames      | 346       | 334       | -12       | -3.468  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 863.112   | 779.947   | -83.165   | -9.636  |
| nvidia | 1    | Strict completion mean     | frames      | 15.455    | 15.030    | -0.424    | -2.745  |
| nvidia | 1    | Strict completion total    | ms          | 28482.708 | 25738.254 | -2744.454 | -9.636  |
| nvidia | 1    | Strict completion total    | frames      | 510       | 496       | -14       | -2.745  |
| nvidia | 1    | Stretch completed episodes | episodes    | 19        | 17        | -2        | -10.526 |
| nvidia | 1    | Stretch completed total    | frames      | 83        | 79        | -4        | -4.819  |
| nvidia | 1    | Stretch completed total    | ms          | 5396.831  | 4675.398  | -721.434  | -13.368 |
| nvidia | 1    | Stretch longest episode    | ms          | 419.555   | 620.308   | 200.753   | 47.849  |
| nvidia | 2    | Relatch proof mean         | ms          | 807.882   | 734.774   | -73.108   | -9.049  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.560    | 13.880    | 0.320     | 2.360   |
| nvidia | 2    | Relatch proof total        | ms          | 20197.047 | 18369.345 | -1827.702 | -9.049  |
| nvidia | 2    | Relatch proof total        | frames      | 339       | 347       | 8         | 2.360   |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 878.697   | 800.456   | -78.241   | -8.904  |
| nvidia | 2    | Strict completion mean     | frames      | 15.121    | 15.545    | 0.424     | 2.806   |
| nvidia | 2    | Strict completion total    | ms          | 28997.004 | 26415.048 | -2581.956 | -8.904  |
| nvidia | 2    | Strict completion total    | frames      | 499       | 513       | 14        | 2.806   |
| nvidia | 2    | Stretch completed episodes | episodes    | 18        | 17        | -1        | -5.556  |
| nvidia | 2    | Stretch completed total    | frames      | 88        | 93        | 5         | 5.682   |
| nvidia | 2    | Stretch completed total    | ms          | 6091.515  | 5600.774  | -490.741  | -8.056  |
| nvidia | 2    | Stretch longest episode    | ms          | 809.540   | 648.801   | -160.739  | -19.856 |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 802.940 / 656.586   | -146.354 | -18.227 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 182.563 / 153.417   | -29.146  | -15.965 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 245.994 / 279.702   | 33.709   | 13.703  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 972.176 / 865.768   | -106.408 | -10.945 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1248.969 / 1120.619 | -128.350 | -10.276 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1152.159 / 1091.192 | -60.967  | -5.292  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1445.305 / 1271.037 | -174.268 | -12.057 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1318.676 / 1301.079 | -17.596  | -1.334  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1263.565 / 1209.841 | -53.724  | -4.252  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 762.358 / 734.508   | -27.849  | -3.653  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 474.974 / 457.950   | -17.023  | -3.584  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 177.775 / 173.229   | -4.546   | -2.557  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 844.424 / 815.505   | -28.919  | -3.425  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1467.444 / 1279.581 | -187.863 | -12.802 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 1137.621 / 727.243  | -410.378 | -36.073 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 816.388 / 923.933   | 107.545  | 13.173  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 795.401 / 689.066   | -106.334 | -13.369 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 861.596 / 735.192   | -126.403 | -14.671 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 734.869 / 1220.774  | 485.906  | 66.121  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 727.042 / 713.693   | -13.349  | -1.836  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 476.993 / 481.173   | 4.180    | 0.876   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 167.992 / 167.496   | -0.496   | -0.295  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 247.137 / 246.583   | -0.553   | -0.224  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1100.556 / 871.391  | -229.165 | -20.823 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1560.185 / 1469.693 | -90.492  | -5.800  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1587.410 / 954.918  | -632.492 | -39.844 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 770.143 / 702.902   | -67.241  | -8.731  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 1002.200 / 881.771  | -120.429 | -12.016 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1978.488 / 1468.512 | -509.976 | -25.776 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 724.710 / 702.745   | -21.965  | -3.031  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 648.718 / 685.023   | 36.305   | 5.596   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 512.205 / 442.075   | -70.130  | -13.692 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 273.734 / 244.053   | -29.681  | -10.843 | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 549.587 / 447.820   | 802.940 / 656.586   | 253.353 / 208.767 | {"blockedOrPreparationToFirstPhysicalMutationMs":312.0717,"dispatchToBlockedOrPreparationMs":97.5029,"firstNewGenerationToCleanupDrainedMs":254.184,"firstPhysicalMutationToFirstNewGenerationMs":139.1818,"presentationToStrictCompletionMs":253.3529}   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.6563,"dispatchToBlockedOrPreparationMs":335.0722,"firstNewGenerationToCleanupDrainedMs":209.0736,"firstPhysicalMutationToFirstNewGenerationMs":109.7841,"presentationToStrictCompletionMs":208.7666}  |
| 2   | 182.563 / 153.417   | 182.563 / 153.417   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":92.3404,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":153.4173,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 245.994 / 279.702   | 245.994 / 279.702   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":245.9936,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":279.7022,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 807.611 / 667.767   | 891.455 / 786.303   | 83.844 / 118.537  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7582,"dispatchToBlockedOrPreparationMs":381.2807,"firstNewGenerationToCleanupDrainedMs":167.3852,"firstPhysicalMutationToFirstNewGenerationMs":339.0311,"presentationToStrictCompletionMs":164.564}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0033,"dispatchToBlockedOrPreparationMs":320.4408,"firstNewGenerationToCleanupDrainedMs":158.6505,"firstPhysicalMutationToFirstNewGenerationMs":304.2086,"presentationToStrictCompletionMs":198.0014}  |
| 5   | 1026.444 / 955.618  | 1162.689 / 1038.214 | 136.246 / 82.596  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1391,"dispatchToBlockedOrPreparationMs":392.2223,"firstNewGenerationToCleanupDrainedMs":178.1069,"firstPhysicalMutationToFirstNewGenerationMs":589.2212,"presentationToStrictCompletionMs":222.5252}   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8194,"dispatchToBlockedOrPreparationMs":329.09,"firstNewGenerationToCleanupDrainedMs":164.8594,"firstPhysicalMutationToFirstNewGenerationMs":541.4455,"presentationToStrictCompletionMs":165.0008}    |
| 6   | 934.609 / 875.191   | 1067.292 / 1007.997 | 132.684 / 132.806 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5337,"dispatchToBlockedOrPreparationMs":383.5321,"firstNewGenerationToCleanupDrainedMs":175.5771,"firstPhysicalMutationToFirstNewGenerationMs":503.6494,"presentationToStrictCompletionMs":217.5505}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2817,"dispatchToBlockedOrPreparationMs":347.7403,"firstNewGenerationToCleanupDrainedMs":174.1092,"firstPhysicalMutationToFirstNewGenerationMs":482.8658,"presentationToStrictCompletionMs":216.0018}  |
| 7   | 1210.306 / 1060.666 | 1355.192 / 1186.782 | 144.887 / 126.116 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8268,"dispatchToBlockedOrPreparationMs":401.0507,"firstNewGenerationToCleanupDrainedMs":189.9864,"firstPhysicalMutationToFirstNewGenerationMs":759.3282,"presentationToStrictCompletionMs":234.9993}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1464,"dispatchToBlockedOrPreparationMs":341.1366,"firstNewGenerationToCleanupDrainedMs":168.5915,"firstPhysicalMutationToFirstNewGenerationMs":673.9075,"presentationToStrictCompletionMs":210.3715}  |
| 8   | 1110.512 / 1095.133 | 1238.053 / 1220.963 | 127.542 / 125.830 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6262,"dispatchToBlockedOrPreparationMs":374.8056,"firstNewGenerationToCleanupDrainedMs":168.328,"firstPhysicalMutationToFirstNewGenerationMs":690.2935,"presentationToStrictCompletionMs":208.1641}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2782,"dispatchToBlockedOrPreparationMs":350.8879,"firstNewGenerationToCleanupDrainedMs":166.2255,"firstPhysicalMutationToFirstNewGenerationMs":700.5718,"presentationToStrictCompletionMs":205.9462}  |
| 9   | 1087.546 / 1006.707 | 1177.833 / 1130.323 | 90.287 / 123.616  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5079,"dispatchToBlockedOrPreparationMs":343.5565,"firstNewGenerationToCleanupDrainedMs":179.2961,"firstPhysicalMutationToFirstNewGenerationMs":651.4723,"presentationToStrictCompletionMs":176.0199}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2557,"dispatchToBlockedOrPreparationMs":335.1845,"firstNewGenerationToCleanupDrainedMs":162.6084,"firstPhysicalMutationToFirstNewGenerationMs":629.2742,"presentationToStrictCompletionMs":203.1347}  |
| 10  | 594.303 / 558.214   | 762.358 / 734.508   | 168.055 / 176.294 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2328,"dispatchToBlockedOrPreparationMs":333.0018,"firstNewGenerationToCleanupDrainedMs":231.9483,"firstPhysicalMutationToFirstNewGenerationMs":194.1747,"presentationToStrictCompletionMs":168.0551}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5139,"dispatchToBlockedOrPreparationMs":333.0484,"firstNewGenerationToCleanupDrainedMs":221.0865,"firstPhysicalMutationToFirstNewGenerationMs":176.8596,"presentationToStrictCompletionMs":176.2943}  |
| 11  | 173.903 / 168.004   | 474.974 / 457.950   | 301.070 / 289.946 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1011,"dispatchToBlockedOrPreparationMs":130.0806,"firstNewGenerationToCleanupDrainedMs":302.0767,"firstPhysicalMutationToFirstNewGenerationMs":38.7151,"presentationToStrictCompletionMs":301.0701}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.388,"dispatchToBlockedOrPreparationMs":126.0806,"firstNewGenerationToCleanupDrainedMs":290.7759,"firstPhysicalMutationToFirstNewGenerationMs":37.7057,"presentationToStrictCompletionMs":289.9458}    |
| 12  | 177.775 / 173.229   | 177.775 / 173.229   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.7751,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":173.229,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 844.424 / 815.505   | 844.424 / 815.505   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":241.4002,"dispatchToBlockedOrPreparationMs":434.4351,"firstNewGenerationToCleanupDrainedMs":45.9668,"firstPhysicalMutationToFirstNewGenerationMs":122.6216,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":210.5794,"dispatchToBlockedOrPreparationMs":443.6748,"firstNewGenerationToCleanupDrainedMs":39.6922,"firstPhysicalMutationToFirstNewGenerationMs":121.5584,"presentationToStrictCompletionMs":0}        |
| 14  | 1372.035 / 1109.697 | 1419.176 / 1233.615 | 47.141 / 123.918  | {"blockedOrPreparationToFirstPhysicalMutationMs":298.7148,"dispatchToBlockedOrPreparationMs":439.3714,"firstNewGenerationToCleanupDrainedMs":182.4697,"firstPhysicalMutationToFirstNewGenerationMs":498.6203,"presentationToStrictCompletionMs":95.4095}  | {"blockedOrPreparationToFirstPhysicalMutationMs":256.5249,"dispatchToBlockedOrPreparationMs":387.052,"firstNewGenerationToCleanupDrainedMs":166.5031,"firstPhysicalMutationToFirstNewGenerationMs":423.5352,"presentationToStrictCompletionMs":169.8841} |
| 15  | 1137.621 / 727.243  | 637.376 / 591.690   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.4656,"dispatchToBlockedOrPreparationMs":374.2975,"firstNewGenerationToCleanupDrainedMs":41.2185,"firstPhysicalMutationToFirstNewGenerationMs":216.3949,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0031,"dispatchToBlockedOrPreparationMs":352.8401,"firstNewGenerationToCleanupDrainedMs":39.6052,"firstPhysicalMutationToFirstNewGenerationMs":195.2417,"presentationToStrictCompletionMs":0}          |
| 16  | 816.388 / 923.933   | 717.128 / 600.731   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.5924,"dispatchToBlockedOrPreparationMs":407.9993,"firstNewGenerationToCleanupDrainedMs":48.3925,"firstPhysicalMutationToFirstNewGenerationMs":255.1434,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0463,"dispatchToBlockedOrPreparationMs":335.5841,"firstNewGenerationToCleanupDrainedMs":41.8456,"firstPhysicalMutationToFirstNewGenerationMs":219.2555,"presentationToStrictCompletionMs":0}          |
| 17  | 795.401 / 689.066   | 644.980 / 603.652   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.3619,"dispatchToBlockedOrPreparationMs":361.9918,"firstNewGenerationToCleanupDrainedMs":45.8614,"firstPhysicalMutationToFirstNewGenerationMs":231.7649,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5866,"dispatchToBlockedOrPreparationMs":368.0976,"firstNewGenerationToCleanupDrainedMs":42.1154,"firstPhysicalMutationToFirstNewGenerationMs":188.8525,"presentationToStrictCompletionMs":0}          |
| 18  | 861.596 / 735.192   | 713.826 / 607.982   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2232,"dispatchToBlockedOrPreparationMs":443.781,"firstNewGenerationToCleanupDrainedMs":46.186,"firstPhysicalMutationToFirstNewGenerationMs":218.6355,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.943,"dispatchToBlockedOrPreparationMs":363.053,"firstNewGenerationToCleanupDrainedMs":39.7992,"firstPhysicalMutationToFirstNewGenerationMs":201.1864,"presentationToStrictCompletionMs":0}            |
| 19  | 734.869 / 1220.774  | 604.526 / 1055.203  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2401,"dispatchToBlockedOrPreparationMs":345.1883,"firstNewGenerationToCleanupDrainedMs":40.6882,"firstPhysicalMutationToFirstNewGenerationMs":213.4096,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":262.079,"dispatchToBlockedOrPreparationMs":395.007,"firstNewGenerationToCleanupDrainedMs":40.3851,"firstPhysicalMutationToFirstNewGenerationMs":357.732,"presentationToStrictCompletionMs":0}           |
| 20  | 552.113 / 475.195   | 727.042 / 713.693   | 174.929 / 238.498 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.038,"dispatchToBlockedOrPreparationMs":367.0488,"firstNewGenerationToCleanupDrainedMs":236.5171,"firstPhysicalMutationToFirstNewGenerationMs":119.4382,"presentationToStrictCompletionMs":174.9289}    | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0452,"dispatchToBlockedOrPreparationMs":340.1828,"firstNewGenerationToCleanupDrainedMs":239.0053,"firstPhysicalMutationToFirstNewGenerationMs":128.4599,"presentationToStrictCompletionMs":238.4978}  |
| 21  | 204.523 / 206.072   | 476.993 / 481.173   | 272.470 / 275.101 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.4185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.4697}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":137.6313,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":275.1007}            |
| 22  | 167.992 / 167.496   | 167.992 / 167.496   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.9918,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.4957,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 247.137 / 246.583   | 247.137 / 246.583   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":247.1366,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":246.5831,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 925.821 / 700.034   | 1100.556 / 871.391  | 174.735 / 171.357 | {"blockedOrPreparationToFirstPhysicalMutationMs":288.9222,"dispatchToBlockedOrPreparationMs":452.6056,"firstNewGenerationToCleanupDrainedMs":216.7565,"firstPhysicalMutationToFirstNewGenerationMs":142.2719,"presentationToStrictCompletionMs":174.735}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5963,"dispatchToBlockedOrPreparationMs":500.3463,"firstNewGenerationToCleanupDrainedMs":216.5493,"firstPhysicalMutationToFirstNewGenerationMs":150.8989,"presentationToStrictCompletionMs":171.3567}  |
| 25  | 1344.409 / 1260.391 | 1475.116 / 1386.754 | 130.707 / 126.364 | {"blockedOrPreparationToFirstPhysicalMutationMs":328.1896,"dispatchToBlockedOrPreparationMs":486.4611,"firstNewGenerationToCleanupDrainedMs":173.322,"firstPhysicalMutationToFirstNewGenerationMs":487.1429,"presentationToStrictCompletionMs":215.7761}  | {"blockedOrPreparationToFirstPhysicalMutationMs":324.2224,"dispatchToBlockedOrPreparationMs":405.8027,"firstNewGenerationToCleanupDrainedMs":166.9843,"firstPhysicalMutationToFirstNewGenerationMs":489.745,"presentationToStrictCompletionMs":209.3018} |
| 26  | 1455.241 / 819.040  | 1540.616 / 910.022  | 85.375 / 90.981   | {"blockedOrPreparationToFirstPhysicalMutationMs":333.2343,"dispatchToBlockedOrPreparationMs":477.0536,"firstNewGenerationToCleanupDrainedMs":168.5136,"firstPhysicalMutationToFirstNewGenerationMs":561.8147,"presentationToStrictCompletionMs":132.1692} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3084,"dispatchToBlockedOrPreparationMs":374.1965,"firstNewGenerationToCleanupDrainedMs":182.4897,"firstPhysicalMutationToFirstNewGenerationMs":349.0272,"presentationToStrictCompletionMs":135.8773}  |
| 27  | 535.480 / 533.530   | 770.143 / 702.902   | 234.663 / 169.372 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5465,"dispatchToBlockedOrPreparationMs":366.8679,"firstNewGenerationToCleanupDrainedMs":234.9134,"firstPhysicalMutationToFirstNewGenerationMs":163.8152,"presentationToStrictCompletionMs":234.6634}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4752,"dispatchToBlockedOrPreparationMs":346.9035,"firstNewGenerationToCleanupDrainedMs":236.7528,"firstPhysicalMutationToFirstNewGenerationMs":115.7706,"presentationToStrictCompletionMs":169.372}   |
| 28  | 914.125 / 755.190   | 957.313 / 837.107   | 43.188 / 81.917   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7464,"dispatchToBlockedOrPreparationMs":436.0405,"firstNewGenerationToCleanupDrainedMs":185.4704,"firstPhysicalMutationToFirstNewGenerationMs":332.0561,"presentationToStrictCompletionMs":88.0751}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3804,"dispatchToBlockedOrPreparationMs":382.759,"firstNewGenerationToCleanupDrainedMs":165.5979,"firstPhysicalMutationToFirstNewGenerationMs":285.3699,"presentationToStrictCompletionMs":126.5807}   |
| 29  | 1759.497 / 1233.191 | 1896.398 / 1375.130 | 136.900 / 141.939 | {"blockedOrPreparationToFirstPhysicalMutationMs":758.6718,"dispatchToBlockedOrPreparationMs":435.4322,"firstNewGenerationToCleanupDrainedMs":181.0707,"firstPhysicalMutationToFirstNewGenerationMs":521.223,"presentationToStrictCompletionMs":218.9908}  | {"blockedOrPreparationToFirstPhysicalMutationMs":22.6826,"dispatchToBlockedOrPreparationMs":668.5188,"firstNewGenerationToCleanupDrainedMs":185.5666,"firstPhysicalMutationToFirstNewGenerationMs":498.362,"presentationToStrictCompletionMs":235.3212}  |
| 30  | 536.504 / 469.735   | 724.710 / 702.745   | 188.207 / 233.010 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8994,"dispatchToBlockedOrPreparationMs":346.1577,"firstNewGenerationToCleanupDrainedMs":256.9617,"firstPhysicalMutationToFirstNewGenerationMs":117.6914,"presentationToStrictCompletionMs":188.2065}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1336,"dispatchToBlockedOrPreparationMs":353.8621,"firstNewGenerationToCleanupDrainedMs":233.7121,"firstPhysicalMutationToFirstNewGenerationMs":112.0376,"presentationToStrictCompletionMs":233.0102}  |
| 31  | 648.718 / 685.023   | 648.718 / 685.023   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":58.2698,"dispatchToBlockedOrPreparationMs":443.9177,"firstNewGenerationToCleanupDrainedMs":44.2242,"firstPhysicalMutationToFirstNewGenerationMs":102.3061,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":47.443,"dispatchToBlockedOrPreparationMs":406.2882,"firstNewGenerationToCleanupDrainedMs":42.135,"firstPhysicalMutationToFirstNewGenerationMs":189.157,"presentationToStrictCompletionMs":0}            |
| 32  | 211.218 / 181.126   | 512.205 / 442.075   | 300.987 / 260.948 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.5879,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":300.9867}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":121.4218,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":260.9483}            |
| 33  | 273.734 / 244.053   | 273.734 / 244.053   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":273.7338,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":244.0526,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 9                   | -1           | 548.756 / 447.513    | -101.244 | 15 / 14           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 4   | 14 / 12                  | -2           | 724.070 / 627.653    | -96.417  | 19 / 17           | -2           |
| 5   | 12 / 13                  | 1            | 984.583 / 873.355    | -111.228 | 17 / 18           | 1            |
| 6   | 16 / 17                  | 1            | 891.715 / 833.888    | -57.827  | 21 / 22           | 1            |
| 7   | 18 / 17                  | -1           | 1165.206 / 1018.191  | -147.015 | 23 / 22           | -1           |
| 8   | 16 / 17                  | 1            | 1069.725 / 1054.738  | -14.987  | 21 / 22           | 1            |
| 9   | 16 / 17                  | 1            | 998.537 / 967.714    | -30.822  | 21 / 22           | 1            |
| 10  | 9 / 9                    | 0            | 530.409 / 513.422    | -16.987  | 15 / 14           | -1           |
| 11  | 3 / 3                    | 0            | 172.897 / 167.174    | -5.723   | 9 / 10            | 1            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 13  | 9 / 9                    | 0            | 798.457 / 775.813    | -22.644  | 10 / 10           | 0            |
| 14  | 23 / 22                  | -1           | 1236.707 / 1067.112  | -169.594 | 28 / 27           | -1           |
| 15  | 12 / 12                  | 0            | 596.158 / 552.085    | -44.073  | 25 / 16           | -9           |
| 16  | 12 / 12                  | 0            | 668.735 / 558.886    | -109.849 | 15 / 20           | 5            |
| 17  | 12 / 12                  | 0            | 599.119 / 561.537    | -37.582  | 16 / 15           | -1           |
| 18  | 12 / 12                  | 0            | 667.640 / 568.182    | -99.457  | 16 / 16           | 0            |
| 19  | 12 / 22                  | 10           | 563.838 / 1014.818   | 450.980  | 16 / 27           | 11           |
| 20  | 10 / 10                  | 0            | 490.525 / 474.688    | -15.837  | 15 / 15           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 15 / 10                  | -5           | 883.800 / 654.841    | -228.958 | 20 / 15           | -5           |
| 25  | 24 / 23                  | -1           | 1301.794 / 1219.770  | -82.024  | 29 / 28           | -1           |
| 26  | 23 / 13                  | -10          | 1372.103 / 727.532   | -644.570 | 28 / 18           | -10          |
| 27  | 10 / 10                  | 0            | 535.230 / 466.149    | -69.080  | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 771.843 / 671.509    | -100.334 | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1715.327 / 1189.563  | -525.764 | 34 / 28           | -6           |
| 30  | 9 / 9                    | 0            | 467.748 / 469.033    | 1.285    | 15 / 14           | -1           |
| 31  | 9 / 10                   | 1            | 604.494 / 642.888    | 38.395   | 10 / 11           | 1            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 10           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 143.665 / 112.696  | -30.970  |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 206.027 / 197.994  | -8.033   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 217.854 / 206.392  | -11.462  |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 366.943 / 380.289  | 13.346   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 419.555 / 375.718  | -43.837  |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 378.178 / 386.744  | 8.566    |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 385.807 / 373.362  | -12.445  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 308.534 / 276.049  | -32.485  |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 139.854 / 139.395  | -0.459   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 162.321 / 156.803  | -5.518   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 177.140 / 139.603  | -37.537  |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 158.000 / 141.290  | -16.710  |
| 19  | 1 / 1                | 0     | 3 / 13             | 10    | 162.532 / 620.308  | 457.776  |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 6 / 6              | 0     | 373.515 / 366.609  | -6.906   |
| 26  | 2 / 1                | -1    | 11 / 2             | -9    | 714.010 / 114.709  | -599.300 |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 1082.895 / 687.436 | -395.459 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 771.853 / 697.747   | -74.106  | -9.601  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 197.624 / 177.483   | -20.141  | -10.191 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 259.645 / 257.493   | -2.152   | -0.829  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1038.109 / 990.084  | -48.025  | -4.626  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 972.278 / 987.027   | 14.749   | 1.517   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1257.361 / 1125.321 | -132.040 | -10.501 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1254.924 / 1148.391 | -106.532 | -8.489  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1228.255 / 1195.793 | -32.463  | -2.643  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1432.332 / 1128.613 | -303.719 | -21.204 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 957.865 / 760.687   | -197.178 | -20.585 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 502.720 / 425.129   | -77.591  | -15.434 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 217.142 / 165.307   | -51.835  | -23.871 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 643.345 / 582.542   | -60.803  | -9.451  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1553.553 / 1370.679 | -182.874 | -11.771 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 817.940 / 1256.793  | 438.854  | 53.654  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 724.718 / 795.099   | 70.381   | 9.712   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 899.652 / 844.197   | -55.455  | -6.164  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1570.412 / 1493.796 | -76.616  | -4.879  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 828.547 / 771.313   | -57.233  | -6.908  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 750.567 / 763.533   | 12.966   | 1.728   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 548.025 / 519.992   | -28.033  | -5.115  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 173.281 / 182.075   | 8.794    | 5.075   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 333.987 / 258.243   | -75.744  | -22.679 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 883.114 / 830.242   | -52.871  | -5.987  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1198.841 / 957.409  | -241.432 | -20.139 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1684.811 / 1468.298 | -216.514 | -12.851 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 879.623 / 781.304   | -98.319  | -11.177 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 1077.337 / 905.797  | -171.540 | -15.923 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 2045.208 / 1520.555 | -524.653 | -25.653 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 782.914 / 730.674   | -52.241  | -6.673  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 661.378 / 594.050   | -67.328  | -10.180 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 538.073 / 479.112   | -58.961  | -10.958 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 311.572 / 250.271   | -61.300  | -19.675 | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 519.524 / 472.522   | 771.853 / 697.747   | 252.330 / 225.225 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7265,"dispatchToBlockedOrPreparationMs":394.2918,"firstNewGenerationToCleanupDrainedMs":252.9548,"firstPhysicalMutationToFirstNewGenerationMs":120.8803,"presentationToStrictCompletionMs":252.3298}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0479,"dispatchToBlockedOrPreparationMs":350.0684,"firstNewGenerationToCleanupDrainedMs":225.9804,"firstPhysicalMutationToFirstNewGenerationMs":118.6504,"presentationToStrictCompletionMs":225.225}    |
| 2   | 197.624 / 177.483   | 197.624 / 177.483   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":197.6237,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.4832,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 259.645 / 257.493   | 259.645 / 257.493   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":259.6454,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":257.4931,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 803.023 / 758.801   | 943.393 / 904.945   | 140.370 / 146.144 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8405,"dispatchToBlockedOrPreparationMs":393.8367,"firstNewGenerationToCleanupDrainedMs":184.6964,"firstPhysicalMutationToFirstNewGenerationMs":361.0196,"presentationToStrictCompletionMs":235.0857}   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9947,"dispatchToBlockedOrPreparationMs":360.3242,"firstNewGenerationToCleanupDrainedMs":193.0035,"firstPhysicalMutationToFirstNewGenerationMs":348.6226,"presentationToStrictCompletionMs":231.2829}   |
| 5   | 756.751 / 762.040   | 886.125 / 898.021   | 129.374 / 135.981 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8012,"dispatchToBlockedOrPreparationMs":365.3724,"firstNewGenerationToCleanupDrainedMs":173.592,"firstPhysicalMutationToFirstNewGenerationMs":343.3596,"presentationToStrictCompletionMs":215.527}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.76,"dispatchToBlockedOrPreparationMs":358.469,"firstNewGenerationToCleanupDrainedMs":181.2246,"firstPhysicalMutationToFirstNewGenerationMs":354.5673,"presentationToStrictCompletionMs":224.9864}      |
| 6   | 1031.045 / 908.790  | 1171.465 / 1041.346 | 140.420 / 132.556 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6885,"dispatchToBlockedOrPreparationMs":446.7609,"firstNewGenerationToCleanupDrainedMs":193.075,"firstPhysicalMutationToFirstNewGenerationMs":527.941,"presentationToStrictCompletionMs":226.3152}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7777,"dispatchToBlockedOrPreparationMs":371.3996,"firstNewGenerationToCleanupDrainedMs":174.592,"firstPhysicalMutationToFirstNewGenerationMs":491.5763,"presentationToStrictCompletionMs":216.5307}    |
| 7   | 974.690 / 924.311   | 1158.145 / 1065.545 | 183.454 / 141.234 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5767,"dispatchToBlockedOrPreparationMs":375.1493,"firstNewGenerationToCleanupDrainedMs":231.5996,"firstPhysicalMutationToFirstNewGenerationMs":547.8189,"presentationToStrictCompletionMs":280.2336}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6605,"dispatchToBlockedOrPreparationMs":392.5265,"firstNewGenerationToCleanupDrainedMs":182.9429,"firstPhysicalMutationToFirstNewGenerationMs":486.4149,"presentationToStrictCompletionMs":224.0807}   |
| 8   | 989.882 / 963.668   | 1145.327 / 1100.544 | 155.445 / 136.876 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1262,"dispatchToBlockedOrPreparationMs":397.3086,"firstNewGenerationToCleanupDrainedMs":202.4733,"firstPhysicalMutationToFirstNewGenerationMs":540.4192,"presentationToStrictCompletionMs":238.3733}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0819,"dispatchToBlockedOrPreparationMs":399.8201,"firstNewGenerationToCleanupDrainedMs":182.6845,"firstPhysicalMutationToFirstNewGenerationMs":514.9579,"presentationToStrictCompletionMs":232.1241}   |
| 9   | 1177.624 / 921.524  | 1337.095 / 1048.488 | 159.471 / 126.963 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0036,"dispatchToBlockedOrPreparationMs":492.9627,"firstNewGenerationToCleanupDrainedMs":222.5509,"firstPhysicalMutationToFirstNewGenerationMs":616.5778,"presentationToStrictCompletionMs":254.7074}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7925,"dispatchToBlockedOrPreparationMs":406.9526,"firstNewGenerationToCleanupDrainedMs":168.8756,"firstPhysicalMutationToFirstNewGenerationMs":467.8669,"presentationToStrictCompletionMs":207.0885}   |
| 10  | 734.019 / 591.581   | 957.865 / 760.687   | 223.846 / 169.106 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3981,"dispatchToBlockedOrPreparationMs":428.804,"firstNewGenerationToCleanupDrainedMs":310.7936,"firstPhysicalMutationToFirstNewGenerationMs":213.8696,"presentationToStrictCompletionMs":223.8465}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7673,"dispatchToBlockedOrPreparationMs":353.5765,"firstNewGenerationToCleanupDrainedMs":217.3099,"firstPhysicalMutationToFirstNewGenerationMs":185.0336,"presentationToStrictCompletionMs":169.1063}   |
| 11  | 204.491 / 165.343   | 502.720 / 425.129   | 298.230 / 259.787 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6884,"dispatchToBlockedOrPreparationMs":103.1938,"firstNewGenerationToCleanupDrainedMs":299.5075,"firstPhysicalMutationToFirstNewGenerationMs":95.3306,"presentationToStrictCompletionMs":298.2296}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0572,"dispatchToBlockedOrPreparationMs":123.3176,"firstNewGenerationToCleanupDrainedMs":260.6446,"firstPhysicalMutationToFirstNewGenerationMs":37.1097,"presentationToStrictCompletionMs":259.7866}    |
| 12  | 217.142 / 165.307   | 217.142 / 165.307   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":217.142,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":165.307,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 643.345 / 582.542   | 643.345 / 582.542   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":55.5131,"dispatchToBlockedOrPreparationMs":449.6862,"firstNewGenerationToCleanupDrainedMs":42.4529,"firstPhysicalMutationToFirstNewGenerationMs":95.6928,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":50.8977,"dispatchToBlockedOrPreparationMs":390.5072,"firstNewGenerationToCleanupDrainedMs":45.281,"firstPhysicalMutationToFirstNewGenerationMs":95.8559,"presentationToStrictCompletionMs":0}            |
| 14  | 1395.573 / 1370.679 | 1498.313 / 1324.611 | 102.740 / 0       | {"blockedOrPreparationToFirstPhysicalMutationMs":299.5089,"dispatchToBlockedOrPreparationMs":465.0504,"firstNewGenerationToCleanupDrainedMs":206.9776,"firstPhysicalMutationToFirstNewGenerationMs":526.7758,"presentationToStrictCompletionMs":157.9802} | {"blockedOrPreparationToFirstPhysicalMutationMs":305.751,"dispatchToBlockedOrPreparationMs":424.6356,"firstNewGenerationToCleanupDrainedMs":165.0926,"firstPhysicalMutationToFirstNewGenerationMs":429.132,"presentationToStrictCompletionMs":0}          |
| 15  | 817.940 / 1256.793  | 641.878 / 1083.699  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.3628,"dispatchToBlockedOrPreparationMs":354.2863,"firstNewGenerationToCleanupDrainedMs":44.36,"firstPhysicalMutationToFirstNewGenerationMs":237.8689,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":281.994,"dispatchToBlockedOrPreparationMs":394.7142,"firstNewGenerationToCleanupDrainedMs":41.5369,"firstPhysicalMutationToFirstNewGenerationMs":365.4539,"presentationToStrictCompletionMs":0}          |
| 16  | 724.718 / 795.099   | 633.573 / 604.079   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.7923,"dispatchToBlockedOrPreparationMs":388.2959,"firstNewGenerationToCleanupDrainedMs":42.611,"firstPhysicalMutationToFirstNewGenerationMs":196.8739,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4423,"dispatchToBlockedOrPreparationMs":344.7541,"firstNewGenerationToCleanupDrainedMs":44.7263,"firstPhysicalMutationToFirstNewGenerationMs":211.1566,"presentationToStrictCompletionMs":0}           |
| 17  | 899.652 / 844.197   | 741.693 / 702.346   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2518,"dispatchToBlockedOrPreparationMs":419.9445,"firstNewGenerationToCleanupDrainedMs":46.7604,"firstPhysicalMutationToFirstNewGenerationMs":269.736,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.6416,"dispatchToBlockedOrPreparationMs":419.1894,"firstNewGenerationToCleanupDrainedMs":47.4633,"firstPhysicalMutationToFirstNewGenerationMs":230.0514,"presentationToStrictCompletionMs":0}           |
| 18  | 1570.412 / 1493.796 | 1303.027 / 1126.684 | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":320.1174,"dispatchToBlockedOrPreparationMs":440.0657,"firstNewGenerationToCleanupDrainedMs":53.7404,"firstPhysicalMutationToFirstNewGenerationMs":489.1037,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":275.2212,"dispatchToBlockedOrPreparationMs":429.5467,"firstNewGenerationToCleanupDrainedMs":48.8497,"firstPhysicalMutationToFirstNewGenerationMs":373.0666,"presentationToStrictCompletionMs":0}         |
| 19  | 828.547 / 771.313   | 658.593 / 640.714   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8719,"dispatchToBlockedOrPreparationMs":365.5769,"firstNewGenerationToCleanupDrainedMs":49.7613,"firstPhysicalMutationToFirstNewGenerationMs":238.3832,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5632,"dispatchToBlockedOrPreparationMs":388.3442,"firstNewGenerationToCleanupDrainedMs":44.5207,"firstPhysicalMutationToFirstNewGenerationMs":203.2858,"presentationToStrictCompletionMs":0}           |
| 20  | 501.087 / 560.505   | 750.567 / 763.533   | 249.480 / 203.027 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9704,"dispatchToBlockedOrPreparationMs":372.8314,"firstNewGenerationToCleanupDrainedMs":249.5907,"firstPhysicalMutationToFirstNewGenerationMs":123.1741,"presentationToStrictCompletionMs":249.4798}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8874,"dispatchToBlockedOrPreparationMs":354.7334,"firstNewGenerationToCleanupDrainedMs":254.5382,"firstPhysicalMutationToFirstNewGenerationMs":149.3737,"presentationToStrictCompletionMs":203.0274}   |
| 21  | 254.349 / 222.925   | 548.025 / 519.992   | 293.676 / 297.067 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":150.7232,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":293.6762}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":143.4389,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":297.0673}             |
| 22  | 173.281 / 182.075   | 173.281 / 182.075   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":173.2812,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":182.0753,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 333.987 / 258.243   | 333.987 / 258.243   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":333.9867,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":258.243,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 24  | 679.878 / 656.731   | 883.114 / 830.242   | 203.236 / 173.511 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6665,"dispatchToBlockedOrPreparationMs":446.5848,"firstNewGenerationToCleanupDrainedMs":251.3625,"firstPhysicalMutationToFirstNewGenerationMs":180.4998,"presentationToStrictCompletionMs":203.236}    | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9576,"dispatchToBlockedOrPreparationMs":464.0565,"firstNewGenerationToCleanupDrainedMs":215.4267,"firstPhysicalMutationToFirstNewGenerationMs":147.8015,"presentationToStrictCompletionMs":173.5111}   |
| 25  | 952.259 / 742.117   | 1107.152 / 873.556  | 154.893 / 131.440 | {"blockedOrPreparationToFirstPhysicalMutationMs":70.1894,"dispatchToBlockedOrPreparationMs":442.3875,"firstNewGenerationToCleanupDrainedMs":205.4701,"firstPhysicalMutationToFirstNewGenerationMs":389.1053,"presentationToStrictCompletionMs":246.5821}  | {"blockedOrPreparationToFirstPhysicalMutationMs":26.7315,"dispatchToBlockedOrPreparationMs":349.6094,"firstNewGenerationToCleanupDrainedMs":173.2963,"firstPhysicalMutationToFirstNewGenerationMs":323.9192,"presentationToStrictCompletionMs":215.2918}  |
| 26  | 1516.839 / 1333.261 | 1629.586 / 1420.786 | 112.747 / 87.524  | {"blockedOrPreparationToFirstPhysicalMutationMs":302.4972,"dispatchToBlockedOrPreparationMs":495.5607,"firstNewGenerationToCleanupDrainedMs":216.8519,"firstPhysicalMutationToFirstNewGenerationMs":614.6761,"presentationToStrictCompletionMs":167.9725} | {"blockedOrPreparationToFirstPhysicalMutationMs":284.7751,"dispatchToBlockedOrPreparationMs":436.6673,"firstNewGenerationToCleanupDrainedMs":178.4377,"firstPhysicalMutationToFirstNewGenerationMs":520.9055,"presentationToStrictCompletionMs":135.0364} |
| 27  | 613.064 / 550.550   | 879.623 / 781.304   | 266.559 / 230.755 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2975,"dispatchToBlockedOrPreparationMs":405.8918,"firstNewGenerationToCleanupDrainedMs":267.7948,"firstPhysicalMutationToFirstNewGenerationMs":200.6391,"presentationToStrictCompletionMs":266.5594}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4852,"dispatchToBlockedOrPreparationMs":401.9437,"firstNewGenerationToCleanupDrainedMs":231.4649,"firstPhysicalMutationToFirstNewGenerationMs":144.4103,"presentationToStrictCompletionMs":230.7545}   |
| 28  | 915.326 / 816.038   | 1027.137 / 859.556  | 111.810 / 43.517  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9692,"dispatchToBlockedOrPreparationMs":479.2482,"firstNewGenerationToCleanupDrainedMs":204.8266,"firstPhysicalMutationToFirstNewGenerationMs":339.0928,"presentationToStrictCompletionMs":162.0104}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3751,"dispatchToBlockedOrPreparationMs":397.3447,"firstNewGenerationToCleanupDrainedMs":171.9637,"firstPhysicalMutationToFirstNewGenerationMs":286.8724,"presentationToStrictCompletionMs":89.7583}    |
| 29  | 1814.022 / 1282.281 | 1960.135 / 1438.319 | 146.113 / 156.038 | {"blockedOrPreparationToFirstPhysicalMutationMs":734.4687,"dispatchToBlockedOrPreparationMs":468.2664,"firstNewGenerationToCleanupDrainedMs":194.8429,"firstPhysicalMutationToFirstNewGenerationMs":562.5572,"presentationToStrictCompletionMs":231.1859} | {"blockedOrPreparationToFirstPhysicalMutationMs":23.5635,"dispatchToBlockedOrPreparationMs":703.5446,"firstNewGenerationToCleanupDrainedMs":202.6941,"firstPhysicalMutationToFirstNewGenerationMs":508.5164,"presentationToStrictCompletionMs":238.2744}  |
| 30  | 499.941 / 503.869   | 782.914 / 730.674   | 282.973 / 226.805 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9882,"dispatchToBlockedOrPreparationMs":368.1975,"firstNewGenerationToCleanupDrainedMs":283.5744,"firstPhysicalMutationToFirstNewGenerationMs":127.1543,"presentationToStrictCompletionMs":282.9732}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2535,"dispatchToBlockedOrPreparationMs":391.7451,"firstNewGenerationToCleanupDrainedMs":227.109,"firstPhysicalMutationToFirstNewGenerationMs":108.566,"presentationToStrictCompletionMs":226.8046}     |
| 31  | 661.378 / 594.050   | 661.378 / 594.050   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":63.6418,"dispatchToBlockedOrPreparationMs":449.8247,"firstNewGenerationToCleanupDrainedMs":46.7498,"firstPhysicalMutationToFirstNewGenerationMs":101.1616,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0265,"dispatchToBlockedOrPreparationMs":404.5485,"firstNewGenerationToCleanupDrainedMs":50.1418,"firstPhysicalMutationToFirstNewGenerationMs":90.333,"presentationToStrictCompletionMs":0}            |
| 32  | 233.967 / 192.354   | 538.073 / 479.112   | 304.106 / 286.758 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":135.8276,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":304.1062}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.1379,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":286.7583}             |
| 33  | 311.572 / 250.271   | 311.572 / 250.271   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":311.5717,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2714,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 10                   | 1            | 518.899 / 471.767    | -47.132  | 14 / 16           | 2            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 12 / 12                  | 0            | 758.697 / 711.942    | -46.755  | 17 / 17           | 0            |
| 5   | 13 / 13                  | 0            | 712.533 / 716.796    | 4.263    | 18 / 18           | 0            |
| 6   | 18 / 17                  | -1           | 978.390 / 866.754    | -111.637 | 23 / 22           | -1           |
| 7   | 16 / 17                  | 1            | 926.545 / 882.602    | -43.943  | 21 / 22           | 1            |
| 8   | 16 / 18                  | 2            | 942.854 / 917.860    | -24.994  | 21 / 23           | 2            |
| 9   | 16 / 18                  | 2            | 1114.544 / 879.612   | -234.932 | 21 / 23           | 2            |
| 10  | 9 / 10                   | 1            | 647.072 / 543.377    | -103.694 | 14 / 16           | 2            |
| 11  | 4 / 3                    | -1           | 203.213 / 164.484    | -38.728  | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 13  | 9 / 9                    | 0            | 600.892 / 537.261    | -63.631  | 10 / 10           | 0            |
| 14  | 24 / 22                  | -2           | 1291.335 / 1159.519  | -131.816 | 29 / 27           | -2           |
| 15  | 12 / 22                  | 10           | 597.518 / 1042.162   | 444.644  | 17 / 27           | 10           |
| 16  | 12 / 12                  | 0            | 590.962 / 559.353    | -31.609  | 15 / 17           | 2            |
| 17  | 12 / 12                  | 0            | 694.932 / 654.882    | -40.050  | 16 / 16           | 0            |
| 18  | 22 / 22                  | 0            | 1249.287 / 1077.834  | -171.452 | 28 / 31           | 3            |
| 19  | 11 / 12                  | 1            | 608.832 / 596.193    | -12.639  | 15 / 16           | 1            |
| 20  | 10 / 9                   | -1           | 500.976 / 508.995    | 8.019    | 15 / 14           | -1           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 5 / 3             | -2           |
| 24  | 9 / 10                   | 1            | 631.751 / 614.816    | -16.935  | 14 / 15           | 1            |
| 25  | 13 / 12                  | -1           | 901.682 / 700.260    | -201.422 | 18 / 17           | -1           |
| 26  | 24 / 23                  | -1           | 1412.734 / 1242.348  | -170.386 | 29 / 28           | -1           |
| 27  | 10 / 11                  | 1            | 611.828 / 549.839    | -61.989  | 16 / 17           | 1            |
| 28  | 11 / 11                  | 0            | 822.310 / 687.592    | -134.718 | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1765.292 / 1235.624  | -529.668 | 34 / 28           | -6           |
| 30  | 9 / 10                   | 1            | 499.340 / 503.565    | 4.225    | 15 / 16           | 1            |
| 31  | 9 / 9                    | 0            | 614.628 / 543.908    | -70.720  | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 221.998 / 228.557  | 6.559    |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 218.343 / 242.748  | 24.404   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 395.562 / 385.643  | -9.919   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 435.796 / 376.351  | -59.445  |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 417.755 / 404.965  | -12.790  |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 484.747 / 363.200  | -121.547 |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 321.888 / 275.831  | -46.057  |
| 15  | 1 / 1                | 0     | 3 / 13             | 10    | 156.505 / 647.943  | 491.438  |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 141.262 / 157.094  | 15.831   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 205.356 / 171.022  | -34.334  |
| 18  | 1 / 1                | 0     | 13 / 13            | 0     | 809.540 / 648.801  | -160.739 |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 183.573 / 149.211  | -34.362  |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 261.100 / 211.046  | -50.054  |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 722.255 / 638.025  | -84.230  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 1115.836 / 700.338 | -415.498 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

## Cumulative gates and other health evidence

### nvidia-20260910T214350263Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                            | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                      | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":159796,"leftPath":"NativeOriginal","referenceFrame":160181,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### nvidia-20260910T214350263Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                            | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":164115,"leftPath":"NativeOriginal","referenceFrame":164474,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":16675,"leftPath":"NativeOriginal","referenceFrame":17084,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":21403,"leftPath":"NativeOriginal","referenceFrame":21805,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                            | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | --------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16720.40234375 / 16485.47265625 / -234.9296875 | 16733.71484375 / 16496.359375 / -237.35546875 | -2.426                |
| nvidia | 1    | systemCommitMiB   | 56627.2421875 / 55807.25390625 / -819.98828125 | 55395.53125 / 55048.33984375 / -347.19140625  | 472.797               |
| nvidia | 1    | dxgiUsageMiB      | 4176.86328125 / 3565.66015625 / -611.203125    | 4285.1640625 / 3534.4765625 / -750.6875       | -139.484              |
| nvidia | 1    | liveTextures      | 0 / 241 / 241                                  | 0 / 263 / 263                                 | 22                    |
| nvidia | 1    | liveTextureMiB    | 0 / 2375.9215354919434 / 2375.9215354919434    | 0 / 2402.765727996826 / 2402.765727996826     | 26.844                |
| nvidia | 2    | processPrivateMiB | 16859.75 / 16633.83984375 / -225.91015625      | 16822.87109375 / 16603.328125 / -219.54296875 | 6.367                 |
| nvidia | 2    | systemCommitMiB   | 56129.0625 / 56480.27734375 / 351.21484375     | 55520.37890625 / 55103.39453125 / -416.984375 | -768.199              |
| nvidia | 2    | dxgiUsageMiB      | 3898.15234375 / 3604.05078125 / -294.1015625   | 3822.21484375 / 3647.90234375 / -174.3125     | 119.789               |
| nvidia | 2    | liveTextures      | 0 / 233 / 233                                  | 0 / 236 / 236                                 | 3                     |
| nvidia | 2    | liveTextureMiB    | 0 / 2349.4316596984863 / 2349.4316596984863    | 0 / 2358.7650413513184 / 2358.7650413513184   | 9.333                 |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2184        | 2294        | 110        |
| cpu/compactPresentationContract/reuses                  | 2162        | 2272        | 110        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 71          | 1          |
| cpu/generationResourceValidation/contractPublishes      | 154         | 145         | -9         |
| cpu/generationResourceValidation/fullValidations        | 582         | 567         | -15        |
| cpu/generationResourceValidation/stableChecks           | 8304        | 8647        | 343        |
| cpu/generationResourceValidation/stableHits             | 8227        | 8568        | 341        |
| cpu/generationResourceValidation/stableMisses           | 77          | 79          | 2          |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 1           | 1           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4211        | 4457        | 246        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4211        | 4457        | 246        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4169        | 4415        | 246        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4211        | 4457        | 246        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4190        | 4436        | 246        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 35          | 1          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4177        | 4422        | 245        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4118        | 4362        | 244        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 94          | 0          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4196        | 4442        | 246        |
| cpu/strongStereoPacket/captures                         | 4540        | 4749        | 209        |
| cpu/strongStereoPacket/commitAccepts                    | 4347        | 4570        | 223        |
| cpu/strongStereoPacket/commitRejects                    | 67          | 64          | -3         |
| cpu/strongStereoPacket/commitValidations                | 4414        | 4634        | 220        |
| cpu/strongStereoPacket/cycleReuses                      | 2229        | 2335        | 106        |
| cpu/strongStereoPacket/fastSkips                        | 3882        | 4165        | 283        |
| cpu/strongStereoPacket/invalidations                    | 4770        | 5009        | 239        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | 101         | -3         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2207        | 2313        | 106        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.855       | 1.630       | -0.225     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 24          | 26.800      | 2.800      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.136       | 0.108       | -0.028     |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.500       | 1.400       | -0.100     |
| cpu/window/currentFrame                                 | 160182      | 17085       | -143097    |
| cpu/window/elapsedFrames                                | 4212        | 4456        | 244        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 155970      | 12629       | -143341    |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 160182      | 17086       | -143096    |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6072400224  | 6408627984  | 336227760  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4786        | 5051        | 265        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12157205760 | 12830348160 | 673142400  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.308       | 0.002      |
| gpu/item5ActiveFSRCopies/activePixels                   | 11703352280 | 12453511360 | 750159080  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26551457320 | 28036639040 | 1485181720 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15060       | 15940       | 880        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1987        | 2105        | 118        |
| gpu/item7EarlyHAM/executedClears                        | 2002        | 2116        | 114        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2002        | 2116        | 114        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4148        | 4380        | 232        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4212        | 4457        | 245        |
| gpu/startFrame                                          | 155970      | 12629       | -143341    |
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
| texture/createdCount                                    | 3969        | 3962        | -7         |
| texture/createdEstimatedBytes                           | 38791801128 | 38741091672 | -50709456  |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3728        | 3699        | -29        |
| texture/destroyedEstimatedBytes                         | 36300466828 | 36221609196 | -78857632  |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 893         | 893         | 0          |
| texture/liveTextureRecordCount                          | 241         | 263         | 22         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 32          | 54          | 22         |
| texture/niSourceTextureMatchedEstimatedBytes            | 115682720   | 143830896   | 28148176   |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1482        | 1504        | 22         |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 241         | 263         | 22         |
| texture/outstandingEstimatedBytes                       | 2491334300  | 2519482476  | 28148176   |
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
| cpu/compactPresentationContract/publishes               | 2032        | 2249        | 217        |
| cpu/compactPresentationContract/reuses                  | 2010        | 2227        | 217        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 147         | 151         | 4          |
| cpu/generationResourceValidation/fullValidations        | 566         | 584         | 18         |
| cpu/generationResourceValidation/stableChecks           | 7790        | 8533        | 743        |
| cpu/generationResourceValidation/stableHits             | 7713        | 8456        | 743        |
| cpu/generationResourceValidation/stableMisses           | 77          | 77          | 0          |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 2           | 2           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 3911        | 4313        | 402        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 3911        | 4313        | 402        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 3869        | 4271        | 402        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 3911        | 4313        | 402        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 3890        | 4292        | 402        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 3877        | 4279        | 402        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 3816        | 4215        | 399        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 98          | 4          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 3896        | 4298        | 402        |
| cpu/strongStereoPacket/captures                         | 4210        | 4646        | 436        |
| cpu/strongStereoPacket/commitAccepts                    | 4045        | 4479        | 434        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 65          | 0          |
| cpu/strongStereoPacket/commitValidations                | 4110        | 4544        | 434        |
| cpu/strongStereoPacket/cycleReuses                      | 2064        | 2283        | 219        |
| cpu/strongStereoPacket/fastSkips                        | 3612        | 3980        | 368        |
| cpu/strongStereoPacket/invalidations                    | 4455        | 4854        | 399        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 102         | -1         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2043        | 2261        | 218        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.966       | 1.673       | -0.293     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 18.100      | 64.200      | 46.100     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.141       | 0.106       | -0.036     |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 2.500       | 1.200       | -1.300     |
| cpu/window/currentFrame                                 | 164474      | 21805       | -142669    |
| cpu/window/elapsedFrames                                | 3910        | 4313        | 403        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 160564      | 17492       | -143072    |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 164476      | 21807       | -142669    |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5591531088  | 6208160112  | 616629024  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4407        | 4893        | 486        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11194485120 | 12429002880 | 1234517760 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.315       | 0.307       | -0.008     |
| gpu/item5ActiveFSRCopies/activePixels                   | 11319119960 | 12004040680 | 684920720  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 24624144040 | 27063620120 | 2439476080 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14150       | 15380       | 1230       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1847        | 2027        | 180        |
| gpu/item7EarlyHAM/executedClears                        | 1826        | 2072        | 246        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1826        | 2072        | 246        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 3834        | 4260        | 426        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 3912        | 4315        | 403        |
| gpu/startFrame                                          | 160564      | 17492       | -143072    |
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
| texture/createdCount                                    | 3964        | 3965        | 1          |
| texture/createdEstimatedBytes                           | 38844584736 | 38833321536 | -11263200  |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3731        | 3729        | -2         |
| texture/destroyedEstimatedBytes                         | 36381027084 | 36359977124 | -21049960  |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 885         | 887         | 2          |
| texture/liveTextureRecordCount                          | 233         | 236         | 3          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 26          | 29          | 3          |
| texture/niSourceTextureMatchedEstimatedBytes            | 87906272    | 97693032    | 9786760    |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1502        | 1509        | 7          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 233         | 236         | 3          |
| texture/outstandingEstimatedBytes                       | 2463557652  | 2473344412  | 9786760    |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 2           | 2           | 0          |
| texture/supported                                       | true        | true        | n/a        |

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
