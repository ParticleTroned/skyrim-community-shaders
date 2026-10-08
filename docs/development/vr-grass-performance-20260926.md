# VR grass production follow-up and performance evidence

## Production behavior

An empty manager grass root no longer waits for the native group lock.
The wrapper acquires the manager's native child lock before reading
`GetChildren().free_idx()` and retains that lock through the original
callback. Native VR `NiNode::OnVisible` at `SkyrimVR+0xC9E0D0` uses the
16-bit field at node `+0x14A` as its traversal limit. Zero allocated capacity
or zero live children is not an equivalent predicate: nonzero traversal
slots may contain holes and still require group ownership.

Nonempty roots retain child-before-group acquisition and both locks across
the native traversal, which can write group visibility. RAII releases
ownership after return or exception. Bulk clearing, installation signatures,
atomic hook installation and the VR 1.4.15 runtime gate remain unchanged.
SE/AE do not install these hooks. The existing native locks continue to
exclude native writers; no pointer snapshot or substitute mutex is used.

A separate non-inlined protected helper keeps lock guards off the shared
node hook's unrelated-node forwarding path. The committed implementation
contains no comparison modes, UI controls, DevBench grass actions, timing
collector, exporter worker, per-frame callbacks or diagnostic build option.
The experimental implementation and its protocol are preserved separately
in the named Git stash `build(diagnostics): stash grass comparison controls`,
while this commit retains the measurement results.

The focused harness covers an empty root while another thread owns the
group lock, a root becoming empty before child ownership is obtained,
nonempty traversal slots containing only holes, and the existing
installation, removal, growth, bulk-clear, unwind and lock-order cases.
It verifies that the slot count is read under child ownership. This is the
coverage for the empty shortcut that the live views did not exercise.

## Wrapper benchmark

Diagnostics were OFF. The same harness and MSVC `/O2 /GL /LTCG` settings
compiled the original hook from `22bb35127` and the reviewed hook. Six
executable runs used baseline/reviewed/reviewed/baseline/baseline/reviewed
order; each run rotated four lanes across six rounds of one million calls.
Each table entry is the median of 18 measurements, in nanoseconds per call.

| Mock lane        | Original | Reviewed |
| ---------------- | -------: | -------: |
| Native callback  |    3.835 |    3.904 |
| Unrelated node   |    4.654 |    4.468 |
| Nonempty grass   |   23.554 |   23.816 |
| Empty grass root |   24.346 |   13.200 |

The empty-root wrapper median falls by about 46%. The nonempty median is
0.263 ns higher, with overlapping observed ranges; this is approximately
unchanged in this mock, not proof of zero overhead. An earlier candidate
increased unrelated-node forwarding cost; moving protected work to the
separate helper removed that increase in the final measurement.

Raw CSVs, ranges, extracted sources, binaries, and the summary are retained
under `build/validation/grass-timing-20260926/benchmark/`, with final
measurements in its `final/` subdirectory.
This benchmark contains no native grass work, lock contention, or engine
scheduling. It cannot establish a frame-time improvement. The bounded VR
comparison below is not physical-HMD or long-duration stability qualification;
SE/AE have build and runtime-gate coverage, not live-game validation.

## Live Whiterun comparison

On 2026-09-26, the user-installed diagnostic producer
`ef01c587c5cf2e12be79b3066269b0a144dea6d04f7cda74e843b0e477ae5dfc`
was measured in the already-running process, after `coc WhiterunExterior01`.
The physical DLL matched its adjacent manifest and AIO receipt: SHA-256
`07099beb2e6ac3ec7a3a2703833a08668adcad7c613a1937cb12e397a4fa7915`,
31,709,696 bytes. The measured source was dirty on base commit
`22bb35127789b89e1b4d01dfc1ab3a28cce673e4`, with dirty digest
`480a2c49b019e530124bd4f73d03e33adf3faa5d6dd8ada91f1f6374c2d109de`.
The build used DevBench and grass diagnostics, with Tracy disabled. It is
not the clean production commit or a live test of its final DLL.

Three fixed free-camera views used Optimized/Off/Off/Optimized windows of
30 seconds with collection disabled, followed by separate 12-second
Optimized/Full locking/Off telemetry windows. A first-window transient in
the wooded view prompted one additional settled Off/Optimized pair. All
original windows remain in the evidence. Positive deltas mean Optimized
took longer.

| View                             | Off CPU ms | Optimized CPU ms | Delta ms | Off GPU ms | Optimized GPU ms | Delta ms |
| -------------------------------- | ---------: | ---------------: | -------: | ---------: | ---------------: | -------: |
| Town wall                        |      4.671 |            4.749 |   +0.078 |     24.464 |           24.227 |   -0.237 |
| Wooded approach, settled repeats |      7.490 |            7.493 |   +0.003 |     30.733 |           30.778 |   +0.045 |
| Upward sky                       |      2.890 |            2.975 |   +0.085 |     16.835 |           16.851 |   +0.016 |

The original wooded ABBA CPU delta was +0.397 ms, with pair differences
of +0.878 and -0.099 ms. Its first On window settled from 8.940 to
7.560 ms across ten-second blocks. The additional pair measured +0.106 ms;
combining the two settled pairs gives the table above. This establishes
no repeatable material regression and does not prove zero overhead.

Protected grass samples recorded roughly 0.00046–0.00057 ms/frame across
the two acquisition intervals and 0.174–0.186 ms/frame of native traversal.
Acquisition intervals include timestamp overhead. There were no contended
acquisitions, dropped or unassigned samples, invalid clocks, aborted
callbacks, or exporter clock errors in the enclosed telemetry windows.
Child slot ranges stayed nonempty (maximum 29); the empty shortcut never
executed. No bulk-clear callbacks were sampled. This run therefore cannot
qualify streaming-time contention, clear-wrapper costs, or the empty-path
performance gain.

The assay retained 19,128 unique OpenVR frames across 23 windows and
529.710 measured seconds, with no missing indices or duplicate/nonpositive
timings. CPU is OpenVR's elapsed NewFrameReady-minus-WaitGetPoses interval,
including waits; GPU is PreSubmitGpuMs. Process CPU, raw frame fields,
QPC boundaries, toggle epochs, camera poses, per-thread grass metrics,
peak provenance, native stereo images and the activity recording are saved.
Compilation finished before the first measured window; final shader status
was idle with 28/28 completed and zero failures.

This was SteamVR null-HMD on an RTX 5070 Ti Laptop GPU at 3024×1680 stereo
output, native DLSS/DLAA quality 0, with the simulation clock running.
These views were primarily GPU limited; the reported severe CPU bad spot
was not reproduced. The separately installed lifetime tracer was recording
at attachment; its trace was preserved and recording stopped before the
assay. Its hooks remained installed and its API still labeled the session
performance-distorted. Comparisons are between equally instrumented modes,
not against an unloaded tracer or a production build.

Optimized and the original collection setting were restored, free camera
was released, recording finalized without truncation, and Skyrim exited
after normal `qqq`. MO2's retained deployment was then cleaned through
exact Unlock and cooperative close, and both session/access leases were
released. No process was force-terminated.

Full local report, raw data and receipts:
`build/validation/grass-ab-whiterun-20260926/report.md`. The run used narrow
evidence-local controller adapters for documented camera/input responses
and correlated recording stop; producer and performance guards remained
enabled. Toolkit feedback: `AUTO-20260926-135701215-FCD243FC`.

## Production split validation

The stripped source passed these checks on 2026-09-26:

-   `pwsh ./tools/cmake.ps1 -S . -B build/ALL -UCSX_GRASS_LIFETIME_DIAGNOSTICS`.
-   `pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target vr_grass_lifetime_test CommunityShaders --parallel 8`.
-   `ctest --test-dir build/ALL -C Release -R '^(VRGrassLifetime|PerformanceTuningDevBenchContract)$' --output-on-failure`: 2/2 passed.
-   `pwsh ./tools/cmake.ps1 -P build/validation/grass-production-20260926/run-asan.cmake`: the production harness passed MSVC AddressSanitizer, compiled with `/W4 /WX`.
-   Scoped pre-commit checks on the two source/test files and two related documents.
-   Source and CMake search found no grass diagnostic build option, controls,
    collector or lifecycle hooks. The linked DLL contains none of the grass
    export markers, DevBench grass action names or comparison UI label.

The universal SE/AE/VR Release DLL was built with DevBench enabled and
Tracy disabled. The source has no grass diagnostic controls in any build.
Its producer Build ID is
`f468f0d872415f047c712ebcd36a35be0ff1fdb784c396123e1ba4c79541b697`.
Its size and SHA-256 match the adjacent manifest; the proof and preserved
DLL are under `build/validation/grass-production-20260926/`. This is a
pre-commit build of the stripped source, not a new in-game measurement.
Shader tests were not run because no shader source changed. The separate
live experiment above retains its original measured identity and limits.

## Adversarial production review

The review audited clean implementation commit
`bc65550756f2ec0d6fb9d23f9e0a784d4962c364` on 2026-09-26. No leftover
comparison code or production behavior discrepancy was found. This review
record adds no runtime or test-source changes.

-   A scan of 1,188 tracked text files outside documentation found no grass
    diagnostic macro, collector, action names, UI label or lifecycle hooks.
    The timing header is absent. CMake, the public grass header, UI,
    DevBench implementation and contract, frame/shutdown integration, engine
    fix registration and DevBench documentation exactly match `22bb35127`.
    Only the production hook, its tests and the two related documents differ
    from that base.
-   The preserved source before runtime toggles and the measured build's
    source with diagnostics disabled both reduce to the committed production
    tokens. The only normalization beyond removing diagnostic blocks,
    comments and whitespace was redundant single-statement loop braces and,
    for the later source, folding its local group-acquisition boolean.
    The measured-source snapshot matches all hashes in the original producer
    review record.
-   MSVC `/O2 /EHsc /W4 /WX /MD` compiled four variants against the same
    focused harness: before toggles, measured source with diagnostics OFF,
    production, and production with the obsolete diagnostic macro defined.
    All four passed. Their complete assembly listings are identical after
    removing source line-number comments only; instructions, data, symbols
    and exception tables were retained. The old macro cannot reactivate
    instrumentation. This is a comparison of the harness translation unit,
    not a claim that separately linked game DLLs are byte-identical.
-   Manual control-flow review confirms that the measured Optimized mode's
    ownership and native callbacks survive unchanged after removing timing
    and mode selection. Child ownership precedes the count read and lasts
    through the callback; nonempty roots retain group ownership and lock
    order. The empty path retains child ownership. Clear handling, exception
    unwinding, unrelated-node forwarding and VR installation gates remain
    intact. No unsafe Off branch remains.
-   The preserved production DLL still matches its manifest and contains no
    grass timing or comparison markers. The diagnostic stash remains intact
    and its patch applies cleanly to the production tree.

The existing Release, CTest and AddressSanitizer results remain applicable
because production source and tests did not change in this review. No code
correction or additional full DLL build was needed. The final production
DLL had not yet been exercised in-game at this review. The subsequent COC
assay below tests the stripped implementation through cell transitions;
the Whiterun comparison's empty-root, null-HMD and tracer limits remain.

Audit inputs, normalized source comparison, assembly listings, four harness
runs and result JSON are preserved under
`build/validation/grass-production-review-20260926/`. The shared normalized
assembly SHA-256 is
`fb0d46e20cba6a178e66ba2fe0bdf20ff29a1efdf508d9db125e801b94a13a12`.

## Stripped implementation: sequential COC validation

The [September 26 COC report](grass-coc-20260926/README.md) records the
subsequent in-game test of source
`3baaf91b90416ad25d067cdb26c34ce293cf7a5e`, producer Build ID
`5c8dfa4c9f482d14faf6cf82e06455724d317eda840b3a59db1baba961660d25`.
The enabled AIO's physical DLL matched its manifest and build receipt:
SHA-256 `63caf373b968553243b5715b411290fb9d9636fd6fddc90303c4fd44efb7d599`,
29,059,584 bytes. It is Release with DevBench enabled, Tracy disabled and
no grass comparison controls. These measured identities remain fixed when
this evidence is folded into the implementation commit.

In one Skyrim process, all 20 COCs at 10-second pacing, 25 at 5 seconds,
and 20 at 3 seconds completed strict qualification. No crash or freeze
occurred. Mean COC-to-ready times were 2.478, 2.401 and 2.322 seconds;
worst times were 6.384, 3.191 and 2.660 seconds. The requested waits are
additional to loading and qualification. Transcript export and analysis
introduced pauses between campaigns; no fixture or positioning setup was
repeated and the game was not restarted.

All resource publications were current, complete and matched. Recoverable
relatch requeues were 3, 12 and 10, with no device-loss, vendor-lifecycle,
fidelity, retirement-fence, OOM, transition or memory-trim failure deltas.
Raw aggregate acceptance was PASS / FAIL / FAIL. Both failures concern
`presentation_recovered` demanding scaled `VendorEvaluated` presentation
after a proven native DLAA target. They remain visible as
`CONTRACT_MISMATCH`, with exact native both-eye proof and failed gate
observations retained; the aggregate flags are not changed to passes.

The external lifetime tracer remained active throughout this later assay
and marked performance as distorted. CPU queue telemetry measures the
strong stereo packet queue, not grass locks. The three 300-frame profiler
captures measure nested scopes and differ in scene mix. This is bounded
streaming stability evidence, not a new production performance comparison,
proof of zero overhead, or proof that rare lifetime failures are absent.

All owned captures were stopped and the profiler restored to disabled.
The subsequent user-requested `qqq` cleanly exited PID 34744 in about
4.6 seconds; no Skyrim or SKSE loader process remained. No forced
termination was used. The exit code was unavailable.

[Snapshot 0005](vr-render-scale-ledger-0005-investigation.csv) retains the
complete summaries, all 65 numeric transition timings, CPU/GPU telemetry,
retries, health gates, memory, profiler, provenance and shutdown receipt.
The [coverage record](grass-coc-20260926/coverage.json) verifies exact
summary reconstruction, scalar timing coverage and unchanged historical
cells. Full raw transcripts remain local. No runtime source or test code
changed while adding these results.
