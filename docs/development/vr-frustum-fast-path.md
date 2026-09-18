# Experimental native frustum fast path

The production menu exposes **Experimental Frustum Fast Path** beside the
VR depth-culling options. It defaults to off, applies to subsequent calls
immediately, and persists as `FrustumFastPathEnabled` when settings are
saved. It is independent of Balanced, Performance and Legacy temporal
policy. It changes none of their recovery rules, defaults or quality.
Unsupported native implementations leave the checkbox unavailable.

This is the first CPU experiment, not a demonstrated performance gain.
Visibility and plane-mask effects must remain native-equivalent. It does
not reduce the intended GPU workload or merge the two eyes' evaluations.
Matched runtime measurements and visual checks remain required.

## Evidence motivating the experiment

The completed-path recording is
`gameft-sw-20260918T112309357Z-f14a7ecd`, with source
`abef2dfa312a1ae9c20a96c0481e2667c9102f08` verified by its producer and
physical build receipts. Its Build ID is
`1ad2b764bfcbf6eba8172a4f74b5cf6f2bd884778e11544439f0aee9321b27c2`,
DLL SHA-256 is
`e029ee054ed34ba92566bc0527f86957794d95e178d199952084e08614865451`,
and PDB SHA-256 is
`b6abca07750d81ff46a94d6733a9d4b56fa668daab8ed411d6b2980896a49942`.
The build receipt, settings and analysis remain in
`build/bisect/measurements/gameft-sw-20260918T112309357Z-f14a7ecd/`.

The native render-thread traversal costs in the saved WPR tail windows
were approximately 0.104/0.340/0.749 ms per recorded frame for DLSS saves
11/12/13. The longest retained first-active-plane true paths contained
15/54/67 tests. These are sampled native costs and observed paths, not
promised savings. Parallel worker CPU sums are not frame-time savings.
All 1,512 captured sphere results and mask effects matched reconstruction.
There were 36 retained complete DLSS depth cases. No repeated exact DLSS
plane sets were established; construction was a smaller sampled cost.

The earlier investigation is retained in
[the investigation report](frustum-optimization-investigation-20260918.md).
The later live snapshot in
`D:\Coding\GitHub\GhidraProjects\Frustum-20260918T113900969Z-pid30424\FrustumConstruction.gpr`
resolves its builder gap: `0xDA41E0` constructs paired planes;
`0xDA4D10` prepares the threaded branch links. The experiment consumes
already-prepared programs and does not change construction.

## Admission and fallback

The startup hooks install only on VR 1.4.15 after matching the complete
live-snapshot compound and opcode-8 sphere instructions, including the
referenced floating-point sign constant. An unknown patch or failed hook
transaction disables the experiment. SE and AE do not install these hooks.

On the thread executing the native depth-pass entry, the candidate:

1. Reads the current object's bound and current prepared program. There
   is no pointer, frame, pass, view or cross-eye visibility cache.
2. Walks at most 128 consecutive opcode-8 true edges. For each plane set,
   reads its current six-bit mask and tests the first **active** plane.
3. Matches the native scalar SSE operation order without FMA or
   reassociation. Admits positive normal radii, normal or zero finite
   operands, nearest rounding and masked floating-point exceptions.
4. Handles only a complete path ending at the actual accept/reject
   terminal with unchanged header and object-bound bits. Every admitted
   sphere operation returns true without changing a mask; a true sphere
   result does not itself mean the object is accepted.
5. Otherwise calls the original native entry. The speculative prefix has
   made no mask, graph or object writes, so native side effects occur as
   usual. Invalid reads, unsupported opcodes, zero/high-bit masks, longer
   or cyclic paths and special numeric cases all take this fallback.

Reads use the native caller's existing lifetime/ownership. They add no
shared cache, retained raw pointer, lock, task or synchronization change.
Repeated metadata reads detect observable changes; they are not proof
against an otherwise illegal concurrent engine mutation. Workers outside
the calling-thread depth scope always use native evaluation.

The shared layout readers live in `Utils/VRNativeFrustum.h`; diagnostics
and the experiment use the same definitions. Production contains no
verification mode, counters, diagnostic status serialization or per-call
logging. The default-off production path still forwards through startup
hooks and the depth scope; zero overhead is not claimed for that path.

## DevBench verification and measurement

`communityshaders.menu` exposes `set_frustum_fast_path_enabled` and the
DevBench-only `set_frustum_fast_path_verification`, both requiring a boolean
`enabled`. The production setter stages the saved setting. Verification
requires the production toggle on; it always returns the native result
and compares each handled prediction and every visited mask. A mismatch
latches native fallback until process restart.

`frustumFastPath` status in both menu and render-scale health receipts
reports installation, requested enable state, verification state,
effective mode (`native`, `verify`, `fast`), mismatch latch and cumulative attempts, handled
calls, first-plane tests on handled calls, fallbacks, verified calls and
mismatches. Speculative work before fallback is not included in that
first-plane total; use sampled CPU stacks to assess its cost.
Counts accumulate in TLS and publish at depth return; an in-flight depth
pass is absent, and individual atomic counters are not a transactional
snapshot. Only compare intervals with unchanged settings. Changing either
experiment mode, or the first verification mismatch, advances the existing
frustum collection generation. An armed toggle can therefore report
effective mode `native` after a verification failure.

Detailed frustum samples deliberately force native execution so their
sphere counters and path reconstruction remain real. They cannot measure
the shortcut itself. Non-detailed aggregate sphere counts exclude
bypassed calls; do not interpret fewer native sphere calls as fewer
visible objects or draws. Equivalent diagnostics settings are required
on both sides of a performance comparison.

Run verification first, then compare off/on with verification off, using
the same build, saved gameft-sw protocol, fixed HMD pose, saves, settings
and tracing. Keep all other variables unchanged. Assess CPU means/tails,
sampled native/CSX work, waits/ready delay and eligible/fallback counts.
Check stereo visibility under head movement and loading/COC transitions.
No runtime savings, stereo or COC pass has yet been established.

## Differential test and validation

`VRFrustumFastPath` includes the production policy header without
`DEVBENCH_BRIDGE_ENABLED`. It executes preserved live native instructions
as an independent oracle, with only relocated relative calls/constants.
The fixture retains 36 captured paths, alongside 12,000 seeded randomized
numeric cases, 16,384 raw-bit cases across all four DAZ/FTZ combinations,
and boundary/fallback cases. It compares visibility and complete
plane-set/mask state, including mask-writing native fallback. No duplicate
C++ reconstruction serves as the correctness oracle.

Oracle bytes come from the verified live snapshot
`D:\Coding\GitHub\GhidraProjects\CPU-DLSS-20260916-pid21340\snapshots\`,
never the packed static executable. Byte-range SHA-256 values:

| Routine          | RVA    | SHA-256                                                            |
| ---------------- | ------ | ------------------------------------------------------------------ |
| Compound         | DA33C0 | `382adc28c01519b4f4dadcc37ed74b19bc63267f0c297b3c02bc4b1855c4f148` |
| Intersect        | DA5410 | `1b3a55528c4c694354779ab3b5b7011604412969f08ff1faf6d219d5b5d222dd` |
| Not fully inside | DA54B0 | `37d84a17580cb8843fbf875508824bc56e9159343d2236b8f7a172be1dfe1b12` |

Validation receipts are under
`build/validation/frustum-fast-path-20260918/`. Runtime qualification is
pending; source inspection and offline equivalence alone do not prove
runtime cost or third-party hook compatibility.

Commands used for this implementation:

```powershell
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests shader_tests -- /m:1
ctest --test-dir build/ALL -C Release -N
ctest --test-dir build/ALL -C Release --output-on-failure --no-tests=error --timeout 300
pwsh ./tools/cmake.ps1 -S . -B build/ALL -DDEVBENCH_BRIDGE=OFF -DAUTO_PLUGIN_DEPLOYMENT=OFF
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 -S . -B build/ALL -DDEVBENCH_BRIDGE=ON -DAUTO_PLUGIN_DEPLOYMENT=OFF
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/git.ps1 diff --check
```

Both explicit test groups built. CTest discovered and passed 127 tests,
zero failed, skipped or not built, in 154.37 seconds. The differential
test handled 22 of the 36 recorded paths and matched native results and
mask effects; the remaining paths use native fallback. Randomized tests
and the numeric, mutation, storage and limit cases also passed.

The universal Release DLL builds with DevBench on and off. The OFF DLL
contains the production checkbox and serialized setting; inspection finds
none of the experiment verification, counter or frustum-schema markers.
Its manifest verification passes. The preserved OFF DLL SHA-256 is
`e74cef7b9c0656250ad4cb8a0d84ec61ceea992632555658b0db286ee235c30b`;
see `production-exclusion.json` and `production-manifest-check.log`.
All builds keep automatic deployment off. No game was launched or modified.
The final restored DevBench build and manifest verification also pass.
Its DLL SHA-256 is
`786fb8335f0ba5e63db8f156df6cf9b6d4b2a7840e3bb2bf87a58cb4f24cd30c`.
An initial build failed to link the newly added implementation; the next
build compiled that source and linked successfully. Both attempt logs
are retained as `dll-on-build.log` and `dll-on-build-retry.log`; the final
confirmation is `devbench-final-build.log`. No failed test is counted as
a pass, and no behavioral red-test claim is made for this new experiment.

Scoped pre-commit passes. For existing `VR.cpp` and `FrameAnnotations.cpp`,
clang-format checks only added line ranges; unrelated existing layout and
includes are preserved. The new CMake registration block passes gersemi
in isolation; full-file gersemi would reformat unrelated existing blocks.
The remaining applicable hooks cover those files normally. Exact logs
are `pre-commit-focused.log`, `pre-commit-existing-files.log` and
`changed-format.log` in the validation directory.

## Adversarial review, 2026-09-18

The review keeps one functional experiment: the optional native-equivalent
CPU traversal. It does not change depth-culling temporal policy, shader
quality, render scale, submission/relatch behavior, material admission,
light lifetime or graphics-context ownership.

| Area                   | Finding and disposition                                                                                                                                                                                                                                                                  |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Saved evidence         | The menu exposed experiment status but the render-scale health receipt used during the saved assay omitted it. Add the same status producer to that receipt, with a regression that failed before the correction. No runner or timing rule changes.                                      |
| Failure reporting      | A verification mismatch disabled the fast path without advancing frustum collection generation. Advance generation only on the first latch; expose effective `native`/`verify`/`fast` mode separately from the requested checkbox state.                                                 |
| Numeric correctness    | Extend the executable live-native differential oracle with 16,384 raw-bit cases under all four DAZ/FTZ combinations. These include unsupported numeric values and fallbacks, complementing the original 12,000 numeric cases. No visibility or mask mismatch was reproduced.             |
| Hook scope             | The existing annotation depth hook is conditional on frame annotations. Reusing it would lose the experimental scope when annotations are off. Retain the independent guarded depth hook; worker threads remain native.                                                                  |
| Ownership and fallback | No object, view, eye or pass result cache; no retained raw pointers, allocations, mask writes or graphics locks. Ineligible prefixes restart at native entry. Header/bound rereads do not establish immunity to illegal concurrent native mutation.                                      |
| DRY                    | Diagnostics and the candidate share native layout readers. Menu and saved health receipts share the status producer. Tests include the production policy and use executable native instructions rather than a duplicate C++ algorithm.                                                   |
| Production exclusion   | All review code that changes status, counters or mismatch reporting remains inside `DEVBENCH_BRIDGE_ENABLED`. The production traversal and checkbox are unchanged by the review. The earlier full OFF build/exclusion check remains applicable; the focused OFF compilation is repeated. |

Review receipts are in
`build/validation/frustum-fast-path-review-20260918/`. The saved-health
regression is retained in `receipt-red-test.log`; all five focused tests
pass in `focused-tests.log`. Changed-line formatting also covers the
render-scale bridge, preserving its unrelated existing formatting.

Runtime performance, stereo/head-motion behavior, loading/COC stability
and third-party hook interactions remain test requirements. Offline
equivalence and successful builds do not establish those results. The
testing AIO keeps the feature off by default and includes DevBench for
verification and the unchanged gameft-sw capture protocol.

The reviewed universal DevBench DLL and both explicit test groups build.
The complete suite passes 127/127 tests, with zero failed, skipped or
unbuilt tests, in 221.86 seconds (`ctest.log`). Scoped pre-commit,
changed-line formatting and whitespace checks pass. The committed AIO is
rebuilt after the commit; its adjacent receipt records its exact Build ID,
DLL/PDB/vendor hashes, unrelated dirty-tree state and archive verification.
