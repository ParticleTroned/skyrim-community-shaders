# NVIDIA render-scale tuning: 1595cffd comparison baseline

**Subsequent health audit:** the PASS counts below are terminal verdicts.
Cumulative stress acceptance records an inapplicable imposed-stretch cutoff
and a scaled-presentation/native-target contract mismatch in both passes.
These diagnostics do not count against switch health. Fidelity mismatch
and vendor-failure stretch counters were zero. See the [per-transition comparison and raw-evidence audit](nvidia-renderscale-baseline-vs-head-20260910.md)
for the complete interpretation; original receipts remain unchanged.

Run `renderscale-tuning-nvidia-20260910T090649592Z` completed two
uninterrupted 33-transition passes in Skyrim VR PID `26500` on
10 September 2026. Render results are **66 PASS / 0 FAIL**.
Per-transition Task 2 evidence counts are **66 PASS / 0 FAIL /
0 INCONCLUSIVE**. Execution and reporting are **COMPLETE**.

Measured source is `390fdea2533ae447849c5f07766c796872996a61`,
`v3.19.0-pr28-266-g390fdea25`, Release, clean producer identity.
This comparison build preserves renderer baseline
`1595cffd6a0a770020074c3d5481d45502df58a7` with tuning preparation
backported. Bound Build ID:
`d20962d1f8b75609f7b97074d1ae8132450d61b477be793227ad88088f5bda0d`.

The fixture was Dragonsreach, DLSS profile K, explicit FSR3, and
foveation `0.3/0.3/0.7`. The protocol used one positioning COC, two
runtime-only baselines, and 66 measured runtime-only public API applies.
Measurement ran from 09:08:06 to 09:14:55 UTC in one game process.

| Measurement                          |                       Pass 1 |                       Pass 2 |
| ------------------------------------ | ---------------------------: | ---------------------------: |
| Completed transitions                |                        33/33 |                        33/33 |
| Strict minimum / mean / maximum (ms) | 163.718 / 838.490 / 2002.054 | 165.289 / 830.058 / 2236.990 |
| Presentation mean (ms)               |                      719.276 |                      706.319 |
| Cleanup tail mean / maximum (ms)     |             96.137 / 286.129 |            102.879 / 411.067 |
| Producer retry events                |                           10 |                            9 |
| Stretch-selected rows, all recovered |                           17 |                           15 |

Strict timings run from qualification dispatch to strict completion;
the five-second server pacing wait is excluded. These measurements
describe transition latency, not steady-state FPS. Client dispatch
pacing was VALID, with a maximum 54.370 ms gap against the diagnostic
250 ms budget. No recovery apply was needed. All 24 required DLSS
trace windows are complete.

Device-loss failures, OOM failures, producer terminal failures,
vendor-native qualification failures, and credible liveness timeouts
were each zero. Stretch recurred at ordinals 4-9, 14-19, 25, 26,
and 29. Ordinals 1 and 20 appeared only in pass 1. Every selected
stretch recovered; consecutive-frame counts at ordinal 29 were not
exposed in either pass and remain explicitly unavailable.

Memory classification is **inconclusive**. Process-private memory
changed by +34.645 MiB in pass 1 and -469.797 MiB in pass 2; system
commit changed by -69.266 and -203.602 MiB. DXGI usage changed by
-1581.980 and -669.727 MiB. Pressure was Normal at all six boundaries.
Fresh texture trackers ended with 261 and 209 live records. Pass 1
system-commit growth was negative, so the required joint positive-growth
predicate is false. The full report preserves all six boundaries,
the ten-second cooldown, exact predicate inputs, and the memory table.
This classification does not prove or disprove a leak.

Both passes verified stress, qualification, CPU, GPU, texture,
load-presentation, trace, and profiler captures inactive. The worker
flushed the complete journal, published COMPLETE with zero pending
evidence, and exited. The journal contains 444 contiguous records,
188,543,185 bytes, and every referenced receipt key; SHA-256 is
`a8cc5b7589172b680bf23a2e2542fa0a880b9b9623bbeb681d7324293cfc9ef3`.

Deployment verification passed. Enabled mod
`CSX-1595cffd-prepare-tuning-390fdea25-DevBench-AIO-FOMOD` is the sole
enabled loose DLL provider. No DLL exists in Overwrite or unmanaged
game Data. Its 27,750,912-byte DLL matches the adjacent manifest,
AIO build receipt, and extracted archive payload with SHA-256
`e0c3405ab0de5ee5717b542136634684b3ce3140a672f227230d54d377f13ba5`.
The archive hash and size match the build receipt. The manifest Build ID
matches the runtime producer; source, compiler, and shader ABI identities
are retained. This baseline does not expose `status.submitInputFreshness`;
that unavailable diagnostic is not required by this assay.

The packaged finalizer omitted the required memory summary and timing
aggregates. They were reconstructed offline from preserved receipts;
the original output is retained as `packaged-summary.json`. This
occurrence was attached to local feedback
`AUTO-20260910-074236818-07917649`.

The existing [comparison ledger](vr-render-scale-ledger.md)
gained one column with all 66 strict timings, 528 numeric timing cells,
and all per-transition timing groups, routes, units, and run/build
identity. All 887 existing rows and historical cells are preserved.
The earlier September 10 run remains available in its existing column.

Validation used the packaged `renderscale-tuning-finalizer/finalizer.js`
with the exact run ID, Build ID, and 66 expected rows, followed by local
`verify-aio.py` and `final-validation.py`. The latter verifies all numeric
timings against raw waiters, complete timing groups, reference routes,
memory boundaries, journal identity, and historical ledger cells.

The final validation passed: 66 terminal rows, 24 complete trace windows,
six memory boundaries, 528 exact numeric timing cells, 66 complete timing
groups, unchanged historical cells, and an unchanged journal hash.
The final export contains 4,389,684 values across all 444 journal revisions
and retained JSON evidence. `git diff --check` and the three-file
`pre-commit.ps1 run --files` check passed. The initial sandboxed hook
attempt could not write its cache database; the authorized retry passed.
`dev-doctor.ps1 -Network` reported zero failures and three existing remote
and push-routing warnings.

Startup audit: local tool metadata was enumerated alongside the initial
skill read before the required preparation call. This deviated from the
skill's immediate-positioning instruction. No additional live admission
calls or measurement delays were introduced; the packaged worker owned
both unchanged matrices and their measured pacing.

Local [full report](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260910T090649592Z/report.md),
[structured summary](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260910T090649592Z/summary.json),
and [receipt index](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260910T090649592Z/receipt-index.json)
preserve the complete results. Raw evidence remains local.
