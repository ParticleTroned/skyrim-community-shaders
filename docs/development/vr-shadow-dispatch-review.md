# PR96 native shadow dispatch review

See the [combined review](vr-shadow-admission-cost-review.md) for the final
adversarial assessment and consolidation of the subsequent shadow fixes.

## Decision

Replace PR96's whole-loop light snapshot and copied dispatch order with
live selection and a per-render ownership lease. Keep its validated native
adapter, runtime restrictions and original lifetime protection. Retain the
scene for the dispatch loop because it directly owns the sun; never add an
intrusive reference to that sun.

This integrates the necessary ownership behavior of `012e5139e` into the
replacement. It does **not** justify reverting that commit alone or
eliminating the distinction between queued lights and the scene-owned sun.

The corrected dispatch semantics are tested. Resolution of the observed
46-node circular render-pass freeze is **not runtime-proven**.

## Evidence reviewed

The review covers PR96 (`2e6d87cf763633917dbee54805476c7da0d2c995`), its
parent, `012e5139e0d3b96109c9a077137ba073ced35218`, the earlier LLF snapshot,
the production hook and ownership code, controller tests, captured native
selector/renderer instructions, dark-circle bisect receipts and the saved
PID 29224 freeze investigation.

| Finding                                             | Evidence                                                                                                                                           | What it establishes                                                                           |
| --------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| Lights were destroyed during rendering              | [Original ownership investigation](vr-shadow-light-lifetime-fix.md), including PID 22880's destruction completing 0.6308 ms before render returned | The current render call must own its light through its final native field access.             |
| PR96 introduced the dark circle                     | Adjacent parent/PR96 visual bisect; 351 identical non-producer payload files                                                                       | Rejecting the scene-owned sun was a real regression.                                          |
| The sun is directly scene-owned                     | Captured constructor/destructor instructions; zero intrusive count in PID 8724                                                                     | Retain the scene, not an artificial sun `NiPointer`.                                          |
| Native dispatch selects live work                   | Selector RVA `0x12FA250` and the native loop fixture                                                                                               | Each completed `Render` is followed by another live array lookup, not a copied-vector lookup. |
| PR96 replays withdrawn work                         | Production-function regression added before changing implementation                                                                                | Existing code dispatches the next light even after the live array is cleared.                 |
| The current freeze loops over a circular pass chain | Saved PID 29224 investigation: two matching bounded chain reads and native/Ghidra loop analysis                                                    | The 46-node cycle is real; the write that created it is not identified.                       |

The historical ownership report also distinguishes a separate NVIDIA
zero-length command-consumer loop from the current native shadow loop.
They must not be treated as the same proven defect.

Locally available primary artifacts:

-   `D:/Coding/GitHub/skyrim-community-shaders/build/dark-circle-bisect/`,
    especially `live-shadow-sun-20260919/native-shadow-selector.bin`,
    `native-shadow-render.bin`, scene constructor/destructor bytes and
    `light-18a11c07900-render.bin` with their disassemblies and receipts.
-   `D:/Coding/GitHub/skyrim-community-shaders/.tmp/worktrees/main-vr-nr/build/devbench-evidence/frozen-ghidra-repeat-pid29224/`, including
    `forensic-summary.json`, repeated chains, complete lower stacks and
    `ghidra-live-render-03.log`.
-   `tests/fixtures/vr_scene_guard_sites.h` and the actual extracted
    production dispatch/adapter tests.

The original PR96 journals and large dumps referenced under historical
`artifacts/` paths were not found in this worktree or the checked main
repository artifact locations. Their findings are attributed to the
retained reports, not claimed as a new examination of those raw captures.

## Why replace the snapshot dispatch

The native selector loads array storage at scene+`0x258` and returns
`array[index]`. The loop calls it again at RVA `0x1323226` after the virtual
render call updates the index. PR96 instead captured all owner lists and
`shadowLightsAccum` before the first call, then used that saved vector for
every subsequent dispatch.

Its existing tests deliberately removed the engine's owners and cleared
the live array during rendering, yet expected a later saved light to
render. That verifies extended lifetime, but enshrines a change to which
work executes. Restoring the sun through `012e5139e` also made sun work
eligible for that copied-order dispatch. Replaying a withdrawn sun is a
plausible regression path; the freeze capture does not establish that it
actually happened in PID 29224.

The replacement:

1. Checks the current live array bounds and terminator under the queue
   lock for each dispatch.
2. Acquires the selected ordinary light from one of the same five owning
   lists, using its raw address only as a lookup key. Alternatively, the
   exact current scene sun uses the retained scene as its owner.
3. Unlocks before type validation and the complete virtual `Render` call.
4. Releases the ordinary light outside the lock, then reselects live work
   using the native updated index. Non-advancing or wrapped indices stop.

A reserved non-owning index of list/slot hints is built once, lazily on
the first ordinary-light dispatch. It does not preserve rendering order
or retain future lights. Every hit validates the current list bounds and
pointer identity before copying its owning reference. A moved or new owner
uses the shared live lookup. Stable lookups therefore avoid scanning every
light for every render call. Empty and sun-only passes allocate nothing;
ordinary dispatch allocates during index creation only. Allocation failure
logs once outside the lock and skips the remaining pass without publishing
an incomplete index. The next invocation can retry.

Existing LLF frame snapshots still protect the separate strict-light and
clustered-light consumers, including their original allocation-failure
behavior. The five owner sources and active-list ordering share one helper.
SE/AE behavior is unchanged; the executable hook remains VR 1.4.15-only and
still validates all 184 bytes before either patch. No new hook, setting or
DevBench action is introduced.

## Adversarial assessment and limits

-   Reverting PR96 entirely restores the demonstrated native lifetime hole.
-   Removing only sun handling restores the dark circle; putting the sun
    into an intrusive owner can delete a directly scene-owned object.
-   Retaining every light while only restoring live selection would fix the
    dispatch divergence but retain unnecessary future/completed lights.
    Per-call retention more narrowly matches the observed native use and
    releases completed lights before the next call.
-   Holding the queue lock across rendering risks blocking worker teardown
    and reentrant consumers. The replacement never does so.
-   Unknown addresses are not dereferenced. Types are checked before
    downcasting. Sun cascades retain their multi-slot index advancement.
-   Render exceptions propagate; ordinary and scene leases unwind outside
    the lock. Empty passes acquire no references and allocate nothing.
-   The tests allow engine workers to destroy **unselected** withdrawn
    lights under their queue lock. They still require render-thread final
    releases outside that lock and prove the selected light survives worker
    teardown until the call returns.
-   Retention protects object lifetime, not arbitrary concurrent edits to
    descriptors, accumulators or linked render passes. Queue-lock coverage
    of the owning-list drain/removal is documented; this review does not
    establish that every native accumulated-array writer uses that lock.
-   The first candidate scanned every owning list per dispatch. Review
    measured a large-list regression and replaced this with validated
    non-owning slot hints. Ordinary stable dispatch has expected O(N + M)
    lookup work for N owner entries and M render calls, rather than O(N\*M).
    Mutation-driven misses still require a live scan; malformed or missing
    ownership ends dispatch. An index is never treated as lifetime proof.
-   If a circular pass list already exists inside the selected `Render`,
    this outer dispatch change cannot terminate or repair it. No links are
    severed and no arbitrary draw-count cutoff hides that corruption.

## Adversarial performance review

The first allocation-free, per-light scan was not accepted unchanged.
Its median synthetic dispatch cost with 1,024 shadow lights increased
from 62.963 to 135.111 microseconds with no ordinary lights, and from
162.604 to 561.377 microseconds with 2,048 ordinary lights. This exposed
the O(N\*M) scaling risk before commit.

The replacement builds a reserved, non-owning owner-location index once.
It retains neither future lights nor dispatch order. Cached positions are
validated under the same owning-list lock; changes trigger a live fallback.
This preserves ownership safety while avoiding the repeated stable scans.
Array index conversions are made only after bounds validation and use the
container's size type, covering both the test vector and engine BSTArray.

The final main-VR Release harness compares the extracted production
function against a frozen pre-change dispatch fixture. Each row is the
median of nine rounds of 100 calls per variant, alternating variant order.
It measures dispatch/ownership overhead with mock render bodies, actual
CommonLib NiPointer and atomic reference counts, not Skyrim frame time.

| Ordinary lights | Shadow lights | Sun | Baseline us | Candidate us | Delta us |
| --------------: | ------------: | :-: | ----------: | -----------: | -------: |
|               0 |             0 | No  |       0.012 |        0.012 |    0.000 |
|            2048 |             0 | Yes |     104.042 |        0.034 | -104.008 |
|             128 |             4 | No  |       6.102 |        5.162 |   -0.940 |
|            2048 |             8 | No  |      97.229 |       68.120 |  -29.109 |
|            2048 |            64 | No  |     102.124 |       73.251 |  -28.873 |
|               0 |           128 | No  |       8.514 |        7.882 |   -0.632 |
|               0 |          1024 | No  |      64.211 |       60.923 |   -3.288 |
|            2048 |          1024 | No  |     162.328 |      137.337 |  -24.991 |

All eight medians were neutral or lower; the largest populated case saves
0.025 ms of modeled dispatch overhead, about 0.22% of an 11.11 ms/90 Hz
frame budget. This is not an in-game performance claim. The CSV preserves
per-case min/max batch timings and allocation counts. In particular, small
differences with overlapping ranges should be treated as neutral. Mutex
behavior and allocator/cache effects in this harness do not establish
native spin-lock contention or loaded-scene frame cost. Repeated live list
mutations can require fallback scans and still need runtime measurement.

## Validation on 2026-09-21

Validation used the dirty working tree based on
`e715653bf620f8442ce48d81e0bc07bd684b8556`, not a clean release producer.

The new live-withdrawal regression was built and run against the unchanged
production dispatch first. `SceneLightSnapshot` failed because the
withdrawn light still rendered. The replacement passes that test and the
extended cases for clear/null/shrink/replace/revoked ownership, selected
light teardown, prompt completed-light release, all five owner sources,
sun-only zero-allocation dispatch, normal sun cascades between queued
lights, withdrawn sun work, stale pointers, bounds, index wrap and unwind.

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target scene_light_snapshot_test vr_scene_guards_test light_limit_fix_vr_hook_policy_test -- /m:2 /v:minimal
ctest --test-dir build/ALL -C Release -R '^(SceneLightSnapshot|VRSceneGuards|LightLimitFixVRHookPolicy)$' --output-on-failure -V
```

All three tests passed (0.20 seconds). `VRSceneGuards` reports 743
assertions, including the native frame/adapter checks. The frame snapshot
still passes all nine injected allocation-failure cases.

The indexed revision also passes the three tests (0.86 seconds), five
index-allocation failure points and retry, shifts/reallocation of owning
arrays, migration to pending queues, newly added keys and duplicate owners.
Type validation is asserted to occur outside the queue lock. The current
light survives worker teardown while unselected withdrawn lights may be
destroyed; cached locations do not keep them alive.

The production translation unit compiled with SE, AE and VR enabled,
zero warnings and zero errors:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders -- /t:ClCompile '/p:SelectedFiles=D:\Coding\GitHub\skyrim-community-shaders\.tmp\worktrees\main-vr-nr\src\Features\LightLimitFix.cpp' /p:BuildProjectReferences=false /m:2 /v:normal
```

Scoped whitespace, line-ending, C++ and Markdown checks passed:

```powershell
pwsh ./tools/pre-commit.ps1 run --files src/Features/LightLimitFix.cpp src/Features/LightLimitFix.h src/Features/LightLimitFix/SceneLightSnapshot.h tests/scene_light_snapshot_test.cpp docs/development/vr-shadow-light-lifetime-fix.md docs/development/vr-shadow-dispatch-review.md
pwsh ./tools/git.ps1 diff --check
```

A direct MSBuild invocation first failed on sandbox access to its default
temporary directory. The repository CMake wrapper resolved that problem.
`pwsh ./tools/dev-doctor.ps1 -Network` reported zero failures and the
existing GitHub CLI authentication warning. It did not block local tests.

Evidence is preserved under
`D:/Coding/GitHub/skyrim-community-shaders/.tmp/worktrees/main-vr-nr/build/pr96-live-dispatch-review/`. No DLL was linked or deployed, no
runtime settings changed, and the frozen game was not touched. Existing
unrelated shader-lookup changes were preserved separately. No provenance
setting was changed.

The final seven-file change was transferred without unrelated edits to
the clean `main-VR` worktree based on
`568e859f90d3f37260910a20725058544cb97689`. The same three tests passed
there in 0.87 seconds, and the production translation unit compiled with
SE/AE/VR enabled. No installer bytes or CMake behavior changed. Final
target-branch receipts are under
`D:/Coding/GitHub/skyrim-community-shaders/build/worktrees/main-vr-fov-navigation/build/pr96-live-dispatch-review/`.

The optional benchmark is reproducible without Skyrim:

```powershell
& build/ALL/Release/scene_light_snapshot_test.exe --benchmark
```

Runtime COC stability, visual sun fidelity, performance and SE/AE runtime
checks have **not** run for this replacement. A matched failing-build
comparison is still needed before declaring the circular-pass freeze
resolved. If it recurs, the next evidence target is the pass-chain writer,
not another blind ownership revert.
