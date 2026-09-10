# NVIDIA transition comparison: nv-mtud89dr

The durable comparison record is the existing
[VR render-scale comparison ledger](vr-render-scale-comparison-ledger.csv).
It now includes 66 timing rows for this matrix, containing 198 available
strict-completion values across the four September 9 runs. This report
presents those results with context; future measurements update that same
ledger. Existing build columns and historical metric cells are preserved.

All 66 measured transitions passed. Pass 2 is one repeat, interrupted and
continued across two runs: `nv-mtud89dr` rows 1–8, then `nv-mtud89dr-s2` rows 9–33.
The game process and Build ID stayed the same; telemetry was stopped and
restarted between segments. No measured destination was replayed.
The five-second inter-row cadence excludes this interruption. Row-local
latencies remain usable; continuous-repeat memory and performance windows do not.

Values are dispatch-to-strict-completion latency in milliseconds, excluding
the five-second pre-dispatch wait. Lower is faster. These are not frame times.
Current clean source: `348803c1831c8cd71ceb75a58143ad0bcb00abb2`. Previous PR66 comparison:
`nv-mtu8nhph`, clean source `89e3964582730b92c1885eaef8f865e2d1d02ea9`.
Both used Dragonsreach, 1512×1680 output per eye, DLSS K, FSR3, and the
0.3/0.3/0.7 foveation fixture. The PR66 repeat retained only rows 1–9.
Different memory/session conditions prevent attributing differences solely to PR66.

Pass 1 means: current 792.385 ms; previous PR66 794.846 ms
(-0.31%). Current repeat mean: 833.175 ms
(+5.15% versus current pass 1).
Matched first-nine repeat means: current 926.779 ms;
previous PR66 898.537 ms.
The earlier pressure-affected PR66 attempt `nvidia-mtu6cpo8` averaged
2569.260 ms in pass 1; its per-row values are an
additional column in `timing-comparison.csv`, not the primary baseline here.

\*Pass 2 resumes at row 9 in segment 2. — means no retained PR66 repeat timing.

|   # | Transition                                      | Current pass 1 | Current pass 2\* | PR66 pass 1 | PR66 pass 2 | RC166 baseline | Sep 3 pass 1 | Sep 3 pass 2 |
| --: | ----------------------------------------------- | -------------: | ---------------: | ----------: | ----------: | -------------- | ------------ | ------------ |
|   1 | DLSS Hoshipa → None                             |          708.5 |            723.7 |       682.3 |       688.3 | N/P            | N/A          | N/A          |
|   2 | None → TAA                                      |          164.6 |            187.4 |       171.5 |       189.6 | N/P            | N/A          | N/A          |
|   3 | TAA → DLAA                                      |          254.1 |            295.7 |       288.4 |       276.5 | N/P            | N/A          | N/A          |
|   4 | DLAA → DLSS Hoshipa                             |          955.0 |           1067.2 |       921.3 |      1090.5 | N/P            | N/A          | N/A          |
|   5 | DLSS Hoshipa → DLSS Ultra Quality               |         1125.4 |           1126.1 |      1197.6 |      1010.6 | N/P            | N/A          | N/A          |
|   6 | DLSS Ultra Quality → DLSS Quality               |         1114.6 |           1257.7 |      1080.8 |      1190.9 | N/P            | N/A          | N/A          |
|   7 | DLSS Quality → DLSS Balanced                    |         1274.8 |           1258.5 |      1293.2 |      1291.9 | N/P            | N/A          | N/A          |
|   8 | DLSS Balanced → DLSS Performance                |         1295.0 |           1243.2 |      1267.1 |      1174.7 | N/P            | N/A          | N/A          |
|   9 | DLSS Performance → DLSS Ultra Performance       |         1249.9 |           1181.6 |      1235.7 |      1173.8 | N/P            | N/A          | N/A          |
|  10 | DLSS Ultra Performance → DLAA                   |          795.4 |            882.2 |       726.9 |           — | N/P            | N/A          | N/A          |
|  11 | DLAA → TAA                                      |          425.4 |            448.2 |       425.3 |           — | N/P            | N/A          | N/A          |
|  12 | TAA → None                                      |          166.9 |            175.0 |       163.8 |           — | N/P            | N/A          | N/A          |
|  13 | None → FSR3 Native AA                           |          769.8 |            605.8 |       731.5 |           — | N/P            | N/A          | N/A          |
|  14 | FSR3 Native AA → FSR3 Hoshipa                   |         1350.1 |            907.3 |      1293.7 |           — | N/P            | N/A          | N/A          |
|  15 | FSR3 Hoshipa → FSR3 Ultra Quality               |          860.1 |            825.9 |       758.6 |           — | N/P            | N/A          | N/A          |
|  16 | FSR3 Ultra Quality → FSR3 Quality               |          813.9 |            785.6 |      1198.7 |           — | N/P            | N/A          | N/A          |
|  17 | FSR3 Quality → FSR3 Balanced                    |          714.5 |            837.4 |       721.3 |           — | N/P            | N/A          | N/A          |
|  18 | FSR3 Balanced → FSR3 Performance                |          705.4 |            871.1 |       703.4 |           — | N/P            | N/A          | N/A          |
|  19 | FSR3 Performance → FSR3 Ultra Performance       |          697.3 |            832.9 |       714.8 |           — | N/P            | N/A          | N/A          |
|  20 | FSR3 Ultra Performance → FSR3 Native AA         |          681.4 |            781.0 |       683.0 |           — | N/P            | N/A          | N/A          |
|  21 | FSR3 Native AA → TAA                            |          445.1 |            427.5 |       478.7 |           — | N/P            | N/A          | N/A          |
|  22 | TAA → None                                      |          164.4 |            196.5 |       160.1 |           — | N/P            | N/A          | N/A          |
|  23 | None → DLAA                                     |          240.9 |            280.1 |       239.3 |           — | N/P            | N/A          | N/A          |
|  24 | DLAA → FSR3 Native AA                           |          927.8 |           1206.8 |       827.5 |           — | N/P            | N/A          | N/A          |
|  25 | FSR3 Native AA → DLSS Hoshipa                   |         1416.7 |           2017.4 |      1424.7 |           — | N/P            | N/A          | N/A          |
|  26 | DLSS Hoshipa → FSR3 Hoshipa                     |         1611.8 |           1577.4 |      1631.1 |           — | N/P            | N/A          | N/A          |
|  27 | FSR3 Hoshipa → None                             |          758.9 |            808.2 |       692.4 |           — | N/P            | N/A          | N/A          |
|  28 | None → FSR3 Ultra Performance                   |          852.0 |            991.2 |       847.3 |           — | N/P            | N/A          | N/A          |
|  29 | FSR3 Ultra Performance → DLSS Ultra Performance |         1404.0 |           1556.9 |      1440.1 |           — | N/P            | N/A          | N/A          |
|  30 | DLSS Ultra Performance → TAA                    |          779.1 |            749.0 |       763.0 |           — | N/P            | N/A          | N/A          |
|  31 | TAA → FSR3 Native AA                            |          672.7 |            657.6 |       663.5 |           — | N/P            | N/A          | N/A          |
|  32 | FSR3 Native AA → None                           |          457.3 |            456.3 |       515.3 |           — | N/P            | N/A          | N/A          |
|  33 | None → DLAA                                     |          295.8 |            276.4 |       287.9 |           — | N/P            | N/A          | N/A          |

## Added historical comparisons

RC166 baseline and the latest complete September 2-3 run are included in
the transition table. `N/P` means the RC166 reference used a different
protocol and has no matched 33-transition timing. `N/A` means September 3
completed the transitions, but its timing bundle is unavailable in the
inspected local evidence locations. Neither marker means zero or not run.
The CSV retains empty numeric cells and a separate availability status.

| Reference                                          | Date (UTC)              | Recorded source revision                       | Recorded completion                                         | Timing availability                                                                    |
| -------------------------------------------------- | ----------------------- | ---------------------------------------------- | ----------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| RC166 baseline                                     | 2026-08-27              | RC166 +13; 94165e2e7 clean                     | Historical fixed-delay COC qualification                    | Different protocol; mean stabilization 22.0 frames, not strict-completion milliseconds |
| Latest complete September 2-3 run: nvidia-mtlid7m3 | 2026-09-03 13:00:11.411 | v3.19.0-pr39 +230; 539705004 dirty; RC166 +510 | 66/66 render PASS; Task 2 60 PASS / 0 FAIL / 6 INCONCLUSIVE | Per-transition timings unavailable locally for both passes                             |

The September 3 run is later than the September 2 20:49 run and the
September 3 05:56 run. Its exact recorded source commit is
`539705004aa993816b79773fa287f91324312fbc`; its uncommitted changes are
part of the measured build and cannot be identified by that commit alone.
Its Build ID is
`54b17d9e36fdbdeead739396a4637acc529bf43867ce39e47db17a6175a14a56`.
The recorded relative distance is RC166 +510; RC166 is the same tag/base
`b95958cc0c119625d8a7dd9657442c92122f240d` listed below.

Both historical references used 2468x2740 output per eye. The current and
PR66 measurements used 1512x1680. No timing percentage against either
historical reference is computed. RC166 +23 also has a separate 25-step
CS-menu record with 21/25 strict completions; its checker, sequence, and
metrics differ, so those values are not substituted into this matrix.

The existing ledger already contains both historical columns. They are
referenced here without duplicating measurements or changing their cells.
Source records are the comparison ledger, the September 3 runtime findings
in `vr-render-scale-authority-map.md`, and the preserved prior comparison
archive. Repository artifacts/build evidence, automation artifacts, sibling
worktrees, and repository backups were searched for the historical run IDs;
no matching bundle was found in the accessible locations. Some unrelated
dependency directories and user-home enumeration were access restricted;
this does not establish that the evidence is absent from every machine or
storage location.

### Committed record check

Commit `539705004aa993816b79773fa287f91324312fbc`, authored September 3
at 10:17:22 UTC, is `docs(vr): preserve render-scale tuning record`.
It committed the aggregate ledger, iteration state, and compact summaries.
It includes detailed per-transition tables for the August 29 run on
`49761bc99`, but predates the September 3 13:00 run measured on
`539705004` with local changes.

The latter run's ledger column and runtime findings were committed in
`190c28a39a52c2bace475d1143a124c5893ac741` at 13:11:51 UTC.
The timing and per-transition fields in that committed column already say
`n/a; renderscale-tuning nvidia; see evidence bundle`. The numeric timings
for this September 3 run are therefore not recoverable from those committed
records. Its original comparison archive also records zero available
timing rows. This gap predates the current comparison update.

## Build identities

| Build                                             | Exact measured source                    | Branch / reference commit                                                |
| ------------------------------------------------- | ---------------------------------------- | ------------------------------------------------------------------------ |
| Current main-VR; v3.19.0-pr28 +274                | 348803c1831c8cd71ceb75a58143ad0bcb00abb2 | main-VR: 348803c1831c8cd71ceb75a58143ad0bcb00abb2                        |
| Previous PR66; v3.19.0-pr28 +278                  | 89e3964582730b92c1885eaef8f865e2d1d02ea9 | fix/vr-dlss-eye-bound-dilation: 85ee7d4a572544410fa29463d5d704b50984610f |
| September 3 complete run; v3.19.0-pr39 +230 dirty | 539705004aa993816b79773fa287f91324312fbc | Recorded RC166 +510; uncommitted changes also included                   |
| Historical RC166 +13 baseline                     | 94165e2e70db2bbefd878aecbfa7733ee336ab63 | RC166 tag/base: b95958cc0c119625d8a7dd9657442c92122f240d                 |
| Historical RC166 +23 comparison                   | 86d21fc0ae35532829b22d7c18a708ff8e21c1eb | RC166 tag/base: b95958cc0c119625d8a7dd9657442c92122f240d                 |

The PR66 measured build merges main-VR 348803c18 and PR66 branch
85ee7d4a5. The RC166 measured branch commits are retained historical
identities from the ledger and iteration record; their objects are absent
from this checkout. The RC166 tag/base and PR66 branch/merge identities
were resolved from local Git. The RC166 tag itself is not the measured
RC166 +13 or +23 build. Relative counts use each producer's recorded
source_describe and are not comparable absolute revision numbers.

## Validation and limitations

All 66 exact terminal receipts and all 24 DLSS trace windows passed the
packaged classification and complete-window checks. The deployed DLL's
SHA-256, byte length, and manifest match the bound Build ID.
All task-owned stress, CPU, GPU, texture, load-presentation, DLSS trace,
qualification and profiler state were confirmed stopped or disabled.
No build, deployment, persistent settings change, commit, or push was done.
No independent screenshot review or matched steady-state frame-time test ran.

Execution remains marked INTERRUPTED with continuation COMPLETED_ACROSS_TWO_RUNS;
reporting is INCOMPLETE for the continuous protocol because repeat telemetry
was restarted. All row timings are retained. No device loss, OOM, terminal
producer failure, vendor-native qualification failure, or credible liveness
timeout was observed in the measured rows: each count is zero.
Optional cumulative operation history returned invalid request; individual
row operations remain preserved. The local finalizer uses the existing
streaming CSV writer with the installed classification logic to avoid its
known large-string export limit. No runtime validator was changed.
Feedback recorded: AUTO-20260909-183826890-0B77F23C.

## Pass timing and anomaly details

| Pass             | Strict mean / max ms | Presentation mean ms | Cleanup tail mean / max ms | Producer retries | Recovered stretch rows |
| ---------------- | -------------------- | -------------------: | -------------------------- | ---------------: | ---------------------: |
| 1                | 792.385 / 1611.836   |              675.343 | 117.042 / 268.055          |                8 |                     16 |
| 2 (two segments) | 833.175 / 2017.373   |              702.919 | 130.256 / 271.519          |                9 |                     16 |

## Memory boundaries

Repeat memory classification: **inconclusive**. Segment 1 lacks an immediate
measured end boundary; delayed cleanup is not substituted. Growth ratios
for a continuous repeat are n/a. No leak or retention conclusion is possible.

| Metric            | Pass 1 start / end / delta       | Cooldown start / end / delta     | Repeat segment 1 start | Repeat segment 2 start / end / delta | Repeat/P1 growth ratio |
| ----------------- | -------------------------------- | -------------------------------- | ---------------------: | ------------------------------------ | ---------------------- |
| processPrivateMiB | 16565.207 / 16890.766 / 325.559  | 16900.352 / 16900.352 / 0.000    |              16799.203 | 16450.477 / 17132.484 / 682.008      | n/a                    |
| systemCommitMiB   | 44695.879 / 49713.457 / 5017.578 | 50165.039 / 51784.930 / 1619.891 |              48778.383 | 48093.531 / 50928.000 / 2834.469     | n/a                    |
| dxgiUsageMiB      | 4413.863 / 3777.941 / -635.922   | 3777.941 / 3777.941 / 0.000      |               3728.590 | 2883.746 / 3830.496 / 946.750        | n/a                    |
| liveTextures      | 0.000 / 241.000 / 241.000        | 241.000 / 241.000 / 0.000        |                  0.000 | 0.000 / 231.000 / 231.000            | n/a                    |
| liveTextureMiB    | 0.000 / 2341.474 / 2341.474      | 2341.474 / 2341.474 / 0.000      |                  0.000 | 0.000 / 2344.786 / 2344.786          | n/a                    |
| pressure          | Normal / Normal / n/a            | Normal / Normal / n/a            |                 Normal | Normal / Normal / n/a                | n/a                    |
