# PR73 pending-drain crash and motivating stretch measurements

`cf16167283cf70b74aedab348e740b23f0e6a49d` changes when the entire physical
relatch transaction may resume. Its new polling permission is not confined
to an observation-only GPU readiness check. The failed run exercises that
change on pass 1, transition 26, DLSS HoshiPa to FSR3 HoshiPa.

The evidence establishes earlier transaction admission after shared-resource
retirement and a subsequent process crash. It does not identify the invalid
pointer or prove the exact memory-corruption mechanism. Resource replacement
inside an active rendering pass is the leading ordering hypothesis. A dump
or controlled instrumented comparison is still needed for that final causal
link. The six-frame delay was not itself a proof of safe resource ownership.

## Exact changes and observed execution

The only runtime change after the successful `c73bae9a7` build is this
commit; intervening commits are documentation. The relevant exact-source
locations are in the preserved `source-cf1616728` snapshot under the
[diagnosis directory](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/diagnosis/).

1. `Upscaling.cpp:28191` removes `memoryReliefActiveForRelatch` from the
   conservative `physicalMutationStarted` polling exclusion. Memory relief
   becomes a separate field. `VRVendorRelatchPolicy.h:1372` permits one-frame
   polling with memory relief active, while line 1389 still rejects early
   presentation promotion in that condition.
2. `Upscaling.cpp:28231` and `28331` use that permission to call
   `requeueRelatch(1, false, Backend)`. The second argument discards the
   existing retry delay. `requeueRelatch` at line 25304 republishes the full
   pending render-target recreation request. It does not schedule a distinct
   read-only poll.
3. Shared-resource mutation precedes these checks. The generic memory-relief
   cleanup at line 28077 calls `UnbindUpscalingResources`, destroys foveated
   resources and retires presentation intermediates at lines 31331–31340.
   The pending-before-release signal describes the provider reset; it does
   not mean that all shared presentation resources remain unchanged.
4. The retry re-enters `ApplyPendingPerfModeRenderTargetRecreate` from either
   `ConfigureUpscaling` or `State::Draw`. It can execute cleanup again, finish
   vendor teardown, relatch the boot contract and recreate native engine and
   loaded-feature resources. `Hooks.cpp:1089` reaches native target creation
   and `State::SetupRenderTargetResources`. There is no caller-phase gate
   restricting this new faster path to a frame boundary.
5. `Hooks::BSGraphics_SetDirtyStates::thunk` calls the original native dirty
   state function, terrain handling and then `State::Draw`. At
   `State.cpp:455`, recreation may run after native pass state has been
   prepared; drawing continues afterward. The existing in-progress and queue
   locks prevent competing transactions. They do not establish that no
   active native draw owns the state being replaced.

The six-frame settling guard retained by this commit controls subsequent
vendor evaluation/presentation. It cannot protect resource ownership during
the earlier cleanup and native target recreation. A correction should make
readiness polling observation-only and admit destructive work separately at
a proven safe renderer boundary, with explicit shared-retirement ownership.
Merely retaining the later settling guard does not provide that separation.

## Crash evidence

Run `nvidia-2026-09-11T06-52-05-183Z`, PID 34484, render/crash thread 51288:

| Local time        |                   Frame | Evidence                                                                                                                                                                                                                                 |
| ----------------- | ----------------------: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 08:55:58.862      |                   28994 | API request 27, epoch 28, requests FSR3 HoshiPa.                                                                                                                                                                                         |
| 08:55:59.163–.164 |                   29001 | First relatch enters from ConfigureUpscaling. Memory relief arms after 19 distinct target changes, retires one intermediate set, and does not preserve presentation intermediates. DLSS reset reports WaitingForDrain; relatch requeues. |
| 08:55:59.212–.259 |             29001–29002 | Presentation fallback recreates textures and uses PresentationStretch for the previous DLSS generation.                                                                                                                                  |
| 08:55:59.216      | not logged on this line | Retry encounters the separate pre-allocation intermediate-cleanup gate.                                                                                                                                                                  |
| 08:55:59.404      |                   29006 | Full transaction resumes from State::Draw. Generic memory cleanup runs again; another retired set is logged.                                                                                                                             |
| 08:55:59.405      |                   29006 | DLSS resources clear. FSR boot generation 28 latches. FSR teardown reports zero contexts; this is not evidence of a crash during FSR evaluation.                                                                                         |
| 08:55:59.408–.419 |       29006 transaction | Engine target recreation and feature resource setup log activity. Last renderer entry is SSGI AO-only resource profile.                                                                                                                  |
| 08:55:59          |  crash logger precision | Execute access violation at 0x000001BE71DC0100. No strict-completion receipt for row 26.                                                                                                                                                 |

The first and second admitted transactions are five frames apart, not one.
The one-frame queue delay made retries eligible earlier, but intermediate
retirement still delayed admission. At frame 29006, the prior six-frame
pacing predicate would reject the frame: `29006 - 29001 < 6`. The first
ordinary eligible frame under that predicate would be 29007. This is a
code-based counterfactual for the same recorded state, not a revert replay.

The first scanned CommunityShaders return address is DLL+0x753327, in
`Hooks::BSGraphics_SetDirtyStates::thunk`. Disassembly of the exact deployed
DLL places it immediately after the call to the original native function.
The rest of the scan contains utility-shader geometry and shadow rendering.
The scan points to a native rendering path, but this is principally a
stack scan, not a reliable unwound call chain. It cannot establish whether
the invalid target came from a stale object, overwritten stack, another hook
or a different source. Neither the named mesh nor the last SSGI log line
identifies the faulty component.

The final pre-relatch sample reports Normal pressure, 4184 MiB local video
usage against an 11176 MiB budget. No OOM or device-loss event was recorded
before the crash. This does not establish every later allocation succeeded.
No matching minidump was available. The worker's fetch failure follows the
game crash; it did not cause it. Rows 27–33 and pass 2 were not executed.

The previous successful build completed both passes, including rows 26 and 28. Source and timing correlation make this commit a strong regression
candidate. An identified corrupt pointer and a controlled revert comparison
remain unavailable; the ordering hypothesis must not be reported as a
proven use-after-free.

## Which PR73 stretches motivated the change

This compares the last successful pre-polling PR73 build, `c73bae9a7`, with
the pinned PR66 build, `a09e1cc77`. It does not compare the interrupted
`cf1616728` run to PR66 or confuse it with the earlier PR73 `d9780bb74` run.

| Identity                 | PR66                                                             | Pre-polling PR73                                                 | Crashing PR73                                                    |
| ------------------------ | ---------------------------------------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------- |
| Compiled renderer source | a09e1cc77de098f85e74e6d5bb341dc184f83640                         | c73bae9a776e67614bba84ee0cecf3d076de259f                         | cf16167283cf70b74aedab348e740b23f0e6a49d                         |
| main-VR base             | bf4ae54a7d49620c41cb32ee9ecfd44657688ead                         | ef7c366dd73989b2b87751c0ef975db7c6fd310f                         | ef7c366dd73989b2b87751c0ef975db7c6fd310f                         |
| Producer Build ID        | 9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757 | d098079db31d3f43d3433ba4023e500774ff1895ee85ea648c726731bc68a31f | a8d2e6a5f759ff30246fe77851f7cf73d01880d431019284482b0f05ee12afc0 |
| Run                      | renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z               | renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z               | nvidia-2026-09-11T06-52-05-183Z                                  |
| Terminal receipts        | 66/66 PASS                                                       | 66/66 PASS                                                       | 25/66 PASS, then crash during row 26                             |

The four substantial regressions gained 9–10 total completed stretch frames
in one pass:

| Row | Route                                | Pass | Frames PR66 → PR73 | Stretch ms PR66 → PR73 | Increase ms |
| --: | ------------------------------------ | ---: | -----------------: | ---------------------: | ----------: |
|  15 | FSR3 HoshiPa → Ultra Quality         |    2 |             3 → 13 |    143.9026 → 647.9434 |    504.0408 |
|  18 | FSR3 Balanced → Performance          |    2 |             3 → 13 |    144.3496 → 648.8011 |    504.4515 |
|  19 | FSR3 Performance → Ultra Performance |    1 |             3 → 13 |    154.8453 → 620.3085 |    465.4632 |
|  26 | DLSS HoshiPa → FSR3 HoshiPa          |    2 |             2 → 11 |     93.4609 → 638.0248 |    544.5639 |

Row 26's 11 frames comprise two completed episodes; this is not one
11-frame consecutive episode. Row 28, NONE to FSR3 Ultra Performance, has
zero completed stretch frames and zero completed stretch milliseconds in
both passes of both builds. A long strict-completion time is a different
measurement from selected presentation stretch.

The retry telemetry links all four outliers to exactly the two call sites
changed by `cf1616728`:

| PR73 pass/row | Retry source in c73bae9a7 Upscaling.cpp | Retry frame → readmission frame | Requeue → admission ms | Later six-frame guard ms |
| ------------- | --------------------------------------: | ------------------------------- | ---------------------: | -----------------------: |
| 2/15          |                        28227, FSR drain | 19389 → 19395                   |               330.6776 |                 303.1802 |
| 2/18          |                        28227, FSR drain | 19792 → 19798                   |               332.6478 |                 311.8622 |
| 1/19          |                        28227, FSR drain | 15145 → 15151                   |               309.0128 |                 295.2829 |
| 2/26          |                     28325, vendor reset | 20837 → 20843                   |               334.6545 |                 339.9724 |

Each records one Backend retry, `settleGuardRequired=true` and
`proofDrivenRelease=false`. The first wait is what the polling commit
attempted to shorten. The later guard remains necessary under its policy.
The full six-frame retry duration is not established avoidable overhead:
these receipts do not identify the exact earlier instant when the drain
became ready. One-frame polling therefore cannot promise a five-frame gain.

Across both passes, longer mean stretch also appears in rows 5 (+8.7363 ms),
8 (+11.54015 ms), 16 (+6.242 ms) and 17 (+12.80155 ms), with unchanged frame
counts. All rows with a positive two-pass mean delta are 5, 8, 15, 16, 17,
18, 19 and 26. If “longer” means any increase in either individual pass,
the complete set is 5, 6, 8, 9, 14, 15, 16, 17, 18, 19, 25, 26 and 29.

Completed stretch totals are 4143.5322 → 4675.3975 ms in pass 1 and
4955.6779 → 5600.7744 ms in pass 2. The mean total per pass increases by
588.4809 ms, approximately 12.935%. This remains a descriptive comparison:
main-VR bases differ and full driver, power, modlist and cache equivalence
is not established. Formal improvement assessment remains INCONCLUSIVE.
Terminal completion and full-history health are separate; the fixed stretch
cutoff is a diagnostic where settling imposes stretch, not a new health gate.

## Validation and retained evidence

The [canonical ledger](vr-render-scale-ledger.md) remains the
durable measurement record. This report is an analysis of existing results;
it does not replace the ledger or change either saved summary. The existing
[complete PR73 comparison](pr73-readiness-nvidia-comparison-20260911.md)
retains the full per-transition strict/relatch timings, health, counters,
resource and profiler evidence, and comparison limitations.

Executed the local `diagnosis/extract-stretch-comparison.py` once. It checked
all 66 paired transition routes and matched all 132 source transition
stretch objects against the existing lossless ledger cells: 396 numeric
episode, frame and duration values. It generated an exact CSV and JSON,
including input hashes and the full retry telemetry for the four outliers:

-   [Exact stretch comparison CSV](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/diagnosis/pr66-vs-pre-poll-pr73-stretch.csv)
-   [Provenance, retry evidence and audit JSON](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/diagnosis/pr66-vs-pre-poll-pr73-stretch.json)
-   [Preserved crash findings and log hashes](../../artifacts/renderscale-tuning/nvidia-2026-09-11T06-52-05-183Z/diagnosis/findings.json)

Static inspection covered the exact commit diff, queue/pacing policy,
cleanup and reset functions, native resource recreation and dirty-state
callers. Disassembled the deployed DLL hook to verify the scanned address.
The installed on-disk game executable did not provide usable decoded
instructions for the native crash-path address; no causal conclusion was
drawn from those bytes. No build, runtime replay, fix or policy test was run
for this follow-up. The commit's added predicate tests do not exercise
renderer entrypoint safety or shared-resource lifetime.

## Complete stretch table

B/C means PR66/pre-polling PR73. Milliseconds are summed across completed
episodes in the individual transition. Positive deltas are longer in PR73.
Values are rounded here to three decimals; the CSV/JSON retains precision.

| Row | Route                                                    | P1 frames B/C |       P1 ms B/C | P1 delta ms | P2 frames B/C |        P2 ms B/C | P2 delta ms | Mean delta ms |
| --: | -------------------------------------------------------- | ------------: | --------------: | ----------: | ------------: | ---------------: | ----------: | ------------: |
|   1 | DLSS HoshiPa -> NONE                                     |           1/1 | 119.890/112.696 |      -7.195 |           0/0 |      0.000/0.000 |      +0.000 |        -3.597 |
|   2 | NONE -> TAA                                              |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|   3 | TAA -> DLSS Native AA (scale off)                        |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|   4 | DLSS Native AA (scale off) -> DLSS HoshiPa               |           2/2 | 199.802/197.994 |      -1.808 |           2/2 |  237.463/228.557 |      -8.906 |        -5.357 |
|   5 | DLSS HoshiPa -> DLSS Ultra Quality                       |           2/2 | 199.320/206.392 |      +7.072 |           2/2 |  232.347/242.748 |     +10.400 |        +8.736 |
|   6 | DLSS Ultra Quality -> DLSS Quality                       |           6/6 | 371.552/380.289 |      +8.738 |           6/6 |  400.832/385.643 |     -15.190 |        -3.226 |
|   7 | DLSS Quality -> DLSS Balanced                            |           6/6 | 376.746/375.718 |      -1.028 |           6/6 |  401.917/376.351 |     -25.566 |       -13.297 |
|   8 | DLSS Balanced -> DLSS Performance                        |           6/6 | 366.685/386.744 |     +20.059 |           6/6 |  401.944/404.965 |      +3.022 |       +11.540 |
|   9 | DLSS Performance -> DLSS Ultra Performance               |           6/6 | 370.248/373.362 |      +3.114 |           6/6 |  390.033/363.200 |     -26.832 |       -11.859 |
|  10 | DLSS Ultra Performance -> DLSS Native AA (scale off)     |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  11 | DLSS Native AA (scale off) -> TAA                        |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  12 | TAA -> NONE                                              |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  13 | NONE -> FSR3 Native AA (scale off)                       |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  14 | FSR3 Native AA (scale off) -> FSR3 HoshiPa               |           6/6 | 268.924/276.049 |      +7.125 |           6/6 |  307.782/275.831 |     -31.951 |       -12.413 |
|  15 | FSR3 HoshiPa -> FSR3 Ultra Quality                       |           3/3 | 142.698/139.395 |      -3.303 |          3/13 |  143.903/647.943 |    +504.041 |      +250.369 |
|  16 | FSR3 Ultra Quality -> FSR3 Quality                       |           3/3 | 160.236/156.803 |      -3.434 |           3/3 |  141.176/157.094 |     +15.918 |        +6.242 |
|  17 | FSR3 Quality -> FSR3 Balanced                            |           3/3 | 140.158/139.603 |      -0.555 |           3/3 |  144.864/171.022 |     +26.158 |       +12.802 |
|  18 | FSR3 Balanced -> FSR3 Performance                        |           3/3 | 148.345/141.290 |      -7.055 |          3/13 |  144.350/648.801 |    +504.452 |      +248.698 |
|  19 | FSR3 Performance -> FSR3 Ultra Performance               |          3/13 | 154.845/620.308 |    +465.463 |           3/3 |  143.964/149.211 |      +5.247 |      +235.355 |
|  20 | FSR3 Ultra Performance -> FSR3 Native AA (scale off)     |           0/0 |     0.000/0.000 |      +0.000 |           5/0 |    334.871/0.000 |    -334.871 |      -167.436 |
|  21 | FSR3 Native AA (scale off) -> TAA                        |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  22 | TAA -> NONE                                              |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  23 | NONE -> DLSS Native AA (scale off)                       |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  24 | DLSS Native AA (scale off) -> FSR3 Native AA (scale off) |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  25 | FSR3 Native AA (scale off) -> DLSS HoshiPa               |           6/6 | 363.912/366.609 |      +2.697 |           6/2 |  395.523/211.046 |    -184.478 |       -90.890 |
|  26 | DLSS HoshiPa -> FSR3 HoshiPa                             |           2/2 |  94.387/114.709 |     +20.322 |          2/11 |   93.461/638.025 |    +544.564 |      +282.443 |
|  27 | FSR3 HoshiPa -> NONE                                     |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  28 | NONE -> FSR3 Ultra Performance                           |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  29 | FSR3 Ultra Performance -> DLSS Ultra Performance         |         11/11 | 665.784/687.436 |     +21.652 |         16/11 | 1041.248/700.338 |    -340.910 |      -159.629 |
|  30 | DLSS Ultra Performance -> TAA                            |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  31 | TAA -> FSR3 Native AA (scale off)                        |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  32 | FSR3 Native AA (scale off) -> NONE                       |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
|  33 | NONE -> DLSS Native AA (scale off)                       |           0/0 |     0.000/0.000 |      +0.000 |           0/0 |      0.000/0.000 |      +0.000 |        +0.000 |
