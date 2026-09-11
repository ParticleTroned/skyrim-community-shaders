# PR73 owned provider drain and manual-test candidate

The new PR73 candidate separates provider drain observation from destructive
render-scale application. It follows the rollback `bc077786d`, which restored
the measured `c73bae9a7` implementation. The six-frame presentation settling
guard and its existing eligibility rules remain unchanged.

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

The exact command, argument lists, logs and source hashes are preserved in
`artifacts/pr73-owned-drain-20260911/controller-test-results.json`, with
JUnit results in `controller-tests.xml`. From the main repository root:

```powershell
python artifacts/pr73-owned-drain-20260911/run-controller-tests.py
```

The universal Release DLL build, DevBench-enabled AIO packaging and archive
identity checks are pending. No archive or new producer identity is claimed
by this preparation record. Automatic deployment and runtime testing have
not run; the requested package is for manual testing.

Manual validation should cover both NVIDIA passes, especially rows 26 and
28 and the retained longer-stretch cases on rows 15, 18 and 19. Delayed drain
completion, supersession and incomplete stereo completion need runtime
coverage as well as the portable policy checks. Preserve the exact DLL
manifest and capture evidence when collecting new results.

The historical measurements in PR73 still belong to PR66 `a09e1cc77`,
previous PR73 `d9780bb74` and measured PR73 `c73bae9a7`. They do not validate
this candidate. The [canonical comparison ledger](vr-render-scale-comparison-ledger.csv)
and its existing numeric cells are unchanged because this preparation adds
no runtime measurements. No performance improvement is claimed. The
separate `csx-render-scale-pr-v1` qualification remains unrun.
