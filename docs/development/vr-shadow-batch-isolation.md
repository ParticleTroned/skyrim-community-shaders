# VR shadow batch submission isolation

The [combined review](vr-shadow-admission-cost-review.md) records the final
adversarial assessment and bookkeeping correction. Source hashes below
identify the historical producers used for each investigation.

## Finding and scope

PID 2912 was the live frozen reproduction at capture. A restricted process query
incorrectly suggested it had exited; an elevated query and fresh
non-suspending debugger capture confirmed it was still running. No process
restart, live-memory repair or DLL deployment was performed.

The retained live investigation establishes a two-node shadow render-pass
cycle, not a shader compiler or diagnostic-mode deadlock:

```text
0x1a4cccad030 -> 0x1a48bea9ad0 -> 0x1a4cccad030
```

Both nodes are lightless Utility passes with technique `0xC0C6`. The native
batch loop follows `BSRenderPass::passGroupNext` at offset `0x30` until
null, so this cycle cannot finish. The passes still belong to their
shader-property caches. Their allocator marker does not establish a
use-after-free. The exact writer and introducing commit remain unproven.

The running producer is source
`216911708f1d545c020dd9bd0e2c8cd35790debf`, Build ID
`c7fdd233aa0cbe019390fab70cdf785218936eb8f56863d9a9609761dcb12fdc`.
This is separate from the earlier portal-cleanup lifetime/lock evidence.
Neither removing the sun nor reverting PR96 is justified by this cycle.

## Implementation

VR 1.4.15 registration hooks substitute stable, batch-owned copies for
lightless Utility shadow-map passes. Cached source links are never edited.
Different renderers, techniques and native buckets receive independent
links. Repeated unchanged submissions to the same live bucket are
idempotent; changed draw state gets another copy. Native registration and
sorting still operate on the submitted copy.

Each copy retains its geometry and shader property. Borrowed light arrays
are not copied: eligibility requires both light counts to be zero. Other
passes retain their native path. The existing light-lifetime protection
and scene-owned sun handling remain unchanged. SE/AE install no new hooks.

Membership follows native bucket lifetime, not frame count. Completed
buckets release their copies; partial drains and `autoClearPasses=false`
retain them. Reset and destruction retire the corresponding records.
Active native readers defer storage reuse and reference release through
reentrant resets. Bulk retirement completes bucket iteration before
releasing owners, because an owner destructor may re-enter registration.
An unavailable native group array is not treated as proof of an empty
bucket.

A per-renderer recursive mutex covers protected registration and ownership
metadata. Neither it nor the registry lock spans native drawing, native
destruction, owner destruction or diagnostics. Reader leases prevent
retirement while native drawing runs unlocked. Retired records are detached
under the mutex, released outside it, then returned to the pool under the
mutex. The engine still owns synchronization of native draw-list mutation;
this is not a general replacement for that contract. Records and membership
capacity are retained until renderer destruction, trading peak memory for
avoiding steady-state per-draw allocation.

The first duplicate emits source/batch identity, technique, bucket, the
first/current registration callers and a bounded stack. It is diagnostic
evidence for a future reproduction, not proof that duplicate registration
created the already-frozen process's cycle. Allocation failure skips the
affected submission with a one-time error; it never falls back to sharing
the original link.

All seven hooks install in one Detours transaction, using the existing
transaction helper. The runtime version, entry bytes and contiguous native
group stride must match before installation. Unknown entry patches fail
startup explicitly rather than install a partial lifetime protocol.
Existing Engine Fixes patches inside these functions were examined in the
live capture and are not overwritten by these entry detours.

| Native entry          | SkyrimVR RVA |
| --------------------- | ------------ |
| Sorted registration   | `0x1348200`  |
| Unsorted registration | `0x13482E0`  |
| Clear pass heads      | `0x1347FE0`  |
| Clear technique map   | `0x1348140`  |
| Render active range   | `0x1348B70`  |
| Render batch step     | `0x1349270`  |
| Base destruction      | `0x1347E60`  |

## Evidence and validation

Current live code was captured without suspending PID 2912 and imported
into `GhidraProjects/Frozen-20260921-pid2912/FrozenLive.gpr`. This analysis
uses live bytes, not an on-disk executable substitute. Local captures and
Ghidra outputs are preserved under the initial worktree's
`.tmp/worktrees/main-vr-nr/build/shadow-batch-membership/`:

-   `pid2912-native-batch-lifecycle.bin`: 28,672 bytes at
    `0x7ff695447000`, SHA-256
    `81b3c9219be661b5834feb7c7389dcc1a6e0a5813f1d32a1e93d54a0d6d6f06a`.
-   `pid2912-batch-trampolines.bin`: 4,096 bytes at `0x7ff615681000`,
    SHA-256
    `e8162e7f9d7e04bd2956fd8729ca0fb50336e0582fc9f7e3c6be1ceb39017a10`.
-   The original freeze report remains under the main checkout's
    `build/forensics-ghidra-pid2912/FINDINGS.md`.

Release controller tests cover A-A/A-B-A, independent buckets/renderers,
changed state, partial drains, persistent buckets, same-frame reuse,
retained owners, nested readers, reentrant reset/release, preparation
failure, pooled allocation reuse and serialized duplicate producers.
The hook harness extracts the production registration, drawing, reset and
destruction bodies; its native functions and engine types are substitutes,
not a runtime test. The byte check compares all seven hook prefixes and
the group-stride pattern against the fresh PID 2912 capture.

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -DCSX_REQUIRE_CLEAN_PROVENANCE=OFF -DBUILD_CONTROLLER_TESTS=ON -DAUTO_PLUGIN_DEPLOYMENT=OFF
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders shadow_batch_submissions_test shadow_batch_hooks_test scene_light_snapshot_test native_lighting_material_guard_test -- /m:1
ctest --test-dir build/ALL -C Release -R '^(ShadowBatchSubmissions|ShadowBatchHooks|SceneLightSnapshot|NativeLightingMaterialGuard)$' --output-on-failure
& build/ALL/Release/shadow_batch_submissions_test.exe build/shadow-batch-membership/pid2912-native-batch-lifecycle.bin
```

The initial DLL build was a dirty `main-vr-nr` worktree validation with SE/AE/VR
enabled, DevBench disabled and automatic deployment disabled. It also
contains pre-existing worktree changes; it is not a clean release or the
producer loaded by PID 2912. The local clean-provenance gate was explicitly
disabled for this development build, not changed in repository defaults.

## Adversarial review

-   Holding the new mutex across drawing or final owner release could create
    a wait cycle with a worker callback. Drawing now uses an unlocked reader
    lease; final releases, including allocation-failure rollback, occur after
    detaching records outside the locks. The regression runs a worker reset
    during a native draw and verifies another worker can acquire the state
    mutex during the last geometry release.
-   Releasing owners while iterating the bucket index could invalidate that
    iteration through reentrant registration. Bulk retirement defers releases;
    tests add 100 new buckets from an owner destructor and retain them.
-   Resetting by frame number would reject legitimate later cascades. Native
    empty/reset/destruction boundaries define lifetime instead. Tests retain
    persistent buckets and admit the same source again after a same-frame drain.
-   A null or Engine Fixes-rejected native array is not evidence of an empty
    bucket. Ownership is conservatively retained until a verified reset.
-   An allocation failure after pinning owners must not publish a raw cached
    link or leak the pins. Ten injected allocation failures are rejected and
    successfully retried, with reference counts restored.
-   The native array is contiguous, unlike CommonLib's declared pointer element
    type. Live stride validation and compile-time group/link layout assertions
    protect the localized VR interpretation.
-   Existing Detours transaction handling and `NiPointer` ownership are reused.
    The two registration modes share admission, the two native reset operations
    share retirement, and both drawing entries share reader/reclamation policy.
    No shader, cache ABI, lighting-dispatch or public settings change is needed.
-   Native Ghidra registration shows only bucket-head/`passGroupNext` insertion,
    with material-order sorting preserved. Examined draw consumers use pass
    fields synchronously; CSX terrain replay retains Lighting passes, outside
    this Utility-only scope. This is not proof about every third-party mod.

The optional full-hook benchmark runs nine alternating rounds of 30 complete
registration/drain repetitions, including atomic mock owner references and
production hook bodies. Final target-worktree medians added 4.29, 35.03 and
295.50 microseconds for 64, 512 and 4,096 submissions respectively, with
zero allocations after warm-up. The last result is 0.30 ms, or 2.7% of an
11.11 ms/90 Hz frame budget, **not** a measured game-frame regression.
The benchmark uses substituted native bodies and does not measure GPU work,
realistic contention or scene lifetime. It makes the protection's CPU cost
explicit; no zero-cost or performance-neutrality claim is supported.

```powershell
& build/ALL/Release/shadow_batch_hooks_test.exe --benchmark
```

### Target-branch validation on 2026-09-21

Only this ten-file change was transferred to the previously clean
`main-VR` worktree based on `216911708`; unrelated NR/shader-lookup edits
remain outside the commit. Six Release tests passed in 0.95 seconds:
`ShadowBatchSubmissions`, `ShadowBatchHooks`, `SceneLightSnapshot`,
`VRSceneGuards`, `NativeLightingMaterialGuard` and
`LightLimitFixVRHookPolicy`. The production translation unit compiled
with SE/AE/VR enabled. All seven native hook prefixes and the group stride
match the preserved PID 2912 bytes. Scoped C++/Markdown and whitespace checks
passed. The CMake formatter's unrelated legacy-file rewrite was discarded;
CMake configuration and builds validate the focused test registration.
Target-worktree receipts are the `build/shadow-batch-review-*` files;
exact clean DLL/archive identity belongs to the subsequent build receipt.
No live deployment or runtime measurement was performed.

COC stability, sun/shadow fidelity, native lifetime behavior and matched
in-game performance still require runtime validation. The frozen process
is preserved; this implementation neither unfreezes it nor proves which
post-`5e9cd203` change introduced the original link corruption.

## Startup CTD correction, 2026-09-21

The `8b3d2b753` AIO introduced a separate startup/load crash. The preserved
PID 7324 dump and exact producer PDB identify `BucketEmpty+0x58`, reading
`0x2209` through CommonLib's declared `renderPassMap`. That declaration
does not match the native VR map. The dump lacks the renderer's heap page;
its exception context nevertheless records the erroneous entry base `1`
and capacity `0x2A1` used by the faulting lookup.

Reopening the preserved live PID 2912 program in Ghidra confirms the native
insertion helper at RVA `0x1349E00` and draw lookup at `0x1348B70` agree:

| Map input      | Incorrect accessor | Native VR renderer offset/rule |
| -------------- | ------------------ | ------------------------------ |
| Capacity       | `+0x24`            | `+0x2C`                        |
| Chain sentinel | `+0x30`            | `+0x38`                        |
| Entry storage  | `+0x40`            | `+0x48`                        |
| Hash           | CRC32              | `technique & (capacity - 1)`   |

Entries have a `0x10` stride, with technique/group/next at `+0/+4/+8`.
Group storage and count remain at renderer `+0x08/+0x18`; contiguous groups
have a `0x30` stride and five pointer-sized heads. A focused read-only VR
accessor now uses these fields without changing shared CommonLib types.
Fixed-size `memcpy` reads avoid type-aliasing assumptions. No allocation,
system call, CRC calculation or additional lock is introduced.

Malformed capacity, pointers, collision links, group index or bucket are
not proof of retirement. The accessor retains owners in those cases and
limits collision traversal to the smaller of capacity and 1,024 entries.
This protects this ownership query, not arbitrary corrupt native engine
operations or unsynchronized third-party mutation. Native reset/destruction
still retires the retained records.

The hook harness now publishes an independently assembled native byte
layout instead of allowing production code to read its logical mock map.
Raw fixtures poison the incorrect offsets with the crash register values
and cover all five buckets, group stride, identity-hash collisions, absent
keys, invalid indices, missing arrays, alignment/overflow, a bad sentinel,
out-of-range links and cyclic chains. A production-hook regression rejects
the old accessor; allocation-failure and lifetime tests remain in place.

Local diagnostic evidence under `build/shadow-batch-membership/` in the NR
worktree includes `startup-7324-cdb.txt`,
`startup-7324-renderer-layout.txt` and `ghidra-startup-map-abi.log`.
The original dump was preserved and size/hash verified before reading at
`D:/Coding/GitHub/CS logs/8b3d2b753__20260921T093146578Z__07ef84f5__SkyrimVR.exe.7324.dmp`
(183,726,732 bytes, SHA-256
`5b025bd8ec9120770a4fad5a7d3ab78348b1d385f8602d68c7ed52daed34637d`).

Six focused Release controller tests pass. The corrected-layout synthetic
benchmark records 4.05/33.55/276.17 microseconds added for 64/512/4,096
submissions, with zero warm allocations. This is approximately 0.28 ms at
4,096 submissions, not an in-game measurement or a demonstrated speedup
over the earlier benchmark. Main-VR receipts are
`build/shadow-batch-layout-tests.txt` and
`build/shadow-batch-layout-benchmark.txt`. Exact clean DLL, cache and archive
identities belong to the new AIO build receipt. Startup, COC stability and
in-game performance remain unverified until the corrected DLL is run.
