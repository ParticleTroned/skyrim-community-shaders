# Shared-context live and captured-input investigation

## Result

The original-coordinate shared context reduces native invocation cost
against a deliberately forced four-call stereo plan. It has not established
an additional production gain at unchanged output. The normal savings gate
already selected two calls in this scene. Expanding those contexts by 256
pixels added work without eliminating a call. Enlarged contexts also changed
the native output on immutable captured inputs. The experiment remains
DevBench-only, session-only and off by default; no production cost profile
was adopted.

The replacement corrects that avoidable experimental overhead: enclosing
mode returns to the original execution path when no eye has multiple calls,
and preserves each single-call eye in a mixed batch. Full-eye mode remains
an explicit diagnostic reference. These changes require a new live test;
the measurements below belong to the preceding DLL.

## Tested producer and scene

-   Build ID: `8cb747096fd6a8f5c2c0d8b81fb53e5e761d8a0f39fe3ffd2a2b695c790896cc`.
-   Compiled source: `b5524f22289e3c527a18f348a695a69061836ee7`, dirty digest
    `f68dcc39f74ed898d1be5735f434305aba31b12a34ec3de1ed7bdd8187866c1e`.
    This implementation was subsequently committed as `1a455129b`; that is
    not substituted for the preserved compile identity.
-   DLL: 31,659,520 bytes; SHA-256
    `78c7eb9387298a4f3d05b0bb20eb2e53a9acd671fa07b99c304923855b932cef`.
-   NR provider 310.8.0: SHA-256
    `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
-   RTX 5070 Ti Laptop, driver 610.88; Bannered Mare, three selected eligible
    actors. Another close clipped person was visible at the right edge.
-   Camera position `(-231.7530823, -27.2449284, 197.6030579)`, pitch 0,
    yaw 1.9798329, free camera off; no camera movement was issued.
-   Per-eye NR input 1008 x 1120, native HMD images 1512 x 1680. AI was
    disabled for stationary comparisons; the fixed-clock image series also
    set timescale to zero. AI alone does not stop lighting changes.

Physical DLL, adjacent manifest, AIO receipt and runtime producer matched.
Enabled loose providers, Overwrite and unmanaged Data were checked; no
alternate DLL provider was found. Local evidence is retained under
`build/validation/nr-shared-context-live-20261004/`.

## In-game timings

Each row is a complete 300-frame bounded profiler window. Capture was
inactive while timing. GPU values are means of the existing D3D11
`Upscaling::DLSSNeuralRenderingStereo` inclusive scope, in milliseconds;
CPU values are the corresponding CPU scope. These are neither isolated
model execution nor total frame cost. Native D3D12 counters are retained
separately and must not be added to inclusive D3D11 intervals.

| Window              |    GPU ms |   CPU ms |
| ------------------- | --------: | -------: |
| forced split off-01 | 13.508910 | 2.571285 |
| enclosing256-01     | 11.710876 | 1.673425 |
| forced split off-02 | 13.689647 | 2.488931 |
| enclosing0-01       | 10.630925 | 1.593984 |
| enclosing64-01      | 11.389812 | 1.619738 |
| enclosing128-01     | 11.307359 | 1.667069 |
| full-01             | 11.990859 | 1.621751 |
| forced split off-03 | 14.250415 | 2.582963 |
| normal-off-01       | 11.087924 | 1.635819 |
| normal-tight-01     | 10.619627 | 1.658157 |
| normal-off-02       | 10.705902 | 1.610210 |
| normal-halo256-01   | 11.847599 | 1.651014 |
| normal-off-03       | 10.817030 | 1.585679 |
| debug-profile-off   | 10.473875 | 3.090922 |

Forced split disabled the savings gate. Its left ownership was
`(448,64,192,256)` and `(192,320,816,800)`; right ownership was
`(448,64,192,256)` and `(128,320,880,800)`. Coalescing four calls to two
saved approximately 3 ms in that diagnostic configuration. This is not a
gain over the normal production planner. With the savings gate restored,
the two normal contexts were `(192,0,816,1120)` and `(128,0,880,1120)`.
Enclosing halo zero had the same native geometry; its small apparent
advantage falls within the observed baseline variation. Halo 256 expanded
both contexts to full input and cost about 1.03 ms more than normal-off-03.
At a 90 Hz reference budget of 11.11 ms, 1.03 ms is about 9.3% of a frame;
this is a scale reference, not a claim about the configured refresh rate.

For normal-off-03, category capture, mask generation and final composition
GPU means were 0.046179, 0.089848 and 0.049862 ms respectively; CPU ROI
setup was 0.014996 ms. These measured stages are much smaller than the
10.817030 ms inclusive NR scope. Copy and synchronization work remains
inside that inclusive scope here; it is not assigned the entire unexplained
cost or inferred by adding overlapping timers. The isolated native replay
below establishes a substantial provider cost independently.

The primary windows ran at Info logging. Debug was enabled later for native
input capture and is a separate phase. Current-source GPU bounds became
intermittently available, producing much tighter plans, then sometimes
returned to safe geometry fallback. A Debug profiler-on repeat also
received ready bounds, so profiler state alone does not explain readiness.
The later native-counter windows contain changing plans and cannot qualify
a cost profile. Pending bounds are not proof that an area is empty. No
stale-bound substitution or render-thread wait was introduced.

Final runtime totals were 303,086 feature evaluations, zero NR failures,
zero stereo failures, zero quarantines and zero device removals. Four
rebuilds preceded an intentional off/on transition for the Debug fixture;
eight followed it. No unexplained resource rebuild was observed.

## Image assessment

Twenty-one sequences of twelve native stereo pairs completed with exact
transaction joins, verified artifacts and no failed/dropped pairs. The
first eleven sequences allowed the clock to advance and are retained but
excluded from cross-lighting quality conclusions. Ten replacement sequences
held game hour at 9.674558639526367 with order
`A1,A2,B1,A3,C1,A4,C2,A5,B2,A6`: A independent baseline, B enclosing halo
zero, C enclosing halo 256. Camera matrices, projection, position adjustment,
output rectangles, viewport and selection policy were identical across
all 120 replacement stereo samples. Frame IDs and temporal jitter were
retained as identities, not mistaken for camera or ownership changes.

Original, unregistered fixed-pixel central-face crops gave these mean-image
RGB absolute differences from A1, in 0..255 codes:

| Sequence |   Left |  Right |
| -------- | -----: | -----: |
| A2       | 0.3614 | 0.3960 |
| A3       | 0.4369 | 0.4910 |
| A4       | 0.3667 | 0.3956 |
| A5       | 0.3915 | 0.4286 |
| A6       | 0.3613 | 0.4080 |
| B1       | 0.5964 | 0.6290 |
| B2       | 0.5783 | 0.6173 |
| C1       | 0.8732 | 0.9068 |
| C2       | 0.9034 | 0.9656 |

These are diagnostics, not perceptual thresholds. Fire, smoke, notification
overlays and residual animation are not suitable constant-input references.
A blinded reviewer inspected 54 native originals and 54 corresponding crops
from nine anonymous sets, both eyes and three sampled ordinals. All four
comparisons were ties for colour and gross stereo (medium confidence).
Useful neural-detail benefit and temporal quality were indeterminate (low
confidence); retained visible detail had no clear preference. The reviewer
had previously seen another anonymous set but had no settings or metric
mapping. Actual reviewed temporal gaps were 2.895–4.459 seconds, so this
does not qualify HMD-rate flicker or motion. No exact-equivalence or general
six-colour-mode qualification follows from the visual ties.

## Immutable-input replay after shutdown

Capture `capture-1791092745516118-48720-1` contains one initialized full-eye
RGBA8 frame per eye. The manifest and eight payloads were copied and hashed
locally (36,126,720 payload bytes). Replay executable SHA-256:
`9fbc0505d7841dc88c569e376ba699fb2221345da9f30c817ab306d37621a536`.
Each layout campaign ran eleven cases in forward/reverse order, with three
warmups and eight steady samples per case/repeat. Skyrim was closed and
no other replay campaign ran concurrently.

The first default output-marker run rejected two coincidental RGBA8 marker
matches. That failed evidence remains preserved. A complementary-marker
run produced byte-identical full output for both eyes, establishing a marker
collision rather than unfilled output. The maintained context runner now
binds the existing alternate-marker option into its immutable plan and
validates both the result and every accepted footprint. It still rejects
ambiguous or incomplete writes.

Below are stereo native GPU means/medians in milliseconds. A layout is
replicated onto both captured eyes within each campaign; these timings are
not the actual asymmetric live pair. All sixteen steady samples are retained,
including substantial outliers; none were trimmed.

| Case                         | Left geometry mean / median | Right geometry mean / median |
| ---------------------------- | --------------------------: | ---------------------------: |
| full                         |            13.4474 / 8.5620 |              8.5799 / 8.5810 |
| tight, two regions           |           17.5391 / 10.7820 |            21.8468 / 10.9900 |
| enclosing raw                |            11.8881 / 7.3000 |             14.9240 / 7.6380 |
| individual raw region 0      |             4.3109 / 4.3140 |              4.3157 / 4.3170 |
| individual raw region 1      |            12.1543 / 6.4755 |              6.6617 / 6.6530 |
| individual halo 0 region 0   |             4.3177 / 4.3170 |              4.3178 / 4.3180 |
| individual halo 0 region 1   |            12.6329 / 6.4845 |             15.0084 / 6.6645 |
| enclosing halo 0             |             7.3674 / 7.3140 |              8.6584 / 7.6295 |
| individual halo 256 region 0 |             5.2340 / 5.2350 |              5.2504 / 5.2345 |
| individual halo 256 region 1 |            13.4743 / 8.3040 |              8.3516 / 8.2940 |
| enclosing halo 256           |             8.5871 / 8.5760 |             11.2819 / 8.5710 |

Tight reference repetitions and independent individual calls reproduce the
same owned pixels exactly. Merged or enlarged inputs do not. Using only the
corresponding actual eye from each campaign, enclosing halo zero differs
from tight output by mean 1.05387/255 left and 1.06914/255 right, maximum
39/255 and 30/255. Halo 256/full context differs by mean 1.65315/255 and
1.46210/255, maximum 48/255 and 28/255. These raw owned-output measurements
include pixels later excluded or attenuated by character-mask composition;
they are not HMD image-error measurements.

The 49,152-pixel region still costs approximately 4.3 ms across two native
calls, whereas a much larger individual region costs a median 6.48–6.65 ms.
This supports substantial fixed/non-area-proportional native cost but does
not identify the opaque provider's internal cause. Existing code already
retains feature instances and batches command submission. Packing patches
or widening input changes the provider's result; preserving output ownership
alone does not preserve the model's input-domain semantics. A verified
independent-patch batch interface or provider-internal optimisation would
be needed to establish the proposed exact-output amortisation. No supported
such capability has been established by this investigation.

## Corrections and shutdown

Alongside the single-call admission correction, DevBench physical-call
telemetry now follows the immutable execution descriptor attached to the
completed timestamp sample. It no longer mistakes independent owned output
rectangles for physical native calls. All eight region banks use existing
mask helpers. Missing execution evidence yields unavailable/null values;
legacy frame-only preparation correlation remains explicitly diagnostic.
The registered schema and description match the `profiling` response path.
The new metadata and policy changes compile only with the DevBench bridge.

CSX menu preparation now returns explicit `ok` from its readiness
postconditions, separately from mutation (`applied`, `changed`). The old
reply could apply successfully and then fail the collector's semantic
success check. This is a CSX producer-contract correction, not a DevBench
host or vr-automation source change. Local feedback receipt:
`AUTO-20261004-060455702-26C6E3BC`.

After all live evidence, AI was restored on, timescale to 20, NR and its
experiments to their initial settings, FOV/TAA restored, capture/profiler
disabled and every recording stopped. Debug was runtime-only and no settings
save was issued. Skyrim exited through `qqq`; MO2 exited normally. No USVFS
modules remained in inspected interactive-session processes, there were no
unreadable processes, and RootBuilder state was released. All 21 recordings
were copied and hashed; originals were preserved. Session and access leases
were released. The new AIO is for manual installation, with DevBench enabled,
VR only, no shader cache and no Horizon Fix installer option.

## Replacement validation and evidence

The isolated build is `build/sctx1004c`, configured with the VR preset,
DevBench on, Tracy off, automatic deployment off, and both controller and
shader test groups on. The build command is
`pwsh tools/cmake.ps1 --build build/sctx1004c --config Release --target CommunityShaders controller_tests shader_tests -- /m:1`.
The complete Release CTest inventory, JUnit results, producer/compiler
hashes, exact-source checks and input-bound preset receipt are retained in
`validation-run-03/` and `validation-summary-03.json` under the evidence root.
This uses the existing isolated-build validator; the general
`tools/validate-local.ps1` hardcodes the separate ALL build tree.

Focused Python runs passed: `test_context_probe.py` 8/8,
`test_packed_report.py` 13/13, and
`tests/neural_current_context_contract_test.py` 5/5. Scoped hooks passed.
Policy regressions cover single-call edge geometry for all halos, unchanged
explicit single-region ownership, mixed-batch policy, all eight physical
slot banks, wrong/missing execution identities, and unavailable telemetry.
The menu contract checks its boolean success marker and registered schema.
Existing bridge-on/off tests retain the production boundary.

The first packaging validation rejected the older preset receipt because
its bound `NeuralRenderingFeature.cpp` changed. The generator and regression
then correctly rejected the stale settings-source fingerprint. Review found
only the DevBench description changed among the bound inputs. The source
fingerprint and generated metadata were refreshed deliberately, retaining
contract revision 8, the base template and every graphics setting in all
three tiers. `preset-refresh-semantic-check.json` preserves that comparison;
the failed receipts remain alongside the replacement checks. Existing
untracked preset archives were not rebuilt or replaced.

The first complete CTest pass ran all 231 tests: 230 passed, including shader
tests, and the old Multi-ROI source contract failed because it required
deriving physical invocation counts from prepared output rectangles. That
expectation was corrected to require the frozen execution plan and the
shared region-mask helpers; the failure output remains in
`validation-run-02/`. The final complete run is recorded separately above.

The review checked admission before mutation, exclusive output ownership,
sample-to-execution identity, nullable unavailable values, schema paths,
reuse of existing slot-mask helpers and lack of new production branches,
resources or GPU waits. No shader source changes were made. A full SE/AE
DLL or a live replacement-DLL run was not performed. The replacement AIO's
producer identity, final source digest, exact DLL/PDB/manifest match,
installer choices, extracted file hashes and archive-integrity result are
in `archive-verification.json` and the adjacent archive receipt. Raw captures,
the rejected marker run and all untrimmed replay timings remain local.
