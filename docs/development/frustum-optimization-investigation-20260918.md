# Native frustum optimization investigation, 2026-09-18

## Conclusion

The strongest measured CPU target is the long native compound-frustum
traversal in the DLSS depth pass. Identical-plane memoization and removal
of program copies are lower priorities. The retained paths instead support
investigating a narrowly guarded batch of consecutive first-plane tests
that take the true branch without changing any mask. This is an
optimization candidate, not an implemented or measured speedup.

No production visibility, quality, render-scale setting, renderer lock,
material guard, light ownership or GPU workload was changed. This work
fixes two diagnostic capture limitations and analyzes existing recordings.
Source inspection and offline replay alone do not prove runtime savings.

## Exact evidence

-   Run: `gameft-sw-20260918T100605827Z-44ead541`.
-   Producer source: `75b9b558c311de755c1803b59c2cf29ab303a19f`, with the
    dirty-tree state preserved in the run provenance; this is not inferred
    from the branch or package name.
-   Build ID: `44fe14aab777075ed1839a8234d04eef6689d00e6da1c892fc8c3b9c216fe89d`.
-   DLL SHA-256: `0b2a742c22bf8a397e051306b41d8819d301eaa73b070230966ca557e15c540e`.
-   PDB SHA-256: `d63613777f86593f7f1c4fba8f2635742f0241c78b7bdd4b205de878f293e454`.
-   Streamline 2.14.1 / DLSS 310.9.1; saves 08, 11, 09, 12, 10, 13 in
    that order. Saves 08/09/10 are DLAA; 11/12/13 are DLSS.
-   Balanced culling, SSGI enabled, GI disabled, AO interiors-only and
    active FOV+TAA 0.30/0.70. Info logging and fixed headset were confirmed
    by the user; this recording has no independent fixed-pose proof.
-   Saved `gameft-sw` holds, timing rules and health schedule are unchanged.
    WPR costs use the exact saved final `[50,60)` windows and recorded fpsVR
    frame counts. Counter deltas use the existing +49/+59 health receipts;
    they are not relabelled as the WPR window.

Evidence remains under
`build/bisect/measurements/gameft-sw-20260918T100605827Z-44ead541/`:
`provenance.json`, `frustum-analysis/README.md`,
`frustum-analysis/counter-deltas.json`,
`frustum-analysis/sampled-leaves.json` and `stack-wait/analysis/`.
The archived ETL is
`D:\Coding\GitHub\CS logs\frustum-75b9b558c__20260918T102433567Z__a0f94673__cpu-stack-wait.etl`;
its preservation receipt is `stack-wait/trace-archive.json` in the run directory.

The existing WPR validation reports no lost events/buffers, all six
scheduler windows within the saved 10 ms tolerance, render-stack gaps
of 0.3–2.8% and no unresolved CSX leaf symbols. Missing native symbols
remain missing: attribution uses live-image RVAs and observed CSX parents.

## Where the production work is

| DLSS save | Compound visits / player-view entry | Sphere calls / visit | Object rejection | Sphere work on accepted objects | Native render-thread traversal ms / recorded frame | Native traversal summed across threads ms / frame |
| --------- | ----------------------------------: | -------------------: | ---------------: | ------------------------------: | -------------------------------------------------: | ------------------------------------------------: |
| 11        |                              760.51 |                14.29 |           17.40% |                          87.34% |                                             0.0917 |                                            0.5010 |
| 12        |                              991.60 |                52.71 |            4.22% |                          98.21% |                                             0.3508 |                                            2.1825 |
| 13        |                             1635.71 |                63.68 |            7.88% |                          96.95% |                                             0.6541 |                                            4.0450 |

These native exclusive sampled costs exclude CSX telemetry leaves. They
describe work available for investigation, not achievable savings. Thread
sums include parallel workers and cannot be subtracted from frame time.
The corresponding depth-path counters are absent in the three DLAA saves;
other passes still do native work. Worker pass/eye attribution is unknown,
so worker work is not silently assigned to the depth pass or to an eye.

Low object rejection does not establish wasted culling. A rejection might
avoid an expensive subtree or draws. Conversely, acceptance does not prove
that every test was necessary. These counters alone cannot price either
outcome in GPU milliseconds.

## Equivalent and overlapping planes

The analysis deduplicates retained samples by thread lifetime, generation
and sequence, and binds their producer frames to each save. This avoids
counting stale ring entries from an earlier save. It reads the existing
+20/+49/+59 receipts, rather than treating these bounded examples as the
whole final-ten-second population.

| DLSS depth samples  | Observed sphere steps | Steps with replayable plane bytes | Result and output-mask matches | First-plane exits | Repeated executed plane equations | Longest directly linked first-plane true streak |
| ------------------- | --------------------: | --------------------------------: | -----------------------------: | ----------------: | --------------------------------: | ----------------------------------------------: |
| Save 11: 12 samples |                   180 |                               180 |                        180/180 |  164/180 (91.11%) |                                 0 |                                              15 |
| Save 12: 12 samples |                   599 |                               357 |                        357/357 |  354/357 (99.16%) |                                 0 |                                     at least 32 |
| Save 13: 12 samples |                   804 |                               384 |                        384/384 |  319/384 (83.07%) |                                 0 |                                     at least 32 |

The last column requires opcode 8, an observed true result, an unchanged
mask, one executed plane and a direct true edge to the next test. It does
not confuse a sphere's true result with final object acceptance. Save 12
has 353 such true/unchanged-mask calls; the other one-plane exit is not
eligible for this candidate.

Within the retained portions, there are no byte-identical full plane sets
or repeated exact sphere inputs. Repeated inactive/zero plane equations
do not offer saved arithmetic. Save 12 has eight repetitions of an exact
normal among eligible first-plane tests, but different plane offsets;
that is limited dot-product reuse, not identical visibility tests.
DLAA Save 08 has repeated executed equations in other contexts, which is
a separate opportunity and does not explain the DLSS depth cost.

No geometric containment/overlap proof has been established. Nonidentical
normals can describe overlapping volumes; bit inequality does not disprove
that. Removing a test would require the actual Boolean program and a
conservative geometric proof for its active planes, bounds and pass.
The old 32-plane cap omitted 242/420 depth steps in Saves 12/13. The
terminal-reader defect also prevented complete terminal verification.
Consequently these samples support a candidate, not program equivalence.

Offline replay uses finite, non-denormal positive-radius inputs and the
captured scalar float32 multiply/add order. Both the native boolean and
output mask must match. It is not a replacement visibility implementation
and does not establish NaN, negative-radius or concurrent-mutation behavior.

## Repeated construction and copies

The counter window reports six copy calls and one camera reset per
player-view entry. Nine fully retained Save 11 copy samples across nine
frames have the same body/equation signature, one source address and three
destination addresses. Save 09 has two signatures among eleven complete
copy samples. Saves 12/13 have no complete copy samples under the old
operator/plane limits.

These signatures intentionally exclude mutable masks and addresses. They
prove repeated body/equation bytes in those observations, not identical
native state, object lifetime, camera or ownership. Sphere tests change
masks; distinct copies may provide necessary worker isolation. Sharing
these mutable objects is not a safe optimization on current evidence.

| DLSS save | Exclusive native instructions in four observed construction ranges, ms/frame | Sampled subtree entered through native child of construction hook, ms/frame |
| --------- | ---------------------------------------------------------------------------: | --------------------------------------------------------------------------: |
| 11        |                                                                      0.00328 |                                                                     0.00650 |
| 12        |                                                                      0.00403 |                                                                     0.01355 |
| 13        |                                                                      0.00202 |                                                                     0.00403 |

The second column omits out-of-range allocation/copy helpers; the third
also includes sampled callees reached through the immediate native child.
Diagnostic `ReadPlanes`/`ReadStructure` children are excluded. Merely
excluding CSX leaf modules would wrongly charge their Windows memory-query
cost to construction. These are sampled estimates, not exact timers or
upper bounds; the four observed hooks do not cover every builder.

## Safest next production experiment

1. Collect complete paths with the corrected diagnostic build, without
   changing settings or `gameft-sw`. Require complete planes/operators,
   valid masks, stable bounds/header and verified native terminals. Keep
   pass/eye identity explicit; unknown attribution stays unknown.
2. Evaluate an offline packet of consecutive opcode-8 first-active-plane
   tests. The candidate succeeds only when every original first test
   returns true without clearing a mask, and the verified true edges lead
   to the expected continuation. Otherwise use the unchanged native path.
   This targets repeated calls/branches rather than weakening culling.
3. Prove equivalence before production implementation: preserve scalar
   rounding/order, comparison boundaries, active masks, object-bound bits,
   immutable program/plane revision, view, eye, pass and native ownership.
   Special values, unsupported graphs or changing state require fallback.
   A pointer/frame match alone is insufficient. Benchmark gathering and
   any precomputation against the calls saved; do not assume SIMD wins.
4. Only if full program geometry supports it, test a conservative group
   bound that skips a long accepting region with the same output and mask
   effects. Overlap alone is not enough to delete a volume or change the
   branch structure. Preserve independent worker masks and both-eye work.
5. Test one production variable with matched `gameft-sw` runs, fixed pose,
   identical settings/vendors and equivalent instrumentation. Compare
   native CPU cost, ready/wait time, frame counts, CPU/GPU means and tails,
   and visual/stereo correctness. Do not compare tracing-on to tracing-off.

The leading candidate preserves admitted objects, so it offers a CPU
opportunity but no demonstrated reduction in GPU work. Any GPU benefit
from steadier submission must be measured. A direct GPU saving requires
separate evidence that conservative culling removes downstream work;
current records do not measure draws/dispatches avoided per rejection.

## Live code and remaining gaps

Only the retained live snapshot in
`D:\Coding\GitHub\GhidraProjects\CPU-DLSS-20260916-pid21340\NativeCPUHotRanges.gpr`
was used, never the packed on-disk EXE. The verified `00DA3000` and
`00DA5000` hashes are recorded in [the telemetry contract](vr-frustum-telemetry.md).
They establish `0xDA33C0` dispatch, `0xDA5410` intersection,
`0xDA54B0` not-fully-inside and the four captured construction ranges.
The `0xDA41E0` threading/builder entry lies outside the retained range.
An attempted read-only capture of the measured process stopped before
opening memory because that process had exited. No new snapshot or
builder interpretation is claimed. Capture it in a later running session
before deciding whether immutable program preparation can be reused.

View/eye/job transfer, all builder revisions, geometric containment,
downstream work avoided and uninstrumented A/B savings remain unproved.
Do not turn the absence of duplicate sampled planes into a universal claim.

## Diagnostic correction and validation

`ReadInstruction` now admits verified accept/reject terminals beyond the
constructed body count but inside allocated storage. Ordinary instructions
and operands remain body-bounded. Unreadable, out-of-storage and other
trailing opcodes fail reconstruction closed; native calls still run
unchanged. A regression failed before the correction and passes after it.

Bounded operator/plane capture increases from 128/32 to 256/128, covering
the observed 200-slot, 83-plane programs. The path limit remains 128.
Advertised limits describe each receipt; old records are unchanged. A
production-header fixture covers the observed size plus external terminal
slots. No per-draw or production logging was added. All changes remain
inside `DEVBENCH_BRIDGE_ENABLED` and are absent with the bridge off.

Local analysis scripts/results are in
`build/cpu-burst-diagnostics/frustum-investigation-20260918/`:
`analyze-programs.py`, `program-investigation.json`,
`complete-copy-comparison.json`, `analyze-construction-cost.py`,
`construction-cost.json`, `analyze-shortcut-candidates.py` and
`shortcut-candidates.json`. They retain source hashes and evidence limits.
No saved assay, automation script, production setting or historical
measurement was modified. Validation receipts are under
`build/validation/frustum-investigation-20260918/`.

Validation commands:

```powershell
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests shader_tests -- /m:1
ctest --test-dir build/ALL -C Release -N
ctest --test-dir build/ALL -C Release --output-on-failure --no-tests=error --timeout 300
pwsh ./tools/run-msvc-command.ps1 cl.exe /nologo /std:c++20 /O2 /c /I src /Fobuild/validation/frustum-investigation-20260918/frustum-off.obj src/Diagnostics/VRFrustumTelemetry.cpp
pwsh ./tools/run-msvc-command.ps1 dumpbin.exe /headers build/validation/frustum-investigation-20260918/frustum-off.obj
pwsh ./tools/pre-commit.ps1 run --files src/Diagnostics/VRFrustumTelemetryPolicy.h src/Diagnostics/VRFrustumTelemetry.cpp tests/vr_frustum_telemetry_test.cpp docs/development/vr-frustum-telemetry.md docs/development/frustum-optimization-investigation-20260918.md
pwsh ./tools/git.ps1 diff --check
```

Universal Release (SE/AE/VR), DevBench on: DLL and both explicit test groups
built; 126 tests discovered and passed, zero failed/skipped/not built.
CTest took 52.36 seconds; the confirmation build/test sequence took
90.22 seconds. Focused ON/OFF tests also passed 2/2. The earlier expected
failure is retained in `terminal-red-test.log`. The actual implementation
compiled without DevBench has only compiler metadata sections, no code,
runtime data or TLS. This is a fresh object-level exclusion check; a new
full OFF DLL build was not run for this diagnostic-only correction.
Scoped pre-commit and whitespace checks pass. The only C++ edit after
the successful suite was clang-format indentation of one continued line;
there was no subsequent semantic change.

The corrected diagnostic build has not yet been exercised in-game. No
production performance or geometric-equivalence claim depends on it.
