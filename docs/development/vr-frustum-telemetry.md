# Native VR frustum workload telemetry

This DevBench-only instrumentation addresses the missing invocation and
address-workload counts in the matched-pose CPU investigation. It does
not change visibility decisions, material/light ownership, render scale,
shader quality, renderer synchronization or depth-culling policy.

`communityshaders.menu` status and `communityshaders.renderscale` status
include `nativeFrustum` schema version 1. Existing saved status snapshots
therefore carry these counters without changing game-ft/gameft-sw saves,
holds, timing calculations or query scheduling. New runs are required;
these counts cannot be reconstructed from historical sampled WPR stacks.

## Coverage and identities

| Field / group                                                                  | Meaning                                                                                                                                                                                                                                                        |
| ------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `installed`, `installError`                                                    | All native and pass detours installed together, or none did. A missing installation is unavailable evidence, not zero work. `installError` is a Windows/Detours error code.                                                                                    |
| `enabled`, `collectionGeneration`                                              | Collection defaults on in DevBench builds. Changing enabled state increments the generation and invalidates same-address tracking without resetting cumulative counts.                                                                                         |
| `active`                                                                       | Both hook installation and collection are enabled.                                                                                                                                                                                                             |
| `threads[].slot`, `threadId`                                                   | One lifetime slot per observed thread. Slots are never reused; Windows thread IDs may be reused, so retain both values.                                                                                                                                        |
| `registeredAtQpc`, `observedFrames`, `firstObservedFrame`, `lastObservedFrame` | First telemetry observation time and producer-frame observations per thread. These are not a replacement for fpsVR frame counts or verified native job/frame ownership.                                                                                        |
| `passEntries`                                                                  | Same-thread entries into player view, depth, world, first-person, water, shadowmask, directional/spot/point shadow, cubemap and CSX recovery scopes. Recovery entries include early returns; existing recovery-attempt counters retain their separate meaning. |
| `rows[].routine`                                                               | `compound_object` at SkyrimVR RVA `0xDA33C0`, `sphere_intersect` at `0xDA5410`, or `sphere_not_fully_inside` at `0xDA54B0`.                                                                                                                                    |
| `rows[].pass`                                                                  | Innermost observed calling-thread scope. An asynchronous worker does not inherit the render thread's current pass; unscoped calls are `unknown`.                                                                                                               |
| `rendererCameraIndex`                                                          | Raw `State+0x58` sampled at pass entry, or null. Header descriptions conflict between eye and camera-state index; **no left/right interpretation is established**. Shared stereo visibility and shadow work must not be classified as duplicate inference.     |
| `callerAddress`, `skyrimImageBase`                                             | Native return address and image base for joining callers to the preserved live snapshot/WPR modules. Do not assume every caller belongs to Skyrim; resolve its module before subtracting an RVA.                                                               |
| `calls`, `completed`, `rawTrue`, `rawFalse`                                    | Entered and returned calls, retaining native results. A compound call is an object visit, including revisits. A sphere call is a bound test, not another object visit. Routine totals are separate and must not be added as unique objects.                    |
| `firstAddressKeys`, `repeatedAddressKeys`                                      | First and repeated retained keys within one thread and observed frame. Keys include routine, pass, raw camera index, caller, owner, object-or-bound and plane addresses. They are not globally unique object identities.                                       |
| `trackingOverflowCalls`, `unknownFrameCalls`                                   | Address classification could not be established. These are not first visits or repeats.                                                                                                                                                                        |
| `rowOverflowCalls`, `threadOverflowCalls`, `threadOverflowPassEntries`         | Explicit loss when a fixed diagnostic table cannot retain a context/thread. Nonzero values limit completeness.                                                                                                                                                 |
| `snapshotStartQpc`, `snapshotEndQpc`, `qpcFrequency`                           | Bounds on the status read, independent of fpsVR frame timestamps. Counter samples are independent atomic reads, not a transaction across active threads.                                                                                                       |
| `qpcValid`                                                                     | The snapshot timing calls succeeded and the clock frequency is positive.                                                                                                                                                                                       |

The two sphere routines have different boolean meanings. In particular,
`sphere_not_fully_inside=true` does not mean simply "culled" or
"accepted". Preserve the raw results and native routine identity.

Repeating an address key does not establish redundant work: planes and
bounds can change, a frame can legitimately revisit a pass, and addresses
are not lifetime identities. No object is retained or dereferenced to
populate address tracking. The tracking table never influences native
dispatch or rendering. It uses full-key equality, not hash equality;
collisions/capacity loss are explicit, not guessed unique counts.

## Safety and installation

All declarations, storage, hooks and call-site instrumentation are guarded
by `DEVBENCH_BRIDGE_ENABLED`. An ordinary production build has none of
these counters, TLS contexts or detours. SE and AE install no native
frustum diagnostics. No per-frame/per-draw log records or timers are added.

The native signatures and prefixes use the preserved live Skyrim VR
1.4.15 capture, not a packed on-disk executable:

```text
D:\Coding\GitHub\GhidraProjects\CPU-DLSS-20260916-pid21340\NativeCPUHotRanges.gpr
```

The `00DA3000` snapshot SHA-256 is
`87fd13008bcae21d1478342d1fd593e807d36b18ea6b29bd450563b95f46ae8d`;
`00DA5000` is
`e1dfbdf56819b313fd5247cff2bfec9e74ed68efeffe5e88a5df7fd053b71a5c`.
Its live disassembly establishes two three-pointer sphere calls and the
two-pointer compound-object call, all returning their result in AL.
The pass signatures reuse the existing FrameAnnotations hook contracts.

Installation checks runtime version and all three native entry prefixes
before beginning a Detours transaction. All targets are resolved and checked
for readable executable memory and duplicate addresses before any attachment.
Vtable slots are checked before reading. The existing SKSE scope-exit helper
aborts an uncommitted transaction on any exit. Existing hook chains are retained.
Unknown/patched native entries disable this instrumentation instead of
replacing an unrecognized implementation. No material or culling exception
handler is broadened. The hooks forward the original arguments exactly
once and return the native boolean unchanged, including when collection
is disabled or its tables are full.

There are 64 lifetime thread slots, 128 counter rows per thread, and 4096
address entries per thread, with at most eight probes per lookup. Hook
recording allocates no heap memory and takes no shared mutex. Each row has
one writer and atomic snapshot fields. JSON allocation occurs only during
status requests. Native work continues even when any capacity is exhausted.

## Reading a measured window

Use snapshots bracketing the intended window from the same producer Build
ID/process. Require installation, enabled collection, unchanged
`collectionGeneration`, and matching `collectionGenerationAtEnd` at both
endpoints. Retain capacity losses and differences between snapshot QPC
bounds and the fpsVR/WPR interval. A status snapshot around +49 seconds is
not an exact +50-second boundary. Do not combine cumulative counts from
menus/loading with a last-ten-second frame-time average.

The menu action `set_frustum_telemetry_enabled` takes boolean `enabled`.
It changes measurement only and never saves settings or clears counters.
Disabling collection leaves detours installed. An on/off comparison can
measure collection overhead, but is not an uninstrumented-hook comparison.
Instrumentation overhead is unmeasured: compare builds with the same
instrumentation/state before attributing CPU deltas to a rendering change.
This does not change the saved benchmark protocol.

Scopes carry their collection generation. Toggling collection invalidates
an already-entered scope, including a parent restored after a nested scope
returns. Calls remain unscoped until a current-generation boundary is
observed. Disabled or uninstalled scopes do not sample the renderer field
or allocate a thread slot. Calls already entered can finish across a toggle
or snapshot boundary; cumulative completion deltas are not a count of calls
that both began and finished wholly inside that interval.

Remaining attribution limits are explicit: the counters cover these three
mapped native routines, not every native culling path; worker pass/camera-index
ownership requires additional verified job context; renderer camera-index samples
do not prove per-eye culling; and address repeats do not prove redundant
operations. WPR is still needed for CPU execution/ready/wait cost, and GPU
timing for GPU cost. No optimization is justified solely by these counts.

## Validation

Adversarial review on 2026-09-18 corrected three findings:

-   Pass scopes could survive a collection toggle and supply stale context.
    Generation validation now rejects that context, including nested
    restoration; focused tests cover disable/re-enable boundaries.
-   `State+0x58` was mapped to left/right using an ambiguous header label.
    Its full raw unsigned index is now retained separately from null, and
    eye ownership remains explicitly unknown. Tests cover 0, 1, 7 and
    `UINT32_MAX` without conflating them with unavailable context.
-   Pass targets were resolved while a Detours transaction was open and
    lacked the native targets' memory checks. Resolution and validation now
    precede the transaction; existing SKSE RAII guarantees abort on early
    exit. This installer path still requires a live smoke test.

The review also checked unchanged native arguments/results, bounded
single-writer storage with atomic readers, unknown-worker attribution,
production compile exclusion, and absence of visibility, ownership,
scheduling or renderer-lock changes. No second culling algorithm, light
retention mechanism or external recording loop was added.

`VRFrustumTelemetry` includes the production policy header and covers native
argument/result forwarding, exceptions, independent pass/camera-index/caller/thread
identities, frame/generation changes, unknown frames, bounded collisions,
capacity losses, nested scope restoration and concurrent status readers.
`VRFrustumTelemetryOff` builds the actual implementation without DevBench.
The menu schema contract test checks the new action and status path.

The compiled DLL still requires an in-game installation/collection smoke
test and an instrumentation-overhead comparison before performance claims.

Local validation on 2026-09-18 (Release, SE/AE/VR enabled, DevBench on):

```powershell
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests shader_tests -- /m:1
ctest --test-dir build/ALL -C Release -N
ctest --test-dir build/ALL -C Release --output-on-failure --no-tests=error --timeout 300
pwsh ./tools/git.ps1 diff --check
```

Both builds passed. The saved inventory contains 126 tests; all 126 passed,
with zero failed, skipped or not-built tests, in 52.55 seconds. The earlier
focused frustum-on, frustum-off and menu-contract run passed 3/3 tests.
Logs and the bound DLL manifest are preserved under
`build/validation/frustum-telemetry-20260918/`.

Scoped pre-commit checks passed for the new files and changed telemetry
bridges. The initial formatter also proposed unrelated existing changes in
`CMakeLists.txt` and `FrameAnnotations.cpp`; those were restored, retaining
only the formatted test registrations and guarded telemetry call sites.
`git diff --check` passed. No runtime benchmark or deployment was performed.

After the adversarial corrections, the focused tests passed 3/3 and the
universal DLL rebuilt successfully. An initial full-suite attempt found
the shader executable unbuilt after regeneration (125 passed, one not run).
Explicitly rebuilding `controller_tests shader_tests` and repeating the
full suite passed all 126 tests in 50.00 seconds, with no skips or missing
executables. Review/build/test logs are retained with the frustum AIO
packaging evidence under `build/packaging/frustum-telemetry-*/`.
