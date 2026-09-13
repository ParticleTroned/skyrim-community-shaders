# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                        | Candidate                                                                                                       |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z                                                              | renderscale-tuning-nvidia-2026-09-13T18-46-33-800Z                                                              |
| Renderer base           | 554e484e3957178e2d144bf35266bfdcc0948642                                                                        | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        |
| Main-VR base/equivalent | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                        | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        |
| Compiled source         | 554e484e3957178e2d144bf35266bfdcc0948642                                                                        | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        |
| Build ID                | e7e9fa2d13f28c6727b9567741511a418c54113c190db2616bfb2c2ad1158d96                                                | 2f6432ddbcbd05826c1fb1ccd66f25e14755d718a5e760a58cbae58b7f625efd                                                |
| DLL SHA-256             | 5bb50a859ccb62f00d20669a9ad24b6b704dc076054e86dfa8af9dda9db8bfac                                                | da3a9b54ee34dee836e3a2843d33acb8d33e49ab5754be06c05f37e038fbcdb0                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-13T18-46-33-800Z |

Assessment limits: retained_context_not_matched:adapter; retained_context_not_matched:scene; retained_context_not_matched:dependencies; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 815.094/917.809 | 12.602       | 15/15       | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 768.534/897.031 | 16.720       | 15/15       | 0/0          | 0/0                 | none             | MET/MET             |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 736.858   | 845.346   | 108.488  | 14.723  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.200    | 13.200    | 0        | 0       |
| nvidia | 1    | Relatch proof total        | ms          | 18421.450 | 21133.656 | 2712.206 | 14.723  |
| nvidia | 1    | Relatch proof total        | frames      | 330       | 330       | 0        | 0       |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 815.094   | 917.809   | 102.715  | 12.602  |
| nvidia | 1    | Strict completion mean     | frames      | 15.182    | 14.848    | -0.333   | -2.196  |
| nvidia | 1    | Strict completion total    | ms          | 26898.109 | 30287.710 | 3389.601 | 12.602  |
| nvidia | 1    | Strict completion total    | frames      | 501       | 490       | -11      | -2.196  |
| nvidia | 1    | Stretch completed episodes | episodes    | 18        | 19        | 1        | 5.556   |
| nvidia | 1    | Stretch completed total    | frames      | 62        | 64        | 2        | 3.226   |
| nvidia | 1    | Stretch completed total    | ms          | 4080.674  | 4716.293  | 635.619  | 15.576  |
| nvidia | 1    | Stretch longest episode    | ms          | 345.476   | 384.767   | 39.291   | 11.373  |
| nvidia | 2    | Relatch proof mean         | ms          | 698.557   | 813.458   | 114.901  | 16.448  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.200    | 13.040    | -0.160   | -1.212  |
| nvidia | 2    | Relatch proof total        | ms          | 17463.914 | 20336.445 | 2872.531 | 16.448  |
| nvidia | 2    | Relatch proof total        | frames      | 330       | 326       | -4       | -1.212  |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 768.534   | 897.031   | 128.496  | 16.720  |
| nvidia | 2    | Strict completion mean     | frames      | 14.818    | 14.576    | -0.242   | -1.636  |
| nvidia | 2    | Strict completion total    | ms          | 25361.631 | 29602.010 | 4240.378 | 16.720  |
| nvidia | 2    | Strict completion total    | frames      | 489       | 481       | -8       | -1.636  |
| nvidia | 2    | Stretch completed episodes | episodes    | 18        | 18        | 0        | 0       |
| nvidia | 2    | Stretch completed total    | frames      | 62        | 62        | 0        | 0       |
| nvidia | 2    | Stretch completed total    | ms          | 4085.360  | 4713.132  | 627.772  | 15.366  |
| nvidia | 2    | Stretch longest episode    | ms          | 354.159   | 422.739   | 68.580   | 19.364  |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 776.411 / 865.321   | 88.910   | 11.451  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 179.985 / 205.391   | 25.406   | 14.116  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 309.025 / 287.683   | -21.342  | -6.906  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1213.636 / 1195.595 | -18.041  | -1.487  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1380.573 / 1412.836 | 32.264   | 2.337   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1177.831 / 1514.293 | 336.461  | 28.566  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1423.853 / 1537.476 | 113.622  | 7.980   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1398.222 / 1502.831 | 104.608  | 7.482   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1367.283 / 1416.121 | 48.838   | 3.572   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 798.935 / 950.429   | 151.495  | 18.962  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 465.233 / 530.933   | 65.700   | 14.122  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 175.991 / 207.161   | 31.169   | 17.711  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 921.078 / 872.516   | -48.562  | -5.272  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1155.429 / 1318.280 | 162.851  | 14.094  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 1002.783 / 989.509  | -13.274  | -1.324  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 1007.986 / 1044.912 | 36.926   | 3.663   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 769.460 / 1015.247  | 245.787  | 31.943  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 786.131 / 994.249   | 208.118  | 26.474  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 1223.303 / 1116.058 | -107.244 | -8.767  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 953.041 / 1180.889  | 227.848  | 23.907  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 467.713 / 565.780   | 98.068   | 20.968  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 158.622 / 206.011   | 47.389   | 29.875  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 240.633 / 291.841   | 51.208   | 21.280  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 725.341 / 921.320   | 195.979  | 27.019  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1127.028 / 1219.526 | 92.497   | 8.207   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1009.984 / 1177.035 | 167.050  | 16.540  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 727.177 / 856.734   | 129.557  | 17.816  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 884.295 / 1101.299  | 217.004  | 24.540  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1053.923 / 1240.951 | 187.028  | 17.746  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 701.801 / 892.782   | 190.981  | 27.213  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 606.601 / 725.622   | 119.021  | 19.621  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 443.467 / 588.114   | 144.647  | 32.617  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 265.332 / 342.964   | 77.632   | 29.258  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 593.317 / 649.901   | 776.411 / 865.321   | 183.094 / 215.420 | {"blockedOrPreparationToFirstPhysicalMutationMs":340.9232,"dispatchToBlockedOrPreparationMs":91.2217,"firstNewGenerationToCleanupDrainedMs":249.0554,"firstPhysicalMutationToFirstNewGenerationMs":95.2109,"presentationToStrictCompletionMs":183.094}   | {"blockedOrPreparationToFirstPhysicalMutationMs":56.066,"dispatchToBlockedOrPreparationMs":416.6559,"firstNewGenerationToCleanupDrainedMs":273.7023,"firstPhysicalMutationToFirstNewGenerationMs":118.897,"presentationToStrictCompletionMs":215.4199}   |
| 2   | 179.985 / 205.391   | 179.985 / 205.391   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":90.3612,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":205.3914,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 309.025 / 287.683   | 309.025 / 287.683   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":96.4899,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":287.6829,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 984.178 / 1000.028  | 1106.032 / 1102.722 | 121.855 / 102.694 | {"blockedOrPreparationToFirstPhysicalMutationMs":52.7617,"dispatchToBlockedOrPreparationMs":414.2681,"firstNewGenerationToCleanupDrainedMs":228.2539,"firstPhysicalMutationToFirstNewGenerationMs":410.7485,"presentationToStrictCompletionMs":229.4586} | {"blockedOrPreparationToFirstPhysicalMutationMs":56.0743,"dispatchToBlockedOrPreparationMs":468.7376,"firstNewGenerationToCleanupDrainedMs":206.8418,"firstPhysicalMutationToFirstNewGenerationMs":371.0682,"presentationToStrictCompletionMs":195.5675} |
| 5   | 1126.045 / 1165.835 | 1274.179 / 1321.772 | 148.134 / 155.936 | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7797,"dispatchToBlockedOrPreparationMs":390.0022,"firstNewGenerationToCleanupDrainedMs":192.3497,"firstPhysicalMutationToFirstNewGenerationMs":636.047,"presentationToStrictCompletionMs":254.5278}  | {"blockedOrPreparationToFirstPhysicalMutationMs":56.0452,"dispatchToBlockedOrPreparationMs":451.8102,"firstNewGenerationToCleanupDrainedMs":208.5631,"firstPhysicalMutationToFirstNewGenerationMs":605.3531,"presentationToStrictCompletionMs":247.0013} |
| 6   | 939.787 / 1316.770  | 1085.005 / 1424.432 | 145.218 / 107.662 | {"blockedOrPreparationToFirstPhysicalMutationMs":49.9869,"dispatchToBlockedOrPreparationMs":380.9207,"firstNewGenerationToCleanupDrainedMs":193.4445,"firstPhysicalMutationToFirstNewGenerationMs":460.653,"presentationToStrictCompletionMs":238.0445}  | {"blockedOrPreparationToFirstPhysicalMutationMs":0.5339,"dispatchToBlockedOrPreparationMs":474.9851,"firstNewGenerationToCleanupDrainedMs":212.7671,"firstPhysicalMutationToFirstNewGenerationMs":736.146,"presentationToStrictCompletionMs":197.5226}   |
| 7   | 1225.830 / 1286.168 | 1321.959 / 1448.324 | 96.129 / 162.157  | {"blockedOrPreparationToFirstPhysicalMutationMs":51.0685,"dispatchToBlockedOrPreparationMs":379.0517,"firstNewGenerationToCleanupDrainedMs":185.0477,"firstPhysicalMutationToFirstNewGenerationMs":706.7908,"presentationToStrictCompletionMs":198.0234} | {"blockedOrPreparationToFirstPhysicalMutationMs":65.2624,"dispatchToBlockedOrPreparationMs":467.6842,"firstNewGenerationToCleanupDrainedMs":214.3658,"firstPhysicalMutationToFirstNewGenerationMs":701.0118,"presentationToStrictCompletionMs":251.308}  |
| 8   | 1157.610 / 1245.985 | 1301.666 / 1412.638 | 144.055 / 166.653 | {"blockedOrPreparationToFirstPhysicalMutationMs":51.3844,"dispatchToBlockedOrPreparationMs":374.9823,"firstNewGenerationToCleanupDrainedMs":187.6664,"firstPhysicalMutationToFirstNewGenerationMs":687.6324,"presentationToStrictCompletionMs":240.6121} | {"blockedOrPreparationToFirstPhysicalMutationMs":58.534,"dispatchToBlockedOrPreparationMs":437.085,"firstNewGenerationToCleanupDrainedMs":218.3591,"firstPhysicalMutationToFirstNewGenerationMs":698.6598,"presentationToStrictCompletionMs":256.8462}   |
| 9   | 1106.020 / 1168.799 | 1248.893 / 1327.274 | 142.873 / 158.476 | {"blockedOrPreparationToFirstPhysicalMutationMs":0.5993,"dispatchToBlockedOrPreparationMs":417.7949,"firstNewGenerationToCleanupDrainedMs":185.5526,"firstPhysicalMutationToFirstNewGenerationMs":644.9458,"presentationToStrictCompletionMs":261.2633}  | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7067,"dispatchToBlockedOrPreparationMs":414.5257,"firstNewGenerationToCleanupDrainedMs":208.9769,"firstPhysicalMutationToFirstNewGenerationMs":648.065,"presentationToStrictCompletionMs":247.3226}  |
| 10  | 664.251 / 772.429   | 798.935 / 950.429   | 134.684 / 178.000 | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3436,"dispatchToBlockedOrPreparationMs":360.4407,"firstNewGenerationToCleanupDrainedMs":182.3977,"firstPhysicalMutationToFirstNewGenerationMs":208.7527,"presentationToStrictCompletionMs":134.6836} | {"blockedOrPreparationToFirstPhysicalMutationMs":54.0209,"dispatchToBlockedOrPreparationMs":453.6247,"firstNewGenerationToCleanupDrainedMs":234.0944,"firstPhysicalMutationToFirstNewGenerationMs":208.6892,"presentationToStrictCompletionMs":178.0003} |
| 11  | 176.768 / 215.232   | 465.233 / 530.933   | 288.465 / 315.701 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4611,"dispatchToBlockedOrPreparationMs":131.4955,"firstNewGenerationToCleanupDrainedMs":289.1769,"firstPhysicalMutationToFirstNewGenerationMs":41.0993,"presentationToStrictCompletionMs":288.4649}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3426,"dispatchToBlockedOrPreparationMs":164.0857,"firstNewGenerationToCleanupDrainedMs":315.9811,"firstPhysicalMutationToFirstNewGenerationMs":47.5233,"presentationToStrictCompletionMs":315.7009}   |
| 12  | 175.991 / 207.161   | 175.991 / 207.161   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":175.9914,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":207.1608,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 921.078 / 872.516   | 921.078 / 872.516   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":302.9725,"dispatchToBlockedOrPreparationMs":434.6174,"firstNewGenerationToCleanupDrainedMs":44.5645,"firstPhysicalMutationToFirstNewGenerationMs":138.9241,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":166.9928,"dispatchToBlockedOrPreparationMs":488.4619,"firstNewGenerationToCleanupDrainedMs":52.3302,"firstPhysicalMutationToFirstNewGenerationMs":164.7314,"presentationToStrictCompletionMs":0}        |
| 14  | 1000.417 / 1154.777 | 1106.483 / 1263.983 | 106.066 / 109.207 | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0613,"dispatchToBlockedOrPreparationMs":449.3138,"firstNewGenerationToCleanupDrainedMs":194.3451,"firstPhysicalMutationToFirstNewGenerationMs":413.7628,"presentationToStrictCompletionMs":155.012}  | {"blockedOrPreparationToFirstPhysicalMutationMs":62.3146,"dispatchToBlockedOrPreparationMs":491.7984,"firstNewGenerationToCleanupDrainedMs":224.7783,"firstPhysicalMutationToFirstNewGenerationMs":485.0919,"presentationToStrictCompletionMs":163.5036} |
| 15  | 1002.783 / 989.509  | 728.137 / 831.235   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2954,"dispatchToBlockedOrPreparationMs":427.4852,"firstNewGenerationToCleanupDrainedMs":44.4301,"firstPhysicalMutationToFirstNewGenerationMs":207.9264,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":55.3282,"dispatchToBlockedOrPreparationMs":487.2901,"firstNewGenerationToCleanupDrainedMs":51.5525,"firstPhysicalMutationToFirstNewGenerationMs":237.0642,"presentationToStrictCompletionMs":0}         |
| 16  | 1007.986 / 1044.912 | 748.234 / 833.007   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2088,"dispatchToBlockedOrPreparationMs":417.264,"firstNewGenerationToCleanupDrainedMs":43.3328,"firstPhysicalMutationToFirstNewGenerationMs":239.4284,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":56.5038,"dispatchToBlockedOrPreparationMs":491.1083,"firstNewGenerationToCleanupDrainedMs":51.1814,"firstPhysicalMutationToFirstNewGenerationMs":234.2137,"presentationToStrictCompletionMs":0}         |
| 17  | 769.460 / 1015.247  | 605.502 / 853.356   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":42.688,"dispatchToBlockedOrPreparationMs":373.0891,"firstNewGenerationToCleanupDrainedMs":0.3491,"firstPhysicalMutationToFirstNewGenerationMs":189.3757,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":64.1835,"dispatchToBlockedOrPreparationMs":500.559,"firstNewGenerationToCleanupDrainedMs":52.9599,"firstPhysicalMutationToFirstNewGenerationMs":235.6535,"presentationToStrictCompletionMs":0}          |
| 18  | 786.131 / 994.249   | 660.916 / 834.353   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":45.3155,"dispatchToBlockedOrPreparationMs":380.3142,"firstNewGenerationToCleanupDrainedMs":40.8901,"firstPhysicalMutationToFirstNewGenerationMs":194.3963,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":57.334,"dispatchToBlockedOrPreparationMs":481.9857,"firstNewGenerationToCleanupDrainedMs":61.6945,"firstPhysicalMutationToFirstNewGenerationMs":233.339,"presentationToStrictCompletionMs":0}           |
| 19  | 1223.303 / 1116.058 | 685.056 / 844.417   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.3383,"dispatchToBlockedOrPreparationMs":397.3388,"firstNewGenerationToCleanupDrainedMs":43.3787,"firstPhysicalMutationToFirstNewGenerationMs":195.9997,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":59.4687,"dispatchToBlockedOrPreparationMs":499.1877,"firstNewGenerationToCleanupDrainedMs":51.7148,"firstPhysicalMutationToFirstNewGenerationMs":234.0461,"presentationToStrictCompletionMs":0}         |
| 20  | 768.589 / 958.945   | 953.041 / 1180.889  | 184.452 / 221.944 | {"blockedOrPreparationToFirstPhysicalMutationMs":211.9214,"dispatchToBlockedOrPreparationMs":428.0444,"firstNewGenerationToCleanupDrainedMs":229.1844,"firstPhysicalMutationToFirstNewGenerationMs":83.8908,"presentationToStrictCompletionMs":184.4523} | {"blockedOrPreparationToFirstPhysicalMutationMs":269.2453,"dispatchToBlockedOrPreparationMs":537.5705,"firstNewGenerationToCleanupDrainedMs":281.0827,"firstPhysicalMutationToFirstNewGenerationMs":92.9907,"presentationToStrictCompletionMs":221.944}  |
| 21  | 209.143 / 238.252   | 467.713 / 565.780   | 258.569 / 327.529 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.3458,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":258.5694}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.6851,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":327.5286}            |
| 22  | 158.622 / 206.011   | 158.622 / 206.011   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":158.6221,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":206.0108,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 240.633 / 291.841   | 240.633 / 291.841   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":240.6335,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":291.8411,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 597.558 / 760.825   | 725.341 / 921.320   | 127.783 / 160.495 | {"blockedOrPreparationToFirstPhysicalMutationMs":43.0545,"dispatchToBlockedOrPreparationMs":377.1836,"firstNewGenerationToCleanupDrainedMs":167.8687,"firstPhysicalMutationToFirstNewGenerationMs":137.2345,"presentationToStrictCompletionMs":127.7829} | {"blockedOrPreparationToFirstPhysicalMutationMs":55.4801,"dispatchToBlockedOrPreparationMs":483.4348,"firstNewGenerationToCleanupDrainedMs":211.3548,"firstPhysicalMutationToFirstNewGenerationMs":171.0503,"presentationToStrictCompletionMs":160.4949} |
| 25  | 914.530 / 965.671   | 1041.725 / 1128.761 | 127.195 / 163.089 | {"blockedOrPreparationToFirstPhysicalMutationMs":24.1865,"dispatchToBlockedOrPreparationMs":494.0467,"firstNewGenerationToCleanupDrainedMs":168.1967,"firstPhysicalMutationToFirstNewGenerationMs":355.2946,"presentationToStrictCompletionMs":212.4982} | {"blockedOrPreparationToFirstPhysicalMutationMs":19.8256,"dispatchToBlockedOrPreparationMs":524.0778,"firstNewGenerationToCleanupDrainedMs":214.9312,"firstPhysicalMutationToFirstNewGenerationMs":369.9262,"presentationToStrictCompletionMs":253.8543} |
| 26  | 918.401 / 1069.360  | 965.214 / 1122.417  | 46.813 / 53.057   | {"blockedOrPreparationToFirstPhysicalMutationMs":46.6122,"dispatchToBlockedOrPreparationMs":421.3456,"firstNewGenerationToCleanupDrainedMs":170.1694,"firstPhysicalMutationToFirstNewGenerationMs":327.0868,"presentationToStrictCompletionMs":91.5829}  | {"blockedOrPreparationToFirstPhysicalMutationMs":64.0045,"dispatchToBlockedOrPreparationMs":467.3395,"firstNewGenerationToCleanupDrainedMs":207.4348,"firstPhysicalMutationToFirstNewGenerationMs":383.6382,"presentationToStrictCompletionMs":107.6747} |
| 27  | 546.985 / 635.691   | 727.177 / 856.734   | 180.192 / 221.043 | {"blockedOrPreparationToFirstPhysicalMutationMs":0.9403,"dispatchToBlockedOrPreparationMs":389.236,"firstNewGenerationToCleanupDrainedMs":233.5112,"firstPhysicalMutationToFirstNewGenerationMs":103.489,"presentationToStrictCompletionMs":180.1917}    | {"blockedOrPreparationToFirstPhysicalMutationMs":56.6709,"dispatchToBlockedOrPreparationMs":410.7407,"firstNewGenerationToCleanupDrainedMs":280.8314,"firstPhysicalMutationToFirstNewGenerationMs":108.4905,"presentationToStrictCompletionMs":221.0428} |
| 28  | 884.295 / 935.376   | 664.092 / 1048.956  | 0 / 113.581       | {"blockedOrPreparationToFirstPhysicalMutationMs":44.6346,"dispatchToBlockedOrPreparationMs":319.5049,"firstNewGenerationToCleanupDrainedMs":41.2796,"firstPhysicalMutationToFirstNewGenerationMs":258.6731,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":55.3406,"dispatchToBlockedOrPreparationMs":483.0644,"firstNewGenerationToCleanupDrainedMs":214.2546,"firstPhysicalMutationToFirstNewGenerationMs":296.2966,"presentationToStrictCompletionMs":165.9236} |
| 29  | 832.834 / 992.341   | 969.609 / 1152.741  | 136.775 / 160.400 | {"blockedOrPreparationToFirstPhysicalMutationMs":27.2958,"dispatchToBlockedOrPreparationMs":422.2767,"firstNewGenerationToCleanupDrainedMs":177.3635,"firstPhysicalMutationToFirstNewGenerationMs":342.6734,"presentationToStrictCompletionMs":221.0887} | {"blockedOrPreparationToFirstPhysicalMutationMs":18.6898,"dispatchToBlockedOrPreparationMs":571.4794,"firstNewGenerationToCleanupDrainedMs":211.67,"firstPhysicalMutationToFirstNewGenerationMs":350.9019,"presentationToStrictCompletionMs":248.6102}   |
| 30  | 526.231 / 661.395   | 701.801 / 892.782   | 175.570 / 231.387 | {"blockedOrPreparationToFirstPhysicalMutationMs":43.5348,"dispatchToBlockedOrPreparationMs":359.2,"firstNewGenerationToCleanupDrainedMs":231.5579,"firstPhysicalMutationToFirstNewGenerationMs":67.5083,"presentationToStrictCompletionMs":175.5695}     | {"blockedOrPreparationToFirstPhysicalMutationMs":55.3799,"dispatchToBlockedOrPreparationMs":471.34,"firstNewGenerationToCleanupDrainedMs":288.9922,"firstPhysicalMutationToFirstNewGenerationMs":77.0701,"presentationToStrictCompletionMs":231.3869}    |
| 31  | 606.601 / 725.622   | 606.601 / 725.622   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":63.0553,"dispatchToBlockedOrPreparationMs":403.6118,"firstNewGenerationToCleanupDrainedMs":43.5018,"firstPhysicalMutationToFirstNewGenerationMs":96.4326,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":42.8366,"dispatchToBlockedOrPreparationMs":502.7917,"firstNewGenerationToCleanupDrainedMs":62.8644,"firstPhysicalMutationToFirstNewGenerationMs":117.1297,"presentationToStrictCompletionMs":0}         |
| 32  | 189.217 / 251.784   | 443.467 / 588.114   | 254.250 / 336.330 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":116.8255,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":254.2499}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":179.75,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":336.3303}              |
| 33  | 265.332 / 342.964   | 265.332 / 342.964   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":265.3315,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":342.9635,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 9                    | 0            | 527.356 / 591.619    | 64.263   | 14 / 14           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 14                  | 0            | 877.778 / 895.880    | 18.102   | 19 / 19           | 0            |
| 5   | 14 / 13                  | -1           | 1081.829 / 1113.208  | 31.380   | 19 / 18           | -1           |
| 6   | 17 / 17                  | 0            | 891.561 / 1211.665   | 320.104  | 22 / 22           | 0            |
| 7   | 16 / 18                  | 2            | 1136.911 / 1233.958  | 97.047   | 21 / 23           | 2            |
| 8   | 17 / 16                  | -1           | 1113.999 / 1194.279  | 80.280   | 22 / 21           | -1           |
| 9   | 17 / 16                  | -1           | 1063.340 / 1118.297  | 54.957   | 22 / 21           | -1           |
| 10  | 11 / 12                  | 1            | 616.537 / 716.335    | 99.798   | 15 / 16           | 1            |
| 11  | 3 / 4                    | 1            | 176.056 / 214.952    | 38.896   | 9 / 10            | 1            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 10                  | 0            | 876.514 / 820.186    | -56.328  | 11 / 11           | 0            |
| 14  | 19 / 18                  | -1           | 912.138 / 1039.205   | 127.067  | 24 / 23           | -1           |
| 15  | 14 / 14                  | 0            | 683.707 / 779.683    | 95.976   | 21 / 18           | -3           |
| 16  | 14 / 14                  | 0            | 704.901 / 781.826    | 76.925   | 21 / 19           | -2           |
| 17  | 14 / 14                  | 0            | 605.153 / 800.396    | 195.243  | 18 / 18           | 0            |
| 18  | 13 / 14                  | 1            | 620.026 / 772.659    | 152.633  | 17 / 18           | 1            |
| 19  | 14 / 14                  | 0            | 641.677 / 792.702    | 151.026  | 27 / 20           | -7           |
| 20  | 16 / 16                  | 0            | 723.857 / 899.807    | 175.950  | 22 / 22           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 10           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 12 / 11                  | -1           | 557.473 / 709.965    | 152.493  | 16 / 15           | -1           |
| 25  | 15 / 14                  | -1           | 873.528 / 913.830    | 40.302   | 20 / 19           | -1           |
| 26  | 15 / 14                  | -1           | 795.045 / 914.982    | 119.938  | 20 / 19           | -1           |
| 27  | 10 / 10                  | 0            | 493.665 / 575.902    | 82.237   | 16 / 16           | 0            |
| 28  | 10 / 12                  | 2            | 622.813 / 834.702    | 211.889  | 16 / 17           | 1            |
| 29  | 15 / 15                  | 0            | 792.246 / 941.071    | 148.825  | 20 / 20           | 0            |
| 30  | 11 / 11                  | 0            | 470.243 / 603.790    | 133.547  | 17 / 16           | -1           |
| 31  | 10 / 10                  | 0            | 563.100 / 662.758    | 99.658   | 12 / 11           | -1           |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 5             | 2            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 0 / 1                | 1     | 0 / 2              | 2     | 0 / 175.737       | 175.737  |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 254.095 / 233.774 | -20.322  |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 217.524 / 247.736 | 30.212   |
| 6   | 1 / 1                | 0     | 5 / 5              | 0     | 337.267 / 384.767 | 47.500   |
| 7   | 1 / 1                | 0     | 5 / 5              | 0     | 345.476 / 380.561 | 35.086   |
| 8   | 1 / 1                | 0     | 5 / 5              | 0     | 337.337 / 376.174 | 38.836   |
| 9   | 1 / 1                | 0     | 5 / 5              | 0     | 343.309 / 373.364 | 30.055   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 5 / 5              | 0     | 219.261 / 265.707 | 46.446   |
| 15  | 1 / 1                | 0     | 4 / 4              | 0     | 207.808 / 237.306 | 29.498   |
| 16  | 1 / 1                | 0     | 4 / 4              | 0     | 239.806 / 234.463 | -5.343   |
| 17  | 1 / 1                | 0     | 4 / 4              | 0     | 189.400 / 235.613 | 46.213   |
| 18  | 1 / 1                | 0     | 4 / 4              | 0     | 194.840 / 233.681 | 38.842   |
| 19  | 1 / 1                | 0     | 4 / 4              | 0     | 196.377 / 234.223 | 37.846   |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 253.677 / 308.933 | 55.256   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 213.478 / 232.643 | 19.165   |
| 26  | 2 / 2                | 0     | 3 / 3              | 0     | 206.467 / 246.161 | 39.694   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 3 / 3              | 0     | 324.551 / 315.450 | -9.102   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 750.957 / 846.759   | 95.802   | 12.757  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 172.861 / 211.369   | 38.508   | 22.277  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 286.278 / 318.164   | 31.887   | 11.138  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1084.862 / 1175.159 | 90.298   | 8.323   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1180.402 / 1208.675 | 28.273   | 2.395   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1195.027 / 1379.021 | 183.994  | 15.397  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1205.893 / 1376.852 | 170.959  | 14.177  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1192.834 / 1364.415 | 171.581  | 14.384  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1194.892 / 1292.348 | 97.456   | 8.156   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 805.412 / 876.395   | 70.983   | 8.813   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 484.798 / 589.965   | 105.167  | 21.693  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 160.247 / 214.569   | 54.322   | 33.899  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 632.597 / 748.309   | 115.712  | 18.292  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1079.451 / 1268.292 | 188.841  | 17.494  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 817.128 / 1002.907  | 185.779  | 22.736  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 819.193 / 1043.784  | 224.591  | 27.416  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 821.419 / 1016.979  | 195.560  | 23.808  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 817.504 / 1065.719  | 248.215  | 30.362  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 939.606 / 950.188   | 10.582   | 1.126   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1009.718 / 1309.248 | 299.530  | 29.665  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 453.071 / 569.578   | 116.507  | 25.715  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 162.812 / 212.868   | 50.056   | 30.745  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 288.590 / 296.737   | 8.146    | 2.823   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 796.742 / 1045.171  | 248.429  | 31.181  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1092.223 / 1281.481 | 189.258  | 17.328  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1068.065 / 1181.035 | 112.969  | 10.577  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 748.586 / 930.086   | 181.500  | 24.246  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 974.430 / 1110.850  | 136.420  | 14.000  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1039.385 / 1252.198 | 212.813  | 20.475  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 720.991 / 857.695   | 136.704  | 18.961  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 601.567 / 734.073   | 132.506  | 22.027  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 482.117 / 563.954   | 81.837   | 16.975  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 281.976 / 307.169   | 25.192   | 8.934   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 556.209 / 629.455   | 750.957 / 846.759   | 194.748 / 217.304 | {"blockedOrPreparationToFirstPhysicalMutationMs":47.8036,"dispatchToBlockedOrPreparationMs":358.5652,"firstNewGenerationToCleanupDrainedMs":260.2503,"firstPhysicalMutationToFirstNewGenerationMs":84.3382,"presentationToStrictCompletionMs":194.7483}  | {"blockedOrPreparationToFirstPhysicalMutationMs":56.7771,"dispatchToBlockedOrPreparationMs":432.3352,"firstNewGenerationToCleanupDrainedMs":277.2094,"firstPhysicalMutationToFirstNewGenerationMs":80.4372,"presentationToStrictCompletionMs":217.3036}   |
| 2   | 172.861 / 211.369   | 172.861 / 211.369   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8606,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":211.3688,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 286.278 / 318.164   | 286.278 / 318.164   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.2776,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":318.1641,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 901.457 / 975.313   | 993.615 / 1080.826  | 92.158 / 105.514  | {"blockedOrPreparationToFirstPhysicalMutationMs":48.9254,"dispatchToBlockedOrPreparationMs":406.4776,"firstNewGenerationToCleanupDrainedMs":180.8965,"firstPhysicalMutationToFirstNewGenerationMs":357.315,"presentationToStrictCompletionMs":183.4047}  | {"blockedOrPreparationToFirstPhysicalMutationMs":56.0115,"dispatchToBlockedOrPreparationMs":423.7954,"firstNewGenerationToCleanupDrainedMs":213.0297,"firstPhysicalMutationToFirstNewGenerationMs":387.9898,"presentationToStrictCompletionMs":199.8465}  |
| 5   | 974.085 / 958.902   | 1078.136 / 1116.964 | 104.051 / 158.062 | {"blockedOrPreparationToFirstPhysicalMutationMs":66.4558,"dispatchToBlockedOrPreparationMs":393.7835,"firstNewGenerationToCleanupDrainedMs":216.7643,"firstPhysicalMutationToFirstNewGenerationMs":401.1323,"presentationToStrictCompletionMs":206.3166} | {"blockedOrPreparationToFirstPhysicalMutationMs":56.9795,"dispatchToBlockedOrPreparationMs":468.7342,"firstNewGenerationToCleanupDrainedMs":210.507,"firstPhysicalMutationToFirstNewGenerationMs":380.7434,"presentationToStrictCompletionMs":249.7724}   |
| 6   | 1015.606 / 1122.132 | 1106.145 / 1282.659 | 90.540 / 160.527  | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1865,"dispatchToBlockedOrPreparationMs":391.249,"firstNewGenerationToCleanupDrainedMs":188.7139,"firstPhysicalMutationToFirstNewGenerationMs":474.996,"presentationToStrictCompletionMs":179.4213}   | {"blockedOrPreparationToFirstPhysicalMutationMs":60.3085,"dispatchToBlockedOrPreparationMs":460.5716,"firstNewGenerationToCleanupDrainedMs":213.9692,"firstPhysicalMutationToFirstNewGenerationMs":547.8101,"presentationToStrictCompletionMs":256.8893}  |
| 7   | 981.378 / 1166.698  | 1119.901 / 1273.196 | 138.524 / 106.498 | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4652,"dispatchToBlockedOrPreparationMs":401.2388,"firstNewGenerationToCleanupDrainedMs":183.5256,"firstPhysicalMutationToFirstNewGenerationMs":483.6717,"presentationToStrictCompletionMs":224.5152} | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7596,"dispatchToBlockedOrPreparationMs":469.3275,"firstNewGenerationToCleanupDrainedMs":213.5704,"firstPhysicalMutationToFirstNewGenerationMs":534.5381,"presentationToStrictCompletionMs":210.1543}  |
| 8   | 971.483 / 1094.578  | 1107.063 / 1270.579 | 135.580 / 176.001 | {"blockedOrPreparationToFirstPhysicalMutationMs":48.6324,"dispatchToBlockedOrPreparationMs":395.5498,"firstNewGenerationToCleanupDrainedMs":179.0661,"firstPhysicalMutationToFirstNewGenerationMs":483.8143,"presentationToStrictCompletionMs":221.3515} | {"blockedOrPreparationToFirstPhysicalMutationMs":56.7725,"dispatchToBlockedOrPreparationMs":421.4259,"firstNewGenerationToCleanupDrainedMs":233.2686,"firstPhysicalMutationToFirstNewGenerationMs":559.1124,"presentationToStrictCompletionMs":269.8373}  |
| 9   | 958.254 / 1044.832  | 1110.311 / 1202.615 | 152.057 / 157.784 | {"blockedOrPreparationToFirstPhysicalMutationMs":50.4611,"dispatchToBlockedOrPreparationMs":401.1379,"firstNewGenerationToCleanupDrainedMs":195.1782,"firstPhysicalMutationToFirstNewGenerationMs":463.5337,"presentationToStrictCompletionMs":236.6381} | {"blockedOrPreparationToFirstPhysicalMutationMs":56.0795,"dispatchToBlockedOrPreparationMs":418.0584,"firstNewGenerationToCleanupDrainedMs":220.3175,"firstPhysicalMutationToFirstNewGenerationMs":508.16,"presentationToStrictCompletionMs":247.5159}    |
| 10  | 672.026 / 720.568   | 805.412 / 876.395   | 133.387 / 155.827 | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1025,"dispatchToBlockedOrPreparationMs":370.241,"firstNewGenerationToCleanupDrainedMs":180.3564,"firstPhysicalMutationToFirstNewGenerationMs":204.7122,"presentationToStrictCompletionMs":133.3865}  | {"blockedOrPreparationToFirstPhysicalMutationMs":54.5822,"dispatchToBlockedOrPreparationMs":412.4751,"firstNewGenerationToCleanupDrainedMs":208.7567,"firstPhysicalMutationToFirstNewGenerationMs":200.5811,"presentationToStrictCompletionMs":155.8275}  |
| 11  | 197.653 / 225.131   | 484.798 / 589.965   | 287.145 / 364.834 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6255,"dispatchToBlockedOrPreparationMs":145.0668,"firstNewGenerationToCleanupDrainedMs":287.8392,"firstPhysicalMutationToFirstNewGenerationMs":48.2665,"presentationToStrictCompletionMs":287.145}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1598,"dispatchToBlockedOrPreparationMs":168.1065,"firstNewGenerationToCleanupDrainedMs":366.1231,"firstPhysicalMutationToFirstNewGenerationMs":52.5753,"presentationToStrictCompletionMs":364.8338}    |
| 12  | 160.247 / 214.569   | 160.247 / 214.569   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":160.2466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":214.5685,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 632.597 / 748.309   | 632.597 / 748.309   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":52.8288,"dispatchToBlockedOrPreparationMs":430.0565,"firstNewGenerationToCleanupDrainedMs":44.353,"firstPhysicalMutationToFirstNewGenerationMs":105.3583,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2042,"dispatchToBlockedOrPreparationMs":519.6567,"firstNewGenerationToCleanupDrainedMs":59.868,"firstPhysicalMutationToFirstNewGenerationMs":120.5801,"presentationToStrictCompletionMs":0}           |
| 14  | 905.914 / 1106.980  | 1033.575 / 1213.389 | 127.660 / 106.410 | {"blockedOrPreparationToFirstPhysicalMutationMs":46.5767,"dispatchToBlockedOrPreparationMs":402.3021,"firstNewGenerationToCleanupDrainedMs":170.0301,"firstPhysicalMutationToFirstNewGenerationMs":414.6658,"presentationToStrictCompletionMs":173.5363} | {"blockedOrPreparationToFirstPhysicalMutationMs":56.8555,"dispatchToBlockedOrPreparationMs":464.9116,"firstNewGenerationToCleanupDrainedMs":209.2546,"firstPhysicalMutationToFirstNewGenerationMs":482.3676,"presentationToStrictCompletionMs":161.3122}  |
| 15  | 817.128 / 1002.907  | 728.821 / 841.766   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":50.0628,"dispatchToBlockedOrPreparationMs":413.8539,"firstNewGenerationToCleanupDrainedMs":41.3177,"firstPhysicalMutationToFirstNewGenerationMs":223.5868,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":55.4334,"dispatchToBlockedOrPreparationMs":480.8262,"firstNewGenerationToCleanupDrainedMs":58.2268,"firstPhysicalMutationToFirstNewGenerationMs":247.2798,"presentationToStrictCompletionMs":0}          |
| 16  | 819.193 / 1043.784  | 685.026 / 820.321   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":44.7755,"dispatchToBlockedOrPreparationMs":391.2656,"firstNewGenerationToCleanupDrainedMs":43.9256,"firstPhysicalMutationToFirstNewGenerationMs":205.0593,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":54.1505,"dispatchToBlockedOrPreparationMs":475.2215,"firstNewGenerationToCleanupDrainedMs":51.9866,"firstPhysicalMutationToFirstNewGenerationMs":238.9626,"presentationToStrictCompletionMs":0}          |
| 17  | 821.419 / 1016.979  | 692.394 / 839.532   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":45.9464,"dispatchToBlockedOrPreparationMs":401.6109,"firstNewGenerationToCleanupDrainedMs":40.6797,"firstPhysicalMutationToFirstNewGenerationMs":204.1569,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":56.3203,"dispatchToBlockedOrPreparationMs":488.7752,"firstNewGenerationToCleanupDrainedMs":55.0511,"firstPhysicalMutationToFirstNewGenerationMs":239.3856,"presentationToStrictCompletionMs":0}          |
| 18  | 817.504 / 1065.719  | 675.719 / 903.603   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":47.1171,"dispatchToBlockedOrPreparationMs":389.3872,"firstNewGenerationToCleanupDrainedMs":41.6418,"firstPhysicalMutationToFirstNewGenerationMs":197.5732,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":65.064,"dispatchToBlockedOrPreparationMs":534.4373,"firstNewGenerationToCleanupDrainedMs":53.4077,"firstPhysicalMutationToFirstNewGenerationMs":250.6942,"presentationToStrictCompletionMs":0}           |
| 19  | 939.606 / 950.188   | 689.071 / 843.173   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":44.9648,"dispatchToBlockedOrPreparationMs":404.2762,"firstNewGenerationToCleanupDrainedMs":41.4998,"firstPhysicalMutationToFirstNewGenerationMs":198.33,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":64.8252,"dispatchToBlockedOrPreparationMs":495.0227,"firstNewGenerationToCleanupDrainedMs":50.2106,"firstPhysicalMutationToFirstNewGenerationMs":233.1145,"presentationToStrictCompletionMs":0}          |
| 20  | 832.213 / 1052.426  | 1009.718 / 1309.248 | 177.505 / 256.822 | {"blockedOrPreparationToFirstPhysicalMutationMs":238.13,"dispatchToBlockedOrPreparationMs":453.5158,"firstNewGenerationToCleanupDrainedMs":227.8665,"firstPhysicalMutationToFirstNewGenerationMs":90.2054,"presentationToStrictCompletionMs":177.5049}   | {"blockedOrPreparationToFirstPhysicalMutationMs":307.0801,"dispatchToBlockedOrPreparationMs":565.0015,"firstNewGenerationToCleanupDrainedMs":323.9777,"firstPhysicalMutationToFirstNewGenerationMs":113.1885,"presentationToStrictCompletionMs":256.8219} |
| 21  | 189.077 / 230.613   | 453.071 / 569.578   | 263.994 / 338.965 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":126.7527,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":263.9937}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":157.3216,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":338.9649}             |
| 22  | 162.812 / 212.868   | 162.812 / 212.868   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":162.8116,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":212.8678,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 288.590 / 296.737   | 288.590 / 296.737   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":288.5903,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.7367,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 662.606 / 860.810   | 796.742 / 1045.171  | 134.135 / 184.361 | {"blockedOrPreparationToFirstPhysicalMutationMs":45.811,"dispatchToBlockedOrPreparationMs":411.9901,"firstNewGenerationToCleanupDrainedMs":185.9268,"firstPhysicalMutationToFirstNewGenerationMs":153.0138,"presentationToStrictCompletionMs":134.1354}  | {"blockedOrPreparationToFirstPhysicalMutationMs":61.6113,"dispatchToBlockedOrPreparationMs":537.0272,"firstNewGenerationToCleanupDrainedMs":241.9291,"firstPhysicalMutationToFirstNewGenerationMs":204.6034,"presentationToStrictCompletionMs":184.3614}  |
| 25  | 861.812 / 1023.547  | 1006.636 / 1189.968 | 144.824 / 166.421 | {"blockedOrPreparationToFirstPhysicalMutationMs":21.7457,"dispatchToBlockedOrPreparationMs":454.0392,"firstNewGenerationToCleanupDrainedMs":195.2621,"firstPhysicalMutationToFirstNewGenerationMs":335.5889,"presentationToStrictCompletionMs":230.4107} | {"blockedOrPreparationToFirstPhysicalMutationMs":24.4873,"dispatchToBlockedOrPreparationMs":545.3084,"firstNewGenerationToCleanupDrainedMs":219.0378,"firstPhysicalMutationToFirstNewGenerationMs":401.1346,"presentationToStrictCompletionMs":257.934}   |
| 26  | 1068.065 / 963.764  | 1011.510 / 1124.949 | 0 / 161.184       | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3468,"dispatchToBlockedOrPreparationMs":408.0517,"firstNewGenerationToCleanupDrainedMs":216.359,"firstPhysicalMutationToFirstNewGenerationMs":339.7526,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":56.3928,"dispatchToBlockedOrPreparationMs":478.1628,"firstNewGenerationToCleanupDrainedMs":214.4529,"firstPhysicalMutationToFirstNewGenerationMs":375.9402,"presentationToStrictCompletionMs":217.2704}  |
| 27  | 567.395 / 701.589   | 748.586 / 930.086   | 181.191 / 228.497 | {"blockedOrPreparationToFirstPhysicalMutationMs":50.2463,"dispatchToBlockedOrPreparationMs":347.906,"firstNewGenerationToCleanupDrainedMs":236.1733,"firstPhysicalMutationToFirstNewGenerationMs":114.2602,"presentationToStrictCompletionMs":181.1906}  | {"blockedOrPreparationToFirstPhysicalMutationMs":59.9345,"dispatchToBlockedOrPreparationMs":463.8098,"firstNewGenerationToCleanupDrainedMs":314.2159,"firstPhysicalMutationToFirstNewGenerationMs":92.126,"presentationToStrictCompletionMs":228.4971}    |
| 28  | 870.041 / 941.434   | 919.042 / 1056.671  | 49.000 / 115.237  | {"blockedOrPreparationToFirstPhysicalMutationMs":57.5457,"dispatchToBlockedOrPreparationMs":396.541,"firstNewGenerationToCleanupDrainedMs":175.0451,"firstPhysicalMutationToFirstNewGenerationMs":289.9098,"presentationToStrictCompletionMs":104.3887}  | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7968,"dispatchToBlockedOrPreparationMs":475.923,"firstNewGenerationToCleanupDrainedMs":218.796,"firstPhysicalMutationToFirstNewGenerationMs":306.1548,"presentationToStrictCompletionMs":169.4157}    |
| 29  | 821.599 / 958.166   | 958.330 / 1154.433  | 136.731 / 196.267 | {"blockedOrPreparationToFirstPhysicalMutationMs":19.8893,"dispatchToBlockedOrPreparationMs":450.0967,"firstNewGenerationToCleanupDrainedMs":178.5163,"firstPhysicalMutationToFirstNewGenerationMs":309.8279,"presentationToStrictCompletionMs":217.7862} | {"blockedOrPreparationToFirstPhysicalMutationMs":21.2125,"dispatchToBlockedOrPreparationMs":528.2694,"firstNewGenerationToCleanupDrainedMs":248.742,"firstPhysicalMutationToFirstNewGenerationMs":356.2088,"presentationToStrictCompletionMs":294.0322}   |
| 30  | 525.686 / 630.361   | 720.991 / 857.695   | 195.306 / 227.335 | {"blockedOrPreparationToFirstPhysicalMutationMs":48.3199,"dispatchToBlockedOrPreparationMs":350.8517,"firstNewGenerationToCleanupDrainedMs":249.0011,"firstPhysicalMutationToFirstNewGenerationMs":72.8185,"presentationToStrictCompletionMs":195.3056}  | {"blockedOrPreparationToFirstPhysicalMutationMs":57.1427,"dispatchToBlockedOrPreparationMs":431.5035,"firstNewGenerationToCleanupDrainedMs":288.0038,"firstPhysicalMutationToFirstNewGenerationMs":81.0453,"presentationToStrictCompletionMs":227.3348}   |
| 31  | 601.567 / 734.073   | 601.567 / 734.073   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.0276,"dispatchToBlockedOrPreparationMs":414.816,"firstNewGenerationToCleanupDrainedMs":42.5589,"firstPhysicalMutationToFirstNewGenerationMs":96.1643,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":44.0358,"dispatchToBlockedOrPreparationMs":521.3186,"firstNewGenerationToCleanupDrainedMs":51.989,"firstPhysicalMutationToFirstNewGenerationMs":116.7294,"presentationToStrictCompletionMs":0}           |
| 32  | 185.789 / 229.057   | 482.117 / 563.954   | 296.327 / 334.897 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":124.7113,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":296.3274}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":157.7606,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":334.897}              |
| 33  | 281.976 / 307.169   | 281.976 / 307.169   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":281.9764,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":307.1687,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 9                   | -1           | 490.707 / 569.549    | 78.842   | 15 / 14           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 5 / 4             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 13                  | -1           | 812.718 / 867.797    | 55.079   | 19 / 18           | -1           |
| 5   | 13 / 15                  | 2            | 861.372 / 906.457    | 45.086   | 18 / 20           | 2            |
| 6   | 17 / 16                  | -1           | 917.432 / 1068.690   | 151.259  | 22 / 21           | -1           |
| 7   | 18 / 17                  | -1           | 936.376 / 1059.625   | 123.249  | 23 / 22           | -1           |
| 8   | 18 / 16                  | -2           | 927.996 / 1037.311   | 109.314  | 23 / 21           | -2           |
| 9   | 17 / 17                  | 0            | 915.133 / 982.298    | 67.165   | 22 / 22           | 0            |
| 10  | 10 / 10                  | 0            | 625.056 / 667.638    | 42.583   | 14 / 14           | 0            |
| 11  | 3 / 3                    | 0            | 196.959 / 223.842    | 26.883   | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 10                  | 0            | 588.244 / 688.441    | 100.197  | 11 / 11           | 0            |
| 14  | 18 / 17                  | -1           | 863.545 / 1004.135   | 140.590  | 23 / 22           | -1           |
| 15  | 14 / 14                  | 0            | 687.504 / 783.539    | 96.036   | 17 / 18           | 1            |
| 16  | 14 / 14                  | 0            | 641.100 / 768.335    | 127.234  | 18 / 19           | 1            |
| 17  | 14 / 14                  | 0            | 651.714 / 784.481    | 132.767  | 18 / 18           | 0            |
| 18  | 14 / 14                  | 0            | 634.077 / 850.196    | 216.118  | 18 / 18           | 0            |
| 19  | 14 / 14                  | 0            | 647.571 / 792.962    | 145.391  | 21 / 17           | -4           |
| 20  | 16 / 16                  | 0            | 781.851 / 985.270    | 203.419  | 21 / 22           | 1            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 9            | -2           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 24  | 11 / 11                  | 0            | 610.815 / 803.242    | 192.427  | 15 / 15           | 0            |
| 25  | 15 / 15                  | 0            | 811.374 / 970.930    | 159.557  | 20 / 20           | 0            |
| 26  | 14 / 15                  | 1            | 795.151 / 910.496    | 115.345  | 19 / 20           | 1            |
| 27  | 10 / 10                  | 0            | 512.413 / 615.870    | 103.458  | 16 / 15           | -1           |
| 28  | 12 / 12                  | 0            | 743.996 / 837.875    | 93.878   | 17 / 17           | 0            |
| 29  | 15 / 15                  | 0            | 779.814 / 905.691    | 125.877  | 20 / 20           | 0            |
| 30  | 9 / 9                    | 0            | 471.990 / 569.692    | 97.701   | 15 / 15           | 0            |
| 31  | 10 / 10                  | 0            | 559.008 / 682.084    | 123.076  | 11 / 11           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 9 / 11            | 2            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 214.495 / 226.702 | 12.207   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 245.001 / 241.781 | -3.220   |
| 6   | 1 / 1                | 0     | 5 / 5              | 0     | 345.614 / 406.159 | 60.544   |
| 7   | 1 / 1                | 0     | 5 / 5              | 0     | 354.159 / 396.322 | 42.163   |
| 8   | 1 / 1                | 0     | 5 / 5              | 0     | 351.013 / 422.739 | 71.726   |
| 9   | 1 / 1                | 0     | 5 / 5              | 0     | 332.710 / 373.622 | 40.912   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 5 / 5              | 0     | 221.958 / 274.777 | 52.819   |
| 15  | 1 / 1                | 0     | 4 / 4              | 0     | 223.742 / 246.900 | 23.158   |
| 16  | 1 / 1                | 0     | 4 / 4              | 0     | 205.174 / 238.880 | 33.706   |
| 17  | 1 / 1                | 0     | 4 / 4              | 0     | 204.045 / 239.859 | 35.814   |
| 18  | 1 / 1                | 0     | 4 / 4              | 0     | 198.058 / 250.779 | 52.721   |
| 19  | 1 / 1                | 0     | 4 / 4              | 0     | 198.300 / 233.252 | 34.951   |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 283.340 / 362.014 | 78.674   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 203.305 / 230.931 | 27.626   |
| 26  | 2 / 2                | 0     | 3 / 3              | 0     | 220.362 / 246.738 | 26.377   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 3 / 3              | 0     | 284.083 / 321.676 | 37.593   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

## Cumulative gates and other health evidence

### renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z / nvidia / pass 1

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

### renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z / nvidia / pass 2

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

### renderscale-tuning-nvidia-2026-09-13T18-46-33-800Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":28080,"leftPath":"NativeOriginal","referenceFrame":28411,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### renderscale-tuning-nvidia-2026-09-13T18-46-33-800Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":31965,"leftPath":"NativeOriginal","referenceFrame":32297,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16430.2890625 / 16245.953125 / -184.3359375    | 16955.65625 / 16833.4765625 / -122.1796875     | 62.156                |
| nvidia | 1    | systemCommitMiB   | 57091.7109375 / 56790.00390625 / -301.70703125 | 54667.65234375 / 54357.87890625 / -309.7734375 | -8.066                |
| nvidia | 1    | dxgiUsageMiB      | 4191.01953125 / 3286.94921875 / -904.0703125   | 5661.03125 / 3865.5390625 / -1795.4921875      | -891.422              |
| nvidia | 1    | liveTextures      | 0 / 247 / 247                                  | 0 / 261 / 261                                  | 14                    |
| nvidia | 1    | liveTextureMiB    | 0 / 2405.307025909424 / 2405.307025909424      | 0 / 2430.020969390869 / 2430.020969390869      | 24.714                |
| nvidia | 2    | processPrivateMiB | 16646.34375 / 16475.06640625 / -171.27734375   | 17102.12109375 / 16749.58984375 / -352.53125   | -181.254              |
| nvidia | 2    | systemCommitMiB   | 57301.5625 / 57090.9921875 / -210.5703125      | 54705.9609375 / 54517.11328125 / -188.84765625 | 21.723                |
| nvidia | 2    | dxgiUsageMiB      | 3680.19921875 / 3536.84765625 / -143.3515625   | 4066.0390625 / 3675.5703125 / -390.46875       | -247.117              |
| nvidia | 2    | liveTextures      | 0 / 241 / 241                                  | 0 / 211 / 211                                  | -30                   |
| nvidia | 2    | liveTextureMiB    | 0 / 2379.9213676452637 / 2379.9213676452637    | 0 / 2274.9311332702637 / 2274.9311332702637    | -104.990              |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2251        | 1908        | -343        |
| cpu/compactPresentationContract/reuses                  | 2229        | 1884        | -345        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 153         | 149         | -4          |
| cpu/generationResourceValidation/fullValidations        | 693         | 683         | -10         |
| cpu/generationResourceValidation/stableChecks           | 8523        | 7300        | -1223       |
| cpu/generationResourceValidation/stableHits             | 8508        | 7283        | -1225       |
| cpu/generationResourceValidation/stableMisses           | 15          | 17          | 2           |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 1           | 1           | 0           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4360        | 3634        | -726        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4360        | 3634        | -726        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4318        | 3592        | -726        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4360        | 3634        | -726        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4337        | 3613        | -724        |
| cpu/stateProportionalSafety/memoryTrim/services         | 23          | 21          | -2          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 30          | 30          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4330        | 3604        | -726        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4266        | 3540        | -726        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 93          | 94          | 1           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4345        | 3619        | -726        |
| cpu/strongStereoPacket/captures                         | 4648        | 3961        | -687        |
| cpu/strongStereoPacket/commitAccepts                    | 4484        | 3796        | -688        |
| cpu/strongStereoPacket/commitRejects                    | 64          | 64          | 0           |
| cpu/strongStereoPacket/commitValidations                | 4548        | 3860        | -688        |
| cpu/strongStereoPacket/cycleReuses                      | 2286        | 1940        | -346        |
| cpu/strongStereoPacket/fastSkips                        | 4072        | 3307        | -765        |
| cpu/strongStereoPacket/invalidations                    | 4906        | 4194        | -712        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 92          | 94          | 2           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2270        | 1927        | -343        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.756       | 1.680       | -0.075      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 68.900      | 23.800      | -45.100     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.158       | 0.151       | -0.006      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 18.900      | 3.900       | -15.000     |
| cpu/window/currentFrame                                 | 21210       | 28412       | 7202        |
| cpu/window/elapsedFrames                                | 4359        | 3634        | -725        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 16851       | 24778       | 7927        |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 21211       | 28412       | 7201        |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6300781344  | 5208358320  | -1092423024 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4966        | 4105        | -861        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12614434560 | 10427356800 | -2187077760 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.305       | 0.107       | -0.198      |
| gpu/item5ActiveFSRCopies/activePixels                   | 12576906800 | 3568581120  | -9008325680 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 28649890000 | 29809121280 | 1159231280  |
| gpu/item5ActiveFSRCopies/copyCalls                      | 16230       | 10368       | -5862       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2138        | 1721        | -417        |
| gpu/item7EarlyHAM/executedClears                        | 2004        | 1724        | -280        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2004        | 1724        | -280        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4328        | 3634        | -694        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4360        | 3634        | -726        |
| gpu/runtimeFSRSharedGuides/directGuideInputs            | n/a         | 2772        | n/a         |
| gpu/runtimeFSRSharedGuides/directGuidePixels            | n/a         | 6651802560  | n/a         |
| gpu/runtimeFSRSharedGuides/enabled                      | n/a         | true        | n/a         |
| gpu/runtimeFSRSharedGuides/fallbackGuideCopies          | n/a         | 7740        | n/a         |
| gpu/runtimeFSRSharedGuides/importFailures               | n/a         | 0           | n/a         |
| gpu/startFrame                                          | 16851       | 24778       | 7927        |
| profiler/available                                      | true        | true        | n/a         |
| profiler/capabilities                                   | 63          | 127         | 64          |
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
| texture/createdCount                                    | 3977        | 3956        | -21         |
| texture/createdEstimatedBytes                           | 38776359208 | 38757917992 | -18441216   |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3730        | 3695        | -35         |
| texture/destroyedEstimatedBytes                         | 36254211988 | 36209856324 | -44355664   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 896         | 891         | -5          |
| texture/liveTextureRecordCount                          | 247         | 261         | 14          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 38          | 52          | 14          |
| texture/niSourceTextureMatchedEstimatedBytes            | 146495640   | 172410088   | 25914448    |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1450        | 1538        | 88          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 247         | 261         | 14          |
| texture/outstandingEstimatedBytes                       | 2522147220  | 2548061668  | 25914448    |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 1           | 1           | 0           |
| texture/supported                                       | true        | true        | n/a         |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2244        | 1868        | -376        |
| cpu/compactPresentationContract/reuses                  | 2220        | 1844        | -376        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 156         | 145         | -11         |
| cpu/generationResourceValidation/fullValidations        | 684         | 675         | -9          |
| cpu/generationResourceValidation/stableChecks           | 8465        | 7148        | -1317       |
| cpu/generationResourceValidation/stableHits             | 8442        | 7133        | -1309       |
| cpu/generationResourceValidation/stableMisses           | 23          | 15          | -8          |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 2           | 2           | 0           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4346        | 3552        | -794        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4346        | 3552        | -794        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4304        | 3510        | -794        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4346        | 3552        | -794        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4325        | 3531        | -794        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 30          | 30          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4316        | 3522        | -794        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4252        | 3458        | -794        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 94          | 0           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4331        | 3537        | -794        |
| cpu/strongStereoPacket/captures                         | 4633        | 3878        | -755        |
| cpu/strongStereoPacket/commitAccepts                    | 4466        | 3715        | -751        |
| cpu/strongStereoPacket/commitRejects                    | 66          | 65          | -1          |
| cpu/strongStereoPacket/commitValidations                | 4532        | 3780        | -752        |
| cpu/strongStereoPacket/cycleReuses                      | 2277        | 1900        | -377        |
| cpu/strongStereoPacket/fastSkips                        | 4059        | 3226        | -833        |
| cpu/strongStereoPacket/invalidations                    | 4893        | 4112        | -781        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 93          | 94          | 1           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2263        | 1884        | -379        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.652       | 1.716       | 0.063       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 27.400      | 21.200      | -6.200      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.155       | 0.150       | -0.005      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 22.100      | 0.800       | -21.300     |
| cpu/window/currentFrame                                 | 25971       | 32298       | 6327        |
| cpu/window/elapsedFrames                                | 4346        | 3552        | -794        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 21625       | 28746       | 7121        |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 25972       | 32298       | 6326        |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6258911472  | 5076404784  | -1182506688 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4933        | 4001        | -932        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12530609280 | 10163180160 | -2367429120 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.305       | 0.107       | -0.198      |
| gpu/item5ActiveFSRCopies/activePixels                   | 12379172080 | 3495919000  | -8883253080 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 28161781520 | 29119735400 | 957953880   |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15960       | 10100       | -5860       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2103        | 1681        | -422        |
| gpu/item7EarlyHAM/executedClears                        | 2014        | 1684        | -330        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2014        | 1684        | -330        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4310        | 3558        | -752        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4347        | 3552        | -795        |
| gpu/runtimeFSRSharedGuides/directGuideInputs            | n/a         | 2740        | n/a         |
| gpu/runtimeFSRSharedGuides/directGuidePixels            | n/a         | 6570517440  | n/a         |
| gpu/runtimeFSRSharedGuides/enabled                      | n/a         | true        | n/a         |
| gpu/runtimeFSRSharedGuides/fallbackGuideCopies          | n/a         | 7532        | n/a         |
| gpu/runtimeFSRSharedGuides/importFailures               | n/a         | 0           | n/a         |
| gpu/startFrame                                          | 21625       | 28746       | 7121        |
| profiler/available                                      | true        | true        | n/a         |
| profiler/capabilities                                   | 63          | 127         | 64          |
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
| texture/createdCount                                    | 3958        | 3908        | -50         |
| texture/createdEstimatedBytes                           | 38734100840 | 38607833840 | -126267000  |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3717        | 3697        | -20         |
| texture/destroyedEstimatedBytes                         | 36238572412 | 36222395652 | -16176760   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 892         | 871         | -21         |
| texture/liveTextureRecordCount                          | 241         | 211         | -30         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 34          | 4           | -30         |
| texture/niSourceTextureMatchedEstimatedBytes            | 119877048   | 9786808     | -110090240  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1484        | 1509        | 25          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 241         | 211         | -30         |
| texture/outstandingEstimatedBytes                       | 2495528428  | 2385438188  | -110090240  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 2           | 2           | 0           |
| texture/supported                                       | true        | true        | n/a         |

</details>

## Context, memory, CPU/GPU and evidence

| Retained context matches | Result |
| ------------------------ | ------ |
| adapter                  | false  |
| scene                    | false  |
| foveation                | true   |
| toolchain                | true   |
| dependencies             | false  |
| shaderCompiler           | true   |

Full start/end memory evidence, CPU/GPU/resource counters, phase timings, retry details, native execution proofs, every gate and raw receipt hash are in comparison.json. comparison.csv contains every paired or missing transition with both original row payloads. Missing samples remain null/n/a. Current and baseline failures are preserved separately; a persistent gate failure is not automatically a newly introduced regression. The fixed stretch-frame cutoff is diagnostic only because settling imposes stretch. Compare its actual frames and duration, together with time to applied relatch and strict completion. Request-to-applied frames begin at the producer request, whereas strict frames/ms begin at qualification dispatch; these intervals must not be conflated.

-   Headset refresh, driver version, power state and full modlist/cache equality require retained proof.
-   One process and ordered repeats do not establish causal or statistically significant gains.
-   Missing profiler samples are unavailable time evidence, never zero cost.
