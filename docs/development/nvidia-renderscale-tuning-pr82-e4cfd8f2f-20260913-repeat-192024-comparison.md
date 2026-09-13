# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                        | Candidate                                                                                                       |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z                                                              | renderscale-tuning-nvidia-2026-09-13T19-20-24-614Z                                                              |
| Renderer base           | 554e484e3957178e2d144bf35266bfdcc0948642                                                                        | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        |
| Main-VR base/equivalent | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                        | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        |
| Compiled source         | 554e484e3957178e2d144bf35266bfdcc0948642                                                                        | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        |
| Build ID                | e7e9fa2d13f28c6727b9567741511a418c54113c190db2616bfb2c2ad1158d96                                                | 2f6432ddbcbd05826c1fb1ccd66f25e14755d718a5e760a58cbae58b7f625efd                                                |
| DLL SHA-256             | 5bb50a859ccb62f00d20669a9ad24b6b704dc076054e86dfa8af9dda9db8bfac                                                | da3a9b54ee34dee836e3a2843d33acb8d33e49ab5754be06c05f37e038fbcdb0                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-13T19-20-24-614Z |

Assessment limits: retained_context_not_matched:adapter; retained_context_not_matched:scene; retained_context_not_matched:dependencies; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 815.094/940.919 | 15.437       | 15/14       | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 768.534/938.066 | 22.059       | 15/15       | 0/0          | 0/0                 | none             | MET/MET             |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 736.858   | 843.301   | 106.443  | 14.446  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.200    | 13.120    | -0.080   | -0.606  |
| nvidia | 1    | Relatch proof total        | ms          | 18421.450 | 21082.528 | 2661.078 | 14.446  |
| nvidia | 1    | Relatch proof total        | frames      | 330       | 328       | -2       | -0.606  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 815.094   | 940.919   | 125.825  | 15.437  |
| nvidia | 1    | Strict completion mean     | frames      | 15.182    | 14.758    | -0.424   | -2.794  |
| nvidia | 1    | Strict completion total    | ms          | 26898.109 | 31050.342 | 4152.233 | 15.437  |
| nvidia | 1    | Strict completion total    | frames      | 501       | 487       | -14      | -2.794  |
| nvidia | 1    | Stretch completed episodes | episodes    | 18        | 18        | 0        | 0       |
| nvidia | 1    | Stretch completed total    | frames      | 62        | 59        | -3       | -4.839  |
| nvidia | 1    | Stretch completed total    | ms          | 4080.674  | 4780.233  | 699.559  | 17.143  |
| nvidia | 1    | Stretch longest episode    | ms          | 345.476   | 428.950   | 83.474   | 24.162  |
| nvidia | 2    | Relatch proof mean         | ms          | 698.557   | 847.314   | 148.758  | 21.295  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.200    | 13.280    | 0.080    | 0.606   |
| nvidia | 2    | Relatch proof total        | ms          | 17463.914 | 21182.854 | 3718.940 | 21.295  |
| nvidia | 2    | Relatch proof total        | frames      | 330       | 332       | 2        | 0.606   |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 768.534   | 938.066   | 169.532  | 22.059  |
| nvidia | 2    | Strict completion mean     | frames      | 14.818    | 15        | 0.182    | 1.227   |
| nvidia | 2    | Strict completion total    | ms          | 25361.631 | 30956.181 | 5594.549 | 22.059  |
| nvidia | 2    | Strict completion total    | frames      | 489       | 495       | 6        | 1.227   |
| nvidia | 2    | Stretch completed episodes | episodes    | 18        | 18        | 0        | 0       |
| nvidia | 2    | Stretch completed total    | frames      | 62        | 62        | 0        | 0       |
| nvidia | 2    | Stretch completed total    | ms          | 4085.360  | 4869.833  | 784.473  | 19.202  |
| nvidia | 2    | Stretch longest episode    | ms          | 354.159   | 403.848   | 49.689   | 14.030  |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 776.411 / 884.753   | 108.342  | 13.954  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 179.985 / 213.208   | 33.223   | 18.459  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 309.025 / 304.752   | -4.274   | -1.383  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1213.636 / 1316.527 | 102.891  | 8.478   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1380.573 / 1307.553 | -73.020  | -5.289  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1177.831 / 1393.632 | 215.801  | 18.322  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1423.853 / 1395.400 | -28.453  | -1.998  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1398.222 / 1483.574 | 85.352   | 6.104   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1367.283 / 1376.046 | 8.762    | 0.641   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 798.935 / 946.396   | 147.461  | 18.457  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 465.233 / 582.436   | 117.203  | 25.192  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 175.991 / 215.409   | 39.417   | 22.397  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 921.078 / 780.017   | -141.062 | -15.315 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1155.429 / 1420.508 | 265.078  | 22.942  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 1002.783 / 1058.724 | 55.941   | 5.579   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 1007.986 / 1113.634 | 105.649  | 10.481  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 769.460 / 1157.341  | 387.880  | 50.409  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 786.131 / 1044.366  | 258.234  | 32.849  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 1223.303 / 1166.553 | -56.750  | -4.639  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 953.041 / 1042.200  | 89.159   | 9.355   | 1/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 467.713 / 637.899   | 170.186  | 36.387  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 158.622 / 227.384   | 68.762   | 43.349  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 240.633 / 296.170   | 55.537   | 23.079  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 725.341 / 986.180   | 260.839  | 35.961  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1127.028 / 1354.466 | 227.438  | 20.180  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1009.984 / 1262.287 | 252.303  | 24.981  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 727.177 / 921.173   | 193.997  | 26.678  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 884.295 / 1172.554  | 288.258  | 32.597  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1053.923 / 1270.816 | 216.893  | 20.580  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 701.801 / 910.395   | 208.594  | 29.723  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 606.601 / 759.387   | 152.785  | 25.187  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 443.467 / 685.512   | 242.046  | 54.580  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 265.332 / 363.093   | 97.761   | 36.845  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 593.317 / 667.350   | 776.411 / 884.753   | 183.094 / 217.403 | {"blockedOrPreparationToFirstPhysicalMutationMs":340.9232,"dispatchToBlockedOrPreparationMs":91.2217,"firstNewGenerationToCleanupDrainedMs":249.0554,"firstPhysicalMutationToFirstNewGenerationMs":95.2109,"presentationToStrictCompletionMs":183.094}   | {"blockedOrPreparationToFirstPhysicalMutationMs":57.5055,"dispatchToBlockedOrPreparationMs":455.6263,"firstNewGenerationToCleanupDrainedMs":279.0581,"firstPhysicalMutationToFirstNewGenerationMs":92.563,"presentationToStrictCompletionMs":217.4031}   |
| 2   | 179.985 / 213.208   | 179.985 / 213.208   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":90.3612,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":213.2077,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 309.025 / 304.752   | 309.025 / 304.752   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":96.4899,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":304.7515,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 984.178 / 1104.153  | 1106.032 / 1214.556 | 121.855 / 110.403 | {"blockedOrPreparationToFirstPhysicalMutationMs":52.7617,"dispatchToBlockedOrPreparationMs":414.2681,"firstNewGenerationToCleanupDrainedMs":228.2539,"firstPhysicalMutationToFirstNewGenerationMs":410.7485,"presentationToStrictCompletionMs":229.4586} | {"blockedOrPreparationToFirstPhysicalMutationMs":62.1883,"dispatchToBlockedOrPreparationMs":479.2731,"firstNewGenerationToCleanupDrainedMs":223.4318,"firstPhysicalMutationToFirstNewGenerationMs":449.6627,"presentationToStrictCompletionMs":212.3746} |
| 5   | 1126.045 / 1105.179 | 1274.179 / 1213.348 | 148.134 / 108.169 | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7797,"dispatchToBlockedOrPreparationMs":390.0022,"firstNewGenerationToCleanupDrainedMs":192.3497,"firstPhysicalMutationToFirstNewGenerationMs":636.047,"presentationToStrictCompletionMs":254.5278}  | {"blockedOrPreparationToFirstPhysicalMutationMs":62.64,"dispatchToBlockedOrPreparationMs":508.255,"firstNewGenerationToCleanupDrainedMs":215.7048,"firstPhysicalMutationToFirstNewGenerationMs":426.7483,"presentationToStrictCompletionMs":202.3735}    |
| 6   | 939.787 / 1133.029  | 1085.005 / 1299.695 | 145.218 / 166.665 | {"blockedOrPreparationToFirstPhysicalMutationMs":49.9869,"dispatchToBlockedOrPreparationMs":380.9207,"firstNewGenerationToCleanupDrainedMs":193.4445,"firstPhysicalMutationToFirstNewGenerationMs":460.653,"presentationToStrictCompletionMs":238.0445}  | {"blockedOrPreparationToFirstPhysicalMutationMs":56.577,"dispatchToBlockedOrPreparationMs":444.8335,"firstNewGenerationToCleanupDrainedMs":220.1678,"firstPhysicalMutationToFirstNewGenerationMs":578.1163,"presentationToStrictCompletionMs":260.6026}  |
| 7   | 1225.830 / 1178.458 | 1321.959 / 1292.039 | 96.129 / 113.582  | {"blockedOrPreparationToFirstPhysicalMutationMs":51.0685,"dispatchToBlockedOrPreparationMs":379.0517,"firstNewGenerationToCleanupDrainedMs":185.0477,"firstPhysicalMutationToFirstNewGenerationMs":706.7908,"presentationToStrictCompletionMs":198.0234} | {"blockedOrPreparationToFirstPhysicalMutationMs":58.328,"dispatchToBlockedOrPreparationMs":441.6724,"firstNewGenerationToCleanupDrainedMs":222.1939,"firstPhysicalMutationToFirstNewGenerationMs":569.845,"presentationToStrictCompletionMs":216.9426}   |
| 8   | 1157.610 / 1265.465 | 1301.666 / 1371.735 | 144.055 / 106.270 | {"blockedOrPreparationToFirstPhysicalMutationMs":51.3844,"dispatchToBlockedOrPreparationMs":374.9823,"firstNewGenerationToCleanupDrainedMs":187.6664,"firstPhysicalMutationToFirstNewGenerationMs":687.6324,"presentationToStrictCompletionMs":240.6121} | {"blockedOrPreparationToFirstPhysicalMutationMs":58.5687,"dispatchToBlockedOrPreparationMs":524.9177,"firstNewGenerationToCleanupDrainedMs":212.9461,"firstPhysicalMutationToFirstNewGenerationMs":575.3024,"presentationToStrictCompletionMs":218.1099} |
| 9   | 1106.020 / 1163.412 | 1248.893 / 1281.721 | 142.873 / 118.309 | {"blockedOrPreparationToFirstPhysicalMutationMs":0.5993,"dispatchToBlockedOrPreparationMs":417.7949,"firstNewGenerationToCleanupDrainedMs":185.5526,"firstPhysicalMutationToFirstNewGenerationMs":644.9458,"presentationToStrictCompletionMs":261.2633}  | {"blockedOrPreparationToFirstPhysicalMutationMs":62.1668,"dispatchToBlockedOrPreparationMs":457.94,"firstNewGenerationToCleanupDrainedMs":222.3196,"firstPhysicalMutationToFirstNewGenerationMs":539.2943,"presentationToStrictCompletionMs":212.634}    |
| 10  | 664.251 / 770.587   | 798.935 / 946.396   | 134.684 / 175.808 | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3436,"dispatchToBlockedOrPreparationMs":360.4407,"firstNewGenerationToCleanupDrainedMs":182.3977,"firstPhysicalMutationToFirstNewGenerationMs":208.7527,"presentationToStrictCompletionMs":134.6836} | {"blockedOrPreparationToFirstPhysicalMutationMs":55.1533,"dispatchToBlockedOrPreparationMs":428.9503,"firstNewGenerationToCleanupDrainedMs":230.8244,"firstPhysicalMutationToFirstNewGenerationMs":231.4676,"presentationToStrictCompletionMs":175.8084} |
| 11  | 176.768 / 229.505   | 465.233 / 582.436   | 288.465 / 352.930 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4611,"dispatchToBlockedOrPreparationMs":131.4955,"firstNewGenerationToCleanupDrainedMs":289.1769,"firstPhysicalMutationToFirstNewGenerationMs":41.0993,"presentationToStrictCompletionMs":288.4649}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1174,"dispatchToBlockedOrPreparationMs":173.6158,"firstNewGenerationToCleanupDrainedMs":354.0747,"firstPhysicalMutationToFirstNewGenerationMs":51.6278,"presentationToStrictCompletionMs":352.9304}   |
| 12  | 175.991 / 215.409   | 175.991 / 215.409   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":175.9914,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":215.4088,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 921.078 / 780.017   | 921.078 / 780.017   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":302.9725,"dispatchToBlockedOrPreparationMs":434.6174,"firstNewGenerationToCleanupDrainedMs":44.5645,"firstPhysicalMutationToFirstNewGenerationMs":138.9241,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":60.8896,"dispatchToBlockedOrPreparationMs":529.7042,"firstNewGenerationToCleanupDrainedMs":55.3579,"firstPhysicalMutationToFirstNewGenerationMs":134.0651,"presentationToStrictCompletionMs":0}         |
| 14  | 1000.417 / 1420.508 | 1106.483 / 1352.975 | 106.066 / 0       | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0613,"dispatchToBlockedOrPreparationMs":449.3138,"firstNewGenerationToCleanupDrainedMs":194.3451,"firstPhysicalMutationToFirstNewGenerationMs":413.7628,"presentationToStrictCompletionMs":155.012}  | {"blockedOrPreparationToFirstPhysicalMutationMs":57.3612,"dispatchToBlockedOrPreparationMs":508.2943,"firstNewGenerationToCleanupDrainedMs":270.7189,"firstPhysicalMutationToFirstNewGenerationMs":516.6006,"presentationToStrictCompletionMs":0}        |
| 15  | 1002.783 / 1058.724 | 728.137 / 873.811   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2954,"dispatchToBlockedOrPreparationMs":427.4852,"firstNewGenerationToCleanupDrainedMs":44.4301,"firstPhysicalMutationToFirstNewGenerationMs":207.9264,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":57.473,"dispatchToBlockedOrPreparationMs":495.9964,"firstNewGenerationToCleanupDrainedMs":61.7808,"firstPhysicalMutationToFirstNewGenerationMs":258.5611,"presentationToStrictCompletionMs":0}          |
| 16  | 1007.986 / 1113.634 | 748.234 / 891.685   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2088,"dispatchToBlockedOrPreparationMs":417.264,"firstNewGenerationToCleanupDrainedMs":43.3328,"firstPhysicalMutationToFirstNewGenerationMs":239.4284,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":65.0882,"dispatchToBlockedOrPreparationMs":512.2377,"firstNewGenerationToCleanupDrainedMs":52.9686,"firstPhysicalMutationToFirstNewGenerationMs":261.3905,"presentationToStrictCompletionMs":0}         |
| 17  | 769.460 / 1157.341  | 605.502 / 967.599   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":42.688,"dispatchToBlockedOrPreparationMs":373.0891,"firstNewGenerationToCleanupDrainedMs":0.3491,"firstPhysicalMutationToFirstNewGenerationMs":189.3757,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":57.6955,"dispatchToBlockedOrPreparationMs":520.3593,"firstNewGenerationToCleanupDrainedMs":60.3859,"firstPhysicalMutationToFirstNewGenerationMs":329.1583,"presentationToStrictCompletionMs":0}         |
| 18  | 786.131 / 1044.366  | 660.916 / 881.764   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":45.3155,"dispatchToBlockedOrPreparationMs":380.3142,"firstNewGenerationToCleanupDrainedMs":40.8901,"firstPhysicalMutationToFirstNewGenerationMs":194.3963,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":60.7126,"dispatchToBlockedOrPreparationMs":494.3268,"firstNewGenerationToCleanupDrainedMs":52.0375,"firstPhysicalMutationToFirstNewGenerationMs":274.687,"presentationToStrictCompletionMs":0}          |
| 19  | 1223.303 / 1166.553 | 685.056 / 872.154   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.3383,"dispatchToBlockedOrPreparationMs":397.3388,"firstNewGenerationToCleanupDrainedMs":43.3787,"firstPhysicalMutationToFirstNewGenerationMs":195.9997,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":58.7832,"dispatchToBlockedOrPreparationMs":505.2963,"firstNewGenerationToCleanupDrainedMs":54.2278,"firstPhysicalMutationToFirstNewGenerationMs":253.8472,"presentationToStrictCompletionMs":0}         |
| 20  | 768.589 / 741.601   | 953.041 / 1042.200  | 184.452 / 300.599 | {"blockedOrPreparationToFirstPhysicalMutationMs":211.9214,"dispatchToBlockedOrPreparationMs":428.0444,"firstNewGenerationToCleanupDrainedMs":229.1844,"firstPhysicalMutationToFirstNewGenerationMs":83.8908,"presentationToStrictCompletionMs":184.4523} | {"blockedOrPreparationToFirstPhysicalMutationMs":1.1928,"dispatchToBlockedOrPreparationMs":535.0174,"firstNewGenerationToCleanupDrainedMs":384.5367,"firstPhysicalMutationToFirstNewGenerationMs":121.453,"presentationToStrictCompletionMs":300.5993}   |
| 21  | 209.143 / 262.473   | 467.713 / 637.899   | 258.569 / 375.425 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.3458,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":258.5694}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.918,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":375.4253}             |
| 22  | 158.622 / 227.384   | 158.622 / 227.384   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":158.6221,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":227.3839,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 240.633 / 296.170   | 240.633 / 296.170   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":240.6335,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.1702,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 597.558 / 815.965   | 725.341 / 986.180   | 127.783 / 170.215 | {"blockedOrPreparationToFirstPhysicalMutationMs":43.0545,"dispatchToBlockedOrPreparationMs":377.1836,"firstNewGenerationToCleanupDrainedMs":167.8687,"firstPhysicalMutationToFirstNewGenerationMs":137.2345,"presentationToStrictCompletionMs":127.7829} | {"blockedOrPreparationToFirstPhysicalMutationMs":62.3675,"dispatchToBlockedOrPreparationMs":523.2926,"firstNewGenerationToCleanupDrainedMs":221.9487,"firstPhysicalMutationToFirstNewGenerationMs":178.5711,"presentationToStrictCompletionMs":170.2152} |
| 25  | 914.530 / 1083.648  | 1041.725 / 1261.005 | 127.195 / 177.358 | {"blockedOrPreparationToFirstPhysicalMutationMs":24.1865,"dispatchToBlockedOrPreparationMs":494.0467,"firstNewGenerationToCleanupDrainedMs":168.1967,"firstPhysicalMutationToFirstNewGenerationMs":355.2946,"presentationToStrictCompletionMs":212.4982} | {"blockedOrPreparationToFirstPhysicalMutationMs":32.4843,"dispatchToBlockedOrPreparationMs":608.2173,"firstNewGenerationToCleanupDrainedMs":233.0946,"firstPhysicalMutationToFirstNewGenerationMs":387.209,"presentationToStrictCompletionMs":270.8183}  |
| 26  | 918.401 / 1262.287  | 965.214 / 1203.507  | 46.813 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":46.6122,"dispatchToBlockedOrPreparationMs":421.3456,"firstNewGenerationToCleanupDrainedMs":170.1694,"firstPhysicalMutationToFirstNewGenerationMs":327.0868,"presentationToStrictCompletionMs":91.5829}  | {"blockedOrPreparationToFirstPhysicalMutationMs":58.5799,"dispatchToBlockedOrPreparationMs":553.5916,"firstNewGenerationToCleanupDrainedMs":220.5511,"firstPhysicalMutationToFirstNewGenerationMs":370.7845,"presentationToStrictCompletionMs":0}        |
| 27  | 546.985 / 692.171   | 727.177 / 921.173   | 180.192 / 229.002 | {"blockedOrPreparationToFirstPhysicalMutationMs":0.9403,"dispatchToBlockedOrPreparationMs":389.236,"firstNewGenerationToCleanupDrainedMs":233.5112,"firstPhysicalMutationToFirstNewGenerationMs":103.489,"presentationToStrictCompletionMs":180.1917}    | {"blockedOrPreparationToFirstPhysicalMutationMs":1.0789,"dispatchToBlockedOrPreparationMs":517.6256,"firstNewGenerationToCleanupDrainedMs":319.5714,"firstPhysicalMutationToFirstNewGenerationMs":82.8973,"presentationToStrictCompletionMs":229.0023}   |
| 28  | 884.295 / 1059.609  | 664.092 / 1116.037  | 0 / 56.428        | {"blockedOrPreparationToFirstPhysicalMutationMs":44.6346,"dispatchToBlockedOrPreparationMs":319.5049,"firstNewGenerationToCleanupDrainedMs":41.2796,"firstPhysicalMutationToFirstNewGenerationMs":258.6731,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":59.7979,"dispatchToBlockedOrPreparationMs":479.2036,"firstNewGenerationToCleanupDrainedMs":212.7171,"firstPhysicalMutationToFirstNewGenerationMs":364.3183,"presentationToStrictCompletionMs":112.9448} |
| 29  | 832.834 / 1050.659  | 969.609 / 1163.834  | 136.775 / 113.175 | {"blockedOrPreparationToFirstPhysicalMutationMs":27.2958,"dispatchToBlockedOrPreparationMs":422.2767,"firstNewGenerationToCleanupDrainedMs":177.3635,"firstPhysicalMutationToFirstNewGenerationMs":342.6734,"presentationToStrictCompletionMs":221.0887} | {"blockedOrPreparationToFirstPhysicalMutationMs":31.8854,"dispatchToBlockedOrPreparationMs":538.3522,"firstNewGenerationToCleanupDrainedMs":217.6422,"firstPhysicalMutationToFirstNewGenerationMs":375.9541,"presentationToStrictCompletionMs":220.1565} |
| 30  | 526.231 / 641.697   | 701.801 / 910.395   | 175.570 / 268.698 | {"blockedOrPreparationToFirstPhysicalMutationMs":43.5348,"dispatchToBlockedOrPreparationMs":359.2,"firstNewGenerationToCleanupDrainedMs":231.5579,"firstPhysicalMutationToFirstNewGenerationMs":67.5083,"presentationToStrictCompletionMs":175.5695}     | {"blockedOrPreparationToFirstPhysicalMutationMs":54.238,"dispatchToBlockedOrPreparationMs":421.5549,"firstNewGenerationToCleanupDrainedMs":336.1358,"firstPhysicalMutationToFirstNewGenerationMs":98.4665,"presentationToStrictCompletionMs":268.6978}   |
| 31  | 606.601 / 759.387   | 606.601 / 759.387   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":63.0553,"dispatchToBlockedOrPreparationMs":403.6118,"firstNewGenerationToCleanupDrainedMs":43.5018,"firstPhysicalMutationToFirstNewGenerationMs":96.4326,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":51.3872,"dispatchToBlockedOrPreparationMs":533.8503,"firstNewGenerationToCleanupDrainedMs":53.4768,"firstPhysicalMutationToFirstNewGenerationMs":120.6727,"presentationToStrictCompletionMs":0}         |
| 32  | 189.217 / 303.658   | 443.467 / 685.512   | 254.250 / 381.854 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":116.8255,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":254.2499}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":211.0087,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":381.854}             |
| 33  | 265.332 / 363.093   | 265.332 / 363.093   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":265.3315,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":363.0928,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 10                   | 1            | 527.356 / 605.695    | 78.339   | 14 / 16           | 2            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 14                  | 0            | 877.778 / 991.124    | 113.346  | 19 / 19           | 0            |
| 5   | 14 / 15                  | 1            | 1081.829 / 997.643   | -84.186  | 19 / 20           | 1            |
| 6   | 17 / 17                  | 0            | 891.561 / 1079.527   | 187.966  | 22 / 22           | 0            |
| 7   | 16 / 17                  | 1            | 1136.911 / 1069.845  | -67.066  | 21 / 22           | 1            |
| 8   | 17 / 18                  | 1            | 1113.999 / 1158.789  | 44.790   | 22 / 23           | 1            |
| 9   | 17 / 17                  | 0            | 1063.340 / 1059.401  | -3.939   | 22 / 22           | 0            |
| 10  | 11 / 11                  | 0            | 616.537 / 715.571    | 99.034   | 15 / 16           | 1            |
| 11  | 3 / 3                    | 0            | 176.056 / 228.361    | 52.305   | 9 / 9             | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 10                  | 0            | 876.514 / 724.659    | -151.855 | 11 / 11           | 0            |
| 14  | 19 / 18                  | -1           | 912.138 / 1082.256   | 170.118  | 24 / 23           | -1           |
| 15  | 14 / 14                  | 0            | 683.707 / 812.030    | 128.323  | 21 / 18           | -3           |
| 16  | 14 / 14                  | 0            | 704.901 / 838.716    | 133.815  | 21 / 19           | -2           |
| 17  | 14 / 14                  | 0            | 605.153 / 907.213    | 302.060  | 18 / 18           | 0            |
| 18  | 13 / 13                  | 0            | 620.026 / 829.726    | 209.700  | 17 / 17           | 0            |
| 19  | 14 / 14                  | 0            | 641.677 / 817.927    | 176.250  | 27 / 19           | -8           |
| 20  | 16 / 10                  | -6           | 723.857 / 657.663    | -66.193  | 22 / 15           | -7           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 12 / 12                  | 0            | 557.473 / 764.231    | 206.759  | 16 / 16           | 0            |
| 25  | 15 / 15                  | 0            | 873.528 / 1027.911   | 154.383  | 20 / 20           | 0            |
| 26  | 15 / 16                  | 1            | 795.045 / 982.956    | 187.911  | 20 / 21           | 1            |
| 27  | 10 / 10                  | 0            | 493.665 / 601.602    | 107.937  | 16 / 16           | 0            |
| 28  | 10 / 12                  | 2            | 622.813 / 903.320    | 280.507  | 16 / 17           | 1            |
| 29  | 15 / 15                  | 0            | 792.246 / 946.192    | 153.946  | 20 / 20           | 0            |
| 30  | 11 / 9                   | -2           | 470.243 / 574.259    | 104.016  | 17 / 14           | -3           |
| 31  | 10 / 10                  | 0            | 563.100 / 705.910    | 142.811  | 12 / 11           | -1           |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 0 / 1                | 1     | 0 / 2              | 2     | 0 / 149.879       | 149.879  |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 254.095 / 268.318 | 14.223   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 217.524 / 239.240 | 21.716   |
| 6   | 1 / 1                | 0     | 5 / 5              | 0     | 337.267 / 428.950 | 91.683   |
| 7   | 1 / 1                | 0     | 5 / 5              | 0     | 345.476 / 407.202 | 61.727   |
| 8   | 1 / 1                | 0     | 5 / 5              | 0     | 337.337 / 421.930 | 84.592   |
| 9   | 1 / 1                | 0     | 5 / 5              | 0     | 343.309 / 381.517 | 38.207   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 5 / 5              | 0     | 219.261 / 273.531 | 54.270   |
| 15  | 1 / 1                | 0     | 4 / 4              | 0     | 207.808 / 258.498 | 50.690   |
| 16  | 1 / 1                | 0     | 4 / 4              | 0     | 239.806 / 261.859 | 22.052   |
| 17  | 1 / 1                | 0     | 4 / 4              | 0     | 189.400 / 328.538 | 139.138  |
| 18  | 1 / 1                | 0     | 4 / 4              | 0     | 194.840 / 274.884 | 80.044   |
| 19  | 1 / 1                | 0     | 4 / 4              | 0     | 196.377 / 254.089 | 57.713   |
| 20  | 1 / 0                | -1    | 5 / 0              | -5    | 253.677 / 0       | -253.677 |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 213.478 / 235.050 | 21.572   |
| 26  | 2 / 2                | 0     | 3 / 3              | 0     | 206.467 / 241.943 | 35.476   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 3 / 3              | 0     | 324.551 / 354.805 | 30.253   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 750.957 / 991.294   | 240.337  | 32.004  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 172.861 / 241.370   | 68.509   | 39.633  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 286.278 / 289.661   | 3.384    | 1.182   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1084.862 / 1227.203 | 142.341  | 13.121  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1180.402 / 1306.646 | 126.244  | 10.695  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1195.027 / 1337.317 | 142.290  | 11.907  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1205.893 / 1318.093 | 112.200  | 9.304   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1192.834 / 1320.059 | 127.225  | 10.666  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1194.892 / 1351.707 | 156.816  | 13.124  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 805.412 / 977.564   | 172.151  | 21.374  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 484.798 / 580.515   | 95.717   | 19.744  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 160.247 / 211.807   | 51.560   | 32.176  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 632.597 / 765.907   | 133.310  | 21.073  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1079.451 / 1442.434 | 362.983  | 33.627  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 817.128 / 1503.259  | 686.131  | 83.969  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 819.193 / 1237.215  | 418.021  | 51.028  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 821.419 / 1096.669  | 275.250  | 33.509  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 817.504 / 1010.180  | 192.676  | 23.569  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 939.606 / 1090.224  | 150.618  | 16.030  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1009.718 / 1215.989 | 206.272  | 20.429  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 453.071 / 558.991   | 105.920  | 23.378  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 162.812 / 205.324   | 42.513   | 26.112  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 288.590 / 324.124   | 35.534   | 12.313  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 796.742 / 977.717   | 180.976  | 22.714  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1092.223 / 1292.331 | 200.108  | 18.321  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1068.065 / 1284.469 | 216.404  | 20.261  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 748.586 / 900.937   | 152.351  | 20.352  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 974.430 / 1135.717  | 161.287  | 16.552  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1039.385 / 1291.659 | 252.274  | 24.271  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 720.991 / 855.807   | 134.816  | 18.699  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 601.567 / 719.204   | 117.637  | 19.555  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 482.117 / 599.604   | 117.487  | 24.369  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 281.976 / 295.183   | 13.207   | 4.684   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 556.209 / 741.082   | 750.957 / 991.294   | 194.748 / 250.213 | {"blockedOrPreparationToFirstPhysicalMutationMs":47.8036,"dispatchToBlockedOrPreparationMs":358.5652,"firstNewGenerationToCleanupDrainedMs":260.2503,"firstPhysicalMutationToFirstNewGenerationMs":84.3382,"presentationToStrictCompletionMs":194.7483}  | {"blockedOrPreparationToFirstPhysicalMutationMs":68.6627,"dispatchToBlockedOrPreparationMs":447.3866,"firstNewGenerationToCleanupDrainedMs":324.3669,"firstPhysicalMutationToFirstNewGenerationMs":150.878,"presentationToStrictCompletionMs":250.2126}  |
| 2   | 172.861 / 241.370   | 172.861 / 241.370   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8606,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":241.3699,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 286.278 / 289.661   | 286.278 / 289.661   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.2776,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":289.6615,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 901.457 / 1022.152  | 993.615 / 1130.312  | 92.158 / 108.159  | {"blockedOrPreparationToFirstPhysicalMutationMs":48.9254,"dispatchToBlockedOrPreparationMs":406.4776,"firstNewGenerationToCleanupDrainedMs":180.8965,"firstPhysicalMutationToFirstNewGenerationMs":357.315,"presentationToStrictCompletionMs":183.4047}  | {"blockedOrPreparationToFirstPhysicalMutationMs":56.8343,"dispatchToBlockedOrPreparationMs":472.2174,"firstNewGenerationToCleanupDrainedMs":212.5164,"firstPhysicalMutationToFirstNewGenerationMs":388.7435,"presentationToStrictCompletionMs":205.0506} |
| 5   | 974.085 / 1051.329  | 1078.136 / 1211.485 | 104.051 / 160.156 | {"blockedOrPreparationToFirstPhysicalMutationMs":66.4558,"dispatchToBlockedOrPreparationMs":393.7835,"firstNewGenerationToCleanupDrainedMs":216.7643,"firstPhysicalMutationToFirstNewGenerationMs":401.1323,"presentationToStrictCompletionMs":206.3166} | {"blockedOrPreparationToFirstPhysicalMutationMs":66.2974,"dispatchToBlockedOrPreparationMs":473.1666,"firstNewGenerationToCleanupDrainedMs":219.418,"firstPhysicalMutationToFirstNewGenerationMs":452.6025,"presentationToStrictCompletionMs":255.3178}  |
| 6   | 1015.606 / 1139.698 | 1106.145 / 1245.531 | 90.540 / 105.833  | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1865,"dispatchToBlockedOrPreparationMs":391.249,"firstNewGenerationToCleanupDrainedMs":188.7139,"firstPhysicalMutationToFirstNewGenerationMs":474.996,"presentationToStrictCompletionMs":179.4213}   | {"blockedOrPreparationToFirstPhysicalMutationMs":0.7204,"dispatchToBlockedOrPreparationMs":492.227,"firstNewGenerationToCleanupDrainedMs":208.4891,"firstPhysicalMutationToFirstNewGenerationMs":544.0945,"presentationToStrictCompletionMs":197.6183}   |
| 7   | 981.378 / 1063.800  | 1119.901 / 1220.474 | 138.524 / 156.674 | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4652,"dispatchToBlockedOrPreparationMs":401.2388,"firstNewGenerationToCleanupDrainedMs":183.5256,"firstPhysicalMutationToFirstNewGenerationMs":483.6717,"presentationToStrictCompletionMs":224.5152} | {"blockedOrPreparationToFirstPhysicalMutationMs":57.0677,"dispatchToBlockedOrPreparationMs":414.1763,"firstNewGenerationToCleanupDrainedMs":210.6635,"firstPhysicalMutationToFirstNewGenerationMs":538.5666,"presentationToStrictCompletionMs":254.2926} |
| 8   | 971.483 / 1119.006  | 1107.063 / 1228.469 | 135.580 / 109.463 | {"blockedOrPreparationToFirstPhysicalMutationMs":48.6324,"dispatchToBlockedOrPreparationMs":395.5498,"firstNewGenerationToCleanupDrainedMs":179.0661,"firstPhysicalMutationToFirstNewGenerationMs":483.8143,"presentationToStrictCompletionMs":221.3515} | {"blockedOrPreparationToFirstPhysicalMutationMs":54.8912,"dispatchToBlockedOrPreparationMs":418.3487,"firstNewGenerationToCleanupDrainedMs":219.4153,"firstPhysicalMutationToFirstNewGenerationMs":535.8134,"presentationToStrictCompletionMs":201.0537} |
| 9   | 958.254 / 1143.799  | 1110.311 / 1253.432 | 152.057 / 109.633 | {"blockedOrPreparationToFirstPhysicalMutationMs":50.4611,"dispatchToBlockedOrPreparationMs":401.1379,"firstNewGenerationToCleanupDrainedMs":195.1782,"firstPhysicalMutationToFirstNewGenerationMs":463.5337,"presentationToStrictCompletionMs":236.6381} | {"blockedOrPreparationToFirstPhysicalMutationMs":59.2707,"dispatchToBlockedOrPreparationMs":413.1491,"firstNewGenerationToCleanupDrainedMs":227.1591,"firstPhysicalMutationToFirstNewGenerationMs":553.853,"presentationToStrictCompletionMs":207.9086}  |
| 10  | 672.026 / 797.683   | 805.412 / 977.564   | 133.387 / 179.880 | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1025,"dispatchToBlockedOrPreparationMs":370.241,"firstNewGenerationToCleanupDrainedMs":180.3564,"firstPhysicalMutationToFirstNewGenerationMs":204.7122,"presentationToStrictCompletionMs":133.3865}  | {"blockedOrPreparationToFirstPhysicalMutationMs":68.743,"dispatchToBlockedOrPreparationMs":431.679,"firstNewGenerationToCleanupDrainedMs":237.6414,"firstPhysicalMutationToFirstNewGenerationMs":239.5002,"presentationToStrictCompletionMs":179.8803}   |
| 11  | 197.653 / 223.277   | 484.798 / 580.515   | 287.145 / 357.239 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6255,"dispatchToBlockedOrPreparationMs":145.0668,"firstNewGenerationToCleanupDrainedMs":287.8392,"firstPhysicalMutationToFirstNewGenerationMs":48.2665,"presentationToStrictCompletionMs":287.145}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6067,"dispatchToBlockedOrPreparationMs":166.3486,"firstNewGenerationToCleanupDrainedMs":358.1378,"firstPhysicalMutationToFirstNewGenerationMs":52.4223,"presentationToStrictCompletionMs":357.2388}   |
| 12  | 160.247 / 211.807   | 160.247 / 211.807   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":160.2466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":211.8068,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 632.597 / 765.907   | 632.597 / 765.907   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":52.8288,"dispatchToBlockedOrPreparationMs":430.0565,"firstNewGenerationToCleanupDrainedMs":44.353,"firstPhysicalMutationToFirstNewGenerationMs":105.3583,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":51.0512,"dispatchToBlockedOrPreparationMs":536.7817,"firstNewGenerationToCleanupDrainedMs":53.663,"firstPhysicalMutationToFirstNewGenerationMs":124.4108,"presentationToStrictCompletionMs":0}          |
| 14  | 905.914 / 1442.434  | 1033.575 / 1384.714 | 127.660 / 0       | {"blockedOrPreparationToFirstPhysicalMutationMs":46.5767,"dispatchToBlockedOrPreparationMs":402.3021,"firstNewGenerationToCleanupDrainedMs":170.0301,"firstPhysicalMutationToFirstNewGenerationMs":414.6658,"presentationToStrictCompletionMs":173.5363} | {"blockedOrPreparationToFirstPhysicalMutationMs":62.9968,"dispatchToBlockedOrPreparationMs":498.1145,"firstNewGenerationToCleanupDrainedMs":218.0888,"firstPhysicalMutationToFirstNewGenerationMs":605.5143,"presentationToStrictCompletionMs":0}        |
| 15  | 817.128 / 1503.259  | 728.821 / 900.121   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":50.0628,"dispatchToBlockedOrPreparationMs":413.8539,"firstNewGenerationToCleanupDrainedMs":41.3177,"firstPhysicalMutationToFirstNewGenerationMs":223.5868,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":60.0466,"dispatchToBlockedOrPreparationMs":504.908,"firstNewGenerationToCleanupDrainedMs":52.6014,"firstPhysicalMutationToFirstNewGenerationMs":282.5653,"presentationToStrictCompletionMs":0}          |
| 16  | 819.193 / 1237.215  | 685.026 / 1115.073  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":44.7755,"dispatchToBlockedOrPreparationMs":391.2656,"firstNewGenerationToCleanupDrainedMs":43.9256,"firstPhysicalMutationToFirstNewGenerationMs":205.0593,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":69.2357,"dispatchToBlockedOrPreparationMs":721.4366,"firstNewGenerationToCleanupDrainedMs":66.9626,"firstPhysicalMutationToFirstNewGenerationMs":257.4384,"presentationToStrictCompletionMs":0}         |
| 17  | 821.419 / 1096.669  | 692.394 / 930.178   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":45.9464,"dispatchToBlockedOrPreparationMs":401.6109,"firstNewGenerationToCleanupDrainedMs":40.6797,"firstPhysicalMutationToFirstNewGenerationMs":204.1569,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":72.4922,"dispatchToBlockedOrPreparationMs":530.3887,"firstNewGenerationToCleanupDrainedMs":55.6453,"firstPhysicalMutationToFirstNewGenerationMs":271.6523,"presentationToStrictCompletionMs":0}         |
| 18  | 817.504 / 1010.180  | 675.719 / 849.834   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":47.1171,"dispatchToBlockedOrPreparationMs":389.3872,"firstNewGenerationToCleanupDrainedMs":41.6418,"firstPhysicalMutationToFirstNewGenerationMs":197.5732,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":56.4187,"dispatchToBlockedOrPreparationMs":494.8535,"firstNewGenerationToCleanupDrainedMs":50.4983,"firstPhysicalMutationToFirstNewGenerationMs":248.0639,"presentationToStrictCompletionMs":0}         |
| 19  | 939.606 / 1090.224  | 689.071 / 834.499   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":44.9648,"dispatchToBlockedOrPreparationMs":404.2762,"firstNewGenerationToCleanupDrainedMs":41.4998,"firstPhysicalMutationToFirstNewGenerationMs":198.33,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":55.9181,"dispatchToBlockedOrPreparationMs":480.5868,"firstNewGenerationToCleanupDrainedMs":61.0476,"firstPhysicalMutationToFirstNewGenerationMs":236.9465,"presentationToStrictCompletionMs":0}         |
| 20  | 832.213 / 988.184   | 1009.718 / 1215.989 | 177.505 / 227.805 | {"blockedOrPreparationToFirstPhysicalMutationMs":238.13,"dispatchToBlockedOrPreparationMs":453.5158,"firstNewGenerationToCleanupDrainedMs":227.8665,"firstPhysicalMutationToFirstNewGenerationMs":90.2054,"presentationToStrictCompletionMs":177.5049}   | {"blockedOrPreparationToFirstPhysicalMutationMs":280.0552,"dispatchToBlockedOrPreparationMs":542.6941,"firstNewGenerationToCleanupDrainedMs":294.6826,"firstPhysicalMutationToFirstNewGenerationMs":98.5575,"presentationToStrictCompletionMs":227.8049} |
| 21  | 189.077 / 231.103   | 453.071 / 558.991   | 263.994 / 327.888 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":126.7527,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":263.9937}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":155.1385,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":327.8881}            |
| 22  | 162.812 / 205.324   | 162.812 / 205.324   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":162.8116,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":205.3243,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 288.590 / 324.124   | 288.590 / 324.124   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":288.5903,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":324.1239,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 662.606 / 804.298   | 796.742 / 977.717   | 134.135 / 173.419 | {"blockedOrPreparationToFirstPhysicalMutationMs":45.811,"dispatchToBlockedOrPreparationMs":411.9901,"firstNewGenerationToCleanupDrainedMs":185.9268,"firstPhysicalMutationToFirstNewGenerationMs":153.0138,"presentationToStrictCompletionMs":134.1354}  | {"blockedOrPreparationToFirstPhysicalMutationMs":54.5674,"dispatchToBlockedOrPreparationMs":512.8794,"firstNewGenerationToCleanupDrainedMs":233.7439,"firstPhysicalMutationToFirstNewGenerationMs":176.5267,"presentationToStrictCompletionMs":173.4192} |
| 25  | 861.812 / 1035.681  | 1006.636 / 1199.491 | 144.824 / 163.810 | {"blockedOrPreparationToFirstPhysicalMutationMs":21.7457,"dispatchToBlockedOrPreparationMs":454.0392,"firstNewGenerationToCleanupDrainedMs":195.2621,"firstPhysicalMutationToFirstNewGenerationMs":335.5889,"presentationToStrictCompletionMs":230.4107} | {"blockedOrPreparationToFirstPhysicalMutationMs":23.6869,"dispatchToBlockedOrPreparationMs":572.4159,"firstNewGenerationToCleanupDrainedMs":216.5101,"firstPhysicalMutationToFirstNewGenerationMs":386.8785,"presentationToStrictCompletionMs":256.65}   |
| 26  | 1068.065 / 1284.469 | 1011.510 / 1227.072 | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3468,"dispatchToBlockedOrPreparationMs":408.0517,"firstNewGenerationToCleanupDrainedMs":216.359,"firstPhysicalMutationToFirstNewGenerationMs":339.7526,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":73.4369,"dispatchToBlockedOrPreparationMs":561.6782,"firstNewGenerationToCleanupDrainedMs":210.87,"firstPhysicalMutationToFirstNewGenerationMs":381.0871,"presentationToStrictCompletionMs":0}          |
| 27  | 567.395 / 673.938   | 748.586 / 900.937   | 181.191 / 227.000 | {"blockedOrPreparationToFirstPhysicalMutationMs":50.2463,"dispatchToBlockedOrPreparationMs":347.906,"firstNewGenerationToCleanupDrainedMs":236.1733,"firstPhysicalMutationToFirstNewGenerationMs":114.2602,"presentationToStrictCompletionMs":181.1906}  | {"blockedOrPreparationToFirstPhysicalMutationMs":1.0265,"dispatchToBlockedOrPreparationMs":491.6174,"firstNewGenerationToCleanupDrainedMs":314.2638,"firstPhysicalMutationToFirstNewGenerationMs":94.0295,"presentationToStrictCompletionMs":226.9995}   |
| 28  | 870.041 / 975.543   | 919.042 / 1080.175  | 49.000 / 104.632  | {"blockedOrPreparationToFirstPhysicalMutationMs":57.5457,"dispatchToBlockedOrPreparationMs":396.541,"firstNewGenerationToCleanupDrainedMs":175.0451,"firstPhysicalMutationToFirstNewGenerationMs":289.9098,"presentationToStrictCompletionMs":104.3887}  | {"blockedOrPreparationToFirstPhysicalMutationMs":56.6935,"dispatchToBlockedOrPreparationMs":484.432,"firstNewGenerationToCleanupDrainedMs":208.0057,"firstPhysicalMutationToFirstNewGenerationMs":331.0436,"presentationToStrictCompletionMs":160.1736}  |
| 29  | 821.599 / 1034.817  | 958.330 / 1200.473  | 136.731 / 165.656 | {"blockedOrPreparationToFirstPhysicalMutationMs":19.8893,"dispatchToBlockedOrPreparationMs":450.0967,"firstNewGenerationToCleanupDrainedMs":178.5163,"firstPhysicalMutationToFirstNewGenerationMs":309.8279,"presentationToStrictCompletionMs":217.7862} | {"blockedOrPreparationToFirstPhysicalMutationMs":25.6725,"dispatchToBlockedOrPreparationMs":580.9947,"firstNewGenerationToCleanupDrainedMs":218.6975,"firstPhysicalMutationToFirstNewGenerationMs":375.108,"presentationToStrictCompletionMs":256.8421}  |
| 30  | 525.686 / 626.463   | 720.991 / 855.807   | 195.306 / 229.344 | {"blockedOrPreparationToFirstPhysicalMutationMs":48.3199,"dispatchToBlockedOrPreparationMs":350.8517,"firstNewGenerationToCleanupDrainedMs":249.0011,"firstPhysicalMutationToFirstNewGenerationMs":72.8185,"presentationToStrictCompletionMs":195.3056}  | {"blockedOrPreparationToFirstPhysicalMutationMs":52.7647,"dispatchToBlockedOrPreparationMs":437.9015,"firstNewGenerationToCleanupDrainedMs":290.4613,"firstPhysicalMutationToFirstNewGenerationMs":74.6795,"presentationToStrictCompletionMs":229.3436}  |
| 31  | 601.567 / 719.204   | 601.567 / 719.204   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.0276,"dispatchToBlockedOrPreparationMs":414.816,"firstNewGenerationToCleanupDrainedMs":42.5589,"firstPhysicalMutationToFirstNewGenerationMs":96.1643,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":45.7602,"dispatchToBlockedOrPreparationMs":508.5183,"firstNewGenerationToCleanupDrainedMs":49.8653,"firstPhysicalMutationToFirstNewGenerationMs":115.0601,"presentationToStrictCompletionMs":0}         |
| 32  | 185.789 / 256.322   | 482.117 / 599.604   | 296.327 / 343.281 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":124.7113,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":296.3274}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":163.2485,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":343.2813}            |
| 33  | 281.976 / 295.183   | 281.976 / 295.183   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":281.9764,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":295.183,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 490.707 / 666.927    | 176.220  | 15 / 15           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 5 / 4             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 15                  | 1            | 812.718 / 917.795    | 105.077  | 19 / 20           | 1            |
| 5   | 13 / 14                  | 1            | 861.372 / 992.067    | 130.695  | 18 / 19           | 1            |
| 6   | 17 / 16                  | -1           | 917.432 / 1037.042   | 119.610  | 22 / 21           | -1           |
| 7   | 18 / 16                  | -2           | 936.376 / 1009.811   | 73.435   | 23 / 21           | -2           |
| 8   | 18 / 16                  | -2           | 927.996 / 1009.053   | 81.057   | 23 / 21           | -2           |
| 9   | 17 / 17                  | 0            | 915.133 / 1026.273   | 111.140  | 22 / 22           | 0            |
| 10  | 10 / 11                  | 1            | 625.056 / 739.922    | 114.866  | 14 / 15           | 1            |
| 11  | 3 / 3                    | 0            | 196.959 / 222.378    | 25.419   | 10 / 9            | -1           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 10                  | 0            | 588.244 / 712.244    | 124.000  | 11 / 11           | 0            |
| 14  | 18 / 18                  | 0            | 863.545 / 1166.626   | 303.081  | 23 / 23           | 0            |
| 15  | 14 / 14                  | 0            | 687.504 / 847.520    | 160.016  | 17 / 26           | 9            |
| 16  | 14 / 15                  | 1            | 641.100 / 1048.111   | 407.010  | 18 / 18           | 0            |
| 17  | 14 / 14                  | 0            | 651.714 / 874.533    | 222.819  | 18 / 18           | 0            |
| 18  | 14 / 14                  | 0            | 634.077 / 799.336    | 165.259  | 18 / 18           | 0            |
| 19  | 14 / 14                  | 0            | 647.571 / 773.451    | 125.880  | 21 / 19           | -2           |
| 20  | 16 / 16                  | 0            | 781.851 / 921.307    | 139.456  | 21 / 21           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 24  | 11 / 12                  | 1            | 610.815 / 743.973    | 133.159  | 15 / 16           | 1            |
| 25  | 15 / 15                  | 0            | 811.374 / 982.981    | 171.608  | 20 / 20           | 0            |
| 26  | 14 / 15                  | 1            | 795.151 / 1016.202   | 221.051  | 19 / 20           | 1            |
| 27  | 10 / 10                  | 0            | 512.413 / 586.673    | 74.261   | 16 / 16           | 0            |
| 28  | 12 / 12                  | 0            | 743.996 / 872.169    | 128.173  | 17 / 17           | 0            |
| 29  | 15 / 15                  | 0            | 779.814 / 981.775    | 201.961  | 20 / 20           | 0            |
| 30  | 9 / 10                   | 1            | 471.990 / 565.346    | 93.356   | 15 / 15           | 0            |
| 31  | 10 / 10                  | 0            | 559.008 / 669.339    | 110.331  | 11 / 11           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 9 / 11            | 2            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 214.495 / 229.891 | 15.396   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 245.001 / 266.919 | 21.918   |
| 6   | 1 / 1                | 0     | 5 / 5              | 0     | 345.614 / 397.028 | 51.413   |
| 7   | 1 / 1                | 0     | 5 / 5              | 0     | 354.159 / 386.721 | 32.562   |
| 8   | 1 / 1                | 0     | 5 / 5              | 0     | 351.013 / 384.459 | 33.447   |
| 9   | 1 / 1                | 0     | 5 / 5              | 0     | 332.710 / 403.848 | 71.138   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 5 / 5              | 0     | 221.958 / 354.233 | 132.276  |
| 15  | 1 / 1                | 0     | 4 / 4              | 0     | 223.742 / 283.395 | 59.653   |
| 16  | 1 / 1                | 0     | 4 / 4              | 0     | 205.174 / 256.974 | 51.799   |
| 17  | 1 / 1                | 0     | 4 / 4              | 0     | 204.045 / 272.009 | 67.964   |
| 18  | 1 / 1                | 0     | 4 / 4              | 0     | 198.058 / 248.126 | 50.068   |
| 19  | 1 / 1                | 0     | 4 / 4              | 0     | 198.300 / 237.173 | 38.873   |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 283.340 / 323.684 | 40.344   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 203.305 / 227.833 | 24.528   |
| 26  | 2 / 2                | 0     | 3 / 3              | 0     | 220.362 / 249.889 | 29.528   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 3 / 3              | 0     | 284.083 / 347.651 | 63.567   |
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

### renderscale-tuning-nvidia-2026-09-13T19-20-24-614Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":65391,"leftPath":"NativeOriginal","referenceFrame":65700,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### renderscale-tuning-nvidia-2026-09-13T19-20-24-614Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":69151,"leftPath":"NativeOriginal","referenceFrame":69490,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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
| nvidia | 1    | processPrivateMiB | 16430.2890625 / 16245.953125 / -184.3359375    | 17063.30859375 / 16836.5859375 / -226.72265625 | -42.387               |
| nvidia | 1    | systemCommitMiB   | 57091.7109375 / 56790.00390625 / -301.70703125 | 54768.60546875 / 54660.30078125 / -108.3046875 | 193.402               |
| nvidia | 1    | dxgiUsageMiB      | 4191.01953125 / 3286.94921875 / -904.0703125   | 4257.390625 / 3415.01171875 / -842.37890625    | 61.691                |
| nvidia | 1    | liveTextures      | 0 / 247 / 247                                  | 0 / 209 / 209                                  | -38                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2405.307025909424 / 2405.307025909424      | 0 / 2265.7227210998535 / 2265.7227210998535    | -139.584              |
| nvidia | 2    | processPrivateMiB | 16646.34375 / 16475.06640625 / -171.27734375   | 17194.94140625 / 16866.9609375 / -327.98046875 | -156.703              |
| nvidia | 2    | systemCommitMiB   | 57301.5625 / 57090.9921875 / -210.5703125      | 55611.83203125 / 54697.69921875 / -914.1328125 | -703.563              |
| nvidia | 2    | dxgiUsageMiB      | 3680.19921875 / 3536.84765625 / -143.3515625   | 3722.0625 / 3310.13671875 / -411.92578125      | -268.574              |
| nvidia | 2    | liveTextures      | 0 / 241 / 241                                  | 0 / 217 / 217                                  | -24                   |
| nvidia | 2    | liveTextureMiB    | 0 / 2379.9213676452637 / 2379.9213676452637    | 0 / 2281.5979194641113 / 2281.5979194641113    | -98.323               |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2251        | 1813        | -438        |
| cpu/compactPresentationContract/reuses                  | 2229        | 1789        | -440        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 153         | 146         | -7          |
| cpu/generationResourceValidation/fullValidations        | 693         | 664         | -29         |
| cpu/generationResourceValidation/stableChecks           | 8523        | 6925        | -1598       |
| cpu/generationResourceValidation/stableHits             | 8508        | 6912        | -1596       |
| cpu/generationResourceValidation/stableMisses           | 15          | 13          | -2          |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 1           | 3           | 2           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4360        | 3416        | -944        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4360        | 3416        | -944        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4318        | 3374        | -944        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4360        | 3416        | -944        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4337        | 3395        | -942        |
| cpu/stateProportionalSafety/memoryTrim/services         | 23          | 21          | -2          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 30          | 30          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4330        | 3386        | -944        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4266        | 3322        | -944        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 93          | 94          | 1           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4345        | 3401        | -944        |
| cpu/strongStereoPacket/captures                         | 4648        | 3770        | -878        |
| cpu/strongStereoPacket/commitAccepts                    | 4484        | 3605        | -879        |
| cpu/strongStereoPacket/commitRejects                    | 64          | 65          | 1           |
| cpu/strongStereoPacket/commitValidations                | 4548        | 3670        | -878        |
| cpu/strongStereoPacket/cycleReuses                      | 2286        | 1847        | -439        |
| cpu/strongStereoPacket/fastSkips                        | 4072        | 3062        | -1010       |
| cpu/strongStereoPacket/invalidations                    | 4906        | 3998        | -908        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 92          | 93          | 1           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2270        | 1830        | -440        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.756       | 1.794       | 0.039       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 68.900      | 33.800      | -35.100     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.158       | 0.164       | 0.006       |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 18.900      | 18.700      | -0.200      |
| cpu/window/currentFrame                                 | 21210       | 65700       | 44490       |
| cpu/window/elapsedFrames                                | 4359        | 3416        | -943        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 16851       | 62284       | 45433       |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 21211       | 65701       | 44490       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6300781344  | 4908925296  | -1391856048 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4966        | 3869        | -1097       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12614434560 | 9827879040  | -2786555520 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.305       | 0.106       | -0.199      |
| gpu/item5ActiveFSRCopies/activePixels                   | 12576906800 | 3356517120  | -9220389680 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 28649890000 | 28166868480 | -483021520  |
| gpu/item5ActiveFSRCopies/copyCalls                      | 16230       | 9798        | -6432       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2138        | 1633        | -505        |
| gpu/item7EarlyHAM/executedClears                        | 2004        | 1634        | -370        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2004        | 1634        | -370        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4328        | 3456        | -872        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4360        | 3417        | -943        |
| gpu/runtimeFSRSharedGuides/directGuideInputs            | n/a         | 2612        | n/a         |
| gpu/runtimeFSRSharedGuides/directGuidePixels            | n/a         | 6245376960  | n/a         |
| gpu/runtimeFSRSharedGuides/enabled                      | n/a         | true        | n/a         |
| gpu/runtimeFSRSharedGuides/fallbackGuideCopies          | n/a         | 7316        | n/a         |
| gpu/runtimeFSRSharedGuides/importFailures               | n/a         | 0           | n/a         |
| gpu/startFrame                                          | 16851       | 62284       | 45433       |
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
| texture/createdCount                                    | 3977        | 3890        | -87         |
| texture/createdEstimatedBytes                           | 38776359208 | 38547505504 | -228853704  |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3730        | 3681        | -49         |
| texture/destroyedEstimatedBytes                         | 36254211988 | 36171723036 | -82488952   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 896         | 866         | -30         |
| texture/liveTextureRecordCount                          | 247         | 209         | -38         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 38          | 2           | -36         |
| texture/niSourceTextureMatchedEstimatedBytes            | 146495640   | 131088      | -146364552  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1450        | 1484        | 34          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 247         | 209         | -38         |
| texture/outstandingEstimatedBytes                       | 2522147220  | 2375782468  | -146364752  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 1           | 3           | 2           |
| texture/supported                                       | true        | true        | n/a         |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2244        | 1830        | -414        |
| cpu/compactPresentationContract/reuses                  | 2220        | 1806        | -414        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 156         | 145         | -11         |
| cpu/generationResourceValidation/fullValidations        | 684         | 673         | -11         |
| cpu/generationResourceValidation/stableChecks           | 8465        | 7035        | -1430       |
| cpu/generationResourceValidation/stableHits             | 8442        | 7021        | -1421       |
| cpu/generationResourceValidation/stableMisses           | 23          | 14          | -9          |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 2           | 4           | 2           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4346        | 3472        | -874        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4346        | 3472        | -874        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4304        | 3430        | -874        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4346        | 3472        | -874        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4325        | 3451        | -874        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 30          | 30          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4316        | 3442        | -874        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4252        | 3378        | -874        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 94          | 0           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4331        | 3457        | -874        |
| cpu/strongStereoPacket/captures                         | 4633        | 3805        | -828        |
| cpu/strongStereoPacket/commitAccepts                    | 4466        | 3642        | -824        |
| cpu/strongStereoPacket/commitRejects                    | 66          | 62          | -4          |
| cpu/strongStereoPacket/commitValidations                | 4532        | 3704        | -828        |
| cpu/strongStereoPacket/cycleReuses                      | 2277        | 1863        | -414        |
| cpu/strongStereoPacket/fastSkips                        | 4059        | 3139        | -920        |
| cpu/strongStereoPacket/invalidations                    | 4893        | 4045        | -848        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 93          | 95          | 2           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2263        | 1847        | -416        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.652       | 1.801       | 0.149       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 27.400      | 94.200      | 66.800      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.155       | 0.160       | 0.004       |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 22.100      | 1.700       | -20.400     |
| cpu/window/currentFrame                                 | 25971       | 69490       | 43519       |
| cpu/window/elapsedFrames                                | 4346        | 3472        | -874        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 21625       | 66018       | 44393       |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 25972       | 69491       | 43519       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6258911472  | 4974902064  | -1284009408 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4933        | 3921        | -1012       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12530609280 | 9959967360  | -2570641920 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.305       | 0.107       | -0.199      |
| gpu/item5ActiveFSRCopies/activePixels                   | 12379172080 | 3416755960  | -8962416120 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 28161781520 | 28589260040 | 427478520   |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15960       | 9916        | -6044       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2103        | 1647        | -456        |
| gpu/item7EarlyHAM/executedClears                        | 2014        | 1642        | -372        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2014        | 1642        | -372        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4310        | 3482        | -828        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4347        | 3473        | -874        |
| gpu/runtimeFSRSharedGuides/directGuideInputs            | n/a         | 2684        | n/a         |
| gpu/runtimeFSRSharedGuides/directGuidePixels            | n/a         | 6428268480  | n/a         |
| gpu/runtimeFSRSharedGuides/enabled                      | n/a         | true        | n/a         |
| gpu/runtimeFSRSharedGuides/fallbackGuideCopies          | n/a         | 7396        | n/a         |
| gpu/runtimeFSRSharedGuides/importFailures               | n/a         | 0           | n/a         |
| gpu/startFrame                                          | 21625       | 66018       | 44393       |
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
| texture/createdCount                                    | 3958        | 3920        | -38         |
| texture/createdEstimatedBytes                           | 38734100840 | 38628805568 | -105295272  |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3717        | 3703        | -14         |
| texture/destroyedEstimatedBytes                         | 36238572412 | 36236376748 | -2195664    |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 892         | 874         | -18         |
| texture/liveTextureRecordCount                          | 241         | 217         | -24         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 34          | 10          | -24         |
| texture/niSourceTextureMatchedEstimatedBytes            | 119877048   | 16777440    | -103099608  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1484        | 1483        | -1          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 241         | 217         | -24         |
| texture/outstandingEstimatedBytes                       | 2495528428  | 2392428820  | -103099608  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 2           | 4           | 2           |
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
