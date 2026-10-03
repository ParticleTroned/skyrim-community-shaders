# Task 9/10: stationary-scene measurements and calibration controls

## Decision

The current producer passes unpaused cost-command admission, bounded
candidate enumeration and the stationary count sweep. The tested retained
compact candidate is rejected for production: its NR stereo block costs
7.39151 ms against a 5.14950 ms matched full-coordinate baseline (+43.54%).
It reduces logical transport storage by 47.76%, but exposes no native
allocation-byte measurement. Keep full coordinates and the existing
unknown-cost heuristic. No production cost profile was loaded or adopted.

The live run also identified two DevBench defects: compact admission
depended on image-evidence capture, and continuous unqualified enumeration
substantially disturbed timing. This change separates runtime mode from
capture metadata and makes unqualified inspection one-shot. It also adds
the missing bounded candidate-execution control so an alternative can be
tested without inventing a qualified cost profile.

These replacement-DLL controls have not run in Skyrim. Their offline
validation is separate from the completed measurements below. The negative
compact decision is final for this candidate; it is not a successful
compact-production qualification. Task 9's production calibration remains
unqualified rather than being silently inferred from this count sweep.

## Exact live producer and scene

-   Build ID:
    `23397d0f9a2653102b34cd0567e5a287686011633b5c1d1c484347698c81f892`.
-   Compiled source: `916c861d32d7750cb40200bd77f263709e71225f`, dirty digest
    `d520ee17a357cde891025d8e6029a5f68a03572bbd8a21e2ed6d14c320f00c3a`.
    The implementation was subsequently committed as `744815b7f7a8`;
    that does not change the producer's original compile identity.
-   Physical DLL: 31,736,832 bytes, SHA-256
    `9b035657eaad8965122666fe83e005c5e6d40dfe58605814fdcd835e57ec74bd`.
    Adjacent manifest, AIO receipt, enabled-provider inventory, Overwrite
    and unmanaged Data checks establish the deployed identity.
-   Skyrim PID 77452, started `2026-10-03T20:38:21.6589835Z`.
-   Evidence root: `build/validation/nr-task9-10-search-live-20261003`.
    `window-audit.json` retains exact averages/tails, source status, geometry,
    counters, capture IDs and hashes of the preserved raw histories.
-   The user's camera position was retained. Global `tai` was issued only
    after proving no selected console reference; its captured response was
    `All AI Processing is Off`. All 15 loaded actor positions matched the
    AI-off snapshot at final restoration. No camera, COC or save command ran.
    Three prominent NPCs were visible; detection can include a fourth actor.

All 16 bounded profiler windows completed 300 submitted/resolved frames:
4,800 total, of which 3,900 followed the AI-off command. The first 900
frames are retained as uncontrolled actor-content evidence and excluded
from the stationary comparisons. Each measurement used the maintained
performance-neutral transport guard. DevBench was enabled; Tracy was off.
HMD image acquisition occurred outside measured windows. AI-off does not
prove that every animation, lighting value or GPU input byte is identical.

## Task 9 controls and search

Cost configure/status/disable succeeded with active, unpaused NR. An empty
profile and a deliberately stale-identity negative fixture were rejected
without mutation. Its synthetic numbers were never measured observations
or an adopted profile. Tight face-only coverage generated 90 joint stereo
candidates (9 left, 10 right), including split and merge alternatives.
Current-source coverage and bounded-search diagnostics were present.
Sequential A and C routes also enumerated alternatives with native output.

The installed API could enumerate alternatives but could execute a chosen
alternative only through a fully qualified profile. Loading invented costs
or asserting unmeasured qualification gates would invalidate this assay.
The replacement API removes that dependency with an explicit experiment.

### Enumeration overhead

AI-off, four regions per eye, capture disabled, 300 frames per row:

| Window                 | NR stereo GPU self, ms | NR stereo CPU self, ms |
| ---------------------- | ---------------------: | ---------------------: |
| Heuristic before       |              19.192738 |               4.581019 |
| Continuous enumeration |              22.519487 |              22.979332 |
| Heuristic after        |              19.097996 |               4.504441 |

The GPU bracket drifts 0.50%. Enumeration adds 3.37412 ms to the GPU
timestamp interval and 18.43660 ms CPU self against bracket midpoints.
The GPU interval can include starvation while the CPU submits work; it is
not evidence that shader computation alone increased by that amount.
Continuous experimental search must not be included in a production
baseline or described as performance-neutral. Production preprocessing
excludes the experiment, and ordinary DevBench operation leaves it off.

### Invocation count versus evaluated area

Mode C, face-only, unchanged camera, 30 m range, minimum face size 1,
zero ROI margin/hold, savings gate off, cost search and capture off:

| Maximum / actual regions per eye | Left evaluated pixels | Right evaluated pixels | NR GPU self, ms | NR CPU self, ms |
| -------------------------------- | --------------------: | ---------------------: | --------------: | --------------: |
| 1 / 1, before                    |                540672 |                 585728 |        8.128489 |        1.713803 |
| 2 / 2                            |                233472 |                 249856 |       11.478531 |        2.392107 |
| 3 / 3                            |                147456 |                 159744 |       15.224906 |        3.336235 |
| 4 / 4                            |                135168 |                 135168 |       19.199854 |        4.680786 |
| 1 / 1, after                     |                540672 |                 585728 |        8.346515 |        1.554684 |

Each row contains 300 frames. No resource rebuild occurred inside these
windows (counter 32 throughout). The one-region bracket midpoint is
8.23750 ms, with 2.68% drift. Four regions reduce total evaluated pixels
from 1,126,400 to 270,336 (76%) while costing 2.33 times the NR GPU block.
At a 90 Hz / 11.11 ms frame budget, the 10.96235 ms difference is about
98.7% of a frame budget. This is a pass-level cost comparison, not a claim
about total application frame time or pure native model time.

The table sums actual `computeRegions[].pixels`; `computeSubrectPixels`
is the enclosing rectangle. Legacy count zero represents one native
region per eye here, not NoWork. Summing enclosure pixels would have
misreported the area reduction. CPU and GPU clocks remain separate.

The audit retains all available per-pass timing and raw history. These
ordered profiler samples do not expose individual source-frame IDs, so
they cannot be relabeled as exact per-key native calibration samples.
Held-out scenes, prediction-error bounds, native residency bytes and
fully joined transition/quality evidence remain unavailable. The safe
result is rejection of profile promotion, not guessed cost coefficients.

## Task 10 compact evidence

With a 3 m face-only fixture and 192x192 evaluated region per eye, compact
enabled plus capture disabled left both active slots full-coordinate.
Changing only capture evidence to enabled activated compact storage.
This was a real experimental-control defect: runtime policy read optional
capture metadata. The new DevBench-only mode field is assigned before
the capture early return; compact selection and cost keys now use it.

The installed-DLL compact comparison therefore keeps capture evidence
enabled on both sides. It uses stateless C, with unchanged source density
and output ownership. Five bounded windows were preserved:

| Window               | NR GPU self, ms | NR CPU self, ms | Rebuild counter before/after |
| -------------------- | --------------: | --------------: | ---------------------------- |
| Full before          |        5.403574 |        1.576714 | 34 / 34                      |
| First compact        |        5.179068 |        1.665270 | 36 / 38                      |
| Full after           |        5.178129 |        1.710612 | 40 / 40                      |
| Retained compact 768 |        7.391507 |        2.152698 | 42 / 42                      |
| Full final           |        5.120872 |        1.656795 | 44 / 44                      |

The first compact window spans a 256-to-768 retained-capacity change and
cannot qualify a steady bucket; the available histories cannot locate
that transition per sample. The retained 768 repeat is stable, and its
surrounding full-coordinate bracket drifts only 1.11%. It regresses by
2.24201 ms / 43.54%, about 20.2% of the 90 Hz frame budget.

Logical transport storage is 18,874,368 compact versus 36,126,720 full
bytes (-47.76%). Native allocation bytes are explicitly null with
`provider_does_not_expose_residency_bytes`; `physicalResidencyMeasured`
is false. Known native instance state is not measured physical residency.
No net memory-saving claim includes unknown provider allocations.

Native stereo images were preserved separately. Inspection showed no
obvious seam in the viewed still, but this is not blinded temporal/stereo
quality qualification or a matched moving sequence. Prior standalone
capacity replay evidence was reused rather than recreating that assay.
The negative live cost result rejects this candidate for production;
unperformed promotion gates are not marked passed.

## Replacement implementation and adversarial review

All new rendering-side work is conditional on `DEVBENCH_BRIDGE_ENABLED`.
There is no new production setting, render pass, D3D resource, shader,
runtime divergence or automatic cost training. SE/AE/VR share the same
guarded implementation. Existing coverage, capacity and fallback policies
remain authoritative.

-   `calibration_start` accepts an observed backend identity, an admissible
    final candidate key and 1..600 unique source frames, bounded to 60 seconds.
    It requires enabled search, no loaded profile and no active calibration.
    It cannot supply costs or relax any profile-adoption gate.
-   Exact geometry, banks, caller reset, mode and identity remain pinned.
    Only effective-reset and resource-rebuild flags may vary for warm-up;
    recorded full keys retain those actual flags. Each source recomputes
    admissibility. Missing candidates, capacity fallback, failed native
    commit, backend/generation changes or repeated sources stop the run.
-   Review fixed a first-frame generation gap by binding generation and last
    source to the inspected candidate before arming. Stop reasons remain
    explicit. Cancellation/configure stops calibration and resets dwell.
-   Bounded records retain engine/source frame, generation, native commit,
    fallback, exact key index and output domains independently of image
    capture. Evidence-allocation failure stops calibration explicitly;
    it cannot escape a scope-exit destructor. Native commit does not prove
    outer composition, image quality or a measured cost.
-   Configure/status arm one next-source enumeration when neither a profile
    nor calibration is active. Status returns the attributed prior snapshot
    with `inspectionPending`. Per-call execution state prevents later frames
    from publishing stale one-shot decisions. Continuous search is explicit
    and its measured CPU cost must remain visible.
-   DRY review reused final-plan keys, the existing completed-frame command
    queue, validated candidate search, output-domain publication and profile
    validator. No second capture, profiler or graphics ownership mechanism
    was introduced. Descriptor/schema and preset source-contract hashes are
    updated together; preset rendering values are unchanged.

The separate host-controller fix is automation commit `4cb088f`, integrated
into local `dev`. It recognizes only typed `communityshaders.menu/status`
observations, preserves identity/error checks and cannot verify mutations.
Source and packaged suites each pass 322 checks; the preserved live response
passes offline revalidation and all three source/package hashes match.
Feedback `AUTO-20261003-211747444-E77445B4` is resolved. No DevBench server
code changed, no server PR is required for this host-only correction, and
the installed plugin was not rotated from a development branch.

## Offline validation and replacement producer

`pwsh ./tools/validate-local.ps1 -OutputDirectory
build/validation/nr-task9-calibration-final-elevated-20261003` passed:
universal Release DLL, both test groups, 228/228 CTest tests including
shader tests, preset regression/reproducibility and manifest verification.
No tests were skipped or disabled. Scoped pre-commit checks passed.

Replacement Build ID:
`0eff0e9ceac16477e908573386f2ff834cf23d604dc0922357f81b7009eea489`.
Compiled source `744815b7f7a8317fd93519bb2ad0453e8d19afbd`, dirty digest
`60fddb97f0878afba02807ce5234902b480c29ee5f1766165e80687c4bf893a1`.
DLL size 31,756,288 bytes, SHA-256
`72a419a42240b28026a4a0dd52709c33cd7f15729695e58fc2b38f3376d73c67`.
DevBench is ON, Tracy is OFF, SE/AE/VR are ON; dependencies match their
pinned, clean commits. Automatic deployment and automatic archive targets
were disabled throughout validation.

Verified archive:
`dist/CSX_AIO-main-vr-nr-NR-Task9Calibration-DevBench-20261003-0eff0e9ceac1.7z`.
It is 199,517,358 bytes, SHA-256
`a823da5cd3b33d4ce050de7934740b9e7640fc3b3d7fc814723b0d12e91c500a`.
Archive integrity, all 386 extracted file sizes/hashes and extracted
DLL/manifest/PDB identity match the producer. The adjacent `.receipt.json`
and `.7z.sha256` retain the result; detailed package evidence is under
`build/validation/nr-task9-calibration-aio-20261003`. No personal settings
or shader cache are packaged. No installation or game launch occurred.

The initial sandboxed validation stopped at vcpkg's external metadata write
permission. Its failed record is preserved separately; elevated validation
used a fresh evidence directory and completed successfully. The earlier
read-only pre-commit cache and environment-doctor failures also remain
recorded rather than being relabeled as successful checks.

## Cleanup and evidence boundary

Final native evaluations: 417,274; native failures, stereo failures,
device removals and quarantines: all zero. NR/cost/capture/profiler settings
were restored to initial fingerprint `4f9d26a4169698e79c873cf379f47728`.
Screenshot dispatcher/worker and native capture were inactive. AI remained
off as requested; no save was written. The exact Skyrim process exited
after console `qqq`, verified at `2026-10-03T21:34:02.3772263Z`.
The user's MO2 and SteamVR processes were retained; no lifecycle lease or
profile deployment was acquired by this test.

The replacement DLL needs one focused live check of bounded alternative
execution/cancellation, first-source identity rejection, one-shot search
and capture-independent compact admission. It must use the verified new
AIO and preserve the same camera/AI-off fixture. Production profile
promotion additionally requires the missing held-out/native-memory/quality
evidence; this record does not make that evidence a prerequisite to an
unrelated behavior-preserving structural task. Task 11's existing ownership
and packing audit remains valid; equipment rendering is not implemented
or claimed complete by this change.
