# Matched-pose baseline versus head: six runs

These three head and three baseline repeats do **not** reproduce a broad
DLSS CPU-execution or GPU-time regression. Small additional render-thread
execution remains in DLAA Saves 08 and 09. There is also measurable added
native frustum/job work in the DLSS scenes, even though their total
execution per recorded frame is lower. These are separate findings.

The comparison uses the new evening cohort with the user-confirmed matched
HMD position. Older poses, the original September 15 run and SSGI-disabled
head runs are excluded from these statistics. No rendering code, settings,
benchmark scripts, timing windows or spike rules changed during analysis.

-   [All timing, P95/P99, single-spike and grouped-spike values](timing-tables.md)
-   [All WPR execution, ready-delay, wait and category comparisons](wpr-comparison.md)
-   [Settling, stretch and complete lifecycle descriptions](health.md)
-   [Settings and exact source/DLL/PDB identity](settings-provenance.md)
-   [Late-hold freshness counters](late-health-counters.md)
-   [Native address-page differences](native-pages.md)
-   [Validation receipt and output hashes](validation.json)

## Comparison method

Save order was 08, 11, 09, 12, 10, 13. DLAA saves 08/09/10 are interiors;
DLSS saves 11/12/13 are exteriors. Mode and scene therefore vary together:
these groups do not isolate the effect of switching DLAA to DLSS in one
scene. Each table compares the same save across builds.

Every timing, percentile and spike statistic uses the saved world-entry
+[50,60) seconds. WPR uses those same markers and windows. Tables below
show the median of three repeats per save, with head minus baseline deltas.
Complete repeat values and ranges remain in the linked tables. There is
no pooling of different scenes as independent replicates, no mean/SE
assumption and no claim that nine pairwise contrasts are nine experiments.

The user's practical variation budgets are 0.35 ms CPU and 0.15 ms GPU
**for means**, not significance thresholds or percentile tolerances.
fpsVR CPU time is not interchangeable with scheduled render-thread CPU
execution. WPR is reported both per recorded fpsVR frame and over the
fixed ten-second window. Recorded frame counts are not substituted for
producer-frame counters, eye evaluations or individual draw counts.

## CPU and GPU frame times

CPU means, milliseconds:

| Save | Mode | Baseline |   Head |  Delta |
| ---- | ---- | -------: | -----: | -----: |
| 08   | DLAA |    4.839 |  5.054 | +0.215 |
| 09   | DLAA |    5.457 |  5.493 | +0.036 |
| 10   | DLAA |    3.081 |  3.032 | -0.049 |
| 11   | DLSS |    8.499 |  9.010 | +0.511 |
| 12   | DLSS |    8.443 |  8.398 | -0.045 |
| 13   | DLSS |   17.745 | 13.231 | -4.514 |

GPU means, milliseconds:

| Save | Mode | Baseline |   Head |  Delta |
| ---- | ---- | -------: | -----: | -----: |
| 08   | DLAA |   10.004 | 10.049 | +0.045 |
| 09   | DLAA |    8.971 |  9.005 | +0.034 |
| 10   | DLAA |    9.688 |  9.683 | -0.005 |
| 11   | DLSS |    8.440 |  8.277 | -0.163 |
| 12   | DLSS |    8.918 |  8.911 | -0.007 |
| 13   | DLSS |   12.396 | 11.750 | -0.646 |

Save 11 is the only higher median CPU mean beyond the stated practical
budget. Its WPR result below shows why this must not be equated with more
CPU execution. DLAA GPU means remain within 0.045 ms of baseline. There is
no positive DLSS GPU mean delta in this cohort.

Save 13 varies substantially in both builds: baseline CPU 14.532–19.775 ms
and GPU 12.001–13.535 ms; head CPU 11.321–17.173 ms and GPU
11.340–12.636 ms. The lower head median is an observation, not an isolated
causal improvement. Save 13 remains a useful workload example, not the
sole optimization target.

## What WPR changes about the interpretation

Running, ready and blocked-wait values are milliseconds per **recorded**
frame. These are independently summarized medians; their component deltas
must not be added to reconstruct the median total.

| Save | Recorded fps baseline → head | Running baseline → head | Running delta | Ready delta | Wait delta | Running share delta, percentage points |
| ---- | ---------------------------- | ----------------------- | ------------: | ----------: | ---------: | -------------------------------------: |
| 08   | 60.0 → 60.0                  | 13.136 → 13.456         |        +0.320 |      +0.044 |     -0.286 |                                 +1.922 |
| 09   | 75.1 → 75.4                  | 10.427 → 10.825         |        +0.398 |      -0.001 |     -0.200 |                                 +1.801 |
| 10   | 60.0 → 60.0                  | 10.198 → 9.926          |        -0.272 |      -0.050 |     +0.326 |                                 -1.646 |
| 11   | 93.0 → 107.1                 | 10.101 → 8.629          |        -1.472 |      +0.043 |     +0.012 |                                 -1.907 |
| 12   | 86.0 → 93.6                  | 11.170 → 10.089         |      -1.080\* |    +0.005\* |   +0.145\* |                               -1.648\* |
| 13   | 57.5 → 59.8                  | 16.920 → 16.177         |      -0.743\* |    -0.011\* |   +0.059\* |                               -0.551\* |

\* Scheduler aggregates include a flagged head window; see validation.
Sampled execution and fpsVR metrics are retained separately.

For Save 11, all three head running/frame observations are below all
three baseline observations: head 8.593–9.740 versus baseline
9.963–11.092 ms. The running share of the fixed window also falls.
Thus the +0.511 ms fpsVR CPU mean is **not evidence of a heavier steady
render thread** in this save. Frame timing/cadence changed; the exact cause
of that change is not established by these traces alone.

Saves 08/09 instead show small additional execution with similar recorded
cadence. Ready delay does not dominate their median difference. Save 08
running ranges are 12.397–13.285 ms baseline versus 13.344–16.395 ms head;
Save 09 ranges overlap, 10.347–10.818 versus 10.481–10.868 ms. That makes
Save 08 the clearer residual execution target and Save 09 a supporting,
less decisive case.

## Remaining components and priorities

| Priority | Finding                                             | Evidence and next step                                                                                                                                                                                                                                                                                                                              |
| -------- | --------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1        | Small interior CPU-execution increase               | Saves 08/09 add about 0.32/0.40 ms per recorded frame. CSX leaf weight rises about 0.20/0.10 ms; D3D11 leaf weight rises about 0.10 ms in each. Named shadow-map contexts also rise about 0.20/0.16 ms, but overlap other categories. Measure equivalent material-validation paths and work counts before proposing a correction.                   |
| 2        | Added native frustum/job work in exterior rendering | The live-mapped sphere/compound ranges add about 0.21/0.045 ms in Save 12 and 0.56/0.12 ms in Save 13. Save 11/12 also add about 0.18/0.20 ms of native job-helper execution. Count tests, visited objects and pass/eye invocations to determine whether work is necessary or redundant. Do not disable culling or assume stereo work is duplicate. |
| 3        | Changed CPU spike grouping                          | Saves 11/12 shift from isolated spikes toward consecutive slow frames. Investigate frame pacing and the relation to actual work/ready/wait intervals; a lower single-spike count alone is not an improvement.                                                                                                                                       |
| 4        | Repeat variability and scene/physics work           | Whole-process CPU varies much more than render-thread medians. Large kernel-leaf weights occur beneath CBP/hdtsmp stacks and vary within the same build. Keep those contexts separate from CSX ownership and rendering costs; the traces do not prove these mods caused the frame spikes.                                                           |
| 5        | GPU attribution remains a measurement gap           | Current matched means do not show a DLSS GPU regression. CPU stacks cannot assign GPU pass cost. If GPU optimization is pursued, use matched per-pass GPU captures and counts, preserving the original prepared input and rendering quality.                                                                                                        |

Source inspection alone cannot prove runtime cost or an optimization's
benefit. These priorities identify measurements and candidates, not an
instruction to remove protections or alter scheduling.

### Material and light ownership costs

Observed inclusive material-guard union and RetainScene CPU sample weights,
milliseconds per recorded frame:

| Save | Material guard, head | RetainScene, head |
| ---- | -------------------: | ----------------: |
| 08   |                0.119 |             0.018 |
| 09   |                0.097 |             0.013 |
| 10   |                0.125 |             0.015 |
| 11   |                0.061 |             0.024 |
| 12   |                0.048 |             0.017 |
| 13   |                0.146 |             0.027 |

The baseline does not have the same named material guard; zero matching
samples there is not a measurement of an equivalent safe implementation.
This cohort cannot isolate the last material optimization from all other
changes since baseline. It does establish that the guard still has a
measurable cost and cannot explain multi-millisecond outliers by itself.
Keep its malformed-material protections. Any cheaper replacement needs
equivalent safety/lifetime tests and a one-variable benchmark.

Retention hash samples are about 0.001–0.007 ms per recorded frame in the
head. No sustained retention spin-lock cost is established. Sampling and
inlining limit absence claims; ownership of lights must remain valid while
raw pointers are consumed.

### Native culling and named contexts

The retained [live-snapshot interpretation](../cpu-dlss-regression-handover-20260916.md#live-ghidra-findings)
identifies RVA 0xDA54B0 as six-plane bounding-sphere tests and 0xDA33C0 as
compound-frustum traversal. The current traces match the captured Skyrim
image version 1.4.15.0, timestamp 0x5AEADAA0 and size 60,133,376 bytes.
This is reused live-code evidence, not a new static Ghidra analysis;
identical in-memory patch bytes in every later run are not proven.

These native routines are not equivalent to the complete selectable depth
culling feature. The named CSX temporal/recovery scope is only about
0.005–0.062 ms per recorded frame in this head. The native job/helper
ranges execute work and must not be labeled blocked waiting. Save 13's
additional frustum work is real sampled execution even though its total
CPU execution falls; it is partly offset by reductions elsewhere.

Named immediate-render wrapper coverage differs between builds:
DrawRenderPassImmediately versus BSBatchRenderer_RenderPassImmediately2.
The raw literal counts are retained, but their large apparent subtraction
is explicitly marked **not comparable**. Likewise, inclusive shadow/depth
contexts are not exclusive hook overhead or direct GPU-pass measurements.
Do not attribute their full descendant weight to the wrapper itself.

## Noise and outliers

Spike thresholds, quantiles and grouping rules remain the saved rules.
All-spike groups include singles; the separate multi-frame count requires
at least two consecutive above-threshold samples. The threshold is each
tail's median +2 ms, so absolute P95/P99 and mean remain necessary.

| Save | CPU P95 delta, ms | CPU P99 delta, ms | CPU singles delta, /s | CPU multi-frame groups delta, /s | GPU P95 delta, ms | GPU P99 delta, ms |
| ---- | ----------------: | ----------------: | --------------------: | -------------------------------: | ----------------: | ----------------: |
| 08   |            -0.005 |            -1.605 |                  -0.1 |                             -0.3 |            +0.100 |             0.000 |
| 09   |            +0.100 |            +0.050 |                  +0.7 |                             +0.1 |            +0.055 |            +0.100 |
| 10   |            -0.100 |            +0.200 |                  +0.1 |                              0.0 |             0.000 |            -0.100 |
| 11   |            +0.600 |            +0.303 |                 -15.4 |                             +4.0 |            -0.400 |            -2.041 |
| 12   |            -0.035 |            +0.100 |                  -7.8 |                             +4.6 |             0.000 |            +0.222 |
| 13   |            -4.600 |            -2.627 |                  +0.4 |                             -0.4 |            -1.685 |            -0.997 |

For Save 11, CPU multi-frame groups rise from median 2.8/s to 6.8/s,
while the above-threshold sample fraction falls from 28.28% to 26.05%.
Save 12 rises from 2.4/s to 7.0/s, with nearly unchanged sample fraction
(26.51% to 26.92%). This is a change in clustering, not uniformly more
high-time samples. The full table includes GPU spike groups and every
individual repeat.

Head repeat 1, Save 08 has CPU mean 10.744 ms, versus 5.054 and 4.918 ms
in the other head repeats. It remains in every table. Its WPR execution
increase is spread across native Skyrim, CSX, D3D11 and other modules;
the material union rises only from about 0.109 ms in head repeat 2 to
0.154 ms in repeat 1. This does not explain the whole outlier.

## Health, settings and evidence limits

All 36 final lifecycle verdicts are successful: no active stretch,
incomplete stereo or owner/retirement debt at stop. Recorded samples
from +20 seconds onward show no active stretch or increase in episode
count. All 108 late mode checks match the requested native DLAA or
DLSS/render-scale profile. This is not a canonical qualification pass:
raw strict assay gate failures remain preserved.

The head's late counter intervals show no boundary or input-freshness
rejections, no vendor retries and no prepared/output fallback reuse.
DLSS performs the expected two eye attempts per producer frame. These
approximately +49-to-stop counter intervals are labeled separately from
the exact +50-to-60 timing windows. Baseline freshness counters are absent,
and inactive cpuPerformance counters are not evidence of zero work.

All 813 shared settings match in every available snapshot: SSGI enabled,
GI off, AO/IL interiors-only, Balanced culling and active FOV+TAA centre
0.30 / outer 0.70. The saved FOV-only centre 0.60 is inactive. Fourteen
additional head fields remain documented, not silently equated with absent
baseline fields. WPR contains positive post-enabled-guard SSGI execution
in six head and four baseline interior windows; missing samples do not
prove the effect was disabled in the other windows.

Baseline repeats 1 and 3 lack successful after-run settings snapshots.
Their holds, before settings, late profiles, physical DLL provenance and
traces are available; those do not replace the missing snapshots. HMD
pose equality is user-confirmed rather than independently measured.
All head runs precede all baseline runs, so order/time effects remain
unisolated. Vendor packages also differ: Streamline 2.14.1 / DLSS 310.9.1
in the head, versus 2.12.0 / 310.7 in baseline. This is not a demonstrated
vendor regression. Loaded Skyrim, D3D11 and NVIDIA driver image metadata
match across all six runs; driver version is 32.0.16.1656.

These are the attributed DevBench builds, not a production-off benchmark.
Baseline renderer base is 190c28a39a52c2bace475d1143a124c5893ac741,
compiled diagnostic source a1a11fe0d722fd6dbc18315a023701e0ec4a84de.
Head is compiled source 6588831aabf472dff76340ed58aaf07075fccacd with
the recorded dirty digest. It is not relabeled as the analysis repository
HEAD. Full Build IDs and DLL/PDB hashes are in the provenance table.

## Validation and preservation

-   Six ETLs were archived with matching byte length and SHA-256 before analysis.
-   All six trace-statistics checks report zero lost events and buffers.
-   All 36 marker-based windows reconstruct the saved fpsVR means.
-   All six preserved PDB hashes match the attributed physical build receipts.
-   Saved runner/reporter/comparison hashes and the WPR profile match across runs.
-   Scheduler accounting passes 33/36 windows under the unchanged 5 ms tolerance.
-   Head R1 Save 12 is +7.532 ms, Head R2 Save 12 +11.3829 ms, and Head R3
    Save 13 +7.1802 ms over a 10,000 ms window. They remain provisional;
    no acceptance exceptions or renormalization were introduced.
-   Missing render stacks account for about 0.23–0.79% of sampled render
    weight. No CSX unresolved-PDB marker weight was found, but many native,
    driver and kernel frames remain unnamed. Zero named matches are not
    proof of zero execution or zero locking.

The original six culling runs' twelve source/archive ETLs were deleted at
the user's request, freeing 248.98 GiB. Their analyses, WPA exports,
receipts and fpsVR recordings remain; see the
[retention record](../depth-culling-comparison-20260916/raw-trace-retention.md).
All six current traces remain preserved. Exact archive paths are in
[trace-archives.json](trace-archives.json).

Analysis commands and script identities are recorded in the
[reproduction record](reproduction.md). Reports are uncommitted. Rendering
code and the existing user changes were untouched.

Publication note: machine-specific paths use portable root labels. Historical
scheduler flags retain their original policy; see the additive
[10 ms reassessment and complete ledger record](../gameft-publication-20260918/README.md).
