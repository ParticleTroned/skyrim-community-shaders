# Capture-owned NR diagnostics

An HMD screenshot can outlive the rolling NR measurement and exposure
histories. Polling those histories after PNG encoding could lose exact
same-frame evidence even when the image and its frozen identities were
valid. Accepted captures now retain only their exact CPU companions until
the request reaches a terminal state.

The change extends the existing opt-in `captureFrameEvidence` path.
Private measurements still require `diagnostics`; engine observations
still require `captureEngineExposure`. Existing render-thread GPU
readbacks publish into bounded capture-owned snapshots. No additional
GPU work, polling, waits, context protection, render settings or shader
changes are introduced.

`actual.captureDiagnostics` is finalized before a request's terminal
receipt and sequence-manifest child. It contains the acquisition
`transactionId`, exact exposure lookup results, complete private batches,
and a request/result entry for each expected measurement batch. Missing,
incomplete, invalid or capacity-exhausted results remain explicit.
The original acquisition and its pending exposure stamps are unchanged.

The shared retention utility owns only weak references. Active captures
hold immutable-key leases; publishers update their CPU values under short
locks and consumers copy them under the lease lock. At most 32 distinct
keys per publisher are retained. Capacity exhaustion fails the additional
evidence request without evicting an existing owner or preventing PNG
output. Terminal transitions release leases on normal completion, failure,
cancellation and shutdown. Completed JSON remains in the ordinary receipt
and saved manifest after all leases are gone.

Batch assembly reuses the existing membership checks. A mismatched revision,
generation, frame, insertion or slot cannot supply another batch's missing
eye. Exposure completion reuses exact identity and source-resource checks;
ambiguity survives later completion and rolling-history replacement.
Capture ownership does not change rendering or infer an exposure binding.

The canonical importer reads transaction-matched per-capture companions.
The controller capture runner uses them directly and polls exact exposure
stamps only for older captures without embedded diagnostics. Both live
transport lanes receive the same saved evidence.

Finalized exact companions take precedence over earlier valid acquisition
stamps, so later ambiguity or unavailable data cannot disappear. Each
capture keeps its own companion results; legacy lookup results remain
separate even when old and new captures reference the same exposure stamp.

The implementation belongs to the CSX task branch because the runtime
histories, screenshot service and canonical colour tools live there.
It does not change the VR automation repository or require a plugin/cache
rotation. The current installed AIO remains in place; exercising the new
runtime path later requires a DLL built from this change.

## Validation

The focused regressions cover pending capture ownership, delayed completion
after history eviction, exact-key rejection, ambiguous/retired exposure,
capacity exhaustion and reuse, terminal release, immutable saved evidence,
concurrent CPU publication, malformed metadata, and importing without a
live diagnostic lookup. The capture assembly test uses the production
factory and histories with CPU publisher and serializer substitutes.

Passed on 2026-09-17 from the task worktree:

-   `pwsh tools/cmake.ps1 --build <root>/.tmp/hmd916 --config Release --target CommunityShaders`
    built the universal SE/AE/VR DLL with DevBench enabled. Packaging and
    automatic deployment were disabled. The validation build was based on
    `00e365e42cd2c93cd99e50efedcb9bab9fb94ca1` plus this working diff;
    it is not the installed game DLL.
-   `ctest --test-dir <root>/.tmp/hmd916-tests-color -C Release --output-on-failure`:
    19/19 checks passed, including the measurement/exposure retention and
    existing WARP shader checks.
-   `ctest --test-dir <root>/.tmp/hmd916-tests-devbench -C Release -R Screenshot --output-on-failure`:
    6/6 checks passed, including capture companion assembly/finalization.
-   `python tests/neural_color/test_hmd_assess.py`: 33 tests passed with the
    task's image dependencies available through `PYTHONPATH`.
-   `python tests/neural_color/test_hmd_capture.py`: 24 tests passed.
-   Scoped `pwsh tools/pre-commit.ps1 run --files <changed files>` and
    `git diff --check` passed after formatting.

Logs and validation-build provenance are preserved locally under
`build/validation/nr-capture-retention-20260917`. Live game validation of
this retention change was not run: the user requested keeping the current
AIO. SE/AE/VR compilation does not establish live runtime behavior.

The reviewed diff leaves the render ownership and D3D11 protection paths
of `5e9cd203876e8e610f756ad1a2f6b98f6f56cdb5` unchanged. Capture snapshots
introduce only CPU locks; no GPU resource or context operation takes place
under a new lock.

## Adversarial review

The review reproduced and corrected two importer defects with failing
regressions before the fixes: an earlier valid stamp bypassed a later
ambiguous companion, and flattening owned results into legacy lookups
duplicated same-stamp matches. Finalized matching evidence is now
authoritative; the legacy lookup collection contains only legacy results.

Capacity exhaustion now has its own measurement reason, distinct from an
invalid key. Added checks cover all 32 occupied entries, capacity reuse,
overlapping capture owners, NR-off empty regions, diagnostics disabled,
malformed metadata, and concurrent whole-record publication. The shared
retention utility and existing measurement/source checks avoid parallel
lifetime or identity-validation implementations.

The review found no remaining code issue within the capture-retention
scope. In-game integration and delayed-readback availability at actual PNG
completion remain untested; those limitations require a later build/test
session, not changes to rendering or to the current AIO.
