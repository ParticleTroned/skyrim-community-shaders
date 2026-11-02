# main-SE: second adversarial review

This review covers the complete incremental implementation in
`165eda4ca05ec6c3b1a39ad0475c2e7f4e0e62f2` and its diagnostics follow-up
`3299b34b35f9a45cf1c62bc3a11810a24711de43`, against the published SE baseline
`72424bfbee2dc03d416c9606269461f02c2aee0f`.

The source scope remains the 39 commits after
`f140c878138fe2783a6a4ad40420a3a2a996ccd0` through
`da73f53d8f6429d2d968cabde3c3eec9f3a04ab8`. The review examined the actual
implementation and native interface contracts; the earlier passing suite
and implementation report were not treated as proof that failure paths or
cross-subsystem state ownership were correct.

## Findings and corrections

### 1. P1: Reverse Z lost logical state across isolated context swaps

The new hooks virtualize depth, rasterizer and viewport state: callers see
their original settings while the device can use mapped reverse-depth
settings. NR's existing isolated D3D11 context swaps and `ClearState()` calls
were not represented in this bookkeeping. Restoring the native context could
therefore restore device state while leaving the logical cache empty or
belonging to the isolated pass. Later operations could select defaults or
map an already mapped state again.

The correction observes `SwapDeviceContextState` and associates logical
bookkeeping with the native context-state object's lifetime through COM
private data. It covers nested, self, null-output and exception-driven
restoration, and remaps when camera/target convention changes during
isolation. There is no reference back to the owning context-state object,
no unbounded global map, and no allocation after a state is warmed.

Capability admission is explicit: a base context without Context1 needs no
swap hook. An unverified interface layout or failed interface query prevents
Reverse Z activation before converted depths are published, preserving
standard rendering instead of installing an out-of-range vtable hook.

Affected implementation: `165eda4ca`. Regression:
`tests/reverse_z_context_state_test.py`, using the actual production hooks,
tracking helpers and existing `ScopedDeviceContextState` guard with narrow
D3D/COM boundary fakes.

### 2. P1: failed DLSS retirement could reuse an incompatible native context

The incremental port recreates DLSS when the actual camera depth convention
changes. Its teardown continued after failed/incomplete GPU-idle proof,
ignored native free failures, erased ownership and returned no failure
signal. Reversed-depth inputs could then be submitted to a still-live
standard-depth context. The earlier regression substituted an
always-successful teardown fake and missed this path.

Teardown now succeeds only after GPU completion and successful SDK release.
Failure retains the native convention, latches retirement and blocks
evaluation and dependent texture replacement. Generic frame/token resets
and feature-availability changes preserve ownership; only proven native
release or provider initialization/shutdown clears it. Failed cleanup can
retry without accepting incompatible inputs, including when the camera
switches back before the retry.

Affected implementation: `165eda4ca`. The expanded
`tests/se_upscaling_depth_contract_test.py` compiles the real wait, teardown,
reset and evaluation gate. It injects missing/failed queries, incomplete
completion, timeouts, missing APIs, SDK free failure, feature loss and
camera changes, checks reference balancing, and verifies successful retry.
Restoring the previous failure handling in a temporary mutation fails the
intended assertion that evaluation must stop.

### 3. P1: recoverable NR creation failure became permanent quarantine

The newly ported staged startup recognizes native creation out-of-memory as
recoverable and schedules safe retirement. However, the existing
`ReleasePassResources()` guard rejected every failed runtime status before
trying cleanup. Even after the caller proved GPU completion, retirement
permanently abandoned NR and retained its resources until process exit.

Cleanup now accepts safely retired feature creation, evaluation and
configuration failures, releases handles/scratch, clears parameter bindings
and restores the initialized backend for recovery. A fully empty admission
or initialization rollback preserves its diagnostic without pretending it
owns GPU work. Failed native release, partial rollback/shutdown and abandoned
ownership still refuse reuse; caller-side completion proof remains required.

The restrictive guard predates the incremental port; the integration defect
is attributed to `165eda4ca`, whose new recovery path depends on this cleanup.
`tests/neural_native_creation_test.cpp` now extracts the actual retirement
methods and verifies both recovery and unsafe cases. It failed on the
unchanged implementation and passes after correction.

### 4. P2: idle NR prewarm forced a memory-budget query on every call

After a creation-only prewarm fence completed, `PollNativeStartup()` forced
a fresh DXGI memory-budget query on every render/service call. It could not
finish that observation until an actual evaluation, so this repeated work
could continue throughout the 30-second prewarm lease.

Creation-only idle polling now uses the existing 250 ms memory-budget cache.
Completed real evaluation still forces its final fresh observation. The
regression executes 120 idle polls and verifies no forced query, then checks
that evaluation completion forces its final sample. Reservations, high-water
tracking and fence-identity requirements remain intact.

Affected implementation: `165eda4ca`. Regression:
`tests/neural_mono_startup_test.cpp`; the unchanged method failed the new
assertion before the fix. This proves removal of redundant query requests,
not a measured FPS or GPU-time gain.

### 5. P2: Release optimization could remove non-finite depth guards

The native Release configuration uses `/fp:fast`. The new camera-range and
raster-bias guards used `std::isfinite`, which finite-math optimization can
fold away. The actual camera-range predicate accepted an infinite far plane
when compiled with `-ffast-math` in the reproduction.

The guards now reuse `Util::IsFinite`, with a matching bit-based double
overload for camera ranges. The existing depth regression includes volatile
IEEE infinity and NaN inputs and runs in both ordinary and explicit
fast-math configurations. This avoids introducing a second finite-value
policy or relying on compiler assumptions about the inputs.

Affected implementation: `165eda4ca`. Regression:
`tests/reverse_z_depth_test.cpp`, including the additional portable
`reverse_z_depth_fastmath_test` target.

### 6. P2: Reverse Z API changes were missing from unsaved-settings status

The new `reverse_z` developer action changed a setting without requesting
the menu's cached dirty-state comparison. The footer/API could continue to
report that settings were up to date. Merely requesting a comparison after
assignment was insufficient before the first menu draw: it could establish
the already changed value as the clean baseline.

The action now uses the existing menu mutation lifecycle: reject blocked or
unavailable settings boundaries, preserve the baseline before assignment,
and request comparison only after a real change. Capture failure preserves
the previous value. Status queries and no-op setters do not trigger work.

Affected implementation: `165eda4ca`. Regression:
`tests/reverse_z_settings_mutation_test.py`, covering the actual action and
lazy-baseline/dirty-update methods before the first menu draw and after a
baseline reset, plus failed capture, blocked boundaries and malformed input.

## Scope, performance and reuse assessment

The [39-commit inventory](main-se-sync-2026-11-02-commits.json) remains the
scope record: 5 direct ports, 30 SE adaptations, 1 existing SE guarantee
preserved and 3 VR-only exclusions. The review found no additional missing
port in that range. SE frame generation, HDR, presentation ownership, current
UI, native render/output dimensions and mono NR remain preserved. No headset,
controller, per-eye FOV or stereo-seam facility was introduced by this review.

The fixes reuse the existing finite-value helper, menu baseline/dirty-state
methods, native feature-reset routines, memory-budget cache and scoped
context guard. The resource-ownership reset is centralized separately from
temporal-frame reset, so a generic cache invalidation cannot silently claim
that native resources have been released.

The existing synchronous DLSS retirement wait retains its 1,000 ms upper
bound. It runs for an owned context at resource/convention transitions and
failure retries; normal evaluation and cleanup without a context avoid it.
This review does not claim measured runtime performance or introduce an
unverified asynchronous retirement subsystem.

## Validation after the corrections

Both complete portable builds succeeded with GCC 13.3 and CMake/CTest 4.4.4.
The final suites ran after all six corrections and cross-review follow-ups
were present:

| Configuration | Passed | Failed | Skipped | Wall time |
| --- | ---: | ---: | ---: | ---: |
| Debug | 121 | 0 | 0 | 19.84 s |
| Release | 121 | 0 | 0 | 19.15 s |

The JUnit results contain 121 test cases in each configuration, with no
failure, error or skipped nodes. The Python suites reported no nested
dependency skips. The existing SE baseline, actual ImGui layout coverage,
compressed-reconstruction policies, resource-generation contracts, packaging
checks and shader dependency validation remained enabled.

The three additional CTest entries are
`reverse_z_context_state_test`, `reverse_z_settings_mutation_test` and
`reverse_z_depth_fastmath_test`. Existing DLSS ownership, NR retirement and
NR startup tests gained focused failure-path assertions. Their boundary
fakes exercise the relevant production methods rather than replacing the
new cleanup behavior with an always-successful mock.

The important reproductions were also checked against the earlier behavior:

- Restoring untracked context-swap behavior fails the logical-state
  restoration assertion.
- Restoring successful-looking DLSS teardown after failed completion or
  release fails the assertion that evaluation must remain blocked.
- The original NR retirement guard fails the recoverable creation-failure
  test; the corrected guard also preserves rejection of unsafe cleanup.
- The original idle-prewarm polling forces refresh requests and fails the
  bounded-query assertion.
- The old range predicate accepts an infinite far plane under fast-math;
  the bit-based predicate rejects it under ordinary and fast-math builds.
- Removing pre-mutation baseline preservation fails the first-menu-draw
  dirty-state regression.

Changed C++ ranges were formatted with clang-format 22.1.4, and
`git diff --check` passed. No HLSL or shader package files were changed by
this correction commit; the initial port's shader diagnostics remain
recorded separately in the [initial validation report](main-se-sync-2026-11-02-validation.md).
Its 118-test totals describe that earlier revision and are not substituted
for the 121-test results above.

### Reproduction

Use the portable configure commands and dependency pins in the initial
validation report, then build and test both configurations:

```sh
cmake --build build/portable-se-debug --parallel 4
ctest --test-dir build/portable-se-debug --output-on-failure --parallel 4 --output-junit debug-results.xml
cmake --build build/portable-se-release --parallel 4
ctest --test-dir build/portable-se-release --output-on-failure --parallel 4 --output-junit release-results.xml
```

The complete runs used verified NR SDK headers, the actual Catch2/ImGui/fmt
dependencies, and hlslkit on the Python import path. No NR runtime DLL is
needed for these host tests or included with this review.

### Native validation limits

These results establish host-side behavior and extracted production-method
contracts. A Windows MSVC plugin build, native D3D11/FXC/WARP execution,
live NGX evaluation and in-game SE/AE performance or image quality were not
run in this Linux environment. The explicit fast-math test uses GCC's
`-ffast-math`; native MSVC `/fp:fast` still needs the Windows build.

The Windows follow-up should exercise nested NR context isolation with
Reverse Z enabled, menu-to-world depth-convention transitions and failed
DLSS retirement/retry, NR creation out-of-memory followed by safe recovery,
and a `reverse_z` developer setting change before the menu's first draw.
Repeat normal render-scale, frame-generation and HDR presentation checks on
the supported SE and AE runtimes. These are remaining environment checks,
not claimed passes or known unfixed defects.
