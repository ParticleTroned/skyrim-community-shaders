# NVIDIA render-scale tuning: 10 September 2026

**Subsequent health audit:** the PASS counts below are terminal verdicts.
Rows 26 and 28 each retained two fidelity mismatches and one vendor-failure
stretch eye observation in both passes before recovering. Cumulative stress
acceptance also records failures. The imposed-stretch cutoff and proven
native-target gate mismatch are diagnostic only; the recovered fidelity
and vendor failures prevent an improvement-or-neutral assessment without
relabeling the completed test. See the [per-transition comparison and raw-evidence audit](nvidia-renderscale-baseline-vs-head-20260910.md)
for the complete health interpretation; original receipts remain unchanged.

Run `nvidia-2026-09-10T07-19-50-405Z` completed both uninterrupted
33-transition passes in Skyrim VR PID `16424`. Render results are **66
PASS / 0 FAIL**. Per-transition Task 2 counts are **66 PASS / 0 FAIL /
0 INCONCLUSIVE**; no aggregate Task 2 verdict is computed.

Measured source: `348803c1831c8cd71ceb75a58143ad0bcb00abb2`,
`v3.19.0-pr28-274-g348803c18`, Release, clean producer identity.
Bound Build ID:
`07238b1fe36024d2c33081f023973c34887ce8a0fa4bd4743bf8a1c85b5ec78d`.
The fixture was Dragonsreach, DLSS profile K, explicit FSR3, and foveation
`0.3/0.3/0.7`. Preparation and every profile apply were runtime only.

| Measurement                          |                       Pass 1 |                       Pass 2 |
| ------------------------------------ | ---------------------------: | ---------------------------: |
| Completed transitions                |                        33/33 |                        33/33 |
| Strict minimum / mean / maximum (ms) | 155.001 / 767.246 / 1392.319 | 154.506 / 730.514 / 1658.352 |
| Presentation mean (ms)               |                      656.928 |                      621.937 |
| Cleanup tail mean / maximum (ms)     |             88.953 / 290.280 |             87.192 / 251.354 |
| Producer retry events                |                            8 |                           10 |
| Stretch-selected rows, all recovered |                           16 |                           15 |

Strict timings run from qualification dispatch to strict terminal completion,
excluding the five-second server wait. Client dispatch pacing was VALID:
maximum 40.695 ms against the diagnostic 250 ms budget. No recovery apply
was needed. All 24 required DLSS trace windows were complete. These are
transition latencies, not steady-state FPS or a measured runtime speedup.

Device-loss failures, OOM failures, producer terminal failures,
vendor-native qualification failures, and credible liveness timeouts were
each zero. Stretch recurred at ordinals 4-9, 14-19, 25, 26, and 29;
ordinal 1 appeared only in pass 1. Every stretch observation recovered.

Memory classification is **inconclusive**. Process-private memory decreased
by 473.492 MiB in pass 1 and 365.262 MiB in pass 2; system commit decreased
by 658.777 and 583.629 MiB. DXGI usage decreased by 743.250 and 413.055 MiB.
The protocol's growth ratios require positive pass-1 growth, so those
ratios are unavailable. Fresh texture trackers ended with 217 and 207 live
records. The full report preserves all six boundaries, the ten-second
cooldown, exact predicate inputs, and the complete memory table. This
classification does not prove or disprove a leak.

Both passes' final receipts verify stress, qualification, CPU, GPU,
texture, load-presentation, trace, and profiler captures inactive.
Worker PID `32064` initially remained RUNNING after the complete live
result, with status frozen at `2026-09-10T07:27:51.074Z`. Inspection proved
the journal was drained and its writer had exited; an aborted notification
stream still had a pending body read. Cancelling that exact reader allowed
the original worker to publish COMPLETE at `2026-09-10T08:19:19.418Z`, exit,
and release its own endpoint lock. The journal hash is unchanged. Feedback:
`AUTO-20260910-073409531-420D8EDC`.

Deployment verification **passed**. The enabled mod
`CSX-main-VR-348803c18-DevBench-AIO` is the sole enabled loose DLL provider,
with no DLL in Overwrite or unmanaged game Data. Its 27,829,760-byte DLL
matches the archived AIO payload and adjacent build manifest:
`2101a4f7faa417b415ebb28dfa7b93a4ef39abf32ab9879f549f25da09bbef38`.
The AIO archive hash also matches its build receipt, and its manifest
Build ID matches the running producer. Compile configuration, source
identity, shader compiler identity, and shader ABI are retained.

Reporting is **COMPLETE**. The verified inactive captures, flushed complete
journal, validated row evidence, and verified DLL establish completeness
independently of worker shutdown. The worker lifecycle issue was subsequently
recovered, with explicit reader cancellation and bounded transport shutdown
implemented and regression-tested in the automation source. The packaged finalizer omitted the
required complete-run memory confirmation, which was reconstructed from
retained receipts.
Its original summary is preserved as `packaged-summary.json`.
Feedback recorded: `AUTO-20260910-074236818-07917649`.

The existing [comparison ledger](vr-render-scale-comparison-ledger.csv)
contains all 66 strict timings, 528 numeric timing cells, and full
per-transition timing groups with routes, units, run and build identity.
Validation confirmed exactly one appended column, all 887 existing rows
in order, and unchanged historical cells. `git diff --check` passed.

The user subsequently selected this run as the sole September 9-10 entry:
all five September 9 columns were removed from the ledger. Today's column,
its complete timings, all 15 older columns, and all 887 rows are unchanged.

The [transition comparison](nvidia-renderscale-transition-comparison-20260910.md)
compares both passes against all five September 9 runs, with per-transition
retry counts, recorded recovery frame spans, and timing limitations.

Local evidence:
`artifacts/renderscale-tuning/nvidia-2026-09-10T07-19-50-405Z`.
The 191,770,752-byte journal contains 446 contiguous records and all
referenced receipt keys. It was explicitly fsynced after cleanup, with
SHA-256 `19bcacbb3287b219e1e901796b71b81f4c920d19a8a90296e6f4af3b56635d2e`.
The packaged finalizer extracted 4,448,320 values across all 446 journal
revisions and validated all 66 terminal rows. `summary.json`, `report.md`,
`transitions.csv`, `evidence-values.csv`, and `receipt-index.json` retain
the complete results. Raw evidence remains local.
