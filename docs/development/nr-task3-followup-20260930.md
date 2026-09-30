# Task 3 follow-up to the September 30 consolidated plan

## Scope and source

Source base: `main-vr-nr` at
`7982bcb34bae1cc8b129a93c66e4f9ddfc521901`; this record accompanies its
Task 3 follow-up implementation.
The supplied `CSX_main-vr-nr_all_tasks_consolidated_2026-09-30.md` describes
an earlier `b80f0b2ca` audit. Its instruction to begin Task 3A does not
supersede the completed implementation or the dated September 28 evidence.
The user requested the first proposed continuation step: reconcile the
new contracts, repair concrete gaps and add regressions while preserving
rendering defaults. This does not start Tasks 4–13.

## Implemented changes

`CharacterRendering::GetPreparedSelection` validates the existing frame,
source, generation, content serial and dimensions, then copies mask,
guarded mask bounds and compute plan under one existing mutex. It replaces
the separate prepared-mask, support-by-pointer and region getters. Main,
Submit, reduced-input and final-LDR consumers pass the copied bounds into
the final shader dispatch. Empty support remains zero; unknown bounds
remain conservative full extent. No second source identity is introduced.

This encodes CPU metadata coherence. It is not evidence of a reproduced
live race or permission to update a mask texture concurrently with its
consumer. The existing render-thread ownership and resource lifetime
remain required; a copied COM owner does not freeze GPU texture contents.

Stereo finalization previously accepted fresh empty result objects and
copied their null evidence pointers back into renderer arguments. It now
joins the retained preparation using the existing exact identity predicate,
including content serial, crop, policy, jitter, output grid and capture
epoch. A failed latest attempt cannot replace a successful retained
preparation during finalization. Invalid slots, wrong-eye records and zero
content serials cannot join prepared evidence. The additional lookup exists only with
`DEVBENCH_BRIDGE_ENABLED` and an active evidence capture. It adds no new
diagnostic allocation or production capture work.

## Preserved transition and ownership contract

| Concern                            | Baseline retained by this follow-up                                                                                                                                                                                                                      |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Semantic selection                 | Authored character mask and its existing category strength, exclusions and feather support. A rectangle is not exact occupancy.                                                                                                                          |
| Color reconstruction               | Actual prepared P and frozen baseline; the existing radius-one neighborhood clamps to the logical owned rectangle.                                                                                                                                       |
| Color-detail transition            | Existing four-texel edge weight on the detail residual. Lighting preservation, appearance mix and baseline alpha retain their current meanings. This is separate from semantic mask strength.                                                            |
| Final selection                    | Character/FOV selection remains after neighborhood reconstruction. Zero character weight avoids loading private native output.                                                                                                                           |
| Missing subject with retained crop | No adaptive policy switch. Existing zero/unknown preparation semantics remain authoritative; retained spatial extent alone does not select pixels.                                                                                                       |
| Insufficient adaptive margin       | Adaptive bands are not enabled. Use the unchanged baseline; do not introduce a minimum 16-pixel band, increase padding, or claim full-strength adaptive coverage. Existing current-context admission still falls back when guarded context does not fit. |
| Stereo                             | Each eye retains its own visibility, support and evaluation. A whole-frame fallback eye does not enlarge the other eye.                                                                                                                                  |
| Read halos                         | Ownership and context roles remain separate, but production providers remain disjoint. Overlap execution still needs Task 8 qualification.                                                                                                               |

There is no new transition setting or unused adaptive descriptor. Existing
owned rectangles and immutable color configuration already bind the fixed
reconstruction policy to its transaction. A future margin-aware policy
must add its qualified parameters to that same contract, including an
explicit insufficient-margin outcome. It must not derive blending bounds
from a separate latest-subject lookup.

No production HLSL, padding, region count, pixel density, native context
policy, allocation capacity, history or user setting changes. Source work
adds no GPU dispatch, copy, readback, wait or allocation. Consolidating
lookups removes locks and the support-by-pointer slot scan. Runtime cost
neutrality remains unmeasured; this change makes no performance-gain claim.

## Regression coverage and validation

Added `NeuralPreparedSelection` and `NeuralPreparedSelectionBridge` extract
the actual lookup, evidence and result-publication functions. Their shared
fixture changes the producer between consumer operations,
checks that earlier metadata and mask ownership remain coherent, rejects
stale identities, covers all four eye/route slots, and distinguishes zero
from unknown support. It also checks retained evidence versus a failed
latest attempt and policy/crop/jitter/grid drift, capture epoch and
bridge/capture-disabled publication. The mask stub counts COM references
to verify that a returned selection retains its mask after slot replacement.

The existing character shader harness now chains production ColorPrepare,
ColorReconstruct and final character composition. It uses a non-identity
color transform and exposure, actual stored P,
nonzero native edits, unequal detail/appearance contributions and three
Lighting-preservation values. A neighboring unedited pixel receives a
filter residual before final selection, then must remain unchanged in the
final output. It covers finite/NaN unused output, odd nonzero crops, both
eye offsets, baseline alpha and subject present/absent/reappearing with
the crop held. ROI descriptor tests also cover retained unknown support
and independent whole-frame eye fallback. Existing ready/pending,
source-reuse, jitter, insufficient context and overlap tests remain.

Source-only checks and their exact commands are retained in
`build/validation/nr-task3-followup-20260930/source-checks.json` and adjacent
logs. These include the multi-ROI, DevBench and Submit-pair CMake contracts,
production-function extraction, Python preparation/current-context
contracts, immutable transaction evidence tests, and actual MSVC
preprocessing of bridge on/off paths. Preprocessing creates no DLL or
object file. Scoped pre-commit checks cover this change only.

Final result: all nine source-check commands passed, including 35 Python
unit cases, three CMake contracts, two extraction scripts and bridge
preprocessing. Scoped trailing-whitespace, line-ending, clang-format and
Markdown formatting checks passed; YAML and gersemi hooks had no matching
files under the existing hook configuration. `git diff --check` passed.
Initial contract assertions still named the removed accessors; these were
updated to require the combined lookup and frozen composite bounds before
the final passing run. Failed attempts are retained beside the final logs.

Not run: C++ test compilation/execution, WARP shader tests, DLL/AIO build,
native replay, live SE/AE/VR image/performance tests or installation. The
new native regressions are registered for the next explicitly requested
build. September 28's 211-test and live results remain historical evidence,
not validation of this source change.

The sandbox prevented writes to the shared pre-commit cache; scoped hooks
used approved cache access. The environment doctor also reported sandbox
write restrictions on vcpkg caches, unavailable origin access and invalid
GitHub CLI authentication. These do not establish build or network health;
no build or remote mutation was attempted.

## Adversarial review

Review covered the complete follow-up diff and its Main/Submit, mono/stereo,
reduced-input and final-LDR consumers. The following findings were fixed
before committing:

-   The new shader fixture took the address of a WRL `ComPtr` through its
    overloaded `operator&`, producing a proxy rather than the intended
    smart-pointer address. It now uses `std::addressof` for the shader table.
-   The initial fixture exercised evidence lookup but not the final result
    publication that lost the pointer. A focused result builder now owns that
    publication and is extracted into bridge-on/off fixtures, including an
    empty destination, failed latest attempt, expired capture and changed
    content. Both targets share one generated-fixture dependency.
-   Evidence lookup now rejects invalid slots, wrong-eye records and zero
    content serials. This is defensive identity validation; no live mismatch
    was reproduced.
-   Composite resolution now moves the returned COM owner instead of taking
    another reference. The color fixture uses a non-identity transform so
    the prepare/reconstruct chain exercises the color adapter as well.
-   New shader-fixture resources reuse the existing extracted
    `Util::SetResourceName` helper and its shared CMake dependency; no
    duplicate naming implementation was added.

The shared color constant layout and existing preparation matcher are
reused. No adaptive feather, stereo dimension policy, extra ROI, overlapping
provider execution or production diagnostic capture was introduced.
Build, WARP, native-driver and measured-neutrality gates remain unrun under
the user's explicit build restriction. Review is not a substitute for them.

Post-review validation: all ten commands in
`build/validation/nr-task3-review-20260930/source-checks.json` passed. These
repeat the nine source checks above against the reviewed implementation
and additionally extract the existing resource-naming helper. The run
includes 35 Python cases; C++ fixtures are registered, not executed.
Scoped formatting and `git diff --check` passed. Commit-time hooks apply
only to the NR implementation, tests and this accompanying record.

## Continuation

After an authorized build, run the new C++/WARP regressions and the existing
controller suite before deploying. Task 2's comparable native A/B/C cost
matrix remains the direct next measurement of the unresolved cost-scaling
problem. Task 4 GPU-empty propagation can then proceed without assuming
that smaller nonempty rectangles already save proportional GPU time.
Adaptive transitions, stereo matching, overlapping read context and
production promotion of current context remain separately qualified work.
