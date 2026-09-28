# Task 3A: explicit ROI roles

## Scope and source

Implemented on `main-vr-nr` from
`b80f0b2ca222855b6d6e667c3f6b991dce5254c5` in the primary checkout,
`C:/src/skyrim-community-shaders`. A targeted fetch confirmed that
`origin/main-vr-nr` still identifies this commit. The pre-existing scene
investigation edit, untracked documents, archives and `.xmake` are unrelated
and preserved. No worktree migration or historical merge was repeated.

The September 27 revised task list and audit identify Task 3A as the next
code change. Tasks 1 and 2 remain existing infrastructure. This change
implements the structural step and retains the corrected Task 3B
invariants; it does not promote the Task 3C context experiment or enable
overlapping inference. The initial source-only work was subsequently built
under the user's explicit DevBench AIO request; its producer is recorded
below. The [live checks and transition failure](nr-task3a-live-20260928.md)
separate sampled ROI validation from incomplete runtime qualification and
the subsequently requested transition fix.

## Contract and integration

[`RoiDescriptor`](../../src/Features/Upscaling/NeuralRendering/RoiDescriptor.h)
separates five roles in output-crop-local, half-open texel coordinates:

| Role                 | Current source and meaning                                                                                                                 |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `samplingSupport`    | Current conservative required enclosure, with the existing guard/alignment already applied; unavailable for the generic full-image adapter |
| `ownedOutput`        | Exclusive rectangle copied from a successful private output; final character selection still uses the exact authored mask                  |
| `inferenceContext`   | Dense rectangle passed to the native provider, including existing spatial padding and retained headroom                                    |
| `temporalEnvelope`   | Optional retained spatial envelope, including on C where native inference resets every evaluation                                          |
| `allocationCapacity` | Full output texture extent, independent of current support and evaluated area                                                              |

The descriptor has no independent source, frame or colour authority.
Character slots retain it under their existing preparation key: source
frame, generation, crop, settings, resource identities and raster phase.
Single-region preparation and finalization carry the descriptor to
`RendererApplyArgs`. Both geometry and GPU-mask split planning populate the
same region-plan descriptors. Split support includes the conservative
eligibility assigned to each disjoint provider; it is not actor-pixel
ownership or an exact selected-pixel count.

Renderer expansion validates each split descriptor against the existing
plan. Resource validation checks capacity, containment and agreement with
the legacy compute rectangle before native resource allocation. Character
evaluation requires prepared roles and current support. Invalid descriptors
use the existing validation failure/fallback path, never an empty proof.

The native call, work accounting and input/guide mappings consume
`inferenceContext`. Resource allocation consumes the validated capacity.
Raw output copies consume `ownedOutput`; colour preparation freezes that
same ownership rectangle in its existing observation, which governs
reconstruction and the colour output commit. Guide mapping continues to
use `MapComputeSubrect` with the existing `ViewportCrop` and motion-vector
conventions. Nonzero full-eye crop origins are not added to local texels.

The current adapter deliberately requires ownership and inference to equal
the existing provider rectangle. It also retains the disjoint-provider
batch admission check. A proposed separate or overlapping halo is rejected
before submission; this does not qualify the future overlap backend.
`computeRegions.count == 0` still means the legacy single evaluation.
Only the existing explicit `requiresEvaluation == false` proof selects
NoWork, which publishes no evaluation descriptors.

## Preserved behavior and evidence

No guard, padding, alignment, shrink/growth, history-age, allocation-size,
context-count, native-backend, shader or settings policy changed. Descriptor
construction never updates stabilization state. The existing shared single
envelope, nonblocking readiness, repeated-source reuse, C jitter correction,
full feather support, C native reset and successful-DLSS input transitions
remain in their existing production paths. SE/AE A+C and VR A/B/C remain
supported. Prepared colour, baseline, alpha, Lighting preservation, logical
filter boundaries and complete mono/stereo publication retain their
existing consumers.

Existing immutable execution/preparation records now serialize the roles,
their coordinate domain and distinct enclosure, ownership, inference,
envelope and capacity pixel counts. Existing texture records retain actual
logical capacity/work bytes; conceptual bounds are not extra allocations.
Exact mask coverage remains in the separately attributed optional GPU
readback. Pending or disabled coverage never becomes numeric zero through
the descriptor. Existing CPU/GPU timing and attempt/success/commit semantics
are unchanged, with no added clocks, queries or waits.

Offline delayed joins reject changed or missing ROI roles when either side
contains them, while retaining compatibility with older evidence that lacks
the fields on both sides. The existing DevBench output-schema description
documents the additive capture fields; no new action or setting was added.

The preset source inventory hashes the entire edited `Upscaling.cpp`.
Serialized settings keys, defaults, loading, saving and migrations are
unchanged. The reviewed fingerprint and generated compatibility metadata
were refreshed, preserving revision 8, the base and every tier setting.
Existing user-owned `.7z` archives were not regenerated.

## Validation

Initial source-only checks performed for this change:

-   `python tests/neural_color/transaction_evidence_test.py`: 29 passed,
    including new immutable execution/preparation ROI join cases.
-   `python tests/neural_color/source_contract_test.py`: 8 passed.
-   `pwsh ./tools/cmake.ps1 -P tests/neural_multi_roi_contract_test.cmake`:
    passed, including preparation, physical expansion and commit adapters.
-   `pwsh ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake`:
    passed with the native-area/history assertions updated to the new field.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check`: all three tiers
    verified after regeneration. A JSON comparison against `HEAD` confirmed
    that all three payloads are unchanged outside `Preset Compatibility`.
-   `pwsh ./tools/pre-commit.ps1 run --files @taskFiles`: passed for the
    explicit task file list, excluding the existing user investigation edit.
    Whitespace, line-ending, clang-format and Prettier hooks passed; YAML and
    Gersemi had no matching files. The first formatting pass normalized the
    renderer line endings and C++ formatting before the successful rerun.
-   `pwsh ./tools/git.ps1 diff --check`: passed.

An initial join-test fixture incorrectly reversed acquisition and companion
envelopes and failed the existing configuration check; the fixture now
removes roles independently from each valid envelope. The first DevBench
source check referenced the retired internal rectangle member and was
updated. The initial preset check rejected the expected source fingerprint
drift. These were precheck failures, not runtime observations.

Regression sources add descriptor containment/capacity rejection,
nonzero/odd crop mapping, unsupported halo/ownership separation, immutable
delayed evidence and NoWork accounting. Existing CPU/GPU planners' safety
checks now verify support containment and roles throughout their motion,
ready/pending/ready, same-source replay, shrink/growth and empty cases.
The new `roi_descriptor` controller test is registered alongside the
existing `compute_subrect`, `character_multi_roi` and `character_mask_roi`
tests. These C++ tests subsequently compiled and passed in the explicitly
requested test AIO build below.

### DevBench test AIO, 2026-09-27

The user explicitly requested an AIO from current `main-vr-nr` with the
DevBench bridge enabled. The universal Release DLL includes the uncommitted
Task 3A implementation and the three requested September 27 commit picks.
The first compile rejected local variable shadowing in the new multi-ROI
adapter (MSVC C4456 under warnings-as-errors). Renaming that local to
`eligibilitySupport` corrected the build without changing its behavior.

Producer identity, before this documentation update:

-   Source: `7c4effa2749bb8d3cf123818ad6cf06bdc03a7cf`, dirty.
-   Dirty digest: `b1fac7c3d17db45542f8130e4bb9b2789b2fdd8aeaebdd9145ad3eab298b99a6`.
-   Build ID: `ee833efd718c902575a8a0ccc13c3424ceb01b71f1df522b6e3cfcd5c0ceec76`.
-   DLL: 31,032,832 bytes; SHA-256
    `11859685c04181344fa352ccc968b4fc0c2be0f54691faf8f1ca01b59173039a`.
-   Options: SE/AE/VR ON, `DEVBENCH_BRIDGE=ON`, `TRACY_SUPPORT=OFF`,
    automatic deployment OFF; version `CSX 3.20.0-VR`.

`pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/nr-task3a-devbench-20260927-build2`
passed: DLL and both test groups built, all 206 CTests passed with zero
failed/skipped/disabled/unbuilt tests, preset regression and deterministic
checks passed, and DLL manifest/source/compiler verification passed. The
shader suite included 191 passing assertions. Failed first-build evidence
remains in the sibling `nr-task3a-devbench-20260927-build1` directory.

The build used cached dependencies after the sandbox blocked a dependency
refresh. Retained logs include the existing CMP0116 deprecation, D9025
assertion-configuration override and PeripheryTAACS X4000 diagnostics;
validation passed with those warnings. The shadowing fix also passed
file-scoped pre-commit checks.

`pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target Package-AIO-Manual -- /m:1`
passed, including its shader-test prerequisite. The internal test archive
is `dist/CSX_AIO-main-vr-nr-Task3A-DevBench-20260927-ee833efd718c.7z`:
210,739,360 bytes, SHA-256
`b2d3f8862632eeb727540968e113bf953ac65b65fcfe1dbf79daa7984f0ddc57`.
7-Zip integrity testing passed; all 385 extracted files matched staging
sizes and SHA-256. The extracted DLL/manifest matched the validated
producer. The included `nvngx_dlssnr.dll` matches the pinned local runtime
SHA-256 `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.

The adjacent `.receipt.json` and `.7z.sha256` retain these identities.
Complete packaging logs, inventories, extracted payload, producer diff and
new source files remain under
`build/validation/nr-task3a-devbench-20260927-package`. Earlier `dist` files
were verified unchanged. The previous AIO staging tree was preserved as
`build/ALL/aio-preserved-before-nr-task3a-20260927`.

This internal archive includes debugging symbols and the NR runtime, with
no precompiled shader cache or FOMOD. Native replay, deployment, game/HMD
testing and performance measurements were not run. Local validation does
not establish native quality, a performance gain or unchanged live output.
Task 2's A/B/C native cost tables remain unmeasured. No commit or push was
made.

## Continuation

The universal DLL and complete local test inventory now pass, including
`roi_descriptor`, `compute_subrect`, `character_multi_roi`,
`character_mask_roi`, `neural_rendering_pipeline_policy`,
`NeuralCharacterEvidence` and `NeuralTransactionEvidence`.

Runtime acceptance still needs the matched VR A/B/C and SE/AE A+C matrix,
current versus delayed bounds, repeated source frames, one/both-empty views,
colour/Lighting preservation and partial failures against the pinned
baseline. The separate
[Task 3C experiment](nr-task3c-current-context-20260928.md) now implements
current context versus historical headroom behind the default-off DevBench
control. Its requested DevBench AIO passed 211/211 local tests; authorized
native quality/cost evidence is still required before promotion.
Four-region capacity and shared transport
remain later tasks.

The [September 28 Bannered Mare continuation](nr-task3a-bannered-mare-20260928.md)
now supplies passing current-session VR A/B/C functional coverage, including
all six mode transitions, single/split descriptors and both-empty recovery.
This closes that bounded functional check, while matched baseline output,
performance, independent fault isolation and live SE/AE remain unqualified.
