# Task 3 implementation closeout, 2026-09-28

Task 3A–3D is complete as an implementation milestone. The five ROI roles
are integrated into preparation, native admission/evaluation and private
output commitment; corrected sampling/history contracts are preserved;
the C current-context experiment is default-off and DevBench-only; output
ownership is represented independently while overlapping providers remain
rejected. This closes the requested groundwork, not production performance
or full runtime qualification.

The central performance issue remains open: substantially smaller selected
or evaluated regions have not established proportionate NR cost savings.
Task 3 makes support, ownership, inference context, historical envelope and
allocation capacity distinguishable. It does not establish which cost
dominates, and a smaller mask is not a timing measurement. Keeping 3C off
preserves the existing character-mask and ROI reductions; the experiment
only removes additional retained context when its current proof permits it.

## Implementation and acceptance boundaries

| Part | Completed                                                                                                                                                     | Qualification boundary                                                                     |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| 3A   | Five-role descriptors used by actual preparation, evaluation and commit adapters; existing rectangles and capacity retained.                                  | Prior local and sampled VR checks passed; no new production performance-neutrality claim.  |
| 3B   | Existing jitter correction, full feather support, shared CPU/GPU envelope and once-per-source aging preserved.                                                | Regression coverage is distinct from exhaustive live readiness/source-reuse qualification. |
| 3C   | Single-region C opt-in uses current proven support plus existing spatial padding; stable allocation, conservative fallback and reset-every-evaluation remain. | Functional checks passed; matched quality and cost benefit remain unqualified.             |
| 3D   | Independent ownership carried through the descriptor and current disjoint-provider adapter.                                                                   | Read-halo overlap remains disabled and belongs to later footprint/composite qualification. |

The [implementation record](nr-task3c-current-context-20260928.md) retains
the authorized universal build's **211/211 passing local tests** and
bridge-off preprocessing equivalence for the ten 3C source units. Those
checks were not rerun here. Bridge-off equivalence establishes removal of
the added experiment paths, not measured neutrality of all Task 3 changes.
See also the earlier [3A/B/D contract](main-vr-nr-roi-contract.md) and
[first 3C live evaluation](nr-task3c-live-20260928.md).

## Restarted VR session

The user restarted Skyrim and requested `coc WhiterunBanneredMare`.
The command was dispatched once and the destination was verified through
scene inspection. PID **18008**, process start
`2026-09-28T13:30:13.0821265Z`, DevBench port **8921**, ran the same installed
Task 3C AIO:

-   Build ID: `40689ffbf63b58479b81ee74ef687137fabf3c2c87585307526f4e980bf919f1`.
-   Compiled source: `7c4effa2749bb8d3cf123818ad6cf06bdc03a7cf`.
-   Compiled dirty digest: `bb3ce892f02f620e4410febc3842bb2efa0f99e647d06f955330dcfdc16eff0a`.
-   Physical DLL: 31,068,160 bytes; SHA-256
    `31f6ac810d437531a34cec5a67e47e7770d6ab2f08994d13f6d7683ba3a1ff76`.

The exact enabled AIO provider matched its adjacent manifest, AIO receipt
and runtime producer. Enabled loose providers, Overwrite and unmanaged Data
had no shadowing DLL. The selected profile remained
`Codex Task - 20260919t055629z-tracy-guardian-main-vr-1bf5803a`.
Loaded-module evidence and the fresh registration ledger contain no
lifetime tracer; the standalone temporal probe was not registered.
The installed automation package was `0.9.0+codex.20260928130304`.
No direct DevBench tools were callable, so the bundled controller was the
sole live transport throughout this session.

The fixture used DLSS K Quality, Render Scale enabled, 1008 × 1120 render
and 1512 × 1680 display pixels per eye. FOV was enabled at 0.95, without
periphery TAA. C used its full-eye source, authored face/skin/hair selection,
single-region planning and the unchanged Original/raw colour settings.
No camera freeze or pose injection was used.

Results:

-   **61,026** successful native region evaluations and **30,513** successful
    stereo submissions. NR failures, device removals, quarantines, validation,
    stereo and reset failures all remained **zero**. Reset success was 21/21.
-   Resource rebuilds stayed at **2** after initial creation throughout the
    baseline/experiment/debug checks and restoration.
-   With the experiment selected, debug-rectangle frame **51158** retained
    the baseline in both eyes. Debug-off frame **51342** applied current
    context in both eyes. Both transactions passed the ROI audit.
-   The offline status audit passed **14 distinct transactions** from
    32 saved NR responses. One initial capture-enable sample had incomplete
    renderer evidence; two settings-boundary samples reported
    `settings_changed_during_frame`. All three remain excluded explicitly.
-   The image receipts supplied another **60 structurally valid stereo
    transactions**: 59 neural pairs and one both-empty NoWork pair. The latter
    correctly published normal DLSS and is not enhanced-image evidence.

This passing session does not establish the cause or resolution of the
earlier intermittent native hang. Fresh-process A2/B1/B2 isolation, live
SE/AE, controlled ready/pending/ready, same-source reuse, mixed empty eyes
and native failure qualification remain separate outstanding evidence.

## Cost evidence: inconclusive

Four bounded profiler API captures completed **128 submitted and resolved
frames each**, ordered retained/current/current/retained. CPU frame evidence,
screenshots and recording were off during measurement. All guarded calls
proved the same PID/start time/build/artifact and neutral, not-applicable
standalone probe state. The profiler was disabled after each capture.
Full timer catalogs and all sampled NR-family GPU/CPU histories are retained.

`Upscaling::DLSSNeuralRenderingStereo` reported:

| Capture    | GPU self-time mean, ms | GPU p95, ms | CPU self-time mean, ms |
| ---------- | ---------------------: | ----------: | ---------------------: |
| Retained 1 |                 11.640 |      16.364 |                  3.046 |
| Current 1  |                 11.217 |      14.468 |                  2.996 |
| Current 2  |                 13.774 |      20.094 |                  2.798 |
| Retained 2 |                 21.905 |      26.453 |                  2.048 |

The unchanged-policy reference drift is too large for a scaling, improvement
or neutrality conclusion. This is a D3D11 CSX stereo-wrapper timer, not an
isolated native D3D12 evaluation or whole-frame time. NPC/view changes and
the absence of exact per-sample ROI joins also prevent an area/cost curve.
The four-window average must not be presented as an optimization result.

The next step directed at the user's cost-scaling problem is the existing
Task 2 native replay matrix: hold input and controls fixed; vary evaluated
rectangle/shape separately from creation capacity and invocation count;
record reset/cold/continuous state and actual nonzero output. Then compare
isolated native costs with in-game preparation, transport, synchronization
and reconstruction/composite costs. Use the existing runner and reports;
PNG sequences are not native replay input bundles. Task 4 empty proofs can
avoid native work entirely, but do not explain poor scaling for nonempty
regions. Later scheduling must use measured costs under Task 9.

## Image evidence: comparison rejected

Five real sequences retained **60 stereo pairs / 120 original PNGs**, all
1512 × 1680, `hmd_submission`, fallback rejected, `sdr_srgb`. Sizes, hashes,
decoding, opaque alpha, native dimensions, stereo acquisition identity,
configuration and ROI execution were verified. Five correlated recordings
were recovered from the explicit Overwrite provider, copied and hashed.
No recording hit its retention limit.

The first sequence's twelve pairs have a fixed camera. All 48 subsequent
pairs fail the preserved camera comparison: maximum matrix/position-element
deltas ranged from 151.255 to 679.770 against the first sequence. These are
substantial viewpoint changes. Actual image spacing was approximately
0.91–2.55 seconds despite a requested 500 ms cadence. Native-image evidence
therefore does not establish colour, detail, temporal or stereo equivalence.
No blinded candidate review was dispatched on this rejected comparison.
Regional metrics retain the confounding flags; no alignment or colour
correction was applied. Lighting-preservation modes were not compared.

The captured current-context regions still demonstrate real policy use:
44 of 48 candidate eye evaluations applied it, with four conservative
fallbacks. Capacity remained 1,128,960 pixels per eye. These image-time area
observations are separate from the capture-off timing windows.

## Automation findings and restoration

Three local automation defects were preserved and recorded:

-   `AUTO-20260928-134904330-5E451EEB`: the legacy collector converts an exact
    process-start timestamp to a culture-formatted string, then rejects its
    own identity. It failed before enabling profiling. The existing bounded
    profiler API was exercised through the same guarded controller instead.
    Its stale recovery journal is retained; independent status proved idle.
-   `AUTO-20260928-134911324-BD9F4500`: missing narrow semantic adapters reject
    input capabilities, FOV configuration and recorder-stop responses. Applied
    settings and recorder cleanup were verified with fresh observations;
    mutations were not replayed. Controller errors remain in the receipts.
-   `AUTO-20260928-141024227-AE62814B`: observation expects acquisition data
    beneath each artifact although this producer supplies it at the sequence
    child. The original child manifest proves native source and attribution.

The capture README's explicit evidence-directory route was used to retain
all artifacts permanently inside this task; no scratch allocator was
available from the installed tool catalog/package. No cache rotation,
automation-source change or game DLL change was made during testing.

NR was disabled successfully before restoring DLSS K NativeAA with Render
Scale off. Eleven read-back checks passed: NR settings/full configuration,
character settings, FOV, upscaling profiles, colour settings/experiments,
profiler off, recorder idle, screenshot workers/queues idle and same PID.
The current-context experiment is off. The game remains running in the
Bannered Mare; no agent-triggered restart, install, build, commit or push
occurred.

Raw evidence is under `build/validation/nr-task3-final-20260928/`.
`final-summary.json`, `structural-audit.json`, `image-audit.json`,
`performance-comparison.json`/`.csv`, `recording-inventory.json`,
`physical-dll-identity.json` and `restoration-verification.json` retain the
audits. The existing HMD analyser passed **36 tests** in a task-local Python
environment with the repository-pinned dependencies. `analyse.py` and
`finalise.py` completed successfully; no new rendering build was required.

## Explicit later Task 8 subtask: overlap qualification

This makes the specification's separate footprint/composite qualification
actionable. It is not an outstanding Task 3D implementation item.

1. Establish read footprints, required spatial/filter margins and guide
   mappings per supported route/runtime using the existing native evidence.
2. Retain separate private outputs and prove safe input/output lifetimes,
   including retirement and failure; coordinate with Task 7 transport work.
3. Partition exclusive write ownership and prove exactly-once composition,
   preserving baseline, alpha, feather support and Lighting preservation.
4. Qualify overlapping read contexts with disjoint writes: seams, crop edges,
   jitter, motion, stereo/mono, empty regions, overflow and partial failures.
5. Compare native quality and complete measured costs against the disjoint
   fallback. Enable overlap only after these checks pass; otherwise preserve
   the existing fallback. Four-region implementation need not enable overlap.
