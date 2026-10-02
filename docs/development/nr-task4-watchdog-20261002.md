# Task 4 B-to-C watchdog investigation

## Follow-up qualification

The [subsequent Task 4/5 record](nr-task4-5-qualification-20261002.md)
preserves the healthy C/mode-transition and asymmetric-eye results,
clean shutdown, remaining live gaps, and the early-guide correction.
The prior incident below remains historical evidence; its exact GPU
cause is not inferred from the later successful run.

The B-to-C watchdog remains an unqualified runtime failure. Source review
found and corrected an unsafe DevBench transition path, but the preserved
crash evidence cannot prove that this race caused the GPU timeout. A fresh
game test of the corrected producer is required before closing the failure
or Task 4.

## Preserved failure

The [Task 4 record](nr-task4-empty-proof-20261002.md) retains the complete
live campaign and installed producer `95c9d742f744`. The relevant evidence
is `build/validation/nr-task4-live-20261002/`:

-   `c-mode.payload.json` reports an accepted reduced-resolution request and
    successful backend retirement. Its last lifetime record is retirement
    sequence 8214, source frame 35202, following the B batch for slots 2/3.
    Before retirement, the shared fence had issued 16424 and completed 16421.
    Successful retirement reports cleared ownership; this is not a GPU fault
    trace of the subsequent C execution.
-   `CommunityShaders.log` records the new native runtime initialization,
    creation at 1008x1120 and a successful CPU return from evaluation at
    `(192, 0, 768, 1120)`. Runtime create/evaluate logging is once per backend,
    not once per eye. The last slot-2 line cannot identify slot 3 as the fault.
-   Windows reports NVIDIA event 153 at 16:42:17 local time, followed by
    LiveKernelEvent 141 and Skyrim/vrcompositor failures. The next status call
    timed out. No further mutations were dispatched.

There is no recovered GPU instruction trace establishing the failing
dispatch. The protected WATCHDOG dump was inaccessible to the ordinary
workspace process. The source and event evidence support investigating
transition ownership; they do not establish a driver/model defect or prove
that Task 4's empty-proof predicate caused the hang.

## Confirmed unsafe path and correction

`nr_configure` previously dispatched an SKSE task, assigned settings,
invalidated frame state and called `Renderer::Reset()` without owning the
native renderer transaction. SKSE task affinity is not graphics ownership:
the preserved log shows task-queue threads differing from the render
thread. NR's mutex excludes another NR call but does not exclude ordinary
engine/DLSS immediate-context work.

Reset reaches `D3D12Interop::WaitForIdleLocked()`, which issues D3D11
`Flush` and `Signal` calls before retiring native resources. A shared
immediate context requires serialized access, as described by
[Microsoft's D3D11 threading contract](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro).
Settings publication and frame invalidation also need to belong to the
same protected transaction, rather than locking only the final reset.

The DevBench bridge now uses the existing `GetRendererContextLock` and
`RendererOwnership` utilities around the entire task callback for:

-   `nr_configure`, `nr_cycle_modes` and `nr_reset`;
-   `foveation_configure` and `foveation_cycle`, which also invalidate NR
    frame state and can invoke the same transition handler.

Ownership is try-only. Contention returns `renderer_busy`; missing or
mismatched renderer/context ownership returns `renderer_unavailable`.
Both return `ok=false` and `mutationApplied=false` before reading or
changing settings, resetting history or touching backend resources. A
rejected callback is discarded, so it cannot apply later. The existing
SKSE timeout/in-progress distinction is preserved; ambiguous timeouts are
not permission to replay a mutation.

The correction is entirely behind `DEVBENCH_BRIDGE_ENABLED`. It adds no
production render-loop lock, wait, GPU command, telemetry or shader change.
The renderer lock is acquired before the NR mutex, matching native render
ownership. It is recursive for calls already on the renderer owner and is
released on exceptions. SE/AE/VR use the same established context/renderer
identity check. The registered tool description and action schema describe
the new rejection contract.

## Regression coverage and review

`NeuralRendererOwnership` compiles the actual bridge dispatch functions and
the production ownership helpers. It checks a held renderer lock on a
second task thread, missing/mismatched ownership, exclusion during an
accepted callback, recursive ownership, exception release and cancellation
of an SKSE task that runs after its deadline. Extraction also verifies all
five transition entry points use the protected dispatcher.

The adversarial review focused on ownership of the complete mutation,
lock order, contention without blocking the task queue, late application,
exception cleanup and reuse of the existing cross-runtime lock utility.
No inference tuning, reset-policy workaround, global D3D11 call protection
or production instrumentation was added. The standalone native replay
does not reproduce concurrent engine/DevBench ownership and therefore
cannot substitute for the in-game transition test.

## Validation and remaining runtime verification

Full offline validation passed with:

```powershell
pwsh -NoProfile -File tools/validate-local.ps1 -OutputDirectory build/validation/nr-watchdog-offline-20261002
```

The universal SE/AE/VR Release build and **219/219 tests passed**, with no
missing, disabled, skipped or failed checks. Preset checks, source
stability and physical DLL/manifest verification also passed. DevBench is
on; Tracy and automatic deployment are off. The focused ownership test
also passed before the full run.

Producer Build ID:
`60ed4a06ee15c8b1a0ef8373d13042e39c30c1d3c23eacc1a2568d6f8bef43b5`.
Compiled source is `694c64716792db45cfe35538d7ff8caab00571e2` plus the
reviewed correction, dirty digest
`ff5440ffa6b393f61f08b162c8c1791bfa08911c670ed85d43826be7e099ef95`.
DLL SHA-256:
`854ab6cea3aa69815702aae75802efdeca0452e2da2f2b0a407f5713ef9583d4`,
31,217,152 bytes. These final reporting paragraphs postdate the build;
the exact compiled changed-source snapshot is retained with the AIO
evidence. Local feedback receipt: `AUTO-20261002-154331902-681E9251`.

Prepared testing archive:
`dist/CSX_AIO-main-vr-nr-NR-Ownership-DevBench-20261002-60ed4a06ee15.7z`.
Its 199,031,050 bytes have SHA-256
`dda8393838037788225ecdb1ee923807ae2af902a0696bfa2b080a5c5ddb7cb9`.
Archive integrity passed, and all **385/385 extracted file sizes and
SHA-256 hashes** match staging. The extracted DLL/PDB/manifest match the
producer. The NR runtime remains the pinned 310.8 payload; no shader cache
is included. Receipt and complete inventory are preserved under
`build/validation/nr-watchdog-aio-20261002/` and beside the archive.

Scoped pre-commit checks passed. The first hook invocation was blocked by
the read-only cache boundary; the approved invocation succeeded. The first
staging call rejected Windows path separators before changing staging;
the canonical CMake prefix succeeded. Both original logs are retained.

After explicitly authorized installation/restart with the corrected AIO:

1. Verify the physical enabled DLL/manifest and runtime Build ID. Establish
   a fresh C scene with capture off, then enable evidence capture.
2. Exercise B-to-C and C-to-B in a controlled NPC scene, preserving every
   accepted/rejected mutation and readback. Only `renderer_busy` with an
   explicit no-mutation result permits a fresh bounded request. Stop after
   a real device/queue failure; do not blindly replay a timed-out mutation.
3. Verify native evaluation, both-eye delivery, empty/re-entry and history
   behavior, plus Windows/NR health throughout. Then finish the remaining
   Task 4 cases listed in the implementation record.

No corrected DLL was installed or game restarted during this source fix.
The original failure remains in the evidence and is not counted as a pass.
