# VR camera upload observation repair

The HMD colour attempt in `nr-colour-hmd-attempt-20260916.md` stopped
because camera evidence froze at source frame 103983 after the first native
screenshot. The game continued rendering newer frames. Strict attribution
correctly rejected those images.

Read-only inspection of PID 31920 with its matching original DLL/PDB
confirmed that the immediate context's active Map and Unmap entries no
longer pointed to the functions detoured during initialization. Inspection
did not suspend, inject into or modify the process. Evidence is retained in
`build/validation/nr-camera-fix-20260916/live-map-hook-inspection.json`.

The Upscaling creation hook omitted the durable context protection used
by the ordinary creation hook. The screenshot worker enabled protection
later, replacing D3D11 dispatch entries. A WARP regression reproduced the
result: startup method detours captured only one of 64 buffer updates.
Enabling protection earlier alone was insufficient: the driver can replace
protected entries during subsequent buffer use too.

## Correction

VR now also observes the engine's frame-buffer upload immediately before
Unmap. The observer copies the same CPU bytes the engine just uploaded,
checks the current context, per-frame resource and subresource, and rejects
a null Map pointer. It clears the supplementary mapped pointer before calling
the current virtual Unmap method. Camera evidence retains its real source
frame; no stale matrix is relabeled.

The hook is restricted to Skyrim VR 1.4.15 and the address-library function 75472. It verifies the Map-result load, source-copy setup and complete
six-byte Unmap call sequence before patching. Its small assembly adapter
passes the verified stack source as the fourth argument, retaining the
original return address and Unmap arguments. Unexpected instructions leave
the original engine code intact and log an error; existing D3D observations
remain supplementary. SE/AE keep their existing buffer observation path.

The correction does not enable multithread API protection or acquire a new
rendering lock. It preserves the intent of
`5e9cd203876e8e610f756ad1a2f6b98f6f56cdb5`: auxiliary context access can use
native renderer ownership without permanent per-call locking. The camera
observer belongs on this NR branch and can be ported independently of device
creation, screenshot ownership and water-flowmap changes. It reuses the
existing frame-buffer cache and camera recorder, without changing colour
math, defaults, render-scale behavior or introducing another cache.

## Validation scope

The WARP regression executes the production assembly adapter with the
validated engine stack layout and all 1,392 camera-buffer bytes. Four
64-frame lanes cover disabled protection, existing protection, late enable
and repeated external toggles. All 256 snapshots were fresh and complete;
each lane also rejected a null Map pointer without publishing another
snapshot. The observer preserved the protection flag in every frame.
The legacy control reproduced one capture out of 64.

The review removed the proposed creation and flowmap protection changes
because they conflicted with `5e9cd203` and were unnecessary for this fix.
It also made installation idempotent and clears the supplementary mapped
pointer on null upload evidence, preventing a second observer from reusing
it. Source contracts cover the VR signature gates and observer ordering.
The adapter uses only volatile registers, tail-calls the observer with the
original return address, and leaves Unmap on its current virtual dispatch.

Validation in Release:

-   `ctest --test-dir .tmp/hmd916-tests-devbench -C Release
--output-on-failure`: 11/11 passed, including `D3DCaptureHooksWARP`.
-   `ctest --test-dir .tmp/hmd916-tests-color -C Release
--output-on-failure`: 19/19 passed, including 31 image assessment tests,
    22 capture workflow tests, seven source contracts and the shader/WARP
    regressions.
-   Scoped formatting and `git diff --check`: passed.

These test build directories are under the outer repository; both are
configured exclusively against the NR task checkout. JUnit results and
the exact capture regression output are retained beside the inspection.

Build, test and archive receipts belong in the evidence directory above.
The replacement passed live native stereo camera attribution after
installation: the initial 24 valid scout pairs and all 36 baseline pairs
retained fresh source-frame matrices. Later fixed-pose and outdoor captures
also retained fresh matrices. The
[live retest](nr-colour-hmd-retest-20260916.md) records the exact producer,
physical DLL verification, stationary-scene variation and camera-tolerance
limitations separately from the repaired upload observation.
SE/AE live execution and target-branch integration are not claimed. This
repair adds no renderer-ownership policy or performance qualification.
