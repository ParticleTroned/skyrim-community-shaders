# Task 6 source transactions and toggle qualification — 2026-10-02

## Status and scope

The standalone GPU mask-support toggle and Task 6 source/readiness changes
are now **live-qualified for the exercised functional scenarios** on
producer `6646e62e56d3`. The [follow-up and Task 7 record](nr-task7-transport-20261002.md)
contains 288 exact stereo transactions, 3,000 profiler frames, drift,
empty/re-entry and policy/route changes without an NR failure. Skyrim and
MO2 closed normally after restoration.

**Task 6's matched improvement-or-neutral performance criterion remains
open.** The follow-up had seven eligible actors versus five in the older
baseline, with different evaluated rectangles. No cross-build performance
gain or final image-quality acceptance is claimed. The sections below
preserve the earlier implementation session and its Task 4/5 baseline.

This implements the October 1 Task 6 specification. Tasks 2, 4 and 5 retain
their recorded conclusions. Native transport sharing, larger ROI capacity,
history scheduling, output ownership and provider aliasing remain later
work. Sparse GPU dispatch remains a default-off DevBench experiment.

## Earlier baseline producer

-   Branch base: `64a22a5f1184c02477bed909ba4ff3462e44d2bf`.
-   Producer Build ID:
    `8c0267be060e89aacb3c5277e86a878e984285943d190be1b9eaf36ea2cbd98c`.
-   Compiled base: `4bd08e6f5a07c7efb28cabf078ce67f67532fde3`;
    dirty digest:
    `80f793885d4c54b2e2b78dcec5c715ae88479b08d88ab6f75282b9aa0929d0f8`.
-   DLL: 31,229,952 bytes; SHA-256:
    `88429ac36fd6a33780270393912efae59da4e79deadaf4d13249a1d2af1f5c94`.
-   Sole enabled loose provider:
    `CSX_AIO-main-vr-nr-NR-Task4-5-DevBench-20261002-8c0267be060e`.
    Physical DLL, adjacent manifest, archive receipt and runtime identity
    matched. Overwrite and unmanaged Data had no replacement DLL.
-   Skyrim PID 66140, start `2026-10-02T19:07:22.7112120Z`.
    Bannered Mare, existing null HMD, five eligible actors; C input
    1008×1120 per eye. Free camera and global AI-off held actor positions.
    Rendering, animation effects and jitter were not immutable native replay.

Evidence remains local in `build/validation/nr-task6-live-20261002/`.
Every command used the bundled DevBench transport and pinned process/build
identity. `qualification-audit.json` verifies 15 complete sequences,
240 exact source-attributed frames and 240 matching artifact hashes,
plus three completed 300-frame profiler windows. Sequence images are
32×16 attribution carriers, not visual-quality evidence. Separate stereo
PNGs preserve the initial view.

## Live results on the installed DLL

| Case                                                     | Result                                                                                                                                                                  |
| -------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Standalone `experimentalGpuMaskSupport` false→true→false | Both one-field requests accepted first try; character settings changed, other character and NR settings unchanged, no backend rebuild for the toggle                    |
| A, B, C baseline                                         | 300 profiler frames and 16 exact attributed frames each; successful native execution                                                                                    |
| Multi-ROI, savings gate off                              | Two regions per eye; four calls and 1,634,304 evaluated pixels per stereo transaction                                                                                   |
| Six cumulative 2-unit camera moves, then return          | Every 16-frame sample retained four calls and 1,634,304 pixels; distinct per-eye/source identities preserved                                                            |
| Range exclusion and re-entry                             | Native evaluations held at 44,956 from frames 42,947 to 43,244; both eyes had current CPU empty proof; re-entry resumed four calls                                      |
| Face-only then all categories                            | Four calls; 786,432 then 1,634,304 evaluated pixels                                                                                                                     |
| Journal menu                                             | Evaluations held at 63,022 while engine frames advanced; NR suspended, so this does not qualify frozen-source reuse                                                     |
| Final B→C repeat                                         | Accepted retirement and C execution without a watchdog or NR fault                                                                                                      |
| Restoration and shutdown                                 | Original NR, character, FOV, colour configuration, profiler, AI and camera state restored; `qqq` exited Skyrim, exact MO2 File→Exit cleanup and lease release succeeded |

Final counters: 64,538 attempts, successes, evaluations and output commits;
29,218 successful stereo batches; zero failures, device removals,
quarantines, stereo failures, validation failures or reset failures.
All 15 resets succeeded. Intentional route/multi-ROI changes caused
16 resource rebuilds and seven initializations. C retained its existing
reset policy: 60,242 caller history resets are not a new fault.

### Readiness and timing baseline

These are measurements of the installed producer, not the Task 6 changes.
Each row has 300 resolved profiler frames. CPU ROI setup includes both-eye
work in the frame timer; mask GPU values are that pass's self time.

| Route                       | ROI setup CPU mean ms | Mask GPU mean ms | Early bounds GPU mean ms | Exact ready/used eye samples |
| --------------------------- | --------------------: | ---------------: | -----------------------: | ---------------------------: |
| A, late full resolution     |              0.018290 |         0.399843 |                 0.038153 |                         0/32 |
| B, late foveated            |              0.017539 |         0.420109 |                 0.037840 |                         0/32 |
| C, early reduced resolution |              0.019544 |         0.182844 |                 0.039034 |                         0/32 |

Read-only calls around those captures cover larger intervals than the
300-frame profiler windows. Their deltas were respectively 644/604/763
queued stereo buffers and 1,288/1,208/1,526 pending per-eye probes, with
zero ready, used, ring-busy or failed results. Those are separate
denominators, not 300-frame readiness rates. Across the whole session,
three stereo buffers were mapped and used by four eye preparations;
the sampled windows therefore do not represent all lifetime outcomes.

The exact corpus contains **240 stereo transactions and
480 eye records**, with zero ready/used eyes. Each frame retains its own
current CPU fallback and execution plan. A/B and C are reported separately;
no blocking readback or larger ring is justified by these pending results.

## Implementation and adversarial review

-   Freeze the complete character policy by source frame, including feather,
    distance, ROI, diagnostics and bridge-only experiments. Capture geometry,
    admitted actor identities, both camera projections/origins and depth
    constants with the authored category/depth transaction. Later producer
    updates cannot change a peer eye's interpretation.
-   Resolve GPU readiness once per exact captured stereo source. Pending and
    failed decisions remain fixed through both eyes and reprepare. A completed
    stereo buffer is mapped once, with each eye retaining its own coordinates.
    The three-slot ring checks each pending slot at most once for reuse;
    exhaustion, failure and no progress preserve current CPU coverage.
-   Reuse mapped tiles, owner input/sorted lists, stripe/occupied/suffix
    buffers and region candidates. Aggregate projections into a retained
    vector indexed by the captured admission order, replacing transient
    per-actor hash nodes. This reduces allocation work without claiming all
    planning allocations are eliminated.
-   Bound actor-detail refinement to 128 attempts per source frame, counting
    failed face projection and its bounded actor fallback in the same
    admission attempt. Exhaustion admits conservative current geometry;
    identity-cache overflow retains authored semantics with full-eye
    uncertainty rather than silently dropping actors. There is no new pose
    scan or sparse LOS culling system.
-   Preserve current-source validation, the shared CPU/GPU single envelope,
    immediate support growth, exact masks, native waits and resource lifetime
    ordering. Strengthen final stereo validation for source, generation,
    complete policy, jitter domain and full input/output grids.
-   Gate CPU poll/planning clocks on opt-in capture evidence. DevBench schema
    documents unique stereo poll counts versus per-eye use/fallback counts
    and `lastPollCpuAvailable`; unmeasured times are zero. Production adds no
    wait, flush, sleep, yield, per-ROI submission or CPU retirement drain.

Review found and corrected snapshot ordering across texture recreation:
the geometry snapshot must be taken after capture-resource invalidation.
Tests exercise production snapshot methods, stale/out-of-order identity,
mono/stereo cameras, producer updates between consumers, saturated rings,
100 no-progress attempts, immutable policy including bridge fields, budget
saturation, and 400 merge/split/drift/reset/reprepare frames with reusable
versus fresh scratch. A real WARP D3D11/D3D12 queue gate proves that producer
completion between attempted reads cannot change a frozen pending decision.
Existing randomized projection, mask, independent-eye, frozen-source and
prepared-selection tests remain applicable.

## Offline validation and testing AIO

The universal SE/AE/VR Release DLL built successfully with DevBench on,
Tracy off and automatic deployment off. The complete CTest inventory
executed **221/221 passed, zero skipped** in
`build/validation/nr-task6-verified-20261002/ctest.xml`.

The complete runner's terminal result remains **failed**: after all tests
passed, its preset check required a deliberate source-fingerprint update.
The fingerprint includes the complete `CharacterSettings.h` file. Review
confirmed that serialized keys, defaults, loading/saving and migrations
were unchanged, so contract revision 8 and the base template were retained.
Regeneration changed only `Preset Compatibility/settingsContract/sourceTreeSha256`
in each tier. Existing local preset archives were preserved.

The follow-up record in
`build/validation/nr-task6-aio-20261002/validation-followup.json` is **passed**:
preset generator/rollback tests, both preset compatibility tests, generator
`-Check`, `git diff --check` and DLL manifest verification passed. All 20
files in the compiled change inventory still matched their recorded
hashes when the follow-up was finalized; only the five preset metadata
files had changed. This final evidence section was added afterward.
The earlier `nr-task6-final-20261002` run retains its 220/221 result from
an obsolete source-contract assertion, corrected and verified in the next
run. Neither failure record was overwritten or silently counted as a pass.

Testing producer:

-   Build ID:
    `6646e62e56d3f88b7583ce3f714dfafd6e0f24ccb17e6ee259ab90a337cdc262`.
-   Compiled base: `64a22a5f1184c02477bed909ba4ff3462e44d2bf`;
    dirty digest:
    `4c7dded50db4f5ecb40860927f1a3c7c3ae5ad0dd7b2a98349d12ffe501ee7b1`.
-   DLL: 31,236,096 bytes; SHA-256:
    `1dbc6e9e2547089999051d106ae7d0c18f8de54db8eecff7ea7be237aa8f5426`.
-   Compiled patch, file hashes, manifest, validation follow-up and complete
    archive receipt are retained in `build/validation/nr-task6-aio-20261002/`.
    The compiled source identity is preserved independently of this final
    documentation and compatibility-metadata update.

Archive: `dist/CSX_AIO-main-vr-nr-NR-Task6-DevBench-20261002-6646e62e56d3.7z`.
It contains 385 verified files, is 199,083,471 bytes, and has SHA-256
`e8d2cc72aadc6b9e8ecec9b21e449722aae7685f17cc263ab06dcafe3e52b851`.
Archive integrity, complete extracted file sizes/hashes, producer DLL/PDB/
manifest identity and pinned NR 310.8 runtime checks passed. No deployment
or game relaunch occurred.

## Remaining acceptance

The new DLL's live functional checks are recorded in the
[October 2 follow-up](nr-task7-transport-20261002.md). The remaining Task 6
criterion is a controlled prior-build comparison of preparation and total
frame cost with identical captured scene/geometry and settings. SE/AE
gameplay and final stereo image/temporal quality remain untested here.
Task 7 now has a separate default-off transport candidate awaiting its own
new-DLL qualification. The previous archive's compiled identity above is
preserved; it was installed by the user for that completed follow-up.
