# Depth-culling comparison and investigation handover

**Keep depth culling enabled; continue with Balanced.** The eight-run
campaign supports a net benefit from native depth culling in the tested
scenes. Neither Legacy nor Performance demonstrated a repeatable overall
advantage over Balanced. This is a practical recommendation, not a claim
that Balanced is always fastest or that visual qualification passed.

The objective is lower main-VR CPU cost and noise, and lower GPU cost with
DLSS enabled. Save 13 is a diagnostic case, not the sole optimization
target. RC166 remains an illustrative counterexample supplied by the user;
it is not substituted for the established historical baseline.

## Reading order and portable evidence

1. [Complete eight-run comparison](tables.md): CPU/GPU means, P95/P99,
   maxima, isolated/grouped spikes, settling and load health, side by side.
2. [Seven enabled-mode runs](mode-analysis.md): repeated-mode variation,
   recovery counters, native frustum work, worker activity, live Ghidra
   interpretation and the earlier matched cross-build findings.
3. [Completed on/off control](depth-off.md): all six saves, render-thread
   execution/ready/wait states, draw contexts and frame-pacing changes.
4. [WPR metrics](wpr-metrics.csv) and [frame cadence](tail-cadence.csv):
   48 matched final-ten-second windows each.
5. [Immutable ledger 0004](../vr-render-scale-ledger-0004-investigation.csv):
   complete saved summaries, parsed final producer records, provenance,
   settings, full derived WPR analysis and stack-context results. This is
   the machine-readable record for further analysis on another machine.
6. [Coverage receipt](coverage.json): source file hashes, selectors,
   reconstruction checks and unchanged historical-ledger hashes.

Raw ETLs, raw fpsVR samples, raw WPA exports, DLLs and PDBs remain local.
Their verified archive identities and locators are retained. The committed
derived results are sufficient to inspect the reported findings; exporting
new stacks or recalculating from raw samples requires those raw archives.
Local paths inside the evidence are historical locators, not portable
dependencies or proof that the files exist on another machine.

## Exact tested identity

All eight runs used the same physical AIO and matched runtime producer,
DLL, manifest, AIO receipt and PDB. This campaign did measure a 503fbfc
build, unlike the earlier source-equivalent 933a4e2 measurement.

| Field                              | Value                                                                             |
| ---------------------------------- | --------------------------------------------------------------------------------- |
| Compiled source                    | `503fbfc92b5a9a58f5ba14a2343c3b0b119c40dc`                                        |
| Build ID                           | `caf9feac1bce8608281b02ddeadbc5b507c68fb739a04044d5f57a6c18b905dd`                |
| DLL SHA-256                        | `C8D41C226BF900B2032C8AE505DAB512476F4B37CCD44E5C22E2BC588695D8D0`                |
| PDB SHA-256                        | `E82DA0EC720EC6B55BD0C10877B12EECD69FAC6A36F1C03004BFE8FEA0A441F8`                |
| Build                              | Universal Release; DevBench ON; Tracy OFF                                         |
| Protocol                           | Unchanged `gameft-sw`, WPR enabled, 60 seconds per save                           |
| Save execution order               | `08, 11, 09, 12, 10, 13`                                                          |
| Per-save mode                      | 08/09/10 DLAA; 11/12/13 DLSS quality 1 with render scale                          |
| Logging                            | Info, user-confirmed; effective logger level is not exported                      |
| Analysis branch before publication | `perf/cpu-dlss-regression-20260916` at `9ad20d60a80cd09eef0f51cf1735ebcfefb5efec` |

The tested source predates the branch's diagnostic-gating cleanup. All
eight runs therefore share the same diagnostic implementation. Settings
snapshots before and after each run retain their full effective feature
state; intended culling flags differ. They do not prove identical NPC,
physics, HMD pose or transient state throughout each hold.

| Label        | Run ID                                   | Interpretation                                                   |
| ------------ | ---------------------------------------- | ---------------------------------------------------------------- |
| Balanced 1   | `gameft-sw-20260916T152832143Z-3828ffef` | Within-campaign table reference                                  |
| Performance  | `gameft-sw-20260916T154518977Z-a6e09be6` | No consumer recovery; producer-pose capture retained             |
| Legacy 1     | `gameft-sw-20260916T155929972Z-2a6a7693` | Native depth culling, no temporal recovery                       |
| Balanced 2\* | `gameft-sw-20260916T161424154Z-401a5499` | Save 13 interaction uncertain; retained, not clean corroboration |
| Balanced 3   | `gameft-sw-20260916T163602908Z-a26f2ef1` | Clean repeat                                                     |
| Balanced 4   | `gameft-sw-20260916T165146902Z-36b2c165` | Clean repeat                                                     |
| Legacy 2     | `gameft-sw-20260916T172315555Z-43f348bd` | Enabled reference for the subsequent off control                 |
| Depth off    | `gameft-sw-20260916T190211584Z-812d171f` | Legacy selector retained; native culling master disabled         |

Balanced 1 is not the historical baseline. That remains traced run
`gameft-sw-20260915T183826985Z`, compiled source
`a1a11fe0d722fd6dbc18315a023701e0ec4a84de`, renderer base
`190c28a39a52c2bace475d1143a124c5893ac741`. Earlier corrected run
`gameft-sw-20260915T234651019Z-dd4d7c3e` compiled
`933a4e2540f962ddc3d2ce656523d1f8c5982ef4`; its Git tree equals 503fbfc,
but its DLL identity remains distinct. See the
[earlier cross-build handover](../cpu-dlss-regression-handover-20260916.md).

## What the final control establishes

Off minus Legacy 2, using saved final-ten-second means. Positive means
slower. FPS is the median recorded FPS, not reciprocal mean CPU/GPU time.

| Save | Mode | CPU delta ms | GPU delta ms | Enabled / off FPS |
| ---- | ---- | -----------: | -----------: | ----------------: |
| 08   | DLAA |      +10.867 |       +1.859 |         60 / 58.5 |
| 09   | DLAA |       +0.620 |       +0.380 |          109 / 98 |
| 10   | DLAA |       -0.514 |       +1.470 |          104 / 60 |
| 11   | DLSS |       +1.642 |       +1.588 |        111 / 68.6 |
| 12   | DLSS |       +0.749 |       +1.637 |           91 / 60 |
| 13   | DLSS |       +4.884 |       +4.241 |         57 / 46.8 |

Rendering-thread execution per recorded frame rises in all six saves.
Sampled CPU in draw submission, depth rendering, technique selection and
geometry setup also rises across all six. This is consistent with more
downstream rendering work when occlusion rejection is removed. It does
not measure object/draw counts or isolate individual GPU passes.

Save 10's CPU mean, P99 and spike frequency improve while GPU time and
throughput worsen. Its cadence moves from mostly 8.33 ms intervals to
16.67 ms. A smoother CPU trace is therefore not sufficient evidence of
less rendering work. The CSV lacks reprojection state; do not assign an
exact compositor mode or claim this explains every spike. Save 08's large
CPU-frametime increase is not fully decomposed by WPR running time.

## Costs, interactions and remaining targets

| Component                      | Finding                                                                                                              | Consequence                                                                                                                                                                |
| ------------------------------ | -------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Native depth/occlusion culling | Off increased GPU time in all six saves against Legacy 2.                                                            | Preserve the observed benefit; no tested scene establishes an overall win from disabling it. One off run does not establish repeat variance.                               |
| Balanced recovery scan         | Clean runs total 12.06–14.73 ms in the timed scope; 0.114–0.119 ms per attempt.                                      | Direct recovery cannot explain sustained several-ms/frame differences. These totals span the complete run, not just measured tails.                                        |
| Recovered-object rendering     | Clean recovery attempts reach the 64-object cap; individual draw costs are not bounded by that count.                | Possible CPU/GPU amplification during motion/disocclusion, but downstream costs have not been attributed. Do not loosen bounds or remove recovery on this evidence.        |
| General native frustum work    | Save 13 retains 4.106 summed CPU ms/frame with depth culling off versus 4.529 on; Save 12 rises from 0.823 to 1.845. | These routines are not synonymous with GPU depth occlusion. Investigate native callers and thread distribution separately.                                                 |
| CPU noise and worker activity  | Noise persists without recovery; physics/kernel worker activity differs substantially between repeats.               | No single culling explanation. Sampled work, blocked waits and fpsVR CPU latency are different quantities.                                                                 |
| DLSS GPU residual              | Earlier matched cross-build DLSS GPU deltas remain positive while DLAA GPU deltas are negative.                      | Attribute preparation, copies, guides, vendor evaluation, composition and queue gaps with GPU measurements. CPU evaluation stacks cannot identify the expensive GPU stage. |

The common supported interaction is visibility admission into expensive
draw/state/material work, followed by frame-budget/pacing effects. A
specific defective Balanced-to-DLSS interaction is not proven. Two DLSS
evaluations per VR frame are expected, one per eye. Existing freshness
counters do not show sustained rejection/retry inflation; they do not
prove preparation or inference is cheap.

Legacy 2 versus preceding Balanced 4 improves Save 13 by 1.519 ms CPU and
0.699 ms GPU, and Save 10 by 0.439 ms GPU. This is a useful signal, but
Balanced 1 was also fast and Legacy itself varied greatly. The enabled-mode
data show no repeatable overall winner. Performance mode supplies no
demonstrated advantage that warrants selecting it. Balanced remains the
practical default because it retains motion recovery, not because a
statistical speed superiority has been established.

Prioritize repeated per-draw state/material work, native visibility caller
and thread distribution, and DLSS GPU pass attribution. Any additional
instrumentation must be DevBench-only and separately agreed; these
findings do not change the saved benchmark protocol. Preserve malformed
material protections, light ownership, native renderer critical-section
ownership and readable colour-managed prepared input. Do not restore
permanent D3D11 multithread protection. The major earlier performance fix
was the partial PR93 reversal; the stereo-hook fix remains a correctness
fix without a demonstrated Save 13 performance gain.

## Evidence interpretation and validation

All 48 final lifecycle summaries are successful: correct selected mode,
inactive stretch, complete stereo, backend ready and no outstanding
owner/retirement debt. This does not erase earlier events or strict gate
results. Full producer final records and lifecycle samples are in the
ledger. Balanced 2 Save 13 includes the uncertain late stretch/recovery.
This campaign is not canonical physical-HMD visual qualification.

Means, P95/P99 and spike metrics use saved `[50,60)` windows from observed
world entry. Settling scans the saved full hold. WPR uses the same windows
mapped to ETL time, with timing uncertainty retained. No protocol formula
or raw measurement was changed for publication. The earlier positional
save-label presentation error was corrected from actual receipt names;
all 48 names agree with their analysis windows.

WPR sampled CPU is not fpsVR CPU frametime. Summed all-thread CPU may exceed
elapsed frame time. Inclusive contexts overlap and must not be summed.
Recovered-object costs are not included in the timed recovery scope.
Zero samples are not proof of no execution. The auxiliary spike endpoint
scopes do not cover complete single-frame intervals. Public/private symbol
gaps and sampling/inlining limits remain. All eight traces report zero
lost events/buffers; small scheduler accounting discrepancies are retained
and are not evidence of material lock cost.

Native range interpretation comes only from the preserved live-read
Ghidra project, captured with the partial-reversal build. Matching native
image version/timestamp/size was checked; identical in-memory patch bytes
in every later process were not proven. The functions and ranges are
retained in the full WPR analysis and earlier investigation, not inferred
from a static executable.

Ledger 0004 contains eight run columns and 168 metric rows. Complete saved
summaries and analysis objects were decoded and compared field for field
with their source JSON. Numeric cells and actual save order were checked;
null, false, zero and empty values remain distinct. No PR exists for this
investigation branch, so the immutable snapshot uses `investigation`
rather than an invented PR number. Older numbered ledgers remain intact.

Only documentation and derived evidence are published. No runtime source,
settings, shaders, defaults or benchmark scripts changed. No source build
or CTest is claimed for this publication.

Run the portable consistency check from the repository root:

```powershell
python docs/development/depth-culling-comparison-20260916/verify.py
```

It checks the complete ledger hash, parses every detail cell, reconstructs
all eight summaries from retained WPR analysis, checks save identities,
scalar timings, CSV frame counts/means and historical ledger hashes. The
publication coverage receipt additionally records field-for-field equality
to the local source objects. Raw ETL re-export is a separate operation.
