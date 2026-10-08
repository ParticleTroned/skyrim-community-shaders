# Native count campaign: 3 October 2026

**Count accounting passed for nine windows. Performance remains provisional.**
Advanced and Legacy marked approximately 61–62% of their examined native
records occluded; Hybrid accepted approximately 19% of its examined records
as occluded. These are repeated culling records from separate windows,
not unique objects, draw calls, triangles, or matched object cohorts.

The eight retained fpsVR captures show higher Hybrid CPU/GPU times than
Advanced or Legacy in this scene. They cannot establish a controlled
performance result: the user confirmed another chat was building a DLL
during this campaign, and menu/simulation state was not recorded for each
window. A later observation found Journal Menu open. Its historical state
during the captures is unknown. No performance or temporal qualification
is claimed. The separately guarded quiet repeat is pending completion of
that build sequence.

## Evidence and compiled identity

The complete local evidence is [the audited JSON summary](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/native-counts-summary.json),
[the flat table](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/native-counts-summary.csv), and
[the offline generator](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/Summarize-NativeCounts.py).
Raw receipts, CSVs, endpoint observations, all count/histogram fields,
failed checks, and an input SHA-256 inventory remain in that directory.
These files are local evidence, outside the tracked repository report.

| Identity              | Preserved value                                                    |
| --------------------- | ------------------------------------------------------------------ |
| Producer Build ID     | `ee0c10f34abe0a5a7197ce4f77436273355c80b1c72747deb7a9331d9a44d5c3` |
| Compiled source       | `c684ff32c9f75c97f6743fe2829ca7eb040525f2`; dirty                  |
| Compiled dirty digest | `92ea1d0bb80e669a84b01843dd8cb1ed4403257ddd298e60051c43cb01839c86` |
| Physical DLL SHA-256  | `7ca6b10542a836e33b59033220e3c477bea97aaef02a272f34f4a391d4d7f1a3` |
| Physical DLL size     | 29,625,856 bytes                                                   |
| Build options         | Release; SE/AE/VR ON; DevBench ON; Tracy OFF                       |
| Game                  | PID 12556; start ticks 639266606067760969                          |
| fpsVR                 | PID 19148; start ticks 639266561345566729                          |

The enabled provider was
`D:\Skyrim Mods\mods\CSX_AIO-astra-hiz-native-counts-DevBench-ee0c10f34abe\SKSE\Plugins\CommunityShaders.dll`.
The physical DLL matched its adjacent manifest and build receipt; one
enabled provider was found, with no Overwrite or unmanaged Data DLL.
[Initial artifact verification](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/physical-artifact.json) and
[final verification](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/final-physical-identity.json) preserve the complete identity.
A later documentation/source commit must not replace this compiled identity.

## Protocol and evidence boundaries

The original order was Advanced 1, Hybrid 1, Legacy 1, Off 1, Off 2,
Legacy 2, Hybrid 2, Advanced 2. Hybrid 1 timing failed; a final Hybrid 3
attempt supplied an additional count window and a replacement timing
capture. All nine count windows remain. Hybrid 3 is an extra final run,
not a matched-order substitute for Hybrid 1.

Each count phase reset the joint measurement window, enabled diagnostics,
requested a 20-second wait, then disabled diagnostics and waited for a
frozen snapshot. Its separate fpsVR phase used diagnostics disabled,
verified unchanged frozen counters, and requested 20 seconds. RPC and
source-observation overhead make the count admission interval longer than
the requested sleep; the JSON retains exact dispatch/response bounds
(20.008–21.759 seconds across all lower/upper bounds). Raw count volumes
are consequently not equal-duration rates.

Both phases reset and verified noon before measurement. Player position
was exactly `[17060.19140625, -12208.3115234375, -4771.19970703125]`
at every available endpoint, in Tamriel / WhiterunExterior01 / Whiterun,
with SkyrimClearTU weather. This differs from the earlier campaign;
there is no quantitative comparison across builds or player positions.
The recorded environment used DLSS Quality preset K, 2016×2240 display
pixels per eye and 1344×1492 render pixels per eye. This records the
environment; it is not render-scale qualification.

## Native and Hybrid count results

Native before/after refer to the native result before and after recovery.
Hybrid values are accepted results divided by examined readback records.
Every nonempty batch here contained 4,096 records. Off produced no count
records; its zero denominator is unavailable, not a zero occlusion rate.

| Run       | Native batches | Native tested | Native occluded before / after | Native % before / after | Hybrid accepted batches | Hybrid tested | Hybrid accepted occluded | Hybrid % |
| --------- | -------------: | ------------: | -----------------------------: | ----------------------: | ----------------------: | ------------: | -----------------------: | -------: |
| balanced1 |          1,649 |     6,754,304 |          4,143,759 / 4,143,759 |       61.3499 / 61.3499 |                       0 |             0 |                        0 |      N/A |
| hybrid1   |              0 |             0 |                          0 / 0 |                     N/A |                   1,005 |     4,116,480 |                  781,521 |  18.9852 |
| legacy1   |          1,855 |     7,598,080 |          4,714,135 / 4,714,135 |       62.0438 / 62.0438 |                       0 |             0 |                        0 |      N/A |
| off1      |              0 |             0 |                          0 / 0 |                     N/A |                       0 |             0 |                        0 |      N/A |
| off2      |              0 |             0 |                          0 / 0 |                     N/A |                       0 |             0 |                        0 |      N/A |
| legacy2   |          1,838 |     7,528,448 |          4,630,608 / 4,630,608 |       61.5081 / 61.5081 |                       0 |             0 |                        0 |      N/A |
| hybrid2   |              0 |             0 |                          0 / 0 |                     N/A |                   1,346 |     5,513,216 |                1,032,817 |  18.7335 |
| balanced2 |          1,978 |     8,101,888 |          5,013,375 / 5,013,375 |       61.8791 / 61.8791 |                       0 |             0 |                        0 |      N/A |
| hybrid3   |              0 |             0 |                          0 / 0 |                     N/A |                   1,326 |     5,431,296 |                1,024,564 |  18.8641 |

Pooled count fractions use summed numerators and denominators:

| Mode                             | Occluded records / examined records | Fraction |
| -------------------------------- | ----------------------------------: | -------: |
| Advanced                         |              9,157,134 / 14,856,192 | 61.6385% |
| Legacy                           |              9,344,743 / 15,126,528 | 61.7772% |
| Hybrid (all three count windows) |              2,838,902 / 15,060,992 | 18.8494% |

All nine windows passed reset identity, frozen/current measurement-window
checks and exact native before/after occluded-plus-visible conservation.
All Hybrid accepted-result bounds, reason counts, and CPU histogram
sample totals reconciled. Recovery, invalid-transform/envelope, unreadable,
empty-native-batch, Hybrid invalidation/fallback/promotion and reason
counters were zero. Native before and after values were identical.
This is settled-scene evidence; it does not exercise moving history,
camera cuts, invalidation, or recovery correctness.

Hybrid submit/readback and native recovery counters can straddle telemetry
admission boundaries. The summary reports their cross-scope differences
separately; all observed differences here were zero. CPU stage timings
from count windows retain means, maxima and histogram percentile bounds.
They are intrusive, nested wall-time scopes and must not be added or
substituted for telemetry-OFF performance measurements.

Advanced 1 lacks the additional active-source-end observation introduced
for subsequent runs. Its reset and frozen count receipts are complete;
the missing observation is retained as an evidence gap.

## Descriptive timing results

**Every timing row in this section is provisional because of concurrent
compilation and unknown per-window menu/simulation state.** Eight captures
passed receipt, raw SHA/size, raw-to-scoped sample reconstruction, monotonic
timestamp/coverage, process/build identity, and frozen-counter checks.
Passing these acquisition checks does not establish comparable workloads.

Values are milliseconds. Percentiles use type-7 interpolation; variance is
sample variance in ms². Samples are observations, not asserted unique
presented frames. CPU and GPU overlap and must not be added.

| Run       | Samples |        CPU mean / p50 / p95 / p99 | CPU variance |        GPU mean / p50 / p95 / p99 | GPU variance |
| --------- | ------: | --------------------------------: | -----------: | --------------------------------: | -----------: |
| balanced1 |   1,249 | 13.397 / 12.700 / 21.820 / 25.252 |       18.750 |   8.893 / 8.500 / 11.500 / 15.352 |        2.176 |
| legacy1   |   1,708 | 10.514 / 10.400 / 14.800 / 17.600 |        8.064 |   8.392 / 8.100 / 10.100 / 13.593 |        1.279 |
| off1      |   1,137 | 17.778 / 16.600 / 26.900 / 28.728 |       17.698 | 11.917 / 11.700 / 15.620 / 19.028 |        4.288 |
| off2      |   1,140 | 16.654 / 15.700 / 25.800 / 29.105 |       16.318 | 11.323 / 11.100 / 14.900 / 18.022 |        3.848 |
| legacy2   |   1,814 | 10.137 / 10.100 / 14.100 / 16.974 |        7.349 |    8.215 / 8.000 / 9.600 / 12.400 |        0.741 |
| hybrid2   |   1,226 | 13.600 / 13.200 / 19.800 / 23.650 |        9.510 | 10.115 / 10.100 / 12.575 / 15.775 |        2.223 |
| balanced2 |   1,709 | 10.480 / 10.400 / 15.000 / 18.384 |        8.647 |   8.380 / 8.000 / 10.200 / 13.600 |        1.148 |
| hybrid3   |   1,234 | 13.525 / 12.900 / 19.700 / 27.301 |       11.768 | 10.185 / 10.100 / 13.500 / 17.100 |        2.837 |

Pooled descriptive distributions exclude Hybrid 1 and include Hybrid 2/3:

| Mode     | Samples |        CPU mean / p50 / p95 / p99 | CPU variance |        GPU mean / p50 / p95 / p99 | GPU variance | CPU / GPU mean, % of 120 Hz budget |
| -------- | ------: | --------------------------------: | -----------: | --------------------------------: | -----------: | ---------------------------------: |
| Off      |   2,277 | 17.215 / 16.100 / 26.200 / 28.920 |       17.316 | 11.620 / 11.400 / 15.320 / 18.496 |        4.154 |                    206.58 / 139.44 |
| Legacy   |   3,522 | 10.320 / 10.200 / 14.500 / 17.279 |        7.729 |    8.300 / 8.000 / 9.900 / 12.800 |        1.010 |                     123.84 / 99.61 |
| Advanced |   2,958 | 11.712 / 11.300 / 19.500 / 23.800 |       14.985 |   8.597 / 8.200 / 11.100 / 14.243 |        1.646 |                    140.54 / 103.16 |
| Hybrid   |   2,460 | 13.563 / 13.100 / 19.800 / 25.164 |       10.640 | 10.150 / 10.100 / 13.000 / 16.241 |        2.531 |                    162.75 / 121.80 |

The reference budget is 8.333 ms at 120 Hz. A fresh, hash-verified fpsVR
session-log snapshot identifies Index at 120.000 Hz for the same fpsVR
process: [refresh provenance](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/fpsvr-refresh-provenance.json).
It is session evidence, not a separate refresh query for each window.

Pooled Hybrid minus Advanced was +1.851 ms CPU / +1.553 ms GPU;
minus Legacy, +3.243 / +1.849 ms; minus Off, −3.652 / −1.470 ms.
These describe the retained samples and cannot attribute a gain or
regression to the culling implementation under the observed confounds.

Advanced 1 itself was 27.84% higher in CPU mean and 6.12% higher in GPU
mean than Advanced 2. Its CPU variance was 18.750 versus 8.647 ms².
It remains included. The first count clock advanced from 12.031858 to
12.156610; its timing clock advanced from 12.036487 to 12.178479.
All later available phase endpoints were exactly 12.0. The subsequent
Journal observation does not establish when a simulation pause began.

## Excluded attempt and pose drift

Hybrid 1 timing failed because four timestamps went backwards in the raw
and scoped data: −24.9966, −8.3383, −8.3324 and −8.3300 ms.
The preserved window contains 1,040 samples. Candidate CPU mean/p95/p99
was 17.048/30.000/36.966 ms; GPU was 10.927/19.505/23.222 ms.
These values are excluded from every pooled and repeat timing comparison.
Sorting is a diagnostic only and does not repair the capture. The owned
logger stopped and settings were restored; end status/scene/pose/health
observations were absent after failure. Its earlier frozen count window
remains valid. See
[the retained ordering diagnosis](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/hybrid1-timestamp-anomaly.json).

Endpoint player position and scene identity were unchanged. HMD drift
is reported without assuming endpoints bound motion inside a window.
Rotation uses normalized quaternions from the recorded float matrices.

| Run       | Count endpoint drift mm / degrees | Timing endpoint drift mm / degrees | Timing start versus first timing start mm / degrees |
| --------- | --------------------------------: | ---------------------------------: | --------------------------------------------------: |
| balanced1 |                   0.911 / 0.01579 |                    0.499 / 0.04119 |                                     0.000 / 0.00000 |
| hybrid1   |                   0.740 / 0.02234 |                        Unavailable |                                     2.997 / 0.09164 |
| legacy1   |                   0.576 / 0.03015 |                    0.530 / 0.03870 |                                     4.339 / 0.10048 |
| off1      |                   0.611 / 0.00809 |                    0.298 / 0.03397 |                                     4.867 / 0.13083 |
| off2      |                   0.899 / 0.02065 |                    0.249 / 0.04216 |                                     5.678 / 0.15968 |
| legacy2   |                   0.810 / 0.04186 |                    0.530 / 0.03101 |                                     6.427 / 0.21617 |
| hybrid2   |                   0.316 / 0.04455 |                    0.810 / 0.02849 |                                     7.264 / 0.19316 |
| balanced2 |                   0.764 / 0.04092 |                    0.355 / 0.03672 |                                     8.207 / 0.22115 |
| hybrid3   |                   0.459 / 0.02702 |                    0.594 / 0.01223 |                                     9.297 / 0.23970 |

## Quiet repeat and finalization

The first quiet attempt, `quiet-balanced1`, rejected active `cl` PID 27276
and `cmake` PID 28932 at 22:19:38 UTC before starting the logger.
Its receipt has zero recording commands, zero samples and no raw/window
paths. Settings were restored. This is a successful activity guard,
not a quiet performance result:
[guard receipt](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/quiet-balanced1-20261003T221938687Z-c18f036e.receipt.json).
The next complete matrix must wait for confirmation that the entire
other build sequence has finished; gaps between build steps are not
completion evidence. Future captures use distinct `quiet2-*` labels.

The prepared quiet protocol checks menus before/after (only HUD/Cursor
allowed), advancing clock, unchanged scene/player, frozen diagnostics,
and a five-second compiler/packager quiet preflight followed by one-second
process and cumulative MSBuild CPU observations during capture. A bounded
process watch can still miss short-lived processes between observations.
No quiet comparison is available in this checkpoint.

At the first campaign finalization, native Advanced interior/exterior
culling and telemetry were restored. Camera ownership was released;
input and recording were inactive. The CSX profiler was disabled and
not capturing. Shader compilation was settled with no failed tasks or
heavy tasks in flight. These CSX shader checks do not establish absence
of unrelated C++ compilation. No new per-pass GPU timings were collected.

The final readable/physical receipts contain `logLevel: null` from an
incorrect selected JSON key. They remain unchanged. Separate
[logging verification](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/logging-verification.json)
at 22:08:51 UTC confirms `Advanced.Log Level = 2` (Info), with settings
SHA-256 retained. This observation required no settings change.

Durable automation feedback records are
`AUTO-20261003-220127115-F3B1E4E2` (fpsVR ordering) and
`AUTO-20261003-220125907-59BE4D31` (identity handling).

Reproduce the retained count/descriptive audit with:

```powershell
python "D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/Summarize-NativeCounts.py"
python "D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T215024Z-native-counts/Write-NativeCountsReport.py"
```

The audit passed all nine count windows and all eight retained acquisition
checks; Hybrid 1 remains failed and quarantined. The report is a provisional
measurement checkpoint. Motion/stereo evidence remains separately scoped
in [the earlier runtime report](vr-hybrid-culling-runtime-2026-10-03.md);
this campaign supplies no new visual or temporal qualification.
