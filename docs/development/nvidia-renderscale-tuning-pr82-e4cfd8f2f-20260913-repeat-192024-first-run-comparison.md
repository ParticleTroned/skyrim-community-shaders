# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                        | Candidate                                                                                                       |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-2026-09-13T18-46-33-800Z                                                              | renderscale-tuning-nvidia-2026-09-13T19-20-24-614Z                                                              |
| Renderer base           | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        |
| Main-VR base/equivalent | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        |
| Compiled source         | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        | e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41                                                                        |
| Build ID                | 2f6432ddbcbd05826c1fb1ccd66f25e14755d718a5e760a58cbae58b7f625efd                                                | 2f6432ddbcbd05826c1fb1ccd66f25e14755d718a5e760a58cbae58b7f625efd                                                |
| DLL SHA-256             | da3a9b54ee34dee836e3a2843d33acb8d33e49ab5754be06c05f37e038fbcdb0                                                | da3a9b54ee34dee836e3a2843d33acb8d33e49ab5754be06c05f37e038fbcdb0                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-13T18-46-33-800Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-13T19-20-24-614Z |

Assessment limits: retained_context_not_matched:scene; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 917.809/940.919 | 2.518        | 15/14       | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 897.031/938.066 | 4.575        | 15/15       | 0/0          | 0/0                 | none             | MET/MET             |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 845.346   | 843.301   | -2.045   | -0.242  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.200    | 13.120    | -0.080   | -0.606  |
| nvidia | 1    | Relatch proof total        | ms          | 21133.656 | 21082.528 | -51.128  | -0.242  |
| nvidia | 1    | Relatch proof total        | frames      | 330       | 328       | -2       | -0.606  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 917.809   | 940.919   | 23.110   | 2.518   |
| nvidia | 1    | Strict completion mean     | frames      | 14.848    | 14.758    | -0.091   | -0.612  |
| nvidia | 1    | Strict completion total    | ms          | 30287.710 | 31050.342 | 762.632  | 2.518   |
| nvidia | 1    | Strict completion total    | frames      | 490       | 487       | -3       | -0.612  |
| nvidia | 1    | Stretch completed episodes | episodes    | 19        | 18        | -1       | -5.263  |
| nvidia | 1    | Stretch completed total    | frames      | 64        | 59        | -5       | -7.813  |
| nvidia | 1    | Stretch completed total    | ms          | 4716.293  | 4780.233  | 63.940   | 1.356   |
| nvidia | 1    | Stretch longest episode    | ms          | 384.767   | 428.950   | 44.183   | 11.483  |
| nvidia | 2    | Relatch proof mean         | ms          | 813.458   | 847.314   | 33.856   | 4.162   |
| nvidia | 2    | Relatch proof mean         | frames      | 13.040    | 13.280    | 0.240    | 1.840   |
| nvidia | 2    | Relatch proof total        | ms          | 20336.445 | 21182.854 | 846.409  | 4.162   |
| nvidia | 2    | Relatch proof total        | frames      | 326       | 332       | 6        | 1.840   |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 897.031   | 938.066   | 41.035   | 4.575   |
| nvidia | 2    | Strict completion mean     | frames      | 14.576    | 15        | 0.424    | 2.911   |
| nvidia | 2    | Strict completion total    | ms          | 29602.010 | 30956.181 | 1354.171 | 4.575   |
| nvidia | 2    | Strict completion total    | frames      | 481       | 495       | 14       | 2.911   |
| nvidia | 2    | Stretch completed episodes | episodes    | 18        | 18        | 0        | 0       |
| nvidia | 2    | Stretch completed total    | frames      | 62        | 62        | 0        | 0       |
| nvidia | 2    | Stretch completed total    | ms          | 4713.132  | 4869.833  | 156.701  | 3.325   |
| nvidia | 2    | Stretch longest episode    | ms          | 422.739   | 403.848   | -18.891  | -4.469  |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 865.321 / 884.753   | 19.432   | 2.246   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 205.391 / 213.208   | 7.816    | 3.806   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 287.683 / 304.752   | 17.069   | 5.933   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1195.595 / 1316.527 | 120.932  | 10.115  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1412.836 / 1307.553 | -105.284 | -7.452  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1514.293 / 1393.632 | -120.661 | -7.968  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1537.476 / 1395.400 | -142.075 | -9.241  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1502.831 / 1483.574 | -19.256  | -1.281  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1416.121 / 1376.046 | -40.076  | -2.830  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 950.429 / 946.396   | -4.034   | -0.424  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 530.933 / 582.436   | 51.503   | 9.700   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 207.161 / 215.409   | 8.248    | 3.981   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 872.516 / 780.017   | -92.500  | -10.601 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1318.280 / 1420.508 | 102.228  | 7.755   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 989.509 / 1058.724  | 69.215   | 6.995   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 1044.912 / 1113.634 | 68.723   | 6.577   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 1015.247 / 1157.341 | 142.093  | 13.996  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 994.249 / 1044.366  | 50.116   | 5.041   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 1116.058 / 1166.553 | 50.495   | 4.524   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1180.889 / 1042.200 | -138.689 | -11.744 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 565.780 / 637.899   | 72.118   | 12.747  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 206.011 / 227.384   | 21.373   | 10.375  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 291.841 / 296.170   | 4.329    | 1.483   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 921.320 / 986.180   | 64.860   | 7.040   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1219.526 / 1354.466 | 134.940  | 11.065  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1177.035 / 1262.287 | 85.252   | 7.243   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 856.734 / 921.173   | 64.440   | 7.522   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 1101.299 / 1172.554 | 71.255   | 6.470   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1240.951 / 1270.816 | 29.865   | 2.407   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 892.782 / 910.395   | 17.613   | 1.973   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 725.622 / 759.387   | 33.765   | 4.653   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 588.114 / 685.512   | 97.398   | 16.561  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 342.964 / 363.093   | 20.129   | 5.869   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 649.901 / 667.350   | 865.321 / 884.753   | 215.420 / 217.403 | {"blockedOrPreparationToFirstPhysicalMutationMs":56.066,"dispatchToBlockedOrPreparationMs":416.6559,"firstNewGenerationToCleanupDrainedMs":273.7023,"firstPhysicalMutationToFirstNewGenerationMs":118.897,"presentationToStrictCompletionMs":215.4199}   | {"blockedOrPreparationToFirstPhysicalMutationMs":57.5055,"dispatchToBlockedOrPreparationMs":455.6263,"firstNewGenerationToCleanupDrainedMs":279.0581,"firstPhysicalMutationToFirstNewGenerationMs":92.563,"presentationToStrictCompletionMs":217.4031}   |
| 2   | 205.391 / 213.208   | 205.391 / 213.208   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":205.3914,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":213.2077,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 287.683 / 304.752   | 287.683 / 304.752   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":287.6829,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":304.7515,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 1000.028 / 1104.153 | 1102.722 / 1214.556 | 102.694 / 110.403 | {"blockedOrPreparationToFirstPhysicalMutationMs":56.0743,"dispatchToBlockedOrPreparationMs":468.7376,"firstNewGenerationToCleanupDrainedMs":206.8418,"firstPhysicalMutationToFirstNewGenerationMs":371.0682,"presentationToStrictCompletionMs":195.5675} | {"blockedOrPreparationToFirstPhysicalMutationMs":62.1883,"dispatchToBlockedOrPreparationMs":479.2731,"firstNewGenerationToCleanupDrainedMs":223.4318,"firstPhysicalMutationToFirstNewGenerationMs":449.6627,"presentationToStrictCompletionMs":212.3746} |
| 5   | 1165.835 / 1105.179 | 1321.772 / 1213.348 | 155.936 / 108.169 | {"blockedOrPreparationToFirstPhysicalMutationMs":56.0452,"dispatchToBlockedOrPreparationMs":451.8102,"firstNewGenerationToCleanupDrainedMs":208.5631,"firstPhysicalMutationToFirstNewGenerationMs":605.3531,"presentationToStrictCompletionMs":247.0013} | {"blockedOrPreparationToFirstPhysicalMutationMs":62.64,"dispatchToBlockedOrPreparationMs":508.255,"firstNewGenerationToCleanupDrainedMs":215.7048,"firstPhysicalMutationToFirstNewGenerationMs":426.7483,"presentationToStrictCompletionMs":202.3735}    |
| 6   | 1316.770 / 1133.029 | 1424.432 / 1299.695 | 107.662 / 166.665 | {"blockedOrPreparationToFirstPhysicalMutationMs":0.5339,"dispatchToBlockedOrPreparationMs":474.9851,"firstNewGenerationToCleanupDrainedMs":212.7671,"firstPhysicalMutationToFirstNewGenerationMs":736.146,"presentationToStrictCompletionMs":197.5226}   | {"blockedOrPreparationToFirstPhysicalMutationMs":56.577,"dispatchToBlockedOrPreparationMs":444.8335,"firstNewGenerationToCleanupDrainedMs":220.1678,"firstPhysicalMutationToFirstNewGenerationMs":578.1163,"presentationToStrictCompletionMs":260.6026}  |
| 7   | 1286.168 / 1178.458 | 1448.324 / 1292.039 | 162.157 / 113.582 | {"blockedOrPreparationToFirstPhysicalMutationMs":65.2624,"dispatchToBlockedOrPreparationMs":467.6842,"firstNewGenerationToCleanupDrainedMs":214.3658,"firstPhysicalMutationToFirstNewGenerationMs":701.0118,"presentationToStrictCompletionMs":251.308}  | {"blockedOrPreparationToFirstPhysicalMutationMs":58.328,"dispatchToBlockedOrPreparationMs":441.6724,"firstNewGenerationToCleanupDrainedMs":222.1939,"firstPhysicalMutationToFirstNewGenerationMs":569.845,"presentationToStrictCompletionMs":216.9426}   |
| 8   | 1245.985 / 1265.465 | 1412.638 / 1371.735 | 166.653 / 106.270 | {"blockedOrPreparationToFirstPhysicalMutationMs":58.534,"dispatchToBlockedOrPreparationMs":437.085,"firstNewGenerationToCleanupDrainedMs":218.3591,"firstPhysicalMutationToFirstNewGenerationMs":698.6598,"presentationToStrictCompletionMs":256.8462}   | {"blockedOrPreparationToFirstPhysicalMutationMs":58.5687,"dispatchToBlockedOrPreparationMs":524.9177,"firstNewGenerationToCleanupDrainedMs":212.9461,"firstPhysicalMutationToFirstNewGenerationMs":575.3024,"presentationToStrictCompletionMs":218.1099} |
| 9   | 1168.799 / 1163.412 | 1327.274 / 1281.721 | 158.476 / 118.309 | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7067,"dispatchToBlockedOrPreparationMs":414.5257,"firstNewGenerationToCleanupDrainedMs":208.9769,"firstPhysicalMutationToFirstNewGenerationMs":648.065,"presentationToStrictCompletionMs":247.3226}  | {"blockedOrPreparationToFirstPhysicalMutationMs":62.1668,"dispatchToBlockedOrPreparationMs":457.94,"firstNewGenerationToCleanupDrainedMs":222.3196,"firstPhysicalMutationToFirstNewGenerationMs":539.2943,"presentationToStrictCompletionMs":212.634}    |
| 10  | 772.429 / 770.587   | 950.429 / 946.396   | 178.000 / 175.808 | {"blockedOrPreparationToFirstPhysicalMutationMs":54.0209,"dispatchToBlockedOrPreparationMs":453.6247,"firstNewGenerationToCleanupDrainedMs":234.0944,"firstPhysicalMutationToFirstNewGenerationMs":208.6892,"presentationToStrictCompletionMs":178.0003} | {"blockedOrPreparationToFirstPhysicalMutationMs":55.1533,"dispatchToBlockedOrPreparationMs":428.9503,"firstNewGenerationToCleanupDrainedMs":230.8244,"firstPhysicalMutationToFirstNewGenerationMs":231.4676,"presentationToStrictCompletionMs":175.8084} |
| 11  | 215.232 / 229.505   | 530.933 / 582.436   | 315.701 / 352.930 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3426,"dispatchToBlockedOrPreparationMs":164.0857,"firstNewGenerationToCleanupDrainedMs":315.9811,"firstPhysicalMutationToFirstNewGenerationMs":47.5233,"presentationToStrictCompletionMs":315.7009}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1174,"dispatchToBlockedOrPreparationMs":173.6158,"firstNewGenerationToCleanupDrainedMs":354.0747,"firstPhysicalMutationToFirstNewGenerationMs":51.6278,"presentationToStrictCompletionMs":352.9304}   |
| 12  | 207.161 / 215.409   | 207.161 / 215.409   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":207.1608,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":215.4088,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 872.516 / 780.017   | 872.516 / 780.017   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":166.9928,"dispatchToBlockedOrPreparationMs":488.4619,"firstNewGenerationToCleanupDrainedMs":52.3302,"firstPhysicalMutationToFirstNewGenerationMs":164.7314,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":60.8896,"dispatchToBlockedOrPreparationMs":529.7042,"firstNewGenerationToCleanupDrainedMs":55.3579,"firstPhysicalMutationToFirstNewGenerationMs":134.0651,"presentationToStrictCompletionMs":0}         |
| 14  | 1154.777 / 1420.508 | 1263.983 / 1352.975 | 109.207 / 0       | {"blockedOrPreparationToFirstPhysicalMutationMs":62.3146,"dispatchToBlockedOrPreparationMs":491.7984,"firstNewGenerationToCleanupDrainedMs":224.7783,"firstPhysicalMutationToFirstNewGenerationMs":485.0919,"presentationToStrictCompletionMs":163.5036} | {"blockedOrPreparationToFirstPhysicalMutationMs":57.3612,"dispatchToBlockedOrPreparationMs":508.2943,"firstNewGenerationToCleanupDrainedMs":270.7189,"firstPhysicalMutationToFirstNewGenerationMs":516.6006,"presentationToStrictCompletionMs":0}        |
| 15  | 989.509 / 1058.724  | 831.235 / 873.811   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":55.3282,"dispatchToBlockedOrPreparationMs":487.2901,"firstNewGenerationToCleanupDrainedMs":51.5525,"firstPhysicalMutationToFirstNewGenerationMs":237.0642,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":57.473,"dispatchToBlockedOrPreparationMs":495.9964,"firstNewGenerationToCleanupDrainedMs":61.7808,"firstPhysicalMutationToFirstNewGenerationMs":258.5611,"presentationToStrictCompletionMs":0}          |
| 16  | 1044.912 / 1113.634 | 833.007 / 891.685   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":56.5038,"dispatchToBlockedOrPreparationMs":491.1083,"firstNewGenerationToCleanupDrainedMs":51.1814,"firstPhysicalMutationToFirstNewGenerationMs":234.2137,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":65.0882,"dispatchToBlockedOrPreparationMs":512.2377,"firstNewGenerationToCleanupDrainedMs":52.9686,"firstPhysicalMutationToFirstNewGenerationMs":261.3905,"presentationToStrictCompletionMs":0}         |
| 17  | 1015.247 / 1157.341 | 853.356 / 967.599   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":64.1835,"dispatchToBlockedOrPreparationMs":500.559,"firstNewGenerationToCleanupDrainedMs":52.9599,"firstPhysicalMutationToFirstNewGenerationMs":235.6535,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":57.6955,"dispatchToBlockedOrPreparationMs":520.3593,"firstNewGenerationToCleanupDrainedMs":60.3859,"firstPhysicalMutationToFirstNewGenerationMs":329.1583,"presentationToStrictCompletionMs":0}         |
| 18  | 994.249 / 1044.366  | 834.353 / 881.764   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":57.334,"dispatchToBlockedOrPreparationMs":481.9857,"firstNewGenerationToCleanupDrainedMs":61.6945,"firstPhysicalMutationToFirstNewGenerationMs":233.339,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":60.7126,"dispatchToBlockedOrPreparationMs":494.3268,"firstNewGenerationToCleanupDrainedMs":52.0375,"firstPhysicalMutationToFirstNewGenerationMs":274.687,"presentationToStrictCompletionMs":0}          |
| 19  | 1116.058 / 1166.553 | 844.417 / 872.154   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":59.4687,"dispatchToBlockedOrPreparationMs":499.1877,"firstNewGenerationToCleanupDrainedMs":51.7148,"firstPhysicalMutationToFirstNewGenerationMs":234.0461,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":58.7832,"dispatchToBlockedOrPreparationMs":505.2963,"firstNewGenerationToCleanupDrainedMs":54.2278,"firstPhysicalMutationToFirstNewGenerationMs":253.8472,"presentationToStrictCompletionMs":0}         |
| 20  | 958.945 / 741.601   | 1180.889 / 1042.200 | 221.944 / 300.599 | {"blockedOrPreparationToFirstPhysicalMutationMs":269.2453,"dispatchToBlockedOrPreparationMs":537.5705,"firstNewGenerationToCleanupDrainedMs":281.0827,"firstPhysicalMutationToFirstNewGenerationMs":92.9907,"presentationToStrictCompletionMs":221.944}  | {"blockedOrPreparationToFirstPhysicalMutationMs":1.1928,"dispatchToBlockedOrPreparationMs":535.0174,"firstNewGenerationToCleanupDrainedMs":384.5367,"firstPhysicalMutationToFirstNewGenerationMs":121.453,"presentationToStrictCompletionMs":300.5993}   |
| 21  | 238.252 / 262.473   | 565.780 / 637.899   | 327.529 / 375.425 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.6851,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":327.5286}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.918,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":375.4253}             |
| 22  | 206.011 / 227.384   | 206.011 / 227.384   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":206.0108,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":227.3839,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 291.841 / 296.170   | 291.841 / 296.170   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":291.8411,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.1702,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 760.825 / 815.965   | 921.320 / 986.180   | 160.495 / 170.215 | {"blockedOrPreparationToFirstPhysicalMutationMs":55.4801,"dispatchToBlockedOrPreparationMs":483.4348,"firstNewGenerationToCleanupDrainedMs":211.3548,"firstPhysicalMutationToFirstNewGenerationMs":171.0503,"presentationToStrictCompletionMs":160.4949} | {"blockedOrPreparationToFirstPhysicalMutationMs":62.3675,"dispatchToBlockedOrPreparationMs":523.2926,"firstNewGenerationToCleanupDrainedMs":221.9487,"firstPhysicalMutationToFirstNewGenerationMs":178.5711,"presentationToStrictCompletionMs":170.2152} |
| 25  | 965.671 / 1083.648  | 1128.761 / 1261.005 | 163.089 / 177.358 | {"blockedOrPreparationToFirstPhysicalMutationMs":19.8256,"dispatchToBlockedOrPreparationMs":524.0778,"firstNewGenerationToCleanupDrainedMs":214.9312,"firstPhysicalMutationToFirstNewGenerationMs":369.9262,"presentationToStrictCompletionMs":253.8543} | {"blockedOrPreparationToFirstPhysicalMutationMs":32.4843,"dispatchToBlockedOrPreparationMs":608.2173,"firstNewGenerationToCleanupDrainedMs":233.0946,"firstPhysicalMutationToFirstNewGenerationMs":387.209,"presentationToStrictCompletionMs":270.8183}  |
| 26  | 1069.360 / 1262.287 | 1122.417 / 1203.507 | 53.057 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":64.0045,"dispatchToBlockedOrPreparationMs":467.3395,"firstNewGenerationToCleanupDrainedMs":207.4348,"firstPhysicalMutationToFirstNewGenerationMs":383.6382,"presentationToStrictCompletionMs":107.6747} | {"blockedOrPreparationToFirstPhysicalMutationMs":58.5799,"dispatchToBlockedOrPreparationMs":553.5916,"firstNewGenerationToCleanupDrainedMs":220.5511,"firstPhysicalMutationToFirstNewGenerationMs":370.7845,"presentationToStrictCompletionMs":0}        |
| 27  | 635.691 / 692.171   | 856.734 / 921.173   | 221.043 / 229.002 | {"blockedOrPreparationToFirstPhysicalMutationMs":56.6709,"dispatchToBlockedOrPreparationMs":410.7407,"firstNewGenerationToCleanupDrainedMs":280.8314,"firstPhysicalMutationToFirstNewGenerationMs":108.4905,"presentationToStrictCompletionMs":221.0428} | {"blockedOrPreparationToFirstPhysicalMutationMs":1.0789,"dispatchToBlockedOrPreparationMs":517.6256,"firstNewGenerationToCleanupDrainedMs":319.5714,"firstPhysicalMutationToFirstNewGenerationMs":82.8973,"presentationToStrictCompletionMs":229.0023}   |
| 28  | 935.376 / 1059.609  | 1048.956 / 1116.037 | 113.581 / 56.428  | {"blockedOrPreparationToFirstPhysicalMutationMs":55.3406,"dispatchToBlockedOrPreparationMs":483.0644,"firstNewGenerationToCleanupDrainedMs":214.2546,"firstPhysicalMutationToFirstNewGenerationMs":296.2966,"presentationToStrictCompletionMs":165.9236} | {"blockedOrPreparationToFirstPhysicalMutationMs":59.7979,"dispatchToBlockedOrPreparationMs":479.2036,"firstNewGenerationToCleanupDrainedMs":212.7171,"firstPhysicalMutationToFirstNewGenerationMs":364.3183,"presentationToStrictCompletionMs":112.9448} |
| 29  | 992.341 / 1050.659  | 1152.741 / 1163.834 | 160.400 / 113.175 | {"blockedOrPreparationToFirstPhysicalMutationMs":18.6898,"dispatchToBlockedOrPreparationMs":571.4794,"firstNewGenerationToCleanupDrainedMs":211.67,"firstPhysicalMutationToFirstNewGenerationMs":350.9019,"presentationToStrictCompletionMs":248.6102}   | {"blockedOrPreparationToFirstPhysicalMutationMs":31.8854,"dispatchToBlockedOrPreparationMs":538.3522,"firstNewGenerationToCleanupDrainedMs":217.6422,"firstPhysicalMutationToFirstNewGenerationMs":375.9541,"presentationToStrictCompletionMs":220.1565} |
| 30  | 661.395 / 641.697   | 892.782 / 910.395   | 231.387 / 268.698 | {"blockedOrPreparationToFirstPhysicalMutationMs":55.3799,"dispatchToBlockedOrPreparationMs":471.34,"firstNewGenerationToCleanupDrainedMs":288.9922,"firstPhysicalMutationToFirstNewGenerationMs":77.0701,"presentationToStrictCompletionMs":231.3869}    | {"blockedOrPreparationToFirstPhysicalMutationMs":54.238,"dispatchToBlockedOrPreparationMs":421.5549,"firstNewGenerationToCleanupDrainedMs":336.1358,"firstPhysicalMutationToFirstNewGenerationMs":98.4665,"presentationToStrictCompletionMs":268.6978}   |
| 31  | 725.622 / 759.387   | 725.622 / 759.387   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":42.8366,"dispatchToBlockedOrPreparationMs":502.7917,"firstNewGenerationToCleanupDrainedMs":62.8644,"firstPhysicalMutationToFirstNewGenerationMs":117.1297,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":51.3872,"dispatchToBlockedOrPreparationMs":533.8503,"firstNewGenerationToCleanupDrainedMs":53.4768,"firstPhysicalMutationToFirstNewGenerationMs":120.6727,"presentationToStrictCompletionMs":0}         |
| 32  | 251.784 / 303.658   | 588.114 / 685.512   | 336.330 / 381.854 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":179.75,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":336.3303}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":211.0087,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":381.854}             |
| 33  | 342.964 / 363.093   | 342.964 / 363.093   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":342.9635,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":363.0928,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 10                   | 1            | 591.619 / 605.695    | 14.076   | 14 / 16           | 2            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 14                  | 0            | 895.880 / 991.124    | 95.244   | 19 / 19           | 0            |
| 5   | 13 / 15                  | 2            | 1113.208 / 997.643   | -115.565 | 18 / 20           | 2            |
| 6   | 17 / 17                  | 0            | 1211.665 / 1079.527  | -132.138 | 22 / 22           | 0            |
| 7   | 18 / 17                  | -1           | 1233.958 / 1069.845  | -164.113 | 23 / 22           | -1           |
| 8   | 16 / 18                  | 2            | 1194.279 / 1158.789  | -35.490  | 21 / 23           | 2            |
| 9   | 16 / 17                  | 1            | 1118.297 / 1059.401  | -58.896  | 21 / 22           | 1            |
| 10  | 12 / 11                  | -1           | 716.335 / 715.571    | -0.764   | 16 / 16           | 0            |
| 11  | 4 / 3                    | -1           | 214.952 / 228.361    | 13.409   | 10 / 9            | -1           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 10                  | 0            | 820.186 / 724.659    | -95.527  | 11 / 11           | 0            |
| 14  | 18 / 18                  | 0            | 1039.205 / 1082.256  | 43.051   | 23 / 23           | 0            |
| 15  | 14 / 14                  | 0            | 779.683 / 812.030    | 32.348   | 18 / 18           | 0            |
| 16  | 14 / 14                  | 0            | 781.826 / 838.716    | 56.891   | 19 / 19           | 0            |
| 17  | 14 / 14                  | 0            | 800.396 / 907.213    | 106.817  | 18 / 18           | 0            |
| 18  | 14 / 13                  | -1           | 772.659 / 829.726    | 57.068   | 18 / 17           | -1           |
| 19  | 14 / 14                  | 0            | 792.702 / 817.927    | 25.224   | 20 / 19           | -1           |
| 20  | 16 / 10                  | -6           | 899.807 / 657.663    | -242.143 | 22 / 15           | -7           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 11 / 12                  | 1            | 709.965 / 764.231    | 54.266   | 15 / 16           | 1            |
| 25  | 14 / 15                  | 1            | 913.830 / 1027.911   | 114.081  | 19 / 20           | 1            |
| 26  | 14 / 16                  | 2            | 914.982 / 982.956    | 67.974   | 19 / 21           | 2            |
| 27  | 10 / 10                  | 0            | 575.902 / 601.602    | 25.700   | 16 / 16           | 0            |
| 28  | 12 / 12                  | 0            | 834.702 / 903.320    | 68.618   | 17 / 17           | 0            |
| 29  | 15 / 15                  | 0            | 941.071 / 946.192    | 5.121    | 20 / 20           | 0            |
| 30  | 11 / 9                   | -2           | 603.790 / 574.259    | -29.531  | 16 / 14           | -2           |
| 31  | 10 / 10                  | 0            | 662.758 / 705.910    | 43.152   | 11 / 11           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 5 / 3             | -2           |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 1 / 1                | 0     | 2 / 2              | 0     | 175.737 / 149.879 | -25.858  |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 233.774 / 268.318 | 34.544   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 247.736 / 239.240 | -8.496   |
| 6   | 1 / 1                | 0     | 5 / 5              | 0     | 384.767 / 428.950 | 44.183   |
| 7   | 1 / 1                | 0     | 5 / 5              | 0     | 380.561 / 407.202 | 26.641   |
| 8   | 1 / 1                | 0     | 5 / 5              | 0     | 376.174 / 421.930 | 45.756   |
| 9   | 1 / 1                | 0     | 5 / 5              | 0     | 373.364 / 381.517 | 8.153    |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 5 / 5              | 0     | 265.707 / 273.531 | 7.824    |
| 15  | 1 / 1                | 0     | 4 / 4              | 0     | 237.306 / 258.498 | 21.192   |
| 16  | 1 / 1                | 0     | 4 / 4              | 0     | 234.463 / 261.859 | 27.395   |
| 17  | 1 / 1                | 0     | 4 / 4              | 0     | 235.613 / 328.538 | 92.925   |
| 18  | 1 / 1                | 0     | 4 / 4              | 0     | 233.681 / 274.884 | 41.202   |
| 19  | 1 / 1                | 0     | 4 / 4              | 0     | 234.223 / 254.089 | 19.867   |
| 20  | 1 / 0                | -1    | 5 / 0              | -5    | 308.933 / 0       | -308.933 |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 232.643 / 235.050 | 2.407    |
| 26  | 2 / 2                | 0     | 3 / 3              | 0     | 246.161 / 241.943 | -4.218   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 3 / 3              | 0     | 315.450 / 354.805 | 39.355   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 846.759 / 991.294   | 144.535  | 17.069  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 211.369 / 241.370   | 30.001   | 14.194  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 318.164 / 289.661   | -28.503  | -8.958  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1175.159 / 1227.203 | 52.043   | 4.429   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1208.675 / 1306.646 | 97.972   | 8.106   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1379.021 / 1337.317 | -41.705  | -3.024  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1376.852 / 1318.093 | -58.760  | -4.268  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1364.415 / 1320.059 | -44.356  | -3.251  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1292.348 / 1351.707 | 59.359   | 4.593   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 876.395 / 977.564   | 101.168  | 11.544  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 589.965 / 580.515   | -9.449   | -1.602  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 214.569 / 211.807   | -2.762   | -1.287  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 748.309 / 765.907   | 17.598   | 2.352   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1268.292 / 1442.434 | 174.142  | 13.730  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 1002.907 / 1503.259 | 500.352  | 49.890  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 1043.784 / 1237.215 | 193.431  | 18.532  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 1016.979 / 1096.669 | 79.690   | 7.836   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1065.719 / 1010.180 | -55.539  | -5.211  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 950.188 / 1090.224  | 140.036  | 14.738  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1309.248 / 1215.989 | -93.258  | -7.123  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 569.578 / 558.991   | -10.587  | -1.859  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 212.868 / 205.324   | -7.543   | -3.544  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 296.737 / 324.124   | 27.387   | 9.229   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1045.171 / 977.717  | -67.454  | -6.454  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1281.481 / 1292.331 | 10.850   | 0.847   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1181.035 / 1284.469 | 103.434  | 8.758   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 930.086 / 900.937   | -29.149  | -3.134  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 1110.850 / 1135.717 | 24.867   | 2.239   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1252.198 / 1291.659 | 39.461   | 3.151   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 857.695 / 855.807   | -1.888   | -0.220  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 734.073 / 719.204   | -14.869  | -2.026  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 563.954 / 599.604   | 35.650   | 6.321   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 307.169 / 295.183   | -11.986  | -3.902  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 629.455 / 741.082   | 846.759 / 991.294   | 217.304 / 250.213 | {"blockedOrPreparationToFirstPhysicalMutationMs":56.7771,"dispatchToBlockedOrPreparationMs":432.3352,"firstNewGenerationToCleanupDrainedMs":277.2094,"firstPhysicalMutationToFirstNewGenerationMs":80.4372,"presentationToStrictCompletionMs":217.3036}   | {"blockedOrPreparationToFirstPhysicalMutationMs":68.6627,"dispatchToBlockedOrPreparationMs":447.3866,"firstNewGenerationToCleanupDrainedMs":324.3669,"firstPhysicalMutationToFirstNewGenerationMs":150.878,"presentationToStrictCompletionMs":250.2126}  |
| 2   | 211.369 / 241.370   | 211.369 / 241.370   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":211.3688,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":241.3699,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 318.164 / 289.661   | 318.164 / 289.661   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":318.1641,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":289.6615,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 975.313 / 1022.152  | 1080.826 / 1130.312 | 105.514 / 108.159 | {"blockedOrPreparationToFirstPhysicalMutationMs":56.0115,"dispatchToBlockedOrPreparationMs":423.7954,"firstNewGenerationToCleanupDrainedMs":213.0297,"firstPhysicalMutationToFirstNewGenerationMs":387.9898,"presentationToStrictCompletionMs":199.8465}  | {"blockedOrPreparationToFirstPhysicalMutationMs":56.8343,"dispatchToBlockedOrPreparationMs":472.2174,"firstNewGenerationToCleanupDrainedMs":212.5164,"firstPhysicalMutationToFirstNewGenerationMs":388.7435,"presentationToStrictCompletionMs":205.0506} |
| 5   | 958.902 / 1051.329  | 1116.964 / 1211.485 | 158.062 / 160.156 | {"blockedOrPreparationToFirstPhysicalMutationMs":56.9795,"dispatchToBlockedOrPreparationMs":468.7342,"firstNewGenerationToCleanupDrainedMs":210.507,"firstPhysicalMutationToFirstNewGenerationMs":380.7434,"presentationToStrictCompletionMs":249.7724}   | {"blockedOrPreparationToFirstPhysicalMutationMs":66.2974,"dispatchToBlockedOrPreparationMs":473.1666,"firstNewGenerationToCleanupDrainedMs":219.418,"firstPhysicalMutationToFirstNewGenerationMs":452.6025,"presentationToStrictCompletionMs":255.3178}  |
| 6   | 1122.132 / 1139.698 | 1282.659 / 1245.531 | 160.527 / 105.833 | {"blockedOrPreparationToFirstPhysicalMutationMs":60.3085,"dispatchToBlockedOrPreparationMs":460.5716,"firstNewGenerationToCleanupDrainedMs":213.9692,"firstPhysicalMutationToFirstNewGenerationMs":547.8101,"presentationToStrictCompletionMs":256.8893}  | {"blockedOrPreparationToFirstPhysicalMutationMs":0.7204,"dispatchToBlockedOrPreparationMs":492.227,"firstNewGenerationToCleanupDrainedMs":208.4891,"firstPhysicalMutationToFirstNewGenerationMs":544.0945,"presentationToStrictCompletionMs":197.6183}   |
| 7   | 1166.698 / 1063.800 | 1273.196 / 1220.474 | 106.498 / 156.674 | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7596,"dispatchToBlockedOrPreparationMs":469.3275,"firstNewGenerationToCleanupDrainedMs":213.5704,"firstPhysicalMutationToFirstNewGenerationMs":534.5381,"presentationToStrictCompletionMs":210.1543}  | {"blockedOrPreparationToFirstPhysicalMutationMs":57.0677,"dispatchToBlockedOrPreparationMs":414.1763,"firstNewGenerationToCleanupDrainedMs":210.6635,"firstPhysicalMutationToFirstNewGenerationMs":538.5666,"presentationToStrictCompletionMs":254.2926} |
| 8   | 1094.578 / 1119.006 | 1270.579 / 1228.469 | 176.001 / 109.463 | {"blockedOrPreparationToFirstPhysicalMutationMs":56.7725,"dispatchToBlockedOrPreparationMs":421.4259,"firstNewGenerationToCleanupDrainedMs":233.2686,"firstPhysicalMutationToFirstNewGenerationMs":559.1124,"presentationToStrictCompletionMs":269.8373}  | {"blockedOrPreparationToFirstPhysicalMutationMs":54.8912,"dispatchToBlockedOrPreparationMs":418.3487,"firstNewGenerationToCleanupDrainedMs":219.4153,"firstPhysicalMutationToFirstNewGenerationMs":535.8134,"presentationToStrictCompletionMs":201.0537} |
| 9   | 1044.832 / 1143.799 | 1202.615 / 1253.432 | 157.784 / 109.633 | {"blockedOrPreparationToFirstPhysicalMutationMs":56.0795,"dispatchToBlockedOrPreparationMs":418.0584,"firstNewGenerationToCleanupDrainedMs":220.3175,"firstPhysicalMutationToFirstNewGenerationMs":508.16,"presentationToStrictCompletionMs":247.5159}    | {"blockedOrPreparationToFirstPhysicalMutationMs":59.2707,"dispatchToBlockedOrPreparationMs":413.1491,"firstNewGenerationToCleanupDrainedMs":227.1591,"firstPhysicalMutationToFirstNewGenerationMs":553.853,"presentationToStrictCompletionMs":207.9086}  |
| 10  | 720.568 / 797.683   | 876.395 / 977.564   | 155.827 / 179.880 | {"blockedOrPreparationToFirstPhysicalMutationMs":54.5822,"dispatchToBlockedOrPreparationMs":412.4751,"firstNewGenerationToCleanupDrainedMs":208.7567,"firstPhysicalMutationToFirstNewGenerationMs":200.5811,"presentationToStrictCompletionMs":155.8275}  | {"blockedOrPreparationToFirstPhysicalMutationMs":68.743,"dispatchToBlockedOrPreparationMs":431.679,"firstNewGenerationToCleanupDrainedMs":237.6414,"firstPhysicalMutationToFirstNewGenerationMs":239.5002,"presentationToStrictCompletionMs":179.8803}   |
| 11  | 225.131 / 223.277   | 589.965 / 580.515   | 364.834 / 357.239 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1598,"dispatchToBlockedOrPreparationMs":168.1065,"firstNewGenerationToCleanupDrainedMs":366.1231,"firstPhysicalMutationToFirstNewGenerationMs":52.5753,"presentationToStrictCompletionMs":364.8338}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6067,"dispatchToBlockedOrPreparationMs":166.3486,"firstNewGenerationToCleanupDrainedMs":358.1378,"firstPhysicalMutationToFirstNewGenerationMs":52.4223,"presentationToStrictCompletionMs":357.2388}   |
| 12  | 214.569 / 211.807   | 214.569 / 211.807   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":214.5685,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":211.8068,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 748.309 / 765.907   | 748.309 / 765.907   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2042,"dispatchToBlockedOrPreparationMs":519.6567,"firstNewGenerationToCleanupDrainedMs":59.868,"firstPhysicalMutationToFirstNewGenerationMs":120.5801,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":51.0512,"dispatchToBlockedOrPreparationMs":536.7817,"firstNewGenerationToCleanupDrainedMs":53.663,"firstPhysicalMutationToFirstNewGenerationMs":124.4108,"presentationToStrictCompletionMs":0}          |
| 14  | 1106.980 / 1442.434 | 1213.389 / 1384.714 | 106.410 / 0       | {"blockedOrPreparationToFirstPhysicalMutationMs":56.8555,"dispatchToBlockedOrPreparationMs":464.9116,"firstNewGenerationToCleanupDrainedMs":209.2546,"firstPhysicalMutationToFirstNewGenerationMs":482.3676,"presentationToStrictCompletionMs":161.3122}  | {"blockedOrPreparationToFirstPhysicalMutationMs":62.9968,"dispatchToBlockedOrPreparationMs":498.1145,"firstNewGenerationToCleanupDrainedMs":218.0888,"firstPhysicalMutationToFirstNewGenerationMs":605.5143,"presentationToStrictCompletionMs":0}        |
| 15  | 1002.907 / 1503.259 | 841.766 / 900.121   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":55.4334,"dispatchToBlockedOrPreparationMs":480.8262,"firstNewGenerationToCleanupDrainedMs":58.2268,"firstPhysicalMutationToFirstNewGenerationMs":247.2798,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":60.0466,"dispatchToBlockedOrPreparationMs":504.908,"firstNewGenerationToCleanupDrainedMs":52.6014,"firstPhysicalMutationToFirstNewGenerationMs":282.5653,"presentationToStrictCompletionMs":0}          |
| 16  | 1043.784 / 1237.215 | 820.321 / 1115.073  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":54.1505,"dispatchToBlockedOrPreparationMs":475.2215,"firstNewGenerationToCleanupDrainedMs":51.9866,"firstPhysicalMutationToFirstNewGenerationMs":238.9626,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":69.2357,"dispatchToBlockedOrPreparationMs":721.4366,"firstNewGenerationToCleanupDrainedMs":66.9626,"firstPhysicalMutationToFirstNewGenerationMs":257.4384,"presentationToStrictCompletionMs":0}         |
| 17  | 1016.979 / 1096.669 | 839.532 / 930.178   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":56.3203,"dispatchToBlockedOrPreparationMs":488.7752,"firstNewGenerationToCleanupDrainedMs":55.0511,"firstPhysicalMutationToFirstNewGenerationMs":239.3856,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":72.4922,"dispatchToBlockedOrPreparationMs":530.3887,"firstNewGenerationToCleanupDrainedMs":55.6453,"firstPhysicalMutationToFirstNewGenerationMs":271.6523,"presentationToStrictCompletionMs":0}         |
| 18  | 1065.719 / 1010.180 | 903.603 / 849.834   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":65.064,"dispatchToBlockedOrPreparationMs":534.4373,"firstNewGenerationToCleanupDrainedMs":53.4077,"firstPhysicalMutationToFirstNewGenerationMs":250.6942,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":56.4187,"dispatchToBlockedOrPreparationMs":494.8535,"firstNewGenerationToCleanupDrainedMs":50.4983,"firstPhysicalMutationToFirstNewGenerationMs":248.0639,"presentationToStrictCompletionMs":0}         |
| 19  | 950.188 / 1090.224  | 843.173 / 834.499   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":64.8252,"dispatchToBlockedOrPreparationMs":495.0227,"firstNewGenerationToCleanupDrainedMs":50.2106,"firstPhysicalMutationToFirstNewGenerationMs":233.1145,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":55.9181,"dispatchToBlockedOrPreparationMs":480.5868,"firstNewGenerationToCleanupDrainedMs":61.0476,"firstPhysicalMutationToFirstNewGenerationMs":236.9465,"presentationToStrictCompletionMs":0}         |
| 20  | 1052.426 / 988.184  | 1309.248 / 1215.989 | 256.822 / 227.805 | {"blockedOrPreparationToFirstPhysicalMutationMs":307.0801,"dispatchToBlockedOrPreparationMs":565.0015,"firstNewGenerationToCleanupDrainedMs":323.9777,"firstPhysicalMutationToFirstNewGenerationMs":113.1885,"presentationToStrictCompletionMs":256.8219} | {"blockedOrPreparationToFirstPhysicalMutationMs":280.0552,"dispatchToBlockedOrPreparationMs":542.6941,"firstNewGenerationToCleanupDrainedMs":294.6826,"firstPhysicalMutationToFirstNewGenerationMs":98.5575,"presentationToStrictCompletionMs":227.8049} |
| 21  | 230.613 / 231.103   | 569.578 / 558.991   | 338.965 / 327.888 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":157.3216,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":338.9649}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":155.1385,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":327.8881}            |
| 22  | 212.868 / 205.324   | 212.868 / 205.324   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":212.8678,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":205.3243,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 296.737 / 324.124   | 296.737 / 324.124   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.7367,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":324.1239,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 860.810 / 804.298   | 1045.171 / 977.717  | 184.361 / 173.419 | {"blockedOrPreparationToFirstPhysicalMutationMs":61.6113,"dispatchToBlockedOrPreparationMs":537.0272,"firstNewGenerationToCleanupDrainedMs":241.9291,"firstPhysicalMutationToFirstNewGenerationMs":204.6034,"presentationToStrictCompletionMs":184.3614}  | {"blockedOrPreparationToFirstPhysicalMutationMs":54.5674,"dispatchToBlockedOrPreparationMs":512.8794,"firstNewGenerationToCleanupDrainedMs":233.7439,"firstPhysicalMutationToFirstNewGenerationMs":176.5267,"presentationToStrictCompletionMs":173.4192} |
| 25  | 1023.547 / 1035.681 | 1189.968 / 1199.491 | 166.421 / 163.810 | {"blockedOrPreparationToFirstPhysicalMutationMs":24.4873,"dispatchToBlockedOrPreparationMs":545.3084,"firstNewGenerationToCleanupDrainedMs":219.0378,"firstPhysicalMutationToFirstNewGenerationMs":401.1346,"presentationToStrictCompletionMs":257.934}   | {"blockedOrPreparationToFirstPhysicalMutationMs":23.6869,"dispatchToBlockedOrPreparationMs":572.4159,"firstNewGenerationToCleanupDrainedMs":216.5101,"firstPhysicalMutationToFirstNewGenerationMs":386.8785,"presentationToStrictCompletionMs":256.65}   |
| 26  | 963.764 / 1284.469  | 1124.949 / 1227.072 | 161.184 / 0       | {"blockedOrPreparationToFirstPhysicalMutationMs":56.3928,"dispatchToBlockedOrPreparationMs":478.1628,"firstNewGenerationToCleanupDrainedMs":214.4529,"firstPhysicalMutationToFirstNewGenerationMs":375.9402,"presentationToStrictCompletionMs":217.2704}  | {"blockedOrPreparationToFirstPhysicalMutationMs":73.4369,"dispatchToBlockedOrPreparationMs":561.6782,"firstNewGenerationToCleanupDrainedMs":210.87,"firstPhysicalMutationToFirstNewGenerationMs":381.0871,"presentationToStrictCompletionMs":0}          |
| 27  | 701.589 / 673.938   | 930.086 / 900.937   | 228.497 / 227.000 | {"blockedOrPreparationToFirstPhysicalMutationMs":59.9345,"dispatchToBlockedOrPreparationMs":463.8098,"firstNewGenerationToCleanupDrainedMs":314.2159,"firstPhysicalMutationToFirstNewGenerationMs":92.126,"presentationToStrictCompletionMs":228.4971}    | {"blockedOrPreparationToFirstPhysicalMutationMs":1.0265,"dispatchToBlockedOrPreparationMs":491.6174,"firstNewGenerationToCleanupDrainedMs":314.2638,"firstPhysicalMutationToFirstNewGenerationMs":94.0295,"presentationToStrictCompletionMs":226.9995}   |
| 28  | 941.434 / 975.543   | 1056.671 / 1080.175 | 115.237 / 104.632 | {"blockedOrPreparationToFirstPhysicalMutationMs":55.7968,"dispatchToBlockedOrPreparationMs":475.923,"firstNewGenerationToCleanupDrainedMs":218.796,"firstPhysicalMutationToFirstNewGenerationMs":306.1548,"presentationToStrictCompletionMs":169.4157}    | {"blockedOrPreparationToFirstPhysicalMutationMs":56.6935,"dispatchToBlockedOrPreparationMs":484.432,"firstNewGenerationToCleanupDrainedMs":208.0057,"firstPhysicalMutationToFirstNewGenerationMs":331.0436,"presentationToStrictCompletionMs":160.1736}  |
| 29  | 958.166 / 1034.817  | 1154.433 / 1200.473 | 196.267 / 165.656 | {"blockedOrPreparationToFirstPhysicalMutationMs":21.2125,"dispatchToBlockedOrPreparationMs":528.2694,"firstNewGenerationToCleanupDrainedMs":248.742,"firstPhysicalMutationToFirstNewGenerationMs":356.2088,"presentationToStrictCompletionMs":294.0322}   | {"blockedOrPreparationToFirstPhysicalMutationMs":25.6725,"dispatchToBlockedOrPreparationMs":580.9947,"firstNewGenerationToCleanupDrainedMs":218.6975,"firstPhysicalMutationToFirstNewGenerationMs":375.108,"presentationToStrictCompletionMs":256.8421}  |
| 30  | 630.361 / 626.463   | 857.695 / 855.807   | 227.335 / 229.344 | {"blockedOrPreparationToFirstPhysicalMutationMs":57.1427,"dispatchToBlockedOrPreparationMs":431.5035,"firstNewGenerationToCleanupDrainedMs":288.0038,"firstPhysicalMutationToFirstNewGenerationMs":81.0453,"presentationToStrictCompletionMs":227.3348}   | {"blockedOrPreparationToFirstPhysicalMutationMs":52.7647,"dispatchToBlockedOrPreparationMs":437.9015,"firstNewGenerationToCleanupDrainedMs":290.4613,"firstPhysicalMutationToFirstNewGenerationMs":74.6795,"presentationToStrictCompletionMs":229.3436}  |
| 31  | 734.073 / 719.204   | 734.073 / 719.204   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":44.0358,"dispatchToBlockedOrPreparationMs":521.3186,"firstNewGenerationToCleanupDrainedMs":51.989,"firstPhysicalMutationToFirstNewGenerationMs":116.7294,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":45.7602,"dispatchToBlockedOrPreparationMs":508.5183,"firstNewGenerationToCleanupDrainedMs":49.8653,"firstPhysicalMutationToFirstNewGenerationMs":115.0601,"presentationToStrictCompletionMs":0}         |
| 32  | 229.057 / 256.322   | 563.954 / 599.604   | 334.897 / 343.281 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":157.7606,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":334.897}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":163.2485,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":343.2813}            |
| 33  | 307.169 / 295.183   | 307.169 / 295.183   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":307.1687,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":295.183,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 10                   | 1            | 569.549 / 666.927    | 97.378   | 14 / 15           | 1            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 13 / 15                  | 2            | 867.797 / 917.795    | 49.999   | 18 / 20           | 2            |
| 5   | 15 / 14                  | -1           | 906.457 / 992.067    | 85.609   | 20 / 19           | -1           |
| 6   | 16 / 16                  | 0            | 1068.690 / 1037.042  | -31.648  | 21 / 21           | 0            |
| 7   | 17 / 16                  | -1           | 1059.625 / 1009.811  | -49.815  | 22 / 21           | -1           |
| 8   | 16 / 16                  | 0            | 1037.311 / 1009.053  | -28.257  | 21 / 21           | 0            |
| 9   | 17 / 17                  | 0            | 982.298 / 1026.273   | 43.975   | 22 / 22           | 0            |
| 10  | 10 / 11                  | 1            | 667.638 / 739.922    | 72.284   | 14 / 15           | 1            |
| 11  | 3 / 3                    | 0            | 223.842 / 222.378    | -1.464   | 10 / 9            | -1           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 10                  | 0            | 688.441 / 712.244    | 23.803   | 11 / 11           | 0            |
| 14  | 17 / 18                  | 1            | 1004.135 / 1166.626  | 162.491  | 22 / 23           | 1            |
| 15  | 14 / 14                  | 0            | 783.539 / 847.520    | 63.981   | 18 / 26           | 8            |
| 16  | 14 / 15                  | 1            | 768.335 / 1048.111   | 279.776  | 19 / 18           | -1           |
| 17  | 14 / 14                  | 0            | 784.481 / 874.533    | 90.052   | 18 / 18           | 0            |
| 18  | 14 / 14                  | 0            | 850.196 / 799.336    | -50.859  | 18 / 18           | 0            |
| 19  | 14 / 14                  | 0            | 792.962 / 773.451    | -19.511  | 17 / 19           | 2            |
| 20  | 16 / 16                  | 0            | 985.270 / 921.307    | -63.963  | 22 / 21           | -1           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 9 / 10            | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 11 / 12                  | 1            | 803.242 / 743.973    | -59.268  | 15 / 16           | 1            |
| 25  | 15 / 15                  | 0            | 970.930 / 982.981    | 12.051   | 20 / 20           | 0            |
| 26  | 15 / 15                  | 0            | 910.496 / 1016.202   | 105.706  | 20 / 20           | 0            |
| 27  | 10 / 10                  | 0            | 615.870 / 586.673    | -29.197  | 15 / 16           | 1            |
| 28  | 12 / 12                  | 0            | 837.875 / 872.169    | 34.294   | 17 / 17           | 0            |
| 29  | 15 / 15                  | 0            | 905.691 / 981.775    | 76.085   | 20 / 20           | 0            |
| 30  | 9 / 10                   | 1            | 569.692 / 565.346    | -4.346   | 15 / 15           | 0            |
| 31  | 10 / 10                  | 0            | 682.084 / 669.339    | -12.745  | 11 / 11           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 226.702 / 229.891 | 3.189    |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 241.781 / 266.919 | 25.138   |
| 6   | 1 / 1                | 0     | 5 / 5              | 0     | 406.159 / 397.028 | -9.131   |
| 7   | 1 / 1                | 0     | 5 / 5              | 0     | 396.322 / 386.721 | -9.601   |
| 8   | 1 / 1                | 0     | 5 / 5              | 0     | 422.739 / 384.459 | -38.280  |
| 9   | 1 / 1                | 0     | 5 / 5              | 0     | 373.622 / 403.848 | 30.226   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 5 / 5              | 0     | 274.777 / 354.233 | 79.456   |
| 15  | 1 / 1                | 0     | 4 / 4              | 0     | 246.900 / 283.395 | 36.495   |
| 16  | 1 / 1                | 0     | 4 / 4              | 0     | 238.880 / 256.974 | 18.094   |
| 17  | 1 / 1                | 0     | 4 / 4              | 0     | 239.859 / 272.009 | 32.150   |
| 18  | 1 / 1                | 0     | 4 / 4              | 0     | 250.779 / 248.126 | -2.654   |
| 19  | 1 / 1                | 0     | 4 / 4              | 0     | 233.252 / 237.173 | 3.922    |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 362.014 / 323.684 | -38.330  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 230.931 / 227.833 | -3.098   |
| 26  | 2 / 2                | 0     | 3 / 3              | 0     | 246.738 / 249.889 | 3.151    |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 3 / 3              | 0     | 321.676 / 347.651 | 25.975   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

## Cumulative gates and other health evidence

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
| nvidia | 1    | processPrivateMiB | 16955.65625 / 16833.4765625 / -122.1796875     | 17063.30859375 / 16836.5859375 / -226.72265625 | -104.543              |
| nvidia | 1    | systemCommitMiB   | 54667.65234375 / 54357.87890625 / -309.7734375 | 54768.60546875 / 54660.30078125 / -108.3046875 | 201.469               |
| nvidia | 1    | dxgiUsageMiB      | 5661.03125 / 3865.5390625 / -1795.4921875      | 4257.390625 / 3415.01171875 / -842.37890625    | 953.113               |
| nvidia | 1    | liveTextures      | 0 / 261 / 261                                  | 0 / 209 / 209                                  | -52                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2430.020969390869 / 2430.020969390869      | 0 / 2265.7227210998535 / 2265.7227210998535    | -164.298              |
| nvidia | 2    | processPrivateMiB | 17102.12109375 / 16749.58984375 / -352.53125   | 17194.94140625 / 16866.9609375 / -327.98046875 | 24.551                |
| nvidia | 2    | systemCommitMiB   | 54705.9609375 / 54517.11328125 / -188.84765625 | 55611.83203125 / 54697.69921875 / -914.1328125 | -725.285              |
| nvidia | 2    | dxgiUsageMiB      | 4066.0390625 / 3675.5703125 / -390.46875       | 3722.0625 / 3310.13671875 / -411.92578125      | -21.457               |
| nvidia | 2    | liveTextures      | 0 / 211 / 211                                  | 0 / 217 / 217                                  | 6                     |
| nvidia | 2    | liveTextureMiB    | 0 / 2274.9311332702637 / 2274.9311332702637    | 0 / 2281.5979194641113 / 2281.5979194641113    | 6.667                 |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 1908        | 1813        | -95         |
| cpu/compactPresentationContract/reuses                  | 1884        | 1789        | -95         |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 149         | 146         | -3          |
| cpu/generationResourceValidation/fullValidations        | 683         | 664         | -19         |
| cpu/generationResourceValidation/stableChecks           | 7300        | 6925        | -375        |
| cpu/generationResourceValidation/stableHits             | 7283        | 6912        | -371        |
| cpu/generationResourceValidation/stableMisses           | 17          | 13          | -4          |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 1           | 3           | 2           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 3634        | 3416        | -218        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 3634        | 3416        | -218        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 3592        | 3374        | -218        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 3634        | 3416        | -218        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 3613        | 3395        | -218        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 30          | 30          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 3604        | 3386        | -218        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 3540        | 3322        | -218        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 94          | 0           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 3619        | 3401        | -218        |
| cpu/strongStereoPacket/captures                         | 3961        | 3770        | -191        |
| cpu/strongStereoPacket/commitAccepts                    | 3796        | 3605        | -191        |
| cpu/strongStereoPacket/commitRejects                    | 64          | 65          | 1           |
| cpu/strongStereoPacket/commitValidations                | 3860        | 3670        | -190        |
| cpu/strongStereoPacket/cycleReuses                      | 1940        | 1847        | -93         |
| cpu/strongStereoPacket/fastSkips                        | 3307        | 3062        | -245        |
| cpu/strongStereoPacket/invalidations                    | 4194        | 3998        | -196        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 94          | 93          | -1          |
| cpu/strongStereoPacket/lifetimeReuses                   | 1927        | 1830        | -97         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.680       | 1.794       | 0.114       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 23.800      | 33.800      | 10.000      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.151       | 0.164       | 0.012       |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 3.900       | 18.700      | 14.800      |
| cpu/window/currentFrame                                 | 28412       | 65700       | 37288       |
| cpu/window/elapsedFrames                                | 3634        | 3416        | -218        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 24778       | 62284       | 37506       |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 28412       | 65701       | 37289       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5208358320  | 4908925296  | -299433024  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4105        | 3869        | -236        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 10427356800 | 9827879040  | -599477760  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.107       | 0.106       | -0.000      |
| gpu/item5ActiveFSRCopies/activePixels                   | 3568581120  | 3356517120  | -212064000  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 29809121280 | 28166868480 | -1642252800 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 10368       | 9798        | -570        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 1721        | 1633        | -88         |
| gpu/item7EarlyHAM/executedClears                        | 1724        | 1634        | -90         |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1724        | 1634        | -90         |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 3634        | 3456        | -178        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 3634        | 3417        | -217        |
| gpu/runtimeFSRSharedGuides/directGuideInputs            | 2772        | 2612        | -160        |
| gpu/runtimeFSRSharedGuides/directGuidePixels            | 6651802560  | 6245376960  | -406425600  |
| gpu/runtimeFSRSharedGuides/enabled                      | true        | true        | n/a         |
| gpu/runtimeFSRSharedGuides/fallbackGuideCopies          | 7740        | 7316        | -424        |
| gpu/runtimeFSRSharedGuides/importFailures               | 0           | 0           | 0           |
| gpu/startFrame                                          | 24778       | 62284       | 37506       |
| profiler/available                                      | true        | true        | n/a         |
| profiler/capabilities                                   | 127         | 127         | 0           |
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
| texture/createdCount                                    | 3956        | 3890        | -66         |
| texture/createdEstimatedBytes                           | 38757917992 | 38547505504 | -210412488  |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3695        | 3681        | -14         |
| texture/destroyedEstimatedBytes                         | 36209856324 | 36171723036 | -38133288   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 891         | 866         | -25         |
| texture/liveTextureRecordCount                          | 261         | 209         | -52         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 52          | 2           | -50         |
| texture/niSourceTextureMatchedEstimatedBytes            | 172410088   | 131088      | -172279000  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1538        | 1484        | -54         |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 261         | 209         | -52         |
| texture/outstandingEstimatedBytes                       | 2548061668  | 2375782468  | -172279200  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 1           | 3           | 2           |
| texture/supported                                       | true        | true        | n/a         |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 1868        | 1830        | -38        |
| cpu/compactPresentationContract/reuses                  | 1844        | 1806        | -38        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 145         | 145         | 0          |
| cpu/generationResourceValidation/fullValidations        | 675         | 673         | -2         |
| cpu/generationResourceValidation/stableChecks           | 7148        | 7035        | -113       |
| cpu/generationResourceValidation/stableHits             | 7133        | 7021        | -112       |
| cpu/generationResourceValidation/stableMisses           | 15          | 14          | -1         |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 2           | 4           | 2          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 3552        | 3472        | -80        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 3552        | 3472        | -80        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 3510        | 3430        | -80        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 3552        | 3472        | -80        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 3531        | 3451        | -80        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 30          | 30          | 0          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 3522        | 3442        | -80        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 3458        | 3378        | -80        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 94          | 0          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 3537        | 3457        | -80        |
| cpu/strongStereoPacket/captures                         | 3878        | 3805        | -73        |
| cpu/strongStereoPacket/commitAccepts                    | 3715        | 3642        | -73        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 62          | -3         |
| cpu/strongStereoPacket/commitValidations                | 3780        | 3704        | -76        |
| cpu/strongStereoPacket/cycleReuses                      | 1900        | 1863        | -37        |
| cpu/strongStereoPacket/fastSkips                        | 3226        | 3139        | -87        |
| cpu/strongStereoPacket/invalidations                    | 4112        | 4045        | -67        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 94          | 95          | 1          |
| cpu/strongStereoPacket/lifetimeReuses                   | 1884        | 1847        | -37        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.716       | 1.801       | 0.085      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 21.200      | 94.200      | 73         |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.150       | 0.160       | 0.009      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 0.800       | 1.700       | 0.900      |
| cpu/window/currentFrame                                 | 32298       | 69490       | 37192      |
| cpu/window/elapsedFrames                                | 3552        | 3472        | -80        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 28746       | 66018       | 37272      |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 32298       | 69491       | 37193      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5076404784  | 4974902064  | -101502720 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4001        | 3921        | -80        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 10163180160 | 9959967360  | -203212800 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.107       | 0.107       | -0.000     |
| gpu/item5ActiveFSRCopies/activePixels                   | 3495919000  | 3416755960  | -79163040  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 29119735400 | 28589260040 | -530475360 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 10100       | 9916        | -184       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1681        | 1647        | -34        |
| gpu/item7EarlyHAM/executedClears                        | 1684        | 1642        | -42        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1684        | 1642        | -42        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 3558        | 3482        | -76        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 3552        | 3473        | -79        |
| gpu/runtimeFSRSharedGuides/directGuideInputs            | 2740        | 2684        | -56        |
| gpu/runtimeFSRSharedGuides/directGuidePixels            | 6570517440  | 6428268480  | -142248960 |
| gpu/runtimeFSRSharedGuides/enabled                      | true        | true        | n/a        |
| gpu/runtimeFSRSharedGuides/fallbackGuideCopies          | 7532        | 7396        | -136       |
| gpu/runtimeFSRSharedGuides/importFailures               | 0           | 0           | 0          |
| gpu/startFrame                                          | 28746       | 66018       | 37272      |
| profiler/available                                      | true        | true        | n/a        |
| profiler/capabilities                                   | 127         | 127         | 0          |
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
| texture/createdCount                                    | 3908        | 3920        | 12         |
| texture/createdEstimatedBytes                           | 38607833840 | 38628805568 | 20971728   |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3697        | 3703        | 6          |
| texture/destroyedEstimatedBytes                         | 36222395652 | 36236376748 | 13981096   |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 871         | 874         | 3          |
| texture/liveTextureRecordCount                          | 211         | 217         | 6          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 4           | 10          | 6          |
| texture/niSourceTextureMatchedEstimatedBytes            | 9786808     | 16777440    | 6990632    |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1509        | 1483        | -26        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 211         | 217         | 6          |
| texture/outstandingEstimatedBytes                       | 2385438188  | 2392428820  | 6990632    |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 2           | 4           | 2          |
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
