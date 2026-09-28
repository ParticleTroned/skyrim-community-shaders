# Task 3C: experimental current context, 2026-09-28

Task 3C is implemented in source on `main-vr-nr`, based on
`7c4effa2749bb8d3cf123818ad6cf06bdc03a7cf` plus the preserved Task 3A and
lifetime changes. The subsequently requested DevBench AIO compiled and
passed all 211 local tests. The subsequent
[VR functional evaluation](nr-task3c-live-20260928.md) passed sampled native
contracts; image-quality and cost qualification remain pending.
The [Task 3 closeout](nr-task3-completion-20260928.md) adds a fresh-process
functional pass, native stereo sequences and bounded profiler samples.
Camera and timing drift leave the quality/cost comparison inconclusive;
poor cost scaling remains unresolved and the experiment remains off.
The user requested the next task part after the
[Task 3A functional continuation](nr-task3a-bannered-mare-20260928.md).
Task 3B's corrected jitter, feather and shared-envelope behavior already
exists and remains the baseline. This change does not resolve the earlier
driver hang or close its independent isolation work.

## Policy and integration

DevBench `nr_configure` accepts `experimentalCurrentContext`, default
`false`. The setting and every added runtime path are compiled only with
`DEVBENCH_BRIDGE_ENABLED`. It is session-only, excluded from saved settings
and reset to false by the NR configuration reset. Status reports the
requested value in `characterRendering.settings`; the registered action
description, input schema and output schema include the control.

The experiment applies only to C (`reduced_resolution`), with character
selection enabled, multi-ROI disabled, the authored mask and debug view
off. The existing `outputIsJittered` preparation contract identifies C's
render-grid path for both VR and mono SE/AE. A/B keep the baseline policy.
The default-off control may remain selected while another mode is active,
but cannot tighten that mode's inference context.

After the existing planner updates its shared single-region envelope,
`ApplyCurrentContextExperiment` derives a current candidate from proven
sampling support using `BuildCharacterProviderComputeSubrect`. This keeps
the existing spatial padding and alignment; resetting native history does
not imply that the model needs no spatial context. The candidate must fit
the allocation and the retained envelope. Unknown support, unavailable
proof, diagnostic masks, split plans and containment failures keep the
original descriptor unchanged.

The applied descriptor carries these separate roles:

-   Sampling support remains the guarded conservative enclosure.
-   Owned output and native inference context use the current candidate.
-   Temporal envelope retains the baseline historical headroom.
-   Allocation capacity retains the full existing resource extent.

Preparation publishes the candidate as the actual provider rectangle.
Existing admission, native evaluation and private-output commitment consume
that same descriptor. Baseline envelope history is not mutated by the
experiment and is still aged once per source preparation. C continues to
reset native history for every evaluation. Resource ownership, context
count, backend, synchronization, shaders and overlap restrictions are
unchanged.

The setting participates in preparation/settings keys and the captured
configuration fingerprint. Frozen ROI evidence includes `contextPolicy`:
`experimental_current_required` when applied, or `retained_envelope` for
the baseline. The requested flag alone is not proof of application or of a
smaller rectangle. Existing exact transaction joins compare the complete
ROI record, including this policy.

## Validation

Source-only evidence is preserved under
`build/validation/nr-task3c-source-20260928/`. The pre-3C source snapshots,
incremental diff and SHA-256 inventory distinguish this experiment from
earlier uncommitted work.

Passed:

-   `python tests/neural_current_context_bridge_gate_test.py --compiler "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe" --baseline build/validation/nr-task3c-source-20260928/before`:
    preprocessing removes the experiment from all ten affected source
    units when the bridge is disabled. Their remaining tokens match the
    pre-3C snapshots. This check strips includes and emits no object file;
    it does not establish that a complete DLL compiles or measure cost.
-   `python tests/neural_current_context_contract_test.py`: three schema,
    configuration and preparation/evidence source checks.
-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_multi_roi_contract_test.cmake`.
-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake`.
-   `python tests/neural_color/transaction_evidence_test.py`: 29 tests.
-   `python tests/neural_color/source_contract_test.py`: eight tests.
-   `pwsh -File ./tools/generate-unified-presets.ps1 -Check`: all three
    preset tiers verified. The source-review script separately confirms
    that their setting payloads changed only in compatibility metadata.
-   File-scoped `tools/pre-commit.ps1 run --files ...` and
    `tools/git.ps1 diff --check -- ...`: passed on the Task 3C files.

The new `NeuralCurrentContext` C++ regression target covers default-off,
A/B, uncertain support, diagnostic and multi-ROI exclusions; current versus
retained bounds; odd extents and edge coverage; invalid descriptors;
containment fallback; and repeated application without aging history.
It was initially registered without compilation and subsequently compiled
and passed in the authorized build below. The two new Python checks are
also registered with CTest and passed in that complete run.

Preset contract revision 8 and all three preset setting payloads remain
unchanged. Generated compatibility metadata reflects the reviewed source
fingerprint
`CAFADE36CC3E111F9BE48B60E1EF3312BAAA7928E0F3213AD0014941DB71586A`.
The initial preset precheck rejected the expected old fingerprint; its
failure receipt is retained separately from final validation.

The initial source-only phase did not build, install, restart or change
live settings. The user subsequently requested the DevBench AIO below.
No commit or push was made. The previous 208/208 compiled result and
installed Build ID `490462394cae` belong to the earlier lifetime build,
which does not contain this change.

## Authorized DevBench AIO

`pwsh -File ./tools/validate-local.ps1 -OutputDirectory build/validation/nr-task3c-devbench-20260928-build1`
passed in 535.79 seconds. The universal SE/AE/VR Release DLL compiled with
DevBench ON, Tracy OFF and automatic deployment OFF. All **211/211 CTests**
passed, with no failures, skips, disabled tests or missing executables.
This includes `NeuralCurrentContext`, `NeuralCurrentContextBridgeGate` and
`NeuralCurrentContextContract`. ShaderTests passed all 191 assertions.
Preset regression/verification, diff checks, DLL/manifest verification and
the unchanged source-snapshot check also passed.

The compiled producer, before this documentation update, is:

-   Build ID: `40689ffbf63b58479b81ee74ef687137fabf3c2c87585307526f4e980bf919f1`.
-   Source: `7c4effa2749bb8d3cf123818ad6cf06bdc03a7cf`, dirty digest
    `bb3ce892f02f620e4410febc3842bb2efa0f99e647d06f955330dcfdc16eff0a`.
-   DLL: 31,068,160 bytes, SHA-256
    `31f6ac810d437531a34cec5a67e47e7770d6ab2f08994d13f6d7683ba3a1ff76`.

The test archive is
`dist/CSX_AIO-main-vr-nr-Task3C-DevBench-20260928-40689ffbf63b.7z`:
198,968,018 bytes, SHA-256
`20c6391dbbe944013a33efa7c2a3f6de7724ab9048ee507fefb7765c47644862`.
The maintained CMake `AIO` target staged the package; 7-Zip created the
archive. Integrity testing passed and all 385 extracted file sizes and
hashes match staging. The DLL and manifest match the validated producer,
including DevBench ON and the Task 3C control/applied-policy markers.
The pinned NR runtime is included. Shader caches and a FOMOD are not.

The adjacent `.receipt.json` and `.7z.sha256` retain package identity.
Full packaging logs, extracted payload, source inputs and inventory are in
`build/validation/nr-task3c-devbench-20260928-package/`. Its `prior-aio`
and `prior-release` directories preserve earlier outputs; all 25 existing
distribution files remained byte-identical. The initial packaging helper
stopped because the preserved staging directory no longer existed; creating
the new workspace staging directory corrected that precheck before the
successful package run. The validated DLL did not change.

Validation logs retain the existing CMP0116 deprecation, expected negative
runtime-payload test warning, and shader X3556/X4000 diagnostics. These did
not fail validation. No installation, restart or live test was performed.
The current-context experiment remains disabled by default.

## Live evaluation and remaining validation

The user installed the AIO and requested continued evaluation. The
[live record](nr-task3c-live-20260928.md) preserves 45,069 successful native
region evaluations with zero NR faults, sampled context reduction and
allocation stability, exclusions, empty/recovery and settings restoration.
The profiling prerequisite tool was unavailable; no timing benefit or
matched visual equivalence is claimed. This completes the sampled VR
functional check without promoting the default-off experiment.

The user controls installation and restarts. Future qualification must
verify the producer identity above before
using `nr_configure` with C, `characterEnabled: true`,
`experimentalMultiRoi: false` and `experimentalCurrentContext: true`.

Compare default-off and opt-in runs with matched sources and settings.
Check current/retained rectangle evidence, unknown-bounds fallback,
ready/pending/ready changes, same-source reuse, motion and shrink/growth,
one/both-empty views, mode changes, native failures and mono/stereo output.
Native quality, colour/Lighting, temporal/stereo consistency and cost remain
unqualified. No production promotion or performance gain is claimed.
Task 3D retains the existing independent ownership fields and disjoint
provider restriction; overlapping native evaluations remain a later task.
