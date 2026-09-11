# NVIDIA render-scale tuning: 9 September 2026

The available evidence shows no functional regression against the preceding
NVIDIA run, `nvidia-mtlid7m3` (3 September). Cross-commit performance remains
undetermined: the previous run used 2468x2740 per eye, and this run used
1512x1680. The previous ledger does not retain numeric transition timings,
and its referenced per-run timing bundle was not found in the inspected local
evidence locations. The lower resolution has about 37.6% of the previous
output pixels; memory or latency changes cannot be attributed to commits.

## Available results

Run `nvidia-mtu2u3dm` exercised source `f2a73cd950e98cd27eadd34502dbeea486dce481`
(dirty), build `a964eb6ae2581c96e5a3d54229c349e261197220bd5fa40d2e0d30b91b312d14`,
in Dragonsreach: two same-process 33-transition passes, five-second pacing,
DLSS K and FSR3. DLL bytes and SHA256 match the deployed build manifest.

| Observation                                                                                     | Previous run                      | This run                                                      | Interpretation                                                     |
| ----------------------------------------------------------------------------------------------- | --------------------------------- | ------------------------------------------------------------- | ------------------------------------------------------------------ |
| Render completion                                                                               | 66/66 PASS                        | Live observed 66/66 PASS; 57 full receipts retained, all PASS | No observed functional regression                                  |
| Device loss / OOM / terminal provider failure / native qualification failure / credible timeout | 0                                 | 0 in retained 57; none reported live across 66                | No new failure observed; coverage is partial                       |
| Task 2 original classification                                                                  | 60 PASS / 0 FAIL / 6 inconclusive | Live 50/0/16; retained 43/0/14                                | Evidence classification changed; not a proven rendering regression |
| Task 2 with corrected validator                                                                 | Not reprocessed                   | 57/57 retained receipts PASS                                  | Native producer-contract mismatch corrected offline                |
| Presentation stretches                                                                          | 32, all recovered                 | 30 in retained 57, all recovered                              | Unequal coverage; do not infer fewer stretches                     |
| Pass 1 private-memory growth                                                                    | +1202.3672 MiB                    | +155.6172 MiB                                                 | Lower observed growth; resolution differs                          |
| Memory confirmation                                                                             | Inconclusive                      | Inconclusive; pass 2 end missing                              | Cannot conclude leak improvement or regression                     |

The native proof problem was in the runner: None/TAA omit an optional shared
vendor flag, and native DLAA may validly use generation zero. Exact same-frame,
owner, backend, device, resources and stereo execution checks now accept those
producer shapes. Original receipts and their original verdicts are preserved.
This reclassification is a tooling correction, not a new runtime measurement.

## Same-build repeat timing

These are server-reported transition completion latencies, not steady-state
frame-render times. Only matching rows 1–24 support a repeat comparison.

| Coverage                        | Mean presentation ms | Mean strict completion ms | Worst strict ms |
| ------------------------------- | -------------------: | ------------------------: | --------------: |
| pass1_all33 (33 rows)           |              662.793 |                   787.217 |        1447.129 |
| pass2_first24 (24 rows)         |              642.471 |                   754.196 |        1335.828 |
| pass1_matched_first24 (24 rows) |              648.365 |                   759.480 |        1386.810 |

The matched mean changes 759.480 → 754.196 ms (−0.70%), consistent with similar
average timing in this same-build repeat. Individual transitions vary: FSR3
rows 14 and 15 rise 865.969 → 1335.828 ms and 819.370 → 1265.105 ms, respectively,
about 54%. Row 15 presentation stretch rises 3 → 13 frames, then recovers. This is
a repeat-variance observation to retain, not evidence identifying a bad commit.

| Row | Target                     | Pass 1 strict ms |      Pass2 strict ms |
| --: | -------------------------- | ---------------: | -------------------: |
|   1 | none quality0, scale=False |          693.477 |              672.609 |
|   2 | taa quality0, scale=False  |          160.716 |              159.893 |
|   3 | dlss quality0, scale=False |          252.047 |              247.240 |
|   4 | dlss quality1, scale=True  |          929.545 |              959.898 |
|   5 | dlss quality2, scale=True  |         1165.858 |              987.578 |
|   6 | dlss quality3, scale=True  |         1129.215 |             1162.545 |
|   7 | dlss quality4, scale=True  |         1311.420 |             1133.710 |
|   8 | dlss quality5, scale=True  |         1341.494 |             1166.929 |
|   9 | dlss quality6, scale=True  |         1262.846 |             1178.773 |
|  10 | dlss quality0, scale=False |          777.340 |              812.760 |
|  11 | taa quality0, scale=False  |          427.240 |              388.229 |
|  12 | none quality0, scale=False |          163.985 |              181.004 |
|  13 | fsr quality0, scale=False  |         1386.810 |              603.329 |
|  14 | fsr quality1, scale=True   |          865.969 |             1335.828 |
|  15 | fsr quality2, scale=True   |          819.370 |             1265.105 |
|  16 | fsr quality3, scale=True   |          806.393 |              817.618 |
|  17 | fsr quality4, scale=True   |          713.672 |              739.931 |
|  18 | fsr quality5, scale=True   |          712.746 |              679.871 |
|  19 | fsr quality6, scale=True   |          724.159 |              688.160 |
|  20 | fsr quality0, scale=False  |         1005.280 |              986.746 |
|  21 | taa quality0, scale=False  |          422.430 |              454.930 |
|  22 | none quality0, scale=False |          181.514 |              174.971 |
|  23 | dlss quality0, scale=False |          240.211 |              250.677 |
|  24 | fsr quality0, scale=False  |          733.775 |             1052.376 |
|  25 | dlss quality1, scale=True  |         1447.129 | n/a; receipt missing |
|  26 | fsr quality1, scale=True   |         1368.140 | n/a; receipt missing |
|  27 | none quality0, scale=False |          702.388 | n/a; receipt missing |
|  28 | fsr quality6, scale=True   |          864.806 | n/a; receipt missing |
|  29 | dlss quality6, scale=True  |         1429.611 | n/a; receipt missing |
|  30 | taa quality0, scale=False  |          684.849 | n/a; receipt missing |
|  31 | fsr quality0, scale=False  |          588.876 | n/a; receipt missing |
|  32 | none quality0, scale=False |          407.674 | n/a; receipt missing |
|  33 | dlss quality0, scale=False |          257.162 | n/a; receipt missing |

## Memory boundaries

All values below are MiB. System commit is machine-wide; DXGI usage is GPU
residency. Texture counts are the capture cohort, not the complete inventory.

| Boundary       | Process private | System commit | DXGI usage | Tracked live textures |
| -------------- | --------------: | ------------: | ---------: | --------------------: |
| pass 1 start   |       16780.922 |     52602.707 |   4536.723 |                     0 |
| pass 1 end     |       16936.539 |     52752.961 |   3836.473 |                   209 |
| cooldown start |       16913.023 |     52806.207 |   3809.348 |                   209 |
| cooldown end   |       16906.793 |     52780.125 |   3809.348 |                   209 |
| pass 2 start   |       16795.527 |     52700.477 |   3682.184 |                     0 |
| pass 2 end     |             n/a |           n/a |        n/a |                   n/a |

The texture capture is stopped during cooldown, so unchanged counts there are
a frozen snapshot. The later cumulative stopped pass 2 capture retains 213 live
cohort textures; it is available separately and does not replace the missing
pass 2 end memory boundary. CPU/GPU stopped-session counters are likewise
preserved separately. The final profiler snapshot contains zero captured
frames/timers; that is unavailable performance evidence, not zero rendering cost.

## Retention and validation

The bundle retains 57 complete transition receipts (pass 1 all 33; pass 2 rows 1–24),
all 24 compact DLSS trace summaries, startup, pass 1 cleanup, five memory
boundaries, and the final cumulative telemetry. Eighteen trace windows retained
only their first 16 records before the old runner reset them; six were complete.
All 24 compact summaries report zero dropped-record, evaluation and duplicated
constants failures. Missing pages and nine missing full row receipts cannot be
reconstructed from the retained data. Failed offline reconstruction diagnostics
are separated under `analysis/`; they are not new live results.

Automation commit `0a9d34a` is integrated into local `dev`. It journals exact
receipts during measurement, drains trace pages before reset, and corrects
native proof validation. Runner, finalizer, protocol, distribution, plugin and
skill checks passed; offline revalidation passed 57/57 unchanged receipts. No
revised live protocol run has been performed. Partial comparisons remain
allowed and explicitly labelled; no stronger ledger admission rule was added.

The finalized bundle contains 132 raw JSON files and 552138 extracted JSON values.
Every bundle file is indexed with byte length and SHA256. Historical ledger
cells are preserved, including a pre-existing short unnamed row (blank-padded
to align the appended column). New timing rows leave historical data unavailable.

-   [Available per-row timing CSV](../../artifacts/renderscale-tuning/nvidia-mtu2u3dm/analysis/available-timings.csv)
-   [Complete extracted values](../../artifacts/renderscale-tuning/nvidia-mtu2u3dm/evidence-values.csv)
-   [Raw evidence index](../../artifacts/renderscale-tuning/nvidia-mtu2u3dm/receipt-index.json)
-   [Generated partial report](../../artifacts/renderscale-tuning/nvidia-mtu2u3dm/report.md)
-   [Corrected validator audit](../../artifacts/renderscale-tuning/nvidia-mtu2u3dm/corrected-validator-audit.json)
-   [Previous run's runtime findings](vr-render-scale-authority-map.md#runtime-validation)
