# PR73 owned provider drain and manual-test candidate

The new PR73 candidate separates provider drain observation from destructive
render-scale application. It follows the rollback `bc077786d`, which restored
the measured `c73bae9a7` implementation. The six-frame presentation settling
guard and its existing eligibility rules remain unchanged.

Implementation source: `269bded159c66f4d8b1a45a836078dfa6cdf3241`.

## Why

The retained PR66/PR73 comparison contains four longer-stretch cases: row 15
pass 2, row 18 pass 2, row 19 pass 1 and row 26 pass 2. An initial provider
drain `Pending` result in these PR73 observations caused a Backend retry
before replacement and retained conservative settling afterward. The
recordings do not establish when that first fence actually completed.

The withdrawn `cf1616728` attempt shortened the retry of the entire relatch
transaction. Its subsequent NVIDIA run crashed at row 26. That transaction
could perform cleanup and target replacement through `State::Draw`; changing
its scheduling did not establish a safe boundary for those operations. The
exact invalid-pointer cause remains unproven.

This candidate observes an owned drain between attempts and moves explicit
settings relatches to the completed native stereo boundary. It does not
claim that the six-frame guard caused the crash or that a shorter wait alone
can restore safe resource ownership.

## Drain and commit contract

Owned observation applies only when the existing readiness predicate
requires a Backend retry. Requests eligible for readiness deferral retain
that path and its settling eligibility.

-   An operation belongs to one immutable transition epoch, source and
    target generations, device and provider resource revision. FSR and DLSS
    retain separate completion proofs. Further provider use or ownership
    mutation invalidates the relevant proof; cancellation or supersession
    clears the operation.
-   Dedicated provider fences establish the work being observed. Their
    observation phase performs no provider teardown, shared-resource
    cleanup, render-target creation or presentation promotion. Completed
    evidence remains available until the matching commit consumes it.
-   A pending operation retains the existing six-frame Backend retry and
    its accounting. Once per completed native stereo frame, readiness can
    admit only that operation's exact queued six-frame retry early. Other
    retry schedules and admission conditions remain authoritative.
-   The observation budget is the original six-frame pending window. If
    readiness remains pending at its end, or the proof fails, the operation
    is disabled for that epoch and execution returns to the conservative
    path. Device loss does not admit destructive work.
-   Commit requires the owning native stereo call to have returned, both
    distinct eye calls to have completed, and unchanged pair token,
    compositor cycle, frame and thread identity. Incomplete, duplicate,
    nested or changed boundaries cannot authorize the commit.
-   Successful shared cleanup is recorded for the operation and retained
    across its subsequent service. Incomplete cleanup still obeys retirement
    backpressure. Provider drain completion does not satisfy independent
    shared-resource retirement fences or memory admission.

The current APIs may establish their dedicated query or queue fence during
initial observation. Read-only polling refers to observing provider
readiness without repeating provider or shared-render-resource mutation;
it does not mean the initial fence setup issues no GPU work.

The guard still starts from the existing post-replacement point. Backend
history remains Backend history, including when an owned drain finishes
early. No wait is reclassified to bypass settling. SE/AE retains the
existing provider lifecycle path; the completed stereo callback applies
only to VR.

## Diagnostics

DevBench retry events retain frame and QPC timestamps for drain begin,
pending, provider readiness, invalidation, commit begin and shared cleanup.
Existing guard and promotion events remain available. Together these
distinguish provider readiness, scheduler delay, resource mutation and
post-replacement settling. They do not create a new measured performance
result without a completed runtime capture.

## Validation

Passed: nine focused CTests against the modified publication worktree,
compiled with MSVC `19.51.36252.0`, C++23, `/W4 /WX /permissive-`:

-   `VRRelatchDrainPolicy`
-   `VRRenderScaleFrameBoundaryPolicy`
-   `VRVendorRelatchPolicy`
-   `FSRHostLifecyclePolicy`
-   `FSRRuntimeLifecyclePolicy`
-   `VRSubmitInputFreshnessComposition`
-   `VRSubmitStereoBatch`
-   `StreamlineFrameTokenPublication`
-   `VRPresentationStretchTelemetryPolicy`

Configure, build and CTest exited zero. Source and included-header SHA-256
values matched before and after the check. This verifies portable policy
behavior; it does not execute D3D resource lifetime or native stereo hooks.

Passed: production syntax checks with universal SE/AE/VR enabled and
DevBench disabled, using MSVC `/Zs /W4 /WX`, for `Upscaling.cpp`,
`FidelityFX.cpp`, `Streamline.cpp`, `VR/InSceneOverlay.cpp` and
`VRRenderScaleDevBenchBridge.cpp`. Existing PCH-independent header warning
exclusions were retained. `Upscaling.cpp` was rechecked after the final
Backend-only eligibility correction and formatting; its monitored source
hashes remained stable during compilation. The combined results and exact
response files are in `artifacts/pr73-owned-drain-20260911/syntax/`.
The full DevBench-disabled DLL link and live SE/AE scenarios are unrun.

Passed: pinned clang-format `22.1.4` over changed ranges in existing C++
files and complete new C++ files, plus scoped whitespace, line-ending and
Prettier hooks. Whole-file legacy C++ formatting was skipped to preserve
unrelated code. Whole-file Gersemi was not run; the 12 added CMake lines
were checked against adjacent test registrations. Exact commands and
results are in `formatting-report.json`, `formatting-upscaling-final.json`
and the scoped pre-commit receipts in the same evidence directory.

The exact command, argument lists, logs and source hashes are preserved in
`artifacts/pr73-owned-drain-20260911/controller-test-results.json`, with
JUnit results in `controller-tests.xml`. From the main repository root:

```powershell
python artifacts/pr73-owned-drain-20260911/run-controller-tests.py
pwsh -NoProfile -File artifacts/pr73-owned-drain-20260911/syntax/compile-submit.ps1
```

Passed: clean universal Release DLL and `Package-AIO-Manual`, with
`DEVBENCH_BRIDGE=ON` and automatic deployment disabled. Shader unit tests
passed: 163 assertions in 1 test case. Staged and archived DLL, PDB and
manifest identity checks passed, as did `7z t` and inspection of all
435 archive entries. No prebuilt shader cache or FOMOD is included.

-   Build ID: `9b8b7f17af7ec7012eb194ad945b45756db313352ef6b2ec84d88a50bef9247b`.
-   DLL SHA-256: `086b2207bd4b615af5f7a1bb4f567bb0dba8e1c1ec5e5498cd440c623682380a` (28,184,576 bytes).
-   AIO: `CSX_AIO-3.19-VR-mainVR-ef7c366d-PR73-269bded1-DevBench-no-cache.7z` (89,639,416 bytes).
-   Archive SHA-256: `614edd3a3ca1f217103b87d29de2ea95621192608dcc2f6d9e539dd440817e5f`.

Recorded build/configure warning: FidelityFX SDK CMake CMP0116
deprecation warning.

The exact producer manifest, package hashes, commands and stage timings are
preserved in `artifacts/pr73-owned-drain-20260911/build-receipt.json`.
The delivered archive has an adjacent receipt and SHA-256 file in local
`dist/`. Automatic deployment and runtime testing have not run; this
archive is prepared for manual testing.

Manual validation should cover both NVIDIA passes, especially rows 26 and
28 and the retained longer-stretch cases on rows 15, 18 and 19. Also check
that None/TAA-to-vendor requests complete and that opening or closing menus
and changing native-AA profiles continues to produce the completed stereo
callback. Record any stalled request and its last valid callback.
Delayed drain completion, supersession and incomplete stereo completion
need runtime coverage as well as the portable policy checks. Preserve the
exact DLL manifest and capture evidence when collecting new results.

The historical measurements in PR73 still belong to PR66 `a09e1cc77`,
previous PR73 `d9780bb74` and measured PR73 `c73bae9a7`. They do not validate
this candidate. The [canonical comparison ledger](vr-render-scale-comparison-ledger.csv)
and its existing numeric cells are unchanged because this preparation adds
no runtime measurements. No performance improvement is claimed. The
separate `csx-render-scale-pr-v1` qualification remains unrun.
