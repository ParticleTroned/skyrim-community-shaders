# VR shadow admission cost review, 2026-09-21

The requested reference, `568e859f90d3f37260910a20725058544cb97689`,
already includes PR96 (`2e6d87cf763633917dbee54805476c7da0d2c995`).
There is no retained matched measurement that isolates PR96's original
in-game CPU or GPU cost. Do not add that unknown cost again when comparing
the following changes against this reference.

## What the retained evidence establishes

The following three commits directly follow the reference in this order.
The costs below are synthetic CPU costs, not measured game-frame deltas.

| Change                                                                              | Evidence relative to its predecessor                                                                                                                         | Interpretation                                                                                                                                                                                                                      |
| ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `216911708f1d545c020dd9bd0e2c8cd35790debf`: retain only live shadow dispatch owners | Eight dispatch medians neutral or lower; 2,048 ordinary/1,024 shadow lights: 162.328 to 137.337 us; sun-only with 2,048 ordinary lights: 104.042 to 0.034 us | Replaces a complete owning snapshot and copied dispatch order with validated non-owning owner hints and current-light retention. Does not establish in-game savings or resolve an already circular pass list.                       |
| `8b3d2b753fae4d4f31985e008559983fb1e1eb22`: isolate shadow batch pass links         | Added 4.29 / 35.03 / 295.50 us for 64 / 512 / 4,096 submissions                                                                                              | Main new bookkeeping cost: per-batch pass copies, geometry/property owners, membership lookup and synchronization. Prevents shared intrusive links from corrupting overlapping batches.                                             |
| `e18992eac1167c02b2e66bdc92ccdef47905d3eb`: use the native shadow batch map layout  | Total isolation overhead 4.05 / 33.55 / 276.17 us for the same counts                                                                                        | Corrects a startup CTD from an incompatible CommonLib map layout. These are total isolation costs, not an additional 276.17 us on top of the preceding change. No controlled speedup claim between these historical benchmark runs. |

Sources: [dispatch review](vr-shadow-dispatch-review.md),
[batch isolation and map correction](vr-shadow-batch-isolation.md), and
[original light lifetime evidence](vr-shadow-light-lifetime-fix.md).
The dispatch harness and batch harness measure different operations and
must not be summed into an asserted frame cost. The batch benchmark uses
production hooks, substituted native rendering bodies and atomic mock
references. It excludes GPU execution, real scene contention and native
draw cost. Its approximately 0.28--0.30 ms result assumes 4,096 submissions
per repetition. The subsequent Whiterun count below provides a measured
scene workload, while the resulting CPU cost remains an estimate.

The preserved PID 2912 investigation identifies a two-node circular
`passGroupNext` list while running `216911708`. The thread was executing
the cycle, not blocked on a mutex. It does not identify the corrupting
writer or prove which commit originally introduced the cycle. The user's
report that the latest build resolves the freeze is separate from a
matched performance measurement.

## Reviewed bookkeeping correction

The correction was developed from `e18992eac` on
`fix/vr-shadow-admission-cost`. It keeps the isolated pass
records, all geometry/property ownership, exact submission identity,
per-batch synchronization, native reader leases and deferred reclamation.
SE/AE do not install these VR hooks. There are no shader, render-scale,
settings, D3D protection or diagnostic-logging changes.

-   Cache the last renderer state per thread with a weak reference and a
    destruction revision. Successful hits avoid the registry lock and map
    lookup; they still acquire strong state ownership. Destruction invalidates
    cached addresses, including reuse while old readers retain the old state.
    Dormant threads cannot keep destroyed state pools alive.
-   Cache the last technique bucket and use one insertion lookup for the
    full submission key. No weaker source-pointer-only identity is used.
-   Preserve live membership across allocation failure. The installed dense
    map can discard old buckets before a throwing rehash allocation. Growing
    an off-side reserved map and swapping its owning pointer preserves the
    old map on failure. The small outer bucket index uses the standard
    unordered map's insertion failure guarantee. Rejected admissions release
    acquired owners using the existing deferred path.

The populated-map allocation regression first failed with a segmentation
fault against the original production store. The corrected implementation
passes injected failures for existing member growth and outer bucket
growth, retry, deduplication, draw preservation and complete owner release.
Cross-thread tests use latches, not sleeps, and cover destruction, address
reuse with an old strong owner, and pool expiration with a dormant cache.

## Adversarial review of the combined series

The review covers the complete diff from `568e859f9`, including live light
selection, batch isolation, native layout correction and bookkeeping.
It distinguishes proven failures from hypothetical changes to Skyrim's
existing synchronization contract.

| Area        | Result                                                                                                                                                                                                                                                    |
| ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scope       | Only native VR shadow ownership/dispatch and batch-link lifetime change. SE/AE exit before fixed-address installation. No settings, shaders, vendor dispatch or D3D protection changes.                                                                   |
| Correctness | Current ordinary lights remain owned through `Render`; live withdrawn work is not replayed. The scene owns the sun. Repeated/overlapping batches receive independent pass links; native sorting and bucket lifetime remain authoritative.                 |
| Robustness  | Fixed the demonstrated populated-map allocation-failure crash. Existing members survive failure and retries. Weak cache invalidation covers renderer-address reuse without retaining destroyed pools. Invalid native metadata does not prove retirement.  |
| Performance | Single membership insertion lookup and cached positive renderer/bucket lookups reduce measured overhead. Full keys, per-batch locks and required owning references remain. No per-frame logging was added; existing error/conflict reporting is one-shot. |
| DRY         | One owning-list visitor serves both snapshot and live-owner lookup. Installation uses the existing checked Detours transaction helper. Tests execute extracted production functions, while the frozen pre-change dispatch fixture is benchmark-only.      |

Retained live native code confirms reset/draw paths remove list links,
not engine-pool-free the copied pass records. The native destructor calls
the clear hook while the outer destruction wrapper holds no state mutex.
The two clear implementations do not call each other. A suspected nested
reset release-under-lock path was therefore not demonstrated, and no
speculative locking change was added. Arbitrary third-party concurrent
mutation remains outside this native lifetime contract.

The installer test executes the extracted production `Install()` and the
real transaction helper. It checks SE/AE zero-address access, unsupported
VR rejection, all 128 hook/stride byte mismatches, seven distinct hook
targets in native order, and failure at each attachment position. No
failed transaction publishes a partial hook set or success message.

The consolidated commit retains the original authorship and follows
`568e859f9`. Historical source hashes above identify the actual tested
producers before consolidation. The subsequent counting run below used the
consolidated implementation plus temporary diagnostics; it does not supply
matched in-game CPU timings or freeze/visual qualification.

## Matched synthetic result

Nine alternating original/candidate executable pairs were run serially.
Each harness reports nine alternating internal rounds of 30 complete
registration/drain repetitions. All warmed cases allocate zero times.
Values are medians of the nine reported added-cost measurements.

| Submissions per repetition | Existing isolation, us | Candidate isolation, us | Delta, us | Reduction | Existing range, us | Candidate range, us |
| -------------------------: | ---------------------: | ----------------------: | --------: | --------: | -----------------: | ------------------: |
|                         64 |                  4.100 |                   3.333 |    -0.767 |     18.7% |       4.027--4.360 |        3.270--3.430 |
|                        512 |                 34.293 |                  27.470 |    -6.823 |     19.9% |     32.867--35.630 |      26.920--28.057 |
|                      4,096 |                286.310 |                 242.933 |   -43.377 |     15.2% |   280.070--300.390 |    235.960--254.163 |

At 4,096 submissions the modeled saving is 0.043 ms, approximately 0.39%
of an 11.11 ms frame budget. The remaining modeled cost is 0.243 ms. This
is a partial bookkeeping reduction, not performance neutrality or an
in-game savings claim. Safe link isolation must not be removed merely to
erase this benchmark cost.

This benchmark repeatedly uses one renderer and bucket. It does not
quantify alternating-renderer/bucket cache misses or realistic contention;
those cases must not inherit the measured reduction as an assumed result.

Local evidence is under `build/shadow-admission-cost/` in this worktree:
`comparison.json`, `comparison.csv`, `compare.py`, the 18 raw benchmark
outputs, `growth-before.txt`, `final-focused-build.txt`,
`final-focused-tests.txt`, and `dll-build.txt`. Original benchmark executable
SHA-256: `8828409e7c02823b07e252e5165b989ee761055e2c64f7ebd89b9f7ed215882d`.
Candidate benchmark executable SHA-256:
`4e61beadd9ab7a18e8848b66e0640801a4c9054febc5968f2ae234a4fd3713da`.

## Live Whiterun submission counts and modeled frame cost

On 2026-09-21, PID 11228 ran the consolidated source
`c610f1690452aef65ff28bf64435fded53b3ca35` plus temporary DevBench-only
counting changes. Its embedded Build ID matched the supplied counting DLL:
`bc07aa988fb12802d1dd448816b5dbc233c430291999946fccbf446504f0568b`.
The supplied DLL SHA-256 was
`5e78726883f09c155046fee3cdd915026dbae7a73af84aed84a762f9b6f8958b`.
The physical enabled-mod DLL hash was not re-resolved during this read.
These remain the actual measured producer identities after the
documentation-only commit amendment; do not replace them with its new hash.

The scene was `WhiterunExterior01`, weather `SkyrimClearTU`, player position
`[17060.19140625, -12208.3115234375, -4771.19970703125]`. Those values
matched before and after sampling. The earlier inspected scene was
Windhelm, so this result must not be attributed to Windhelm. HMD pose was
not recorded; in-game time advanced and the counts varied between windows.
The five snapshots reported identical feature settings. No profiler or
rendering setting was changed.

Five disjoint 300-frame windows supplied 1,500 unique, complete, coherent
frames. There were 4,416 unsampled frames between the first and last
retained frames; this was not a continuous performance recording. Counts
cover eligible lightless Utility shadow-map registrations, not all draws.

| Frame window, inclusive | Frames | Mean inserted submissions/frame |
| ----------------------- | -----: | ------------------------------: |
| 9618--9917              |    300 |                        1263.767 |
| 12593--12892            |    300 |                        1355.380 |
| 14542--14841            |    300 |                        1376.530 |
| 14925--15224            |    300 |                        1388.547 |
| 15234--15533            |    300 |                        1386.380 |

Total eligible and inserted submissions both equal 2,031,181. Mean:
1,354.121/frame; median: 1,373; nearest-rank P95: 1,400; minimum/maximum:
1,237/1,412. Duplicates, allocation failures, unwound admissions,
cross-boundary admissions and rejected windows were all zero.

Scale the prior added registration/drain microseconds by the measured mean
count and divide by the benchmark submission count and 1,000:

| Isolation version                          | Using 512-submission benchmark, ms/frame | Using 4,096-submission benchmark, ms/frame |
| ------------------------------------------ | ---------------------------------------: | -----------------------------------------: |
| Before bookkeeping correction, `e18992eac` |                                 0.090698 |                                   0.094653 |
| Corrected implementation, `c610f1690`      |                                 0.072652 |                                   0.080313 |
| Estimated saving                           |                                 0.018046 |                                   0.014340 |

Thus the scene-specific estimate is **0.073--0.080 ms/frame** for current
shadow-submission protection, versus **0.091--0.095 ms/frame** before the
bookkeeping correction. The range represents two scaling models, not a
confidence interval or a hard runtime bound. Renderer/bucket distribution,
real reference lifetimes, allocation behavior and contention are not
captured by this scaling. Counting adds synchronization; no timings from
the instrumented DLL were used. This does not measure PR96's total CPU/GPU
cost, light dispatch cost, or prove actual in-game performance neutrality.
The 0.3 ms synthetic workload must not be labeled this scene's frame cost.

The counting DLL built successfully and all ten focused tests passed,
including the actual registration hooks with counters enabled/disabled
and deterministic cross-frame counter tests. Counting implementation,
tests and their build registration are stashed separately and are absent
from the production commit.

Local raw snapshots and benchmark inputs are retained under
`build/ShadowSubmissionCounting-c610f1690-bc07aa988f/measurement-20260921-pid11228/`
in the primary checkout. `measurement.json` SHA-256:
`d855a0c0e7a1baf5291c336c2f374a81a98adc0e2604da0d7f45470c2a6ec277`.
`microbenchmark-comparison.json` SHA-256:
`2dd33898dc9091e7288c3d37f6b94ca34734ce8cc90bec100130928c5fb144ae`.
The adjacent counting build receipt retains the diagnostic source copies,
patch, build identity and test output.

## Validation and remaining evidence

```powershell
pwsh ./tools/cmake.ps1 -S . -B build/ALL -DCSX_REQUIRE_CLEAN_PROVENANCE=OFF
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target shadow_batch_submissions_test shadow_batch_hooks_test scene_light_snapshot_test vr_scene_guards_test native_lighting_material_guard_test light_limit_fix_vr_hook_policy_test -- /m:1
ctest --test-dir build/ALL -C Release -R '^(ShadowBatchSubmissions|ShadowBatchHooks|SceneLightSnapshot|VRSceneGuards|NativeLightingMaterialGuard|LightLimitFixVRHookPolicy)$' --output-on-failure -V
python build/shadow-admission-cost/compare.py
```

The Universal Release DLL (SE/AE/VR, DevBench enabled) built and linked.
The initial six focused controller tests passed in 0.32 seconds; populated entry
and bucket tests safely rejected/retried 80 and 196 injected failures.
This is a dirty development build, not a clean release producer or a
deployed AIO. No shader changes require new shader-cache generation.

The complete adversarial review adds installer coverage and runs the
existing shared transaction tests:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target shadow_batch_submissions_test shadow_batch_hooks_test shadow_batch_install_test virtual_function_hook_test scene_light_snapshot_test vr_scene_guards_test native_lighting_material_guard_test light_limit_fix_vr_hook_policy_test -- /m:1
ctest --test-dir build/ALL -C Release -R '^(ShadowBatchSubmissions|ShadowBatchHooks|ShadowBatchInstall|VirtualFunctionHook|SceneLightSnapshot|VRSceneGuards|NativeLightingMaterialGuard|LightLimitFixVRHookPolicy)$' --output-on-failure -V
```

All eight tests passed in 0.36 seconds. The submissions executable also
verified all seven hook prefixes and the group-layout pattern against
the retained PID 2912 live byte capture. Full output is retained in
`review-build.txt`, `review-tests.txt` and `review-live-bytes.txt` in the
same local evidence directory. Formatting the extraction script was
followed by rebuilding both extracted-body tests successfully.

Scoped C++/Markdown/whitespace pre-commit checks passed. Direct Gersemi
checks passed for the extraction script and changed CMake lines
1676--1736, with `--no-cache --workers 1`. Its default cache path failed
in this environment. Whole-file CMake formatting fails on the unchanged
`568e859f9` baseline too; unrelated rewrites were excluded. The scoped
commands and outputs are in `review-pre-commit.txt`,
`gersemi-extractor.txt` and `gersemi-scoped-cmake.txt`. `git diff --check`
passed. No live game or GPU performance validation ran during this review.

Remaining: matched loaded-scene CPU/GPU measurements with the same build
settings, warm caches and headset pose; protected submission counts in
other scenes; COC/menu/load freeze testing and shadow fidelity. Prefer comparing
the fixed `e18992eac` build against this candidate first. Comparing against
`568e859f9` also changes lifetime/dispatch correctness and can reproduce the
known freeze. No PR96-attributable GPU delta is established by these data.
