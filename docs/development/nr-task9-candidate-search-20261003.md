# Task 9 bounded candidate search: offline completion

## Scope and status

The missing split/re-anchor implementation is complete. Task 9 can now
compare alternative actor/component partitions rather than only merging
the partition already selected by the geometric heuristic. This record
covers code and offline verification, not a new runtime cost qualification.
No game installation, launch, live measurement or cost-profile adoption
was performed for this change.

The experiment is compiled only with `DEVBENCH_BRIDGE_ENABLED` and remains
session-only and disabled by default through `communityshaders.nr_cost`.
Production retains the existing heuristic and two-region ceiling. Shared
partition traversal, resource-key finalization and source-owner matching
reuse the existing behavior; experimental snapshot/search/publication work
is absent from production preprocessing. This is not a measured FPS claim.

## Implementation

-   Immutable preparation snapshots carry CPU actor lifetimes and complete
    eligibility, or completed current-source GPU components and tile coverage.
    They are checked against source frame, generation, logical route and grid.
    Missing, stale or incomplete inputs retain keep/local-merge candidates.
    Candidate search does not poll or wait on GPU bounds.
-   The existing geometric partition traversal supplies alternative splits,
    including different memberships at the same count. Re-anchors retain the
    ordinary sampling guard, alignment, headroom and edge clamping. New
    candidates require the experimental 128-pixel context floor and complete
    guarded eligibility with disjoint output inside the prepared enclosure.
    Masks, category strengths, native cadence and source density are unchanged.
-   The search has a 16-wide beam, a 16,384-cut ceiling and a 2,097,152 budget
    for guarded coverage comparisons per eye. Two geometric representatives
    per count feed a maximum of 16 candidates per eye and 256 joint plans.
    Count limits, rejected cuts, coverage budget and pruning are reported.
    This bounded search does not claim an exhaustive global optimum; area
    ranks the geometric beam, never supplies estimated GPU milliseconds.
-   Unchanged memberships retain their history episode and physical bank.
    Changed groups get distinct identities. Final physical resource/history
    keys are regenerated after compact-capacity selection. Cost keys include
    reset/rebuild state, shared-source owner, formats, output-copy requirements,
    native footprints, tuning and exact backend/build identity.
-   Hypothetical validation cannot publish capture observations. Dwell state
    advances only with successful execution; capacity fallback clears the
    selected prediction. Logical routes keep separate selection state.
-   Successful execution publishes its actual output domains against the
    exact prepared-mask identity. Copy/composition consumes those domains;
    it cannot copy unwritten gaps left by a new split. Original mask support
    and cached preparation plans remain unchanged.
-   The registered DevBench description documents the complete contract.
    Preset source fingerprints were refreshed for that description change;
    rendering preset values did not change.

Unknown keys or unqualified/stale profiles still retain the labeled
heuristic fallback. Existing uncertainty, dwell, CPU/tail and resident
memory gates remain in force. No fitted profile or guessed GPU timing is
added. Zero explicit regions still means the legacy single evaluation;
only the separate complete NoWork proof can skip evaluation.

## Adversarial review and offline coverage

Review covered scope, coverage safety, current-source authority, temporal
identity, final physical cost keys, source ownership, output-copy domains,
failed execution, resource fallback, bounded work and production exclusion.
The implementation reuses partition traversal, spatial matching, ROI
validation, resource/history construction and source binding policies.
There are no new D3D resources, tracking registries or duplicate inference
calls for benchmarking.

New/extended regressions exercise alternate equal-count memberships,
counts one through eight, preserved banks/history, re-anchoring, guarded
coverage, edge clamping, deterministic actor ordering, malformed/stale
inputs, bounded dense coverage, unknown costs, capacity rejection and a
measured preference that differs from the area preference. Extracted
production accessors check fresh/stale output publication without changing
mask support. Source ownership is tested with the bridge both on and off;
preprocessing checks exclude experimental work from production.

## Validation and producer identity

`pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/nr-task9-search-final-20261003`
passed the universal Release DLL build (SE/AE/VR, DevBench ON), complete
controller/shader build, all **228/228** discovered tests (zero failed,
skipped, missing or disabled), preset generator tests, preset reproduction,
diff checks, DLL/manifest verification and unchanged-source verification.
The complete run took 458.88 seconds; CTest took 134.14 seconds.

| Field        | Exact compiled identity                                            |
| ------------ | ------------------------------------------------------------------ |
| Build ID     | `23397d0f9a2653102b34cd0567e5a287686011633b5c1d1c484347698c81f892` |
| Source base  | `916c861d32d7750cb40200bd77f263709e71225f`                         |
| Dirty digest | `d520ee17a357cde891025d8e6029a5f68a03572bbd8a21e2ed6d14c320f00c3a` |
| DLL SHA-256  | `9b035657eaad8965122666fe83e005c5e6d40dfe58605814fdcd835e57ec74bd` |
| DLL bytes    | `31736832`                                                         |

The DLL remains at `build/ALL/Release/CommunityShaders.dll`. These are its
pre-commit compile identities, not a claim that the base commit alone
contains this change. This documentation follows the validated build.
No AIO was packaged or installed in this offline-only step.

Local detailed evidence is under
`build/validation/nr-task9-search-final-20261003/` and focused checks under
`build/validation/nr-task9-search-20261003/`. The initial full run at
`build/validation/nr-task9-search-full-20261003/` stopped at a colour-route
extraction ordering check. The measured-search condition was folded into
the existing stereo-batch gate; the unchanged ordering check and complete
suite then passed. The initial preset fingerprint rejection is retained;
only source-contract metadata changed during regeneration. Scoped hooks
pass. Live NR execution, GPU cost calibration and SE/AE/VR game testing
were not run in this offline step.

## Remaining live qualification

1. Use this producer for the focused unpaused cost enable/status/disable
   and invalid/stale-profile rejection check left by the cost-command fix.
2. Verify current-source candidate attribution and actual output regions
   across mono/stereo routes, split/re-anchor transitions and fallback.
   Retain full images for seam, colour, temporal and stereo assessment.
3. Obtain complete matched baseline brackets and held-out observations for
   the published final keys. Include end-to-end preparation, copies,
   synchronization, composition, transition/reset costs, CPU critical path
   and native residency; report prediction errors and unavailable cases.

These are live qualification requirements, not missing split/re-anchor
code. Task 9's production calibration remains open. The prior Task 10
compact regression remains a rejected candidate; this change does not
promote it or implement Task 11.
