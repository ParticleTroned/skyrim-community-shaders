# Native VR frustum workload telemetry

This instrumentation is compiled only with `DEVBENCH_BRIDGE_ENABLED`.
It observes native VR culling without changing visibility, material/light
ownership, render scale, quality, renderer locks or depth-culling policy.
SE and AE install no native frustum hooks. Production builds without the
bridge contain none of this storage, instrumentation or diagnostic reads.

## Why schema 2 replaces schema 1

The first measured build (`37ce48ff668609d96c82f86875eafbf5c5a1a706`)
spent substantial sampled CPU time in per-sphere address hashing. The
address table also overflowed heavily in exterior scenes. Zero retained
repeats did not prove that objects or plane sets were unique. The retained
report is `build/bisect/measurements/gameft-sw-20260918T080954813Z-144f376b/frustum-analysis/README.md`.

Schema 2 removes that address table entirely. Each compound-object visit
resolves cached counter rows once. Its normal sphere calls update local
integer totals, published at compound return/unwind. Only unmatched sphere
calls take the independent cached-row path. No hash/probe, frame read,
slot lookup, atomic publication or object dereference is required for each
ordinary compound-owned sphere test. The hook still verifies owner,
bound and native caller before attributing a sphere call to its parent.

`communityshaders.menu` and `communityshaders.renderscale` status retain
`nativeFrustum` with `schemaVersion=2`. Existing game-ft/gameft-sw status
receipts carry the new evidence without new polling, changed holds or
changed measurement windows. Historical schema-1 receipts stay unchanged;
their address-classification fields have no schema-2 equivalent.

## Exact counters and attribution

| Field                                                            | Meaning                                                                                                                                                        |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `installed`, `installError`, `active`                            | All verified native/pass hooks installed together, or none. Missing installation is unavailable evidence.                                                      |
| `enabled`, `detailEnabled`, `collectionGeneration`               | Counts and bounded detail samples default on in DevBench. Either toggle increments the generation without clearing cumulative totals.                          |
| `threads[].slot`, `threadId`, `registeredAtQpc`                  | One lifetime slot per observed thread, never reused. Windows may reuse thread IDs.                                                                             |
| `observedFrames`, `firstObservedFrame`, `lastObservedFrame`      | Producer-frame observations at compound entry. Not fpsVR frames or verified native job ownership.                                                              |
| `passEntries`                                                    | Same-thread scope entry counts, including player view, depth, world, shadows, water, cubemap and recovery.                                                     |
| `rows[].routine`                                                 | `compound_object`, `sphere_intersect`, or `sphere_not_fully_inside`. Do not sum these as unique objects.                                                       |
| `pass`, `rendererCameraIndex`                                    | Innermost same-thread pass and raw renderer field, or unknown/null. Workers do not inherit render-thread context. The camera field is not a verified eye ID.   |
| `callerAddress`, `skyrimImageBase`                               | Native return address and Skyrim module base; resolve the owning module before deriving RVAs.                                                                  |
| `calls`, `completed`, `rawTrue`, `rawFalse`                      | Exact retained invocations and native returns, not sampled estimates. Exceptions do not become completed calls.                                                |
| `workByOutcome`                                                  | Sphere calls spent in completed compound visits, separated by that compound's native acceptance/rejection. Also counts objects returning without sphere tests. |
| `detachedSphereCalls`                                            | Sphere calls whose owner/bound/caller did not match the current compound. They retain separate routine/caller totals and are not charged to a guessed parent.  |
| `rowLookups`                                                     | Row-cache misses, rather than all invocations.                                                                                                                 |
| `rowResolutionFailures`, `unattributedCalls`                     | Failed row resolutions and exact calls lost to row capacity, respectively. These are different quantities.                                                     |
| `threadOverflowCalls`, `threadOverflowPassEntries`               | Calls/scope entries for which no lifetime thread slot remained.                                                                                                |
| `snapshotStartQpc`, `snapshotEndQpc`, `qpcFrequency`, `qpcValid` | Bounds/validity of the status read. Atomic fields are independent snapshots, not a transaction.                                                                |

There are 64 lifetime thread slots and 128 rows per thread, with at most
eight row probes on a cache miss. Each row has one owning writer. Native
work always continues when diagnostic capacity is exhausted. In-flight
sphere batches can be absent at a status boundary; calls already entered
can complete across collection toggles. A completed-object outcome bucket
excludes incomplete traversals, which still contribute partial sphere totals.

## Sampled operator, plane and construction evidence

Traversal detail admits the first and every 256th compound visit, capped
at one sample per thread/known producer frame. Construction detail admits
the first and every 16th call of each observed operation, also capped at
one per operation/thread/frame. Changing the generation restarts these
budgets. Unknown frame identity prevents detail sampling. This is bounded,
deterministic sampling, **not an unbiased object population**.

Each thread retains four recent traversal and four construction samples.
Sequence numbers expose replacement of older samples. A successful snapshot copies its publication count under the same lock as its samples; an unavailable snapshot reports null counts. Publication and
status copying use a nonblocking try-lock on the sample ring; contention
is reported, never waited upon. Hook recording allocates no heap memory.
JSON allocation/serialization happens only during status requests, outside
the sample lock. Large status replies still have a cost.

| Evidence                                                           | Contents and limits                                                                                                                                                                                                                                                          |
| ------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `traversalDetails.samples`                                         | Thread/frame/generation/context, owner/object/caller addresses, raw four-word bounding sphere, native completion and acceptance. Addresses are not lifetime IDs.                                                                                                             |
| `structure.header`                                                 | Plane/operator array addresses, storage/used counts, first operator, prethreaded byte, validity.                                                                                                                                                                             |
| `structure.operatorSlots`                                          | Up to 256 raw three-word body slots. Some slots are operands, not independent instructions. Storage beyond the body is not copied as additional instructions.                                                                                                                |
| `steps`                                                            | Up to 128 path steps (observed sphere calls plus verified control fall-throughs): operator index/opcode, plane-set index, true/false successor indices, native result and active mask before/after.                                                                          |
| `planeSets`                                                        | Up to 128 distinct visited plane sets, copied before their first sampled test. The 28 raw uint32 words preserve six float4 plane equations and trailing mask/state bytes without float/NaN JSON conversion.                                                                  |
| `sphereTotals`                                                     | All matched sphere calls/results for the sampled object, even if its detail trace exceeds a limit.                                                                                                                                                                           |
| `terminalOperator`, `terminalOpcode`, `terminalVerified`           | Whether the observed final branch reaches native accept/reject consistent with the unchanged native return.                                                                                                                                                                  |
| `completionKind`                                                   | Native return consistent with zero-bound rejection, empty-program acceptance, operator terminal, or explicitly unverified.                                                                                                                                                   |
| `headerStable`, `boundStable`, `operatorChanged`                   | Header/bound stability and observed operator/operand mutation. A detected change invalidates reconstructed terminal evidence. These checks do not prove absence of transient concurrent changes.                                                                             |
| `chainValid`, `stepLimit`, `planeLimit`, `readFault`, `masksValid` | Explicit evidence limits. Unknown operators, mismatched arguments and unreadable metadata do not become inferred paths.                                                                                                                                                      |
| `generation`, `generationAtEnd`                                    | Samples crossing a toggle are identifiable.                                                                                                                                                                                                                                  |
| `constructionCounters`                                             | Exact invocations/completions of each observed builder operation.                                                                                                                                                                                                            |
| `constructionDetails.samples`                                      | Before-prefix/after-appended operator structures, caller/owner/argument, frame/context/generation and newly appended plane bytes. Copy/reset sample from zero; append samples start at the previous used operator/plane counts, including additions beyond the prefix limit. |
| `snapshotAvailable`, `published`, `publishContentionDrops`         | Ring snapshot availability, successful publications and explicit contention loss. Rings are recent examples, not a complete event log.                                                                                                                                       |

Diagnostic reads check committed readable, non-guarded memory first and
isolate access/in-page faults in the byte-copy helper. Native calls remain
outside that exception handler and are forwarded exactly once. Bounds,
read failures, unknown opcodes and diagnostic limits never affect native
arguments, masks, return values or rendering admission.

## Live native contract

Addresses/layouts are grounded in the retained **live** Skyrim VR 1.4.15
capture, not analysis of its packed on-disk executable:

```text
D:\Coding\GitHub\GhidraProjects\CPU-DLSS-20260916-pid21340\NativeCPUHotRanges.gpr
```

Verified snapshot SHA-256 values:

-   `00DA3000`: `87fd13008bcae21d1478342d1fd593e807d36b18ea6b29bd450563b95f46ae8d`
-   `00DA5000`: `e1dfbdf56819b313fd5247cff2bfec9e74ed68efeffe5e88a5df7fd053b71a5c`

| RVA        | Observed contract                                                                                                                     |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| `0xDA33C0` | Two-pointer compound-object call, native boolean in AL. Zero raw radius at object+0xF0 returns false; no used operators returns true. |
| `0xDA5410` | Three-pointer sphere-intersection call, invoked by opcode 7; return address `0xDA345E`.                                               |
| `0xDA54B0` | Three-pointer not-fully-inside sphere call, invoked by opcode 8; return address `0xDA343F`. Raw true is not synonymous with culled.   |
| `0xDA3050` | Two-pointer void copy, source first and destination second.                                                                           |
| `0xDA3750` | Two-pointer void camera/reset operation.                                                                                              |
| `0xDA38E0` | Two-pointer void builder appending intersection/plane operand slots.                                                                  |
| `0xDA3C10` | Two-pointer void builder appending branch/plane structures. The descriptive label does not establish the input object's engine type.  |

The owner holds plane data at +0x00, plane storage at +0x10, operator data
at +0x18, operator storage at +0x28, used plane/operator counts at
+0xB8/+0xBC, first operator at +0xC0 and prethreaded at +0xC5. Live code
uses **12-byte operator slots**, not pointer-sized entries from the
incomplete CommonLib declaration. Plane sets have stride 0x70; the active
mask is at +0x60. A test's next operator slot contains its plane index.
Its raw native result selects +4 (true) or +8 (false) of the test slot. The sampled operator and plane operand are rechecked after the native sphere call; changed metadata stops reconstruction. Bounds are also rechecked before explaining an early return.
Terminals 2 and 3 return true and false respectively. Captured construction
control opcodes 4-6 follow +8 without invoking a sphere test; these edges
are explicitly labelled `verified_control_fallthrough`, with null rawResult.
Only sphere branches contain an observed native boolean. Unknown opcodes
stop reconstruction. No visibility calculation is replayed or substituted.

Native dispatch can reach terminal slots beyond the constructed body count.
Diagnostic instruction reads admit only opcodes 2 and 3 in that trailing
allocated storage; ordinary instructions and operands remain body-bounded.
Unreadable, out-of-storage or nonterminal trailing slots invalidate the
reconstruction. `detailOperatorLimit`, `detailStepLimit` and
`detailPlaneLimit` advertise the independent capture bounds. Construction
plane copies use the same plane limit. Older schema-2 receipts retain
their original advertised bounds and evidence limitations.

Only those four verified construction paths are hooked. Other builders,
threading/relinking and job transfer are not exhaustively observed. In
particular, no hook was invented for `0xDA41E0`, absent from this captured
range. Before/after records and the eventual traversal structure permit
comparison, not an assumed causal join merely because an address matches.

Installation requires VR 1.4.15 and matching prefixes for all seven native
entries. Targets must be executable and distinct; vtable reads are checked.
All attachments use one abortable Detours transaction, preserving existing
hook chains. Unrecognized native entries disable telemetry. Existing pass
signatures reuse FrameAnnotations contracts. No permanent D3D11 protection
or production draw hook is introduced.

## What the next run can establish

Within each save and render pass, compare compound visits, sphere calls
per visit, rejection rate and sphere work spent on accepted versus rejected
objects. Use sampled chains to identify repeated plane sets, inactive
masks, long all-accepting chains, and which observed branch terminates at
rejection. Compare construction count/growth and constructed plane bytes
with the structures actually traversed.

A compound false result eliminates that object's acceptance at this native
stage. It **does not by itself measure draws, descendant visits or GPU
milliseconds saved**. Those need downstream caller/renderer evidence or a
controlled culling A/B. Neither two eyes nor changing bounds/planes imply
redundant work. Worker pass and verified eye attribution remain unresolved.
Source inspection alone cannot establish runtime cost.

The measured follow-up and production optimization priorities are in
[the accepting-path investigation](frustum-optimization-investigation-20260918.md).
That report distinguishes native CPU work from diagnostic cost and does
not infer GPU savings from object rejection counts.

Use unchanged enabled generations at both window endpoints, including
`collectionGenerationAtEnd`, and retain snapshot/frame boundaries and loss
counts. Existing +49/+59-second health differences are not exact +50/+60
WPR windows. Do not silently relabel them. No extra polling or change to
saved gameft-sw timing/health scheduling is required.

`set_frustum_telemetry_enabled` and
`set_frustum_telemetry_detail_enabled` take boolean `enabled`. The latter
allows counts-only versus detailed collection without changing culling.
Neither saves settings or resets cumulative counts. Disabled collection
retains detours, so it is not an uninstrumented baseline. Compare like
instrumentation states before attributing frame-time changes to rendering.

## Validation of the batching revision

The original hot-path regression test was built and failed before the
correction; its output is retained under
`build/validation/frustum-batched-20260918/red-test.log`. The production
policy test now covers cached batching, exact work/result accounting,
partial counts on unwind, nested/disabled scopes, capacity loss, control
boundaries, sample budgets, raw branch/plane consistency, invalid metadata,
truncation, plane ranges, early returns and concurrent snapshots. The
DevBench-off target compiles the actual implementation with diagnostics
excluded. The menu contract test validates the detail toggle schema.

An isolated Release `/O2` counter benchmark executes 100,000 object visits
with 64 sphere calls each, using the preserved old production policy and
the new policy, and verifies equal total calls. Seven alternating A/B
rounds gave median **78.4708 ms old versus 5.6823 ms new** (92.8% lower).
This tests counter machinery only: it excludes detours/TLS checks, detail
reads, JSON, native culling and rendering. It is not an in-game speedup or
an estimate that can be subtracted from fpsVR/WPR timings. Exact source,
CSV and executable remain in the same validation directory as
`counter-benchmark.cpp`, `counter-benchmark.csv` and
`counter-benchmark.exe`.

The revised DLL still needs live installation/collection validation and
matched overhead measurement. No visibility optimisation is made here.

Initial local validation on 2026-09-18, universal Release (SE/AE/VR on,
DevBench on), used:

```powershell
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests shader_tests -- /m:1
ctest --test-dir build/ALL -C Release -N
ctest --test-dir build/ALL -C Release --output-on-failure --no-tests=error --timeout 300
pwsh ./tools/pre-commit.ps1 run --files src/Diagnostics/VRFrustumTelemetryPolicy.h src/Diagnostics/VRFrustumTelemetry.cpp src/Diagnostics/VRFrustumTelemetry.h tests/vr_frustum_telemetry_test.cpp tests/menu_devbench_preflight_contract_test.cmake src/MenuDevBenchBridge.cpp src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp docs/development/vr-frustum-telemetry.md
pwsh ./tools/git.ps1 diff --check
```

The DLL and both test groups built. Inventory: 126; passed: 126; failed,
skipped and not-built: zero. Final CTest took 50.23 seconds; the final
DLL/build-both-groups/inventory/CTest sequence took 151.00 seconds.
Scoped formatting/pre-commit and `git diff --check` passed. This initial
validation preceded the adversarial review and commit below; it included
no runtime benchmark or deployment.

The additional control-operator regression failed before its correction
(`control-flow-red-test.log`) and passed afterwards. It covers verified
control fall-throughs both before sphere calls and before the terminal;
unknown/cyclic paths stay bounded and unverified. The final complete suite
includes that correction.

The validation directory retains the final logs, inventory, DLL manifest,
DLL/PDB hashes and original failing-test evidence. Compiled identity:

-   Base commit: `e98955d293acbb83782ac9925bd5b06645b56d91` plus the recorded dirty source tree.
-   Build ID: `09a48c6e8df7000c3232b30f425eb3f8b9775df63c386847fcfdd34c3e4652a1`.
-   DLL SHA-256: `0b3698661d68fd27c5c666bb32cffc52a1852dca68248817874bacb59c5e14be`.
-   PDB SHA-256: `60bf4a31afc52deca9d8c364a85ee9cc04f0bfff8c9dd4a8d26beb24df339adb`.

Other pre-existing working-tree changes were preserved. No saved assay,
automation runner, preset or rendering setting was changed for this work.

## Adversarial review, 2026-09-18

| Area                 | Finding and correction                                                                                                                                                                                                                                |
| -------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Correctness          | A native sphere call could change an operator or its plane operand while the diagnostic retained the old branch. Re-read both after the call and invalidate reconstruction on disagreement or unreadable metadata. Preserve the actual native result. |
| Correctness          | A changing object bound could falsely explain a zero-bound early return. Re-read the bound at completion and require matching bytes plus no observed sphere calls before claiming an early-return explanation.                                        |
| Concurrent reporting | Reading publication counts after releasing the sample-ring lock could pair new counts with old samples. Copy both under the same nonblocking lock; report unavailable metadata as null if the snapshot is contended.                                  |
| Ownership            | Make the diagnostic detail scope noncopyable, matching the other scoped TLS owners. Nested calls restore the previous diagnostic context.                                                                                                             |
| Production exclusion | Compile the actual implementation without DevBench and inspect its object sections: no code, data or TLS. Build the full universal DLL with `DEVBENCH_BRIDGE=OFF`; verify the frustum schema/control markers are absent.                              |
| Production build     | That full OFF build exposed unused diagnostic-only values in existing upscaling code under `/WX`. Add four `[[maybe_unused]]` annotations, keeping current-state updates, signatures and warnings enabled. No runtime logic changes.                  |
| Scope and DRY        | Reuse the production policy in regression tests, centralize bound reads, retain the existing guarded hook/status integration and shared memory-read checks. No new logging, scheduling, render setting, graphics lock or visibility policy.           |

The operator and bound regressions each failed before their correction.
Tests also cover a changing plane operand, an actual sphere call with no
retained detail step, and publication counts coherent with a copied ring.
Evidence is under `build/validation/frustum-review-20260918/`, including
`red-test.log`, `bound-red-test.log`, `production-exclusion.json` and
`off-object-check.json`. The OFF DLL SHA-256 is
`6e3712ebcba9c4cf1a73180e16e6528d6ed9ef35ac5a84abd71dd92259e0a70c`.

Production-exclusion commands (the final configure restores DevBench):

```powershell
pwsh ./tools/run-msvc-command.ps1 cl.exe /nologo /std:c++20 /O2 /c /I src /Fobuild/validation/frustum-review-20260918/frustum-off.obj src/Diagnostics/VRFrustumTelemetry.cpp
pwsh ./tools/cmake.ps1 -S . -B build/ALL -DDEVBENCH_BRIDGE=OFF
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 -S . -B build/ALL -DDEVBENCH_BRIDGE=ON
```

Only changed line ranges in `Upscaling.cpp` and `Streamline.cpp` are
clang-format checked because full-file formatting changes unrelated
existing code. All other changed files receive the scoped pre-commit
checks listed above; the remaining hooks also cover these two files.
An additional full-file gersemi check flags pre-existing CMake formatting
in the menu contract test. Its new block follows gersemi formatting; the
unrelated existing layout is preserved.

After the corrections, the restored DevBench-on universal DLL and both
explicit test targets built successfully using the commands above.
CTest discovered and ran 126 tests: 126 passed, zero failed, skipped or
not built. CTest took 50.02 seconds; DLL build, test builds, inventory and
CTest together took 169.45 seconds. Scoped pre-commit, changed-line
clang-format 22.1.4 and `git diff --check` passed. Both the OFF production
build and ON test build retain warnings as errors. Final archive receipts
record the committed source, rebuilt DLL/PDB and package hashes.

Live installation, useful detail retention and matched in-game overhead
remain unmeasured for this revision. Samples are bounded observations,
not atomic snapshots of the native engine. Matching metadata at two reads
cannot prove that it never changed between them. No speculative native
hook or visibility optimisation is added to fill these evidence gaps.
