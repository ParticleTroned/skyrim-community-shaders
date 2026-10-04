# Hi-Z original-vertex proof evaluation, 4 October 2026

The original-vertex bias build remains substantially slower than Advanced
in this exterior view. It has not demonstrated a performance improvement.
Advanced remains the default; no further runtime change was made during
this evaluation.

## Final comparison

Four 20-second fpsVR windows ran Advanced, Hybrid, Hybrid, Advanced. Each
phase reset the game to noon and settled for at least five seconds.
Telemetry, traversal diagnostics and profiling were disabled during these
windows. WhiterunExterior01, clear weather, player position and DLSS
Quality K remained fixed, with 1344x1492 render pixels per eye and
2016x2240 display pixels per eye. Render scale remained enabled.

| Mode        | CPU repeat 1 / 2, ms | GPU repeat 1 / 2, ms | Mean CPU / GPU, ms |
| ----------- | -------------------: | -------------------: | -----------------: |
| Advanced    |    10.4315 / 11.7557 |      8.2247 / 8.9305 |   11.0936 / 8.5776 |
| Hybrid Hi-Z |    17.3119 / 17.4951 |    10.1108 / 10.3189 |  17.4035 / 10.2149 |

Equal-weight window means put Hybrid **6.3099 ms CPU (56.9%)** and
**1.6373 ms GPU (19.1%)** above Advanced. Both Hybrid windows exceed both
Advanced windows. Advanced's repeat spread is material; these four windows
are descriptive measurements, not precise confidence intervals.

All selected receipts have complete sample coverage, verified preserved
raw CSV bytes, the same game process identity and no active compiler
workers. Maximum sampled boundary pose difference was 0.897 mm / 0.1144
degrees. Advanced repeat 1 and Hybrid repeat 2 each loaded one additional
cached shader between boundaries; source compilation remained unchanged,
with no failures. Boundary samples do not prove continuous pose or focus
stability. Actual headset refresh rate was unavailable in the receipts,
so no specific refresh-budget or reprojection threshold is asserted.

## Culling cost and rejection

Separate 300-frame GPU captures ran twice per mode, with traversal
diagnostics disabled. Values below are disjoint self-time scopes, not
whole-frame timing or inclusive parent sums.

| GPU scope                                | Repeat 1, ms | Repeat 2, ms |
| ---------------------------------------- | -----------: | -----------: |
| Advanced native downscale + producer     |      0.07678 |      0.07598 |
| Hybrid all culling scopes                |      0.94233 |      0.89620 |
| Hybrid bounds test                       |      0.86543 |      0.82782 |
| Hybrid hierarchy, base and mip reduction |      0.06627 |      0.05778 |

Bounds testing consumes approximately 92% of Hybrid's measured culling
cost. The mean Hybrid total is 0.91926 ms versus 0.07638 ms for Advanced.
Hierarchy optimization alone cannot remove this gap. Culling scopes do
not explain all of the whole-frame CPU/GPU difference.

Separate frozen counter windows recorded:

| Mode            | Tested candidate records | Rejected records | Rejection |
| --------------- | -----------------------: | ---------------: | --------: |
| Advanced        |                6,107,136 |        3,764,565 |  61.6421% |
| Hybrid repeat 1 |                4,866,048 |        1,722,497 |  35.3983% |
| Hybrid repeat 2 |                4,927,488 |        1,806,041 |  36.6524% |

These are aggregate observations from separate windows, not matched
persistent objects or draw-call counts. Hybrid had no fallback,
invalidated history, unreadable batches or promoted objects. Native
recovery promoted no objects in its counter window.

## What the new diagnostics establish

Accepted-history diagnostic coverage was 99.916% in each Hybrid window,
with one boundary batch absent from diagnostic totals. No diagnostic
failure, discarded, unavailable or not-ready batch was reported. Work
sums both eyes and uses diagnostic candidate objects as denominator.

| Work per candidate                       | Repeat 1 | Repeat 2 |
| ---------------------------------------- | -------: | -------: |
| Depth loads                              |  11.6612 |  11.5602 |
| Face regions                             |   3.7204 |   3.6450 |
| Triangle attempts                        |   6.3518 |   6.2036 |
| Actual polygon clipper entries           |   2.3328 |   2.2704 |
| Successful plane proofs                  |   0.6925 |   0.6908 |
| Face proofs gained by base-only bias     |  0.01655 |  0.01630 |
| Triangle proofs gained by base-only bias |  0.01624 |  0.01447 |

The new bias shortcuts execute only about three times per hundred
candidates. They are work events, not uniquely recovered occluded
objects; triangle shortcuts can also avoid a subsequent cheap disjointness
test. Successful plane proofs are not plane-attempt counts. Approximately
2.3 actual polygon clips per candidate make clipping a concrete remaining
work target, but these counters do not assign milliseconds to the clipper.

| First decisive retention reason, % of candidates | Repeat 1 | Repeat 2 |
| ------------------------------------------------ | -------: | -------: |
| Finest depth unresolved                          |   29.282 |   28.963 |
| Nearest vertex unresolved                        |   12.519 |   11.580 |
| Wholly offscreen                                 |   13.090 |   13.470 |
| Partially outside viewport                       |    6.037 |    5.750 |
| Guard only                                       |    0.029 |    0.046 |
| Clip crossing                                    |    3.530 |    3.413 |
| Read budget                                      |    0.116 |    0.126 |
| Invalid input / stack capacity                   |        0 |        0 |

Wholly offscreen bounds are left to native frustum handling. They can
inflate the aggregate rejection gap without representing additional draws.
The partial-viewport and clip-crossing cohorts are possible candidates
for conservative clipping, not guaranteed recoverable occlusion. Finest
depth and nearest-vertex exits also include genuine visibility, masked
depth, coverage guards and depth uncertainty. Matching native and Hybrid
on the same submitted bounds is still necessary to attribute missed
useful culling. Aggregate reason counts cannot do that subtraction.

Increasing the read budget could resolve at most 0.126 percentage points
of candidates in these diagnostic cohorts. Removing nearest-vertex
retention generally moves its failed proof into more expensive face work.
Neither is supported as the next optimization.

## Apparent regression and next work

The whole-frame measurements are much worse than the preceding campaign.
They do not isolate a regression caused by this commit: the prior
diagnostic camera and current camera differ by about 10.7 degrees in
rendered orientation despite identical player position and projection.
The earlier timing campaign's recorded profiler view differs by about
13.8 degrees and used a different position; its physical pose closely
matches that campaign's timing boundary. Raw cross-session OpenVR angles
are not used because the tracking-origin mapping is unverified.
Unrelated GPU scopes changed by differing amounts; a global correction
factor cannot normalize these sessions.

The changed normal path has no new CPU culling loop when telemetry is
off. Counter deltas around the normal-shader profiler captures measured
Hybrid dispatch at 0.0251-0.0257 ms, validation at 0.0127 ms and native
readback at 0.0032 ms. These telemetry windows are broader than the
300-frame GPU captures; nested CPU scopes must not be summed. Separate
traversal-diagnostic windows measured validation/readback around 0.039 ms,
including diagnostic-buffer decoding. These observations do not support
assigning the roughly 6 ms fpsVR CPU difference to those hooks.
Extra retained rendering or
frame pacing/waits are plausible contributors, but were not separately
measured. No stack/wait trace or hardware occupancy/spill capture was made.

Prioritize a matched-input, DevBench-only comparison of old and new bias
on the same captured bounds, camera and depth. Keep the displayed result
unchanged and duplicate diagnostic work outside performance windows.
This distinguishes recovered objects from extra traversal and can pair
native-only rejections with Hybrid reasons. It is proposed, not built.

Then evaluate one bounded cost change: reducing the clipper's dynamically
indexed polygon storage while preserving conservative depth semantics,
or an equivalent stackless traversal preserving visit order and the read
budget. Cached plane setup is another candidate, with a storage/occupancy
tradeoff. Select one using matched-input work and compiled-shader evidence;
the current counters do not prove register spilling or predict its gain.
Do not weaken masks, stereo requirements, coverage guards or interpolated
depth allowances merely to raise the rejection count. Parity is not a
credible promise from the present evidence. Even halving bounds-test time
with other measured costs unchanged leaves about 0.496 ms of culling,
roughly 6.5 times the native scope. This is scope arithmetic, not a
whole-frame speedup prediction.

## Evidence and completion

Measured source: 95edcef20f0044de2de732ccad47184aa9785abf.
Producer Build ID:
e7b855bdbcc4fcbd0a8843c931d61170cde28099f8ba5b335ba55d8094603938.
The enabled physical DLL, adjacent manifest, archive receipt and six depth
shaders matched that producer, with no competing checked loose provider.

Local evidence is under main-workspace
`build/astra-runtime/20261004081212Z-hiz-proof-bias/`:
`final-analysis.json`, `independent-boundary-audit.json`, complete direct
DevBench responses, profiler histories and fpsVR raw/window receipts.
The failed initial sandbox fpsVR connection produced no samples; the
successful host retry is selected. A reused profiler command identifier
was rejected before execution; its corrected catalog request succeeded.
Both failures are preserved and excluded, not counted as measurements.

The preceding implementation's 12/12 focused tests and production
compiler-output isolation checks remain recorded with that source. They
were not rerun for this documentation-only evaluation. No screenshots
were taken because performance was not comparable. Motion, lifecycle and
SE/AE runtime qualification remain open.

Advanced, telemetry enabled, traversal diagnostics disabled and profiler
disabled were restored and verified. Skyrim PID 19236 was left running.
