# Hi-Z proof A/B evaluation, 4 October 2026

This is the earlier proof-only assay. The subsequent save-22 depth/proof
matrix and selected guarded 2x2 implementation are assessed in the
[current analysis](vr-hybrid-culling-guarded2-analysis-2026-10-04.md).
The comparison controls described here are removed from the next build.

The last proof change does not explain the previously observed large
regression. Both variants remain slower than Advanced. The current
original-vertex variant B costs 0.014 ms more culling GPU time than the
guarded baseline A in this assay, with no demonstrated increase in
aggregate rejection. That small observed difference is not an established
whole-frame regression or a reason to expect parity from reverting it.
No runtime implementation changed during this evaluation.

## Whole-frame comparison

Seven 20-second fpsVR windows ran Advanced, A, B, B, A, Advanced,
Advanced. The final Advanced repeat was requested after chat compaction.
Every phase reset to noon and settled for at least five seconds.
Telemetry, traversal diagnostics and profiling were off during timing.
WhiterunExterior01, clear weather, player position, DLSS Quality K and
1344x1492 render pixels per eye remained fixed. Display pixels per eye
were 2016x2240, with render scale enabled throughout.

| Mode                 |            CPU repeats, ms |          GPU repeats, ms |   Summary CPU / GPU, ms |
| -------------------- | -------------------------: | -----------------------: | ----------------------: |
| Advanced             | 9.8897 / 10.2660 / 10.7922 | 7.4750 / 7.7088 / 8.0729 | 10.2660 / 7.7088 median |
| A: guarded baseline  |          12.4080 / 17.4413 |          9.3491 / 9.9816 |   14.9247 / 9.6654 mean |
| B: original vertices |          13.8645 / 13.7883 |          9.4242 / 9.4438 |   13.8264 / 9.4340 mean |

There is no isolated Advanced outlier: its three observations rise
gradually. All are retained; the middle observation is the representative
baseline requested by the user. Their equal-weight mean is
10.3160 / 7.7522 ms. B is 3.5604 ms CPU (34.7%) and 1.7252 ms GPU (22.4%)
above the representative baseline. Every Hi-Z window exceeds every
Advanced window. A's CPU spread prevents a reliable whole-frame A/B gain
claim; its slower repeat demonstrates that the earlier CPU regression
does not require the new proof code.

The windows retained 1,164-1,655 samples each, with complete coverage,
unchanged process identity, no active compiler workers or source shader
compilation, and no shader failures. A's second repeat loaded one cached
shader. Maximum sampled boundary pose difference across all seven windows
was 1.325 mm / 1.123 degrees; within each Hi-Z window it was at most
0.059 degrees. This is a stationary-view comparison, not identical input
replay. Boundary checks cannot establish continuous pose/focus stability.
The actual headset refresh rate was unavailable, so no refresh-budget or
reprojection threshold is asserted. Compaction did not interrupt the
independent logger; its original window remains evidence.

## Culling GPU cost

Two separate 300-frame captures per condition used normal shaders,
telemetry enabled and traversal diagnostics disabled. Each completed all
300 submitted and resolved frames. Values are disjoint self-time scopes;
they are not whole-frame costs or inclusive parent sums.

| GPU scope                            | Repeat 1, ms | Repeat 2, ms | Mean, ms |
| ------------------------------------ | -----------: | -----------: | -------: |
| Advanced downscale + native producer |      0.16631 |      0.20942 |  0.18787 |
| A all culling                        |      0.78306 |      0.78435 |  0.78370 |
| B all culling                        |      0.79168 |      0.80332 |  0.79750 |
| A bounds testing                     |      0.71701 |      0.71739 |  0.71720 |
| B bounds testing                     |      0.72226 |      0.73628 |  0.72927 |

B's observed culling cost is 0.01380 ms (1.76%) above A, while both
remain approximately four times the native scope. This difference is
small relative to baseline/session variation; two captures and imperfectly
fixed inputs do not isolate a precise code-only penalty. Bounds testing
still accounts for about 91% of Hi-Z culling. The hierarchy's base and mip
passes cost 0.056-0.057 ms in A and 0.057-0.059 ms in B. Reverting the
last change cannot plausibly remove the roughly 0.6 ms culling gap seen
here. Do not substitute the previous session's 0.076 ms native cost into
this comparison.

## Rejection and work

Frozen normal-shader counter windows recorded the following. These are
separate candidate cohorts, not persistent object or draw-call matches.

| Condition  | Tested records | Rejected records | Rejection |
| ---------- | -------------: | ---------------: | --------: |
| Advanced   |      5,398,528 |        3,387,395 |  62.7466% |
| A          |      4,231,168 |        1,649,648 |  38.9880% |
| B repeat 1 |      4,296,704 |        1,655,168 |  38.5218% |
| B repeat 2 |      4,280,320 |        1,639,237 |  38.2971% |

Separate 20-second diagnostic windows rejected 37.8685% for A and
37.7634% for B, with 99.928% and 99.930% accepted-history diagnostic
coverage. One boundary batch was absent from each diagnostic population.
No fallback, invalidated/unreadable history, promoted objects or diagnostic
readback failure occurred in these frozen windows.

| Work per diagnostic candidate, both eyes |       A |       B |
| ---------------------------------------- | ------: | ------: |
| Depth loads                              | 12.0332 | 12.0620 |
| Face regions                             |  3.9679 |  3.9927 |
| Triangle attempts                        |  7.1939 |  7.1985 |
| Actual polygon clips                     |  2.4983 |  2.4897 |
| Successful plane proofs                  |  1.2149 |  1.1963 |
| Face bias-only proofs                    |       0 | 0.02005 |
| Triangle bias-only proofs                |       0 | 0.02050 |

The new shortcuts fire about four times per hundred candidates, but do
not demonstrate four extra objects rejected. The two observed rejection
rates cannot establish that B loses culling either: the bounds/depth
cohorts differ, and no paired outcome comparison exists.

B's first decisive retention reasons, divided by candidate objects,
were finest depth 29.00%, nearest vertex 11.50%, wholly offscreen 10.54%,
partial viewport 6.88%, clip crossing 3.84%, read budget 0.278% and guard
only 0.197%. Invalid input and stack-capacity exits were zero. Offscreen
bounds remain native frustum work and can enlarge the count gap without
adding draws. Resolving every read-budget exit could recover at most
0.278 percentage points in this cohort.

## Decision and next work

Keep Advanced as default and PR104 experimental. This assay provides no
measured performance justification for the last bias change, but also
does not support blaming it for the large earlier regression. A blind
revert would recover little of the measured culling cost, at best.

The user's earlier 10.94 / 8.98 ms Advanced versus 11.79 / 9.58 ms Hi-Z
comparison belongs to source `ac2b7dcfc6f67d8c8f2230bcc5dd0f408d03f4c4`,
Build ID `a166eba911d48a6ed16b7a2c624e943442f5a9ef775da0685c0cebc2663b887b`.
It preceded `425b8d373`'s 2x2 depth change: that delivery message explicitly
said the new implementation still required testing. Its 4x4 hierarchy
recorded 0.590-0.598 ms Hi-Z culling, including 0.531-0.533 ms bounds
testing. The historical report is preserved in
[`425b8d373`'s report](https://github.com/ParticleTroned/skyrim-community-shaders/blob/425b8d37334884e9d3995e6b2720384f11558196/docs/development/vr-hybrid-culling-adaptive-2026-10-04.md).
The actual subsequent 2x2 campaign averaged 11.06 / 8.90 ms Hi-Z versus
9.82 / 7.74 ms Advanced, with 0.775 ms Hi-Z culling. Today's A restores
that culling shader and measures 0.784 ms. Different views prevent a
causal comparison between these sessions.

Prioritize an in-game 4x4/2x2 reduction switch with the proof variant
held fixed, warmed resources, noon resets and separate timing/rejection
windows. This isolates a larger plausible cost than the latest bias
change. The user subsequently authorized the independent coarse-depth
selector, now implemented for the next build; its runtime benefit remains
unmeasured. See the [comparison controls](vr-hybrid-culling.md#coarse-depth-baseline-comparison). A coarser hierarchy
can reduce traversal work but lose useful rejection, so whole-frame cost
and visibility must both be measured.

For precise attribution, compare visibility on identical bounds, camera
and depth in a DevBench-only diagnostic pass, and pair native outcomes
where possible. Then evaluate projected-face clipping/storage: about
2.5 actual clipper entries per candidate remain a concrete target.
Counters do not establish register spilling, occupancy or clipper
milliseconds. Preserve stereo, masks, guards and interpolation allowances.
Raising the traversal budget is not supported by the small exit cohort.

## Evidence

Measured source: `6f9ca1c1a0c5f9f1f061891e318901a737f3be1c`.
Producer Build ID:
`17ac3b21a52fd80e62829bf0c2a58ce6466b315e7ac84ed9085a4dafdc9d21b9`.
The installed AIO, physical DLL, adjacent manifest and shader payloads
were verified against that producer. All selected calls used the bundled
controller after the user's explicit transport authorization, with exact
identity and performance-neutrality guards.

Local evidence is under main-workspace
`build/astra-runtime/20261004T091727Z-hiz-proof-ab/`.
`controller/analyze-ab.py` reconstructs `controller/final-analysis.json`
from raw fpsVR windows, capture histories and frozen counters, checking
identities, coverage, sample counts and diagnostic invariants. Raw
render-scale status and maintained preparation/publication normalization
are retained; this producer's status omits the resource-publication field.
No render-scale behavior was changed or qualified.

The maintained profiler collector failed before producing samples because
of status semantic classification and timestamp precision bugs. Its
capture was cancelled and profiler disabled through the identity-pinned
outer controller. That failed attempt is excluded; the six completed
versioned API captures are selected. Local feedback receipts are
`AUTO-20261004-093840270-89D67D68` and
`AUTO-20261004-093841629-0B69A680`. The collector's stale recovery journal
is retained separately from the verified live restoration.

The implementation's 12/12 focused tests and production-isolation checks
remain attached to the measured source; this documentation update did not
rerun them. No pictures were taken because performance was not comparable.
Motion/lifecycle, SE/AE and full production DLL qualification remain open.
Advanced, telemetry enabled, traversal diagnostics disabled, baseline
toggle off and profiler disabled were restored and verified. Skyrim
PID 2180 was left running at noon.
