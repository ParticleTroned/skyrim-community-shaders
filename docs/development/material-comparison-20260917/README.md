# Material candidate: repeated CPU/GPU assessment

The material change does **not demonstrate a consistent time saving**.
Current CPU latency remains above the original traced baseline in all six
saves, while GPU time is lower for DLAA and higher for DLSS. Those CPU
latency differences are not all additional CPU execution: frame cadence
and blocked waiting changed substantially.

-   [Complete timing, spikes, settling and health tables](tables.md)
-   [WPR execution, ready-delay, wait and material tables](wpr-comparison.md)
-   [All 18 retained runs: WPR values](wpr-metrics.csv)
-   [Material sample counts and weighted costs](material-costs.csv)
-   [Settings audit](settings-audit.json)

## Scope and exact identity

Three complete repeats and one interrupted repeat contribute **23 valid
ten-second windows**. Each uses the saved `gameft-sw` world-entry +[50,60)
window, with WPR active. Save order was 08, 11, 09, 12, 10, 13. DLAA/native
was selected for 08/09/10 and DLSS/render scale for 11/12/13.

| Label       | Run                                      |
| ----------- | ---------------------------------------- |
| R1          | `gameft-sw-20260917T052919757Z-6289a268` |
| R2          | `gameft-sw-20260917T054435005Z-137bb086` |
| Interrupted | `gameft-sw-20260917T055944246Z-94445cde` |
| R3          | `gameft-sw-20260917T061652231Z-7703f352` |

Candidate compiled source is
`6588831aabf472dff76340ed58aaf07075fccacd`, not the later repository HEAD.
The three completed provenance receipts agree on:

-   Build ID: `b78946fd101bdb509c418e23b7e3cd1b2d8a27e2fdaf8c270be05a56c472c733`
-   DLL SHA-256: `C758BEEA6C304DEDB91D23F8F26868B23D380C8597EB2C016C8A593FD05F5CB6`
-   PDB SHA-256: `C251760FF11EE102740E66C745BA845DF3AC0085C11F476201B42224CAAB2892`
-   Dirty source digest: `0a05e13e172fc466fc282a19879bea1f895a641f493829275aa9555db21848eb`

The dirty build state is retained explicitly; it is not represented as a
clean commit build. The interrupted run has matching recorded producer
identity and a hash-matched candidate PDB, but lacks completed post-run
physical provenance. Its Save 13 has no valid final window or final health
receipt. Raw reporter zero placeholders are preserved as evidence and
excluded from every statistic.

The original traced reference is compiled source
`a1a11fe0d722fd6dbc18315a023701e0ec4a84de`, renderer base
`190c28a39a52c2bace475d1143a124c5893ac741`. The later main-VR reference is
`503fbfc92b5a9a58f5ba14a2343c3b0b119c40dc`, using Balanced runs 1, 3 and 4.
Balanced 2 remains in historical evidence but is excluded from this
reference aggregate because of its uncertain in-run interaction.

## Important comparison limitation

All current repeats and the original traced baseline have FOV+TAA enabled
with `periphery_taa_center_area=0.30`. The later main-VR Balanced references
have it enabled with **0.60**. Both use outer scale 0.70. With FOV+TAA
enabled, `GetFoveatedMaskProfileParams` selects this periphery centre, so
this is an active rendering setting, not an irrelevant saved preference.
Other recorded non-Upscaling feature settings match the later reference.

Consequently, the later comparison is **not an isolated material A/B**.
The original baseline also differs in source and vendor libraries:
Streamline 2.12.0 / DLSS 310.7 versus 2.14.1 / 310.9.1 in the newer builds.
That remains a confound, not a demonstrated cause. No settings were changed
during this analysis. Menu quality values are not substituted for the
late, per-save profile checks.

## Main results

Values below are per-save medians of the three complete current repeats.
Parentheses show deltas from the original traced baseline. Milliseconds;
positive means slower. The interrupted repeat is shown separately in the
full tables, not included in these medians. Scenes are not pooled into a
mean/SE. CPU 0.35 ms and GPU 0.15 ms remain descriptive screening allowances,
not confidence intervals or substitutes for the measured repeat ranges.

| Mode / save | Original CPU | Current CPU (delta) | Original GPU | Current GPU (delta) | CPU delta vs later main-VR | GPU delta vs later main-VR |
| ----------- | -----------: | ------------------: | -----------: | ------------------: | -------------------------: | -------------------------: |
| DLAA 08     |        4.664 |      8.071 (+3.407) |       10.410 |      9.289 (-1.121) |                     +2.314 |                     -0.525 |
| DLAA 09     |        3.602 |      6.297 (+2.695) |        9.537 |      7.743 (-1.794) |                     +0.297 |                     -0.222 |
| DLAA 10     |        2.937 |      7.307 (+4.370) |       10.036 |      8.736 (-1.300) |                     +0.874 |                     +0.242 |
| DLSS 11     |        7.784 |      8.641 (+0.857) |        7.835 |      8.227 (+0.392) |                     -0.137 |                     +0.032 |
| DLSS 12     |        6.854 |      8.587 (+1.733) |        7.576 |      8.724 (+1.148) |                     -0.239 |                     -0.144 |
| DLSS 13     |        8.675 |     18.355 (+9.680) |       10.259 |     13.108 (+2.849) |                     -1.338 |                     -0.020 |

Save 13's current CPU means range from 17.645 to 19.661 ms. It is a useful
example of the broader DLSS residual, not the sole optimization target.
The interrupted run's Save 08/11 means, 10.572/9.198 ms, further demonstrate
variation; including its five available saves does not create a material
speedup claim.

## Did the material change save time?

The same expanded stack classifier was applied to 107 windows across all
18 retained runs. It includes the old guard names and the new outlined
rejection helper, counting a sample once in the union. PBR predicates and
exclusive named material leaves remain separate. Inclusive sampled context
is not a stopwatch around the predicate or a count of draw calls.

| Save | Later main-VR material median | Current material median [min, max] | Delta (ms/recorded frame) |
| ---- | ----------------------------: | ---------------------------------: | ------------------------: |
| 08   |                        0.1184 |            0.1182 [0.0933, 0.1269] |                   -0.0002 |
| 09   |                        0.0811 |            0.0865 [0.0763, 0.0991] |                   +0.0054 |
| 10   |                        0.1195 |            0.1072 [0.0939, 0.1273] |                   -0.0122 |
| 11   |                        0.0445 |            0.0550 [0.0549, 0.0552] |                   +0.0105 |
| 12   |                        0.0627 |            0.0545 [0.0530, 0.0555] |                   -0.0082 |
| 13   |                        0.1147 |            0.1490 [0.1480, 0.1562] |                   +0.0342 |

The current guard still accounts for roughly 0.055–0.149 ms per recorded
frame in these medians. No current tail sample matches the outlined warning
helper. That is consistent with moving cold warning code away from valid
admission; it does not mean that the protected material reads were removed.
The change retains those safety checks. We cannot claim a 0.15 ms saving,
and the present evidence does not justify removing protections or caching
admission by a potentially reused material pointer.

## What WPR explains

All figures below are median differences from the original traced
baseline, in milliseconds per recorded frame. These use actual saved
window frame counts, not an assumed headset rate. Sampled native work and
scheduler running time are separate estimates and are not added together.

| Save | Render-thread running delta | Blocked wait delta | Native Skyrim leaf delta | Interpretation                                                  |
| ---- | --------------------------: | -----------------: | -----------------------: | --------------------------------------------------------------- |
| 08   |                      +0.179 |             -2.448 |                   -0.081 | CPU latency growth is much larger than execution growth         |
| 09   |                      -0.952 |             -5.997 |                   -0.671 | Higher fpsVR CPU mean despite less execution per recorded frame |
| 10   |                      +0.362 |             -6.045 |                   +0.059 | Large cadence change; smaller actual execution increase         |
| 11   |                      -0.193 |             +0.096 |                   +0.187 | No general render-thread execution increase across repeats      |
| 12   |                    +0.922\* |           -0.315\* |                   +1.434 | Additional native rendering work is a concrete target           |
| 13   |                      +2.843 |             -1.618 |                   +1.664 | Repeated native work increase plus changed cadence              |

_R1 Save 12 exceeds scheduler coverage tolerance. Excluding it gives a
two-valid-repeat running median of 9.698 ms/frame, still about +0.989 versus
the original baseline. No scheduler result is renormalized._

Against the later main-VR reference, Save 08 has **-0.904 ms/frame running**
and **-1.303 waiting**, despite +2.314 ms fpsVR CPU latency. Save 10 instead
has **+0.604 running**, including +0.342 native and +0.094 CSX leaf work.
Save 13's running delta against later main-VR is only +0.053 ms/frame.
The residual therefore predates the material candidate; this run does not
show a new uniform material-induced CPU regression.

The live-snapshot-identified six-plane sphere branch at RVA `0xDA54B0`
accounts for current medians of 0.220 ms/frame in Save 12 and 0.582 in Save
13; the compound-frustum range at `0xDA33C0` adds 0.047 and 0.142. These are
disjoint sampled address ranges. Their identities come from the previously
preserved **live** Ghidra snapshot, not the packed static executable. They
explain part of the native difference. They do not establish that the
Balanced depth-culling implementation alone caused it, nor do CPU samples
measure the amount of GPU work admitted by visibility decisions.

The permanent D3D11-lock classifier has no matching current tail samples.
This does not re-establish PR93's removed per-call overhead as the culprit.
The partial PR93 reversal remains the earlier demonstrated major fix; the
native stereo-hook correction was a correctness fix without demonstrated
Save 13 performance benefit.

## Noise and remaining work, in priority order

1. **Match the active FOV+TAA settings before another material A/B.** Keep
   source, vendor bundle, scene, tracing, logging and culling fixed; change
   only the material candidate. Do not silently edit this campaign or the
   saved protocol. Current results remain descriptive whole-build evidence.
2. **Separate frame pacing from actual execution.** Save 08/10 have many
   isolated CPU spikes: medians 17.6/24.6 per second. Save 10's spike duty
   versus later main-VR is slightly lower (-0.97 percentage points), despite
   more isolated spikes. Frequency alone therefore does not measure total
   bad-frame exposure. Preserve P95/P99, duty and group lengths together.
3. **Investigate native visibility/rendering work, especially DLSS 12/13.**
   The live frustum ranges and broader native rendering are larger targets
   than the measured material delta. Use matched diagnostics for traversals,
   admitted draws and per-eye work before proposing an optimization. Keep
   ownership, malformed-material protections and native renderer locking.
4. **Account for variable worker load.** Across current Save 08 repeats,
   kernel leaf samples beneath HDT-SMP range 0.693–9.270 ms/recorded frame;
   CBP ranges 0.995–7.410. These sum work across multiple threads, not render
   latency. The noise also persists in low-worker-cost windows, so those
   contexts cannot alone explain it. They are evidence of a variable load
   to control, not proof that a mod caused each fpsVR spike.
5. **Measure the DLSS GPU residual by pass.** DLSS GPU mean deltas versus
   the original are +0.392/+1.148/+2.849 ms for 11/12/13. Save 11 GPU P99 is
   also +2.082 ms versus later main-VR. This WPR profile contains CPU stacks
   and scheduling, not GPU pass durations. It cannot distinguish inference,
   input/guide preparation, compositing, or additional admitted geometry.
   A separate matched DevBench GPU capture is needed; do not interpret two
   eye evaluations as duplicate inference or overwrite prepared input.

No single new cause is proven. The next useful measurements are a settings-
matched material comparison and per-pass GPU/native-work attribution, not
speculative timeout, ownership or guard removal.

## Health, evidence and validation

All 23 retained terminal health receipts report successful final state,
inactive stretch, complete stereo and no owner/retirement debt. All **69**
late profile checks (+49/+59/stop) match the intended DLAA/DLSS selection.
Earlier transition/stretch events and raw strict gates remain in the full
tables and ledger; terminal success is not full-history qualification.
Interrupted Save 13 is neither a health pass nor a zero-time measurement.

All four traces report zero lost events/buffers. Scheduler coverage passes
22/23 windows. R1 Save 12 totals 10005.914100 ms for a ten-second window,
exceeding the unchanged +/-5 ms rule; it remains `REVIEW_REQUIRED`.
Sampling/frametime evidence is separately usable. Sampled stack weights are
estimates; zero matches do not prove zero execution.

Immutable ledgers [0006](../vr-render-scale-ledger-0006-investigation.csv)
and [0007](../vr-render-scale-ledger-0007-investigation.csv) partition this
campaign below 100 MiB per file. They preserve selected Balanced reference
cells exactly, the original reference receipts, all available current
receipts including the interrupted segment, numeric results, settings,
comparisons, provenance, WPR analysis and explicit gaps. All earlier
snapshots remain unchanged. [Coverage](coverage.json) binds every source.

Validation commands and results are recorded in [validation](validation.json).
The portable audit is:

```powershell
python docs/development/material-comparison-20260917/verify.py
```

This publication changes analysis/documentation only. Rendering code,
settings, shader caches and the saved measurement protocol were untouched.
It is not a new physical-HMD visual qualification or proof of CTD resolution.
