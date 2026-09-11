# NVIDIA tuning run nv-mtuivutb

The run retained 39 of 66 transitions: all 33 in pass 1 and six in pass 2.
Its orchestration cell became unavailable after a progress request. The game
remained running; ownership-guarded telemetry cleanup was completed and
verified. The long orchestration gaps leave stress cadence **unqualified**.
These individual measurements remain available for comparison, but this is
not a completed stress assay or memory-repeat confirmation.

Measured build: `07238b1fe36024d2c33081f023973c34887ce8a0fa4bd4743bf8a1c85b5ec78d`;
source commit `348803c1831c8cd71ceb75a58143ad0bcb00abb2`,
`v3.19.0-pr28-274-g348803c18`, Release, clean producer identity.
Deployment artifact verification was not repeated for this interrupted run.

| Measurement                                  |                       Pass 1 |                       Pass 2 |
| -------------------------------------------- | ---------------------------: | ---------------------------: |
| Retained transitions                         |                        33/33 |                         6/33 |
| Render PASS rows                             |                           33 |                            6 |
| Task 2 PASS rows                             |                           33 |                            6 |
| Strict elapsed minimum / mean / maximum (ms) | 155.925 / 812.230 / 1621.095 | 189.373 / 779.620 / 1238.096 |

Task 2 classifications remain per transition. Reporting is incomplete and
memory confirmation is `repeat_not_completed`. Pass-2 transitions 7–33 were
not run; their timings are not zero or inferred.

The existing [comparison ledger](vr-render-scale-ledger.md)
contains every one of the 39 strict timings, plus recorded presentation,
cleanup, cleanup-tail, frame and QPC timings with explicit units and source
definitions. The update verified that all historical cells remained intact.
Raw evidence remains under `artifacts/renderscale-tuning/nv-mtuivutb`, including
the original 196 journal entries, cleanup receipts, `summary.json`,
`transitions.csv`, `evidence-values.csv`, and `canonical-ledger-update.json`.
The lossless CSV extraction contains 2,755,332 values from 309 raw JSON files.

## Automation correction

The source correction transfers post-position measurement to a hidden detached
worker and queues complete immutable receipts to a writer thread. It appends
one document, never waits for saving between transitions or passes, and flushes
after measurement and cleanup. Status updates are independent and due every
five transitions, at pass boundaries, and on completion. The admitted startup,
33-transition matrix, five-second server pacing, strict waiters, and full
telemetry remain intact.

An offline replay of this run's 196 receipts (112,538,860 output bytes) queued
in 76.011 ms and finished writing plus flushing in 423.855 ms. The maximum
individual enqueue was 1.426 ms; all 196 receipts were pending before the
drain, and every decoded value matched the original. This measures the writer
only; no live-game speedup is claimed. Evidence is preserved under
`artifacts/tuning-writer-benchmark-1788991479174`.

Mocked validation covers 66 transitions, launcher exit, a 300-receipt backlog,
overlap with delayed saving, complete transition timings, streaming MCP
responses, guarded cleanup after storage failure, and exact offline journal
reconstruction. NVIDIA/AMD protocol and package-distribution checks passed.
Installed-cache activation follows the automation repository's release policy.
