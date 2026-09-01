# NVIDIA render-scale tuning: PR73 parent repeat, September 11

Run `renderscale-tuning-nvidia-20260911T155459454Z` completed 66 / 66 transitions in game PID
`11992`. Execution is COMPLETE; terminal counts are
66 PASS / 0 FAIL, scoped to the terminal condition. Task 2
retains per-transition counts 66 PASS / 0 FAIL /
0 INCONCLUSIVE without an aggregate verdict. Full-history health is
`NO_COUNTED_FAILURES`; reporting is COMPLETE.

The primary reference remains user-pinned PR66 `renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z`,
source/renderer `a09e1cc77de098f85e74e6d5bb341dc184f83640`, main-VR base
`bf4ae54a7d49620c41cb32ee9ecfd44657688ead`. The additional descriptive
comparison uses the previous same-build run `renderscale-tuning-nvidia-20260911T153415138Z`.
Both comparisons retain each pass separately; passes are never averaged.

| Pass | Candidate strict mean ms | PR66 mean ms | Change % | Previous same-build mean ms | Change % |
| ---- | -----------------------: | -----------: | -------: | --------------------------: | -------: |
| 1    |                  905.409 |      793.336 |  +14.127 |                     942.965 |   -3.983 |
| 2    |                  998.312 |      807.126 |  +23.687 |                     807.993 |  +23.554 |

Strict times exclude the server wait before each dispatch. Ordered
passes and this repeated run do not establish statistical significance.
Formal assessment against PR66 is INCONCLUSIVE;
the same-build comparison is INCONCLUSIVE.
Exact assessment limitations are preserved in both complete comparison
sections below. Memory evidence status is complete; the
memory verdict is `inconclusive`. Retained boundaries and
predicates do not establish leak freedom.

| Failure category                    | Recorded count |
| ----------------------------------- | -------------: |
| Device loss                         |              0 |
| Out of memory                       |              0 |
| Producer terminal failure           |              0 |
| Vendor-native qualification failure |              0 |
| Credible liveness timeout           |              0 |

Device, OOM and producer counts use retained pass counters. All native
vendor destinations passed their required qualification. Zero credible
liveness timeouts follows the complete terminal-receipt coverage,
all terminal PASS results, no interruption and no required recovery.

| Pass | Health standard | Raw cumulative accepted | Retries | Stretch episodes | Stretch frames | Stretch ms | Active at stop |
| ---- | --------------- | ----------------------- | ------: | ---------------: | -------------: | ---------: | -------------- |
| 1    | MET             | false                   |       8 |               19 |             79 |   5865.714 | false          |
| 2    | MET             | false                   |      10 |               17 |             73 |   5357.555 | false          |

Selected stretch transitions: 31; recovered PASS:
31; unrecovered: 0. The full pass totals
above remain distinct from transition selection. Raw gate outcomes, observed
values, limits and applicability remain in the retained reports. The fixed
settling-imposed stretch cutoff is DIAGNOSTIC_ONLY where classified as such;
the native-target CONTRACT_MISMATCH requires the retained both-eye proof.

## Physical runtime identity

The candidate is clean Release source/renderer
`bc077786db08637eec8b4c3f718e971e58700a60`, main-VR base
`ef7c366dd73989b2b87751c0ef975db7c6fd310f` with no reporting backport. The physical
28,173,824-byte DLL, adjacent manifest and AIO receipt
match runtime Build ID `0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1` and SHA-256
`47fe6e5f0283ad05d5bd8b9c8ddfe4e22b801dd091bca81050600b9fd2a8e116`. Complete compile and provider evidence remains
in the [physical verification receipt](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/runtime-dll-verification.json).

Producer metadata and harness artifact proof remain separate. The
receipt preserves exact source, manifest, AIO and runtime correlations
and their limitations; no in-memory module hash is inferred.

## Evidence and client diagnostics

Owned captures are verified inactive and pending evidence is zero.
Worker pacing status is EXCEEDED; its reported
client-side maximum is 44.6864 ms.
The [current-run orchestration note](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/client-orchestration-note.json)
is preserved below; previous-run startup diagnostics are not carried
into this run. These client diagnostics remain separate from runtime
execution, terminal results and evidence completeness.

```json
{
    "schemaVersion": "renderscale-client-orchestration-note-v1",
    "runId": "renderscale-tuning-nvidia-20260911T155459454Z",
    "startupDeviation": null,
    "startup": "Skill read followed by one prepare_tuning call, one synchronous positioning scenario, and packaged handoff in the same cell. No tool enumeration or extra live startup call.",
    "measurement": "Unchanged packaged NVIDIA worker and matrix; two passes and 66 measured transitions.",
    "monitoring": "Read-only worker-status.json polling every four seconds; no competing worker or extra live status calls.",
    "pacingDiagnostic": "pacing-diagnostic.json",
    "cleanupVerified": true,
    "evidencePending": 0
}
```

The pacing status also uses the exact QPC interval from the
previous strict terminal to the next dispatch, minus the
prescribed five-second server wait. That interval reached
344.0369 ms, with 3 of
64 intervals exceeding the 250 ms budget. This supports the
EXCEEDED status; the client-side maximum is a separate metric.
The pacing result remains separate from completed runtime and
terminal classifications. The offending next transitions are:

| Pass | Previous row | Next row | Beyond prescribed wait ms |
| ---- | ------------ | -------- | ------------------------: |
| 2    | 23           | 24       |                  344.0369 |
| 2    | 24           | 25       |                  297.5051 |
| 2    | 27           | 28       |                  325.8886 |

The complete [pacing diagnostic](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/pacing-diagnostic.json)
preserves every interval and its exact endpoints:

```json
{
    "schemaVersion": "renderscale-pacing-diagnostic-v1",
    "runId": "renderscale-tuning-nvidia-20260911T155459454Z",
    "buildId": "0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1",
    "workerPacing": {
        "status": "EXCEEDED",
        "maximumClientDispatchGapMs": 44.68640000000596,
        "budgetMs": 250
    },
    "cadenceIntervalCount": 64,
    "cadenceMaximumBeyondPrescribedWaitMs": 344.03690000000006,
    "cadenceIntervalsExceedingBudget": 3,
    "cadenceRecords": [
        {
            "journalRecord": {
                "sequence": 11,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100142",
                "value": {
                    "previousTransitionId": 100141,
                    "transitionId": 100142,
                    "terminalToDispatchBeyondPacingMs": 105.76959999999963,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 1,
            "ordinal": 2,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/01/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/02/retained.json",
            "previousStrictTick": 1609730847194,
            "nextDispatchTick": 1609781904890,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 105.76959999999963,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 16,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100143",
                "value": {
                    "previousTransitionId": 100142,
                    "transitionId": 100143,
                    "terminalToDispatchBeyondPacingMs": 88.5594000000001,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 2,
            "ordinal": 3,
            "source": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/02/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/03/retained.json",
            "previousStrictTick": 1609783835319,
            "nextDispatchTick": 1609834720913,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 88.5594000000001,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 23,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100144",
                "value": {
                    "previousTransitionId": 100143,
                    "transitionId": 100144,
                    "terminalToDispatchBeyondPacingMs": 95.01109999999971,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 3,
            "ordinal": 4,
            "source": {
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/03/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/04/retained.json",
            "previousStrictTick": 1609837575025,
            "nextDispatchTick": 1609888525136,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 95.01109999999971,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 34,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100145",
                "value": {
                    "previousTransitionId": 100144,
                    "transitionId": 100145,
                    "terminalToDispatchBeyondPacingMs": 128.9296000000004,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 4,
            "ordinal": 5,
            "source": {
                "method": "dlss",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 2,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/04/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/05/retained.json",
            "previousStrictTick": 1609901039760,
            "nextDispatchTick": 1609952329056,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 128.9296000000004,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 45,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100146",
                "value": {
                    "previousTransitionId": 100145,
                    "transitionId": 100146,
                    "terminalToDispatchBeyondPacingMs": 115.4399999999996,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 5,
            "ordinal": 6,
            "source": {
                "method": "dlss",
                "qualityMode": 2,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 3,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/05/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/06/retained.json",
            "previousStrictTick": 1609963276163,
            "nextDispatchTick": 1610014430563,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 115.4399999999996,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 56,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100147",
                "value": {
                    "previousTransitionId": 100146,
                    "transitionId": 100147,
                    "terminalToDispatchBeyondPacingMs": 137.9450999999999,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 6,
            "ordinal": 7,
            "source": {
                "method": "dlss",
                "qualityMode": 3,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 4,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/06/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/07/retained.json",
            "previousStrictTick": 1610026964341,
            "nextDispatchTick": 1610078343792,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 137.9450999999999,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 67,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100148",
                "value": {
                    "previousTransitionId": 100147,
                    "transitionId": 100148,
                    "terminalToDispatchBeyondPacingMs": 118.21799999999985,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 7,
            "ordinal": 8,
            "source": {
                "method": "dlss",
                "qualityMode": 4,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 5,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/07/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/08/retained.json",
            "previousStrictTick": 1610090506734,
            "nextDispatchTick": 1610141688914,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 118.21799999999985,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 78,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100149",
                "value": {
                    "previousTransitionId": 100148,
                    "transitionId": 100149,
                    "terminalToDispatchBeyondPacingMs": 131.6322,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 8,
            "ordinal": 9,
            "source": {
                "method": "dlss",
                "qualityMode": 5,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/08/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/09/retained.json",
            "previousStrictTick": 1610153541950,
            "nextDispatchTick": 1610204858272,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 131.6322,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 89,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100150",
                "value": {
                    "previousTransitionId": 100149,
                    "transitionId": 100150,
                    "terminalToDispatchBeyondPacingMs": 162.53409999999985,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 9,
            "ordinal": 10,
            "source": {
                "method": "dlss",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/09/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/10/retained.json",
            "previousStrictTick": 1610218119328,
            "nextDispatchTick": 1610269744669,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 162.53409999999985,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 98,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100151",
                "value": {
                    "previousTransitionId": 100150,
                    "transitionId": 100151,
                    "terminalToDispatchBeyondPacingMs": 157.91550000000007,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 10,
            "ordinal": 11,
            "source": {
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/10/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/11/retained.json",
            "previousStrictTick": 1610278347442,
            "nextDispatchTick": 1610329926597,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 157.91550000000007,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 103,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100152",
                "value": {
                    "previousTransitionId": 100151,
                    "transitionId": 100152,
                    "terminalToDispatchBeyondPacingMs": 119.11129999999957,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 11,
            "ordinal": 12,
            "source": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/11/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/12/retained.json",
            "previousStrictTick": 1610335046125,
            "nextDispatchTick": 1610386237238,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 119.11129999999957,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 108,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100153",
                "value": {
                    "previousTransitionId": 100152,
                    "transitionId": 100153,
                    "terminalToDispatchBeyondPacingMs": 166.82319999999982,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 12,
            "ordinal": 13,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/12/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/13/retained.json",
            "previousStrictTick": 1610388316658,
            "nextDispatchTick": 1610439984890,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 166.82319999999982,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 113,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100154",
                "value": {
                    "previousTransitionId": 100153,
                    "transitionId": 100154,
                    "terminalToDispatchBeyondPacingMs": 124.24210000000039,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 13,
            "ordinal": 14,
            "source": {
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/13/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/14/retained.json",
            "previousStrictTick": 1610446459661,
            "nextDispatchTick": 1610497702082,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 124.24210000000039,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 118,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100155",
                "value": {
                    "previousTransitionId": 100154,
                    "transitionId": 100155,
                    "terminalToDispatchBeyondPacingMs": 158.54060000000027,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 14,
            "ordinal": 15,
            "source": {
                "method": "fsr",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 2,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/14/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/15/retained.json",
            "previousStrictTick": 1610509160921,
            "nextDispatchTick": 1610560746327,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 158.54060000000027,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 123,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100156",
                "value": {
                    "previousTransitionId": 100155,
                    "transitionId": 100156,
                    "terminalToDispatchBeyondPacingMs": 150.52300000000014,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 15,
            "ordinal": 16,
            "source": {
                "method": "fsr",
                "qualityMode": 2,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 3,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/15/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/16/retained.json",
            "previousStrictTick": 1610569859656,
            "nextDispatchTick": 1610621364886,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 150.52300000000014,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 128,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100157",
                "value": {
                    "previousTransitionId": 100156,
                    "transitionId": 100157,
                    "terminalToDispatchBeyondPacingMs": 149.33849999999984,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 16,
            "ordinal": 17,
            "source": {
                "method": "fsr",
                "qualityMode": 3,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 4,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/16/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/17/retained.json",
            "previousStrictTick": 1610630042275,
            "nextDispatchTick": 1610681535660,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 149.33849999999984,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 133,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100158",
                "value": {
                    "previousTransitionId": 100157,
                    "transitionId": 100158,
                    "terminalToDispatchBeyondPacingMs": 142.17410000000018,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 17,
            "ordinal": 18,
            "source": {
                "method": "fsr",
                "qualityMode": 4,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 5,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/17/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/18/retained.json",
            "previousStrictTick": 1610690149365,
            "nextDispatchTick": 1610741571106,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 142.17410000000018,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 138,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100159",
                "value": {
                    "previousTransitionId": 100158,
                    "transitionId": 100159,
                    "terminalToDispatchBeyondPacingMs": 129.78290000000015,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 18,
            "ordinal": 19,
            "source": {
                "method": "fsr",
                "qualityMode": 5,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/18/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/19/retained.json",
            "previousStrictTick": 1610749536057,
            "nextDispatchTick": 1610800833886,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 129.78290000000015,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 143,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100160",
                "value": {
                    "previousTransitionId": 100159,
                    "transitionId": 100160,
                    "terminalToDispatchBeyondPacingMs": 150.66060000000016,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 19,
            "ordinal": 20,
            "source": {
                "method": "fsr",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/19/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/20/retained.json",
            "previousStrictTick": 1610809987098,
            "nextDispatchTick": 1610861493704,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 150.66060000000016,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 148,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100161",
                "value": {
                    "previousTransitionId": 100160,
                    "transitionId": 100161,
                    "terminalToDispatchBeyondPacingMs": 162.22739999999976,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 20,
            "ordinal": 21,
            "source": {
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/20/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/21/retained.json",
            "previousStrictTick": 1610869379288,
            "nextDispatchTick": 1610921001562,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 162.22739999999976,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 153,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100162",
                "value": {
                    "previousTransitionId": 100161,
                    "transitionId": 100162,
                    "terminalToDispatchBeyondPacingMs": 185.1822000000002,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 21,
            "ordinal": 22,
            "source": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/21/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/22/retained.json",
            "previousStrictTick": 1610926740936,
            "nextDispatchTick": 1610978592758,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 185.1822000000002,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 158,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100163",
                "value": {
                    "previousTransitionId": 100162,
                    "transitionId": 100163,
                    "terminalToDispatchBeyondPacingMs": 196.82870000000003,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 22,
            "ordinal": 23,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/22/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/23/retained.json",
            "previousStrictTick": 1610980905111,
            "nextDispatchTick": 1611032873398,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 196.82870000000003,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 165,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100164",
                "value": {
                    "previousTransitionId": 100163,
                    "transitionId": 100164,
                    "terminalToDispatchBeyondPacingMs": 150.17630000000008,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 23,
            "ordinal": 24,
            "source": {
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/23/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/24/retained.json",
            "previousStrictTick": 1611036291117,
            "nextDispatchTick": 1611087792880,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 150.17630000000008,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 170,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100165",
                "value": {
                    "previousTransitionId": 100164,
                    "transitionId": 100165,
                    "terminalToDispatchBeyondPacingMs": 160.09990000000016,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 24,
            "ordinal": 25,
            "source": {
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/24/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/25/retained.json",
            "previousStrictTick": 1611096795005,
            "nextDispatchTick": 1611148396004,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 160.09990000000016,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 177,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100166",
                "value": {
                    "previousTransitionId": 100165,
                    "transitionId": 100166,
                    "terminalToDispatchBeyondPacingMs": 191.63960000000043,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 25,
            "ordinal": 26,
            "source": {
                "method": "dlss",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/25/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/26/retained.json",
            "previousStrictTick": 1611167646476,
            "nextDispatchTick": 1611219562872,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 191.63960000000043,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 182,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100167",
                "value": {
                    "previousTransitionId": 100166,
                    "transitionId": 100167,
                    "terminalToDispatchBeyondPacingMs": 145.4341999999997,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 26,
            "ordinal": 27,
            "source": {
                "method": "fsr",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "target": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/26/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/27/retained.json",
            "previousStrictTick": 1611240128457,
            "nextDispatchTick": 1611291582799,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 145.4341999999997,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 187,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100168",
                "value": {
                    "previousTransitionId": 100167,
                    "transitionId": 100168,
                    "terminalToDispatchBeyondPacingMs": 152.57629999999972,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 27,
            "ordinal": 28,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/27/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/28/retained.json",
            "previousStrictTick": 1611301038497,
            "nextDispatchTick": 1611352564260,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 152.57629999999972,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 192,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100169",
                "value": {
                    "previousTransitionId": 100168,
                    "transitionId": 100169,
                    "terminalToDispatchBeyondPacingMs": 159.56900000000041,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 28,
            "ordinal": 29,
            "source": {
                "method": "fsr",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/28/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/29/retained.json",
            "previousStrictTick": 1611363082946,
            "nextDispatchTick": 1611414678636,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 159.56900000000041,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 199,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100170",
                "value": {
                    "previousTransitionId": 100169,
                    "transitionId": 100170,
                    "terminalToDispatchBeyondPacingMs": 193.58569999999963,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 29,
            "ordinal": 30,
            "source": {
                "method": "dlss",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "target": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/29/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/30/retained.json",
            "previousStrictTick": 1611436290682,
            "nextDispatchTick": 1611488226539,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 193.58569999999963,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 204,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100171",
                "value": {
                    "previousTransitionId": 100170,
                    "transitionId": 100171,
                    "terminalToDispatchBeyondPacingMs": 200.07999999999993,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 30,
            "ordinal": 31,
            "source": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/30/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/31/retained.json",
            "previousStrictTick": 1611497333648,
            "nextDispatchTick": 1611549334448,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 200.07999999999993,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 209,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100172",
                "value": {
                    "previousTransitionId": 100171,
                    "transitionId": 100172,
                    "terminalToDispatchBeyondPacingMs": 204.01069999999982,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 31,
            "ordinal": 32,
            "source": {
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/31/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/32/retained.json",
            "previousStrictTick": 1611557790489,
            "nextDispatchTick": 1611609830596,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 204.01069999999982,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 214,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100173",
                "value": {
                    "previousTransitionId": 100172,
                    "transitionId": 100173,
                    "terminalToDispatchBeyondPacingMs": 142.1175000000003,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 1,
            "previousOrdinal": 32,
            "ordinal": 33,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-1/transitions/32/retained.json",
            "receipt": "raw/lane-nvidia/pass-1/transitions/33/retained.json",
            "previousStrictTick": 1611615832258,
            "nextDispatchTick": 1611667253433,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 142.1175000000003,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 234,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100182",
                "value": {
                    "previousTransitionId": 100181,
                    "transitionId": 100182,
                    "terminalToDispatchBeyondPacingMs": 87.3717999999999,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 1,
            "ordinal": 2,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/01/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/02/retained.json",
            "previousStrictTick": 1611861500672,
            "nextDispatchTick": 1611912374390,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 87.3717999999999,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 239,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100183",
                "value": {
                    "previousTransitionId": 100182,
                    "transitionId": 100183,
                    "terminalToDispatchBeyondPacingMs": 93.89220000000023,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 2,
            "ordinal": 3,
            "source": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/02/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/03/retained.json",
            "previousStrictTick": 1611914216510,
            "nextDispatchTick": 1611965155432,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 93.89220000000023,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 246,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100184",
                "value": {
                    "previousTransitionId": 100183,
                    "transitionId": 100184,
                    "terminalToDispatchBeyondPacingMs": 94.85540000000037,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 3,
            "ordinal": 4,
            "source": {
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/03/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/04/retained.json",
            "previousStrictTick": 1611968580498,
            "nextDispatchTick": 1612019529052,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 94.85540000000037,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 255,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100185",
                "value": {
                    "previousTransitionId": 100184,
                    "transitionId": 100185,
                    "terminalToDispatchBeyondPacingMs": 147.78719999999976,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 4,
            "ordinal": 5,
            "source": {
                "method": "dlss",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 2,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/04/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/05/retained.json",
            "previousStrictTick": 1612030235487,
            "nextDispatchTick": 1612081713359,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 147.78719999999976,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 266,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100186",
                "value": {
                    "previousTransitionId": 100185,
                    "transitionId": 100186,
                    "terminalToDispatchBeyondPacingMs": 124.16240000000016,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 5,
            "ordinal": 6,
            "source": {
                "method": "dlss",
                "qualityMode": 2,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 3,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/05/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/06/retained.json",
            "previousStrictTick": 1612092090499,
            "nextDispatchTick": 1612143332123,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 124.16240000000016,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 277,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100187",
                "value": {
                    "previousTransitionId": 100186,
                    "transitionId": 100187,
                    "terminalToDispatchBeyondPacingMs": 143.8176999999996,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 6,
            "ordinal": 7,
            "source": {
                "method": "dlss",
                "qualityMode": 3,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 4,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/06/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/07/retained.json",
            "previousStrictTick": 1612156534701,
            "nextDispatchTick": 1612207972878,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 143.8176999999996,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 288,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100188",
                "value": {
                    "previousTransitionId": 100187,
                    "transitionId": 100188,
                    "terminalToDispatchBeyondPacingMs": 142.7547000000004,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 7,
            "ordinal": 8,
            "source": {
                "method": "dlss",
                "qualityMode": 4,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 5,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/07/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/08/retained.json",
            "previousStrictTick": 1612220667654,
            "nextDispatchTick": 1612272095201,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 142.7547000000004,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 299,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100189",
                "value": {
                    "previousTransitionId": 100188,
                    "transitionId": 100189,
                    "terminalToDispatchBeyondPacingMs": 158.34810000000016,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 8,
            "ordinal": 9,
            "source": {
                "method": "dlss",
                "qualityMode": 5,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/08/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/09/retained.json",
            "previousStrictTick": 1612284010317,
            "nextDispatchTick": 1612335593798,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 158.34810000000016,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 310,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100190",
                "value": {
                    "previousTransitionId": 100189,
                    "transitionId": 100190,
                    "terminalToDispatchBeyondPacingMs": 163.66769999999997,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 9,
            "ordinal": 10,
            "source": {
                "method": "dlss",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/09/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/10/retained.json",
            "previousStrictTick": 1612350119192,
            "nextDispatchTick": 1612401755869,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 163.66769999999997,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 319,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100191",
                "value": {
                    "previousTransitionId": 100190,
                    "transitionId": 100191,
                    "terminalToDispatchBeyondPacingMs": 167.02229999999963,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 10,
            "ordinal": 11,
            "source": {
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/10/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/11/retained.json",
            "previousStrictTick": 1612410217407,
            "nextDispatchTick": 1612461887630,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 167.02229999999963,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 324,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100192",
                "value": {
                    "previousTransitionId": 100191,
                    "transitionId": 100192,
                    "terminalToDispatchBeyondPacingMs": 101.8153000000002,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 11,
            "ordinal": 12,
            "source": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/11/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/12/retained.json",
            "previousStrictTick": 1612466776821,
            "nextDispatchTick": 1612517794974,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 101.8153000000002,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 329,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100193",
                "value": {
                    "previousTransitionId": 100192,
                    "transitionId": 100193,
                    "terminalToDispatchBeyondPacingMs": 117.63860000000022,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 12,
            "ordinal": 13,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/12/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/13/retained.json",
            "previousStrictTick": 1612519516661,
            "nextDispatchTick": 1612570693047,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 117.63860000000022,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 334,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100194",
                "value": {
                    "previousTransitionId": 100193,
                    "transitionId": 100194,
                    "terminalToDispatchBeyondPacingMs": 100.48710000000028,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 13,
            "ordinal": 14,
            "source": {
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/13/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/14/retained.json",
            "previousStrictTick": 1612576710562,
            "nextDispatchTick": 1612627715433,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 100.48710000000028,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 339,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100195",
                "value": {
                    "previousTransitionId": 100194,
                    "transitionId": 100195,
                    "terminalToDispatchBeyondPacingMs": 129.6931999999997,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 14,
            "ordinal": 15,
            "source": {
                "method": "fsr",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 2,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/14/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/15/retained.json",
            "previousStrictTick": 1612642190422,
            "nextDispatchTick": 1612693487354,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 129.6931999999997,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 344,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100196",
                "value": {
                    "previousTransitionId": 100195,
                    "transitionId": 100196,
                    "terminalToDispatchBeyondPacingMs": 140.89329999999973,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 15,
            "ordinal": 16,
            "source": {
                "method": "fsr",
                "qualityMode": 2,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 3,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/15/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/16/retained.json",
            "previousStrictTick": 1612701228082,
            "nextDispatchTick": 1612752637015,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 140.89329999999973,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 349,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100197",
                "value": {
                    "previousTransitionId": 100196,
                    "transitionId": 100197,
                    "terminalToDispatchBeyondPacingMs": 128.64410000000044,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 16,
            "ordinal": 17,
            "source": {
                "method": "fsr",
                "qualityMode": 3,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 4,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/16/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/17/retained.json",
            "previousStrictTick": 1612759865181,
            "nextDispatchTick": 1612811151622,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 128.64410000000044,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 354,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100198",
                "value": {
                    "previousTransitionId": 100197,
                    "transitionId": 100198,
                    "terminalToDispatchBeyondPacingMs": 174.67039999999997,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 17,
            "ordinal": 18,
            "source": {
                "method": "fsr",
                "qualityMode": 4,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 5,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/17/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/18/retained.json",
            "previousStrictTick": 1612819870597,
            "nextDispatchTick": 1612871617301,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 174.67039999999997,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 359,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100199",
                "value": {
                    "previousTransitionId": 100198,
                    "transitionId": 100199,
                    "terminalToDispatchBeyondPacingMs": 161.26829999999973,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 18,
            "ordinal": 19,
            "source": {
                "method": "fsr",
                "qualityMode": 5,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/18/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/19/retained.json",
            "previousStrictTick": 1612881941423,
            "nextDispatchTick": 1612933554106,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 161.26829999999973,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 364,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100200",
                "value": {
                    "previousTransitionId": 100199,
                    "transitionId": 100200,
                    "terminalToDispatchBeyondPacingMs": 125.02360000000044,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 19,
            "ordinal": 20,
            "source": {
                "method": "fsr",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/19/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/20/retained.json",
            "previousStrictTick": 1612943024323,
            "nextDispatchTick": 1612994274559,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 125.02360000000044,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 369,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100201",
                "value": {
                    "previousTransitionId": 100200,
                    "transitionId": 100201,
                    "terminalToDispatchBeyondPacingMs": 137.39310000000023,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 20,
            "ordinal": 21,
            "source": {
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/20/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/21/retained.json",
            "previousStrictTick": 1613001993109,
            "nextDispatchTick": 1613053367040,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 137.39310000000023,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 374,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100202",
                "value": {
                    "previousTransitionId": 100201,
                    "transitionId": 100202,
                    "terminalToDispatchBeyondPacingMs": 193.78009999999995,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 21,
            "ordinal": 22,
            "source": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/21/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/22/retained.json",
            "previousStrictTick": 1613059249537,
            "nextDispatchTick": 1613111187338,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 193.78009999999995,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 379,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100203",
                "value": {
                    "previousTransitionId": 100202,
                    "transitionId": 100203,
                    "terminalToDispatchBeyondPacingMs": 172.67919999999958,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 22,
            "ordinal": 23,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/22/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/23/retained.json",
            "previousStrictTick": 1613113644759,
            "nextDispatchTick": 1613165371551,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 172.67919999999958,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 386,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100204",
                "value": {
                    "previousTransitionId": 100203,
                    "transitionId": 100204,
                    "terminalToDispatchBeyondPacingMs": 344.03690000000006,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 23,
            "ordinal": 24,
            "source": {
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/23/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/24/retained.json",
            "previousStrictTick": 1613169982785,
            "nextDispatchTick": 1613223423154,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 344.03690000000006,
            "budgetExceeded": true
        },
        {
            "journalRecord": {
                "sequence": 391,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100205",
                "value": {
                    "previousTransitionId": 100204,
                    "transitionId": 100205,
                    "terminalToDispatchBeyondPacingMs": 297.5051000000003,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 24,
            "ordinal": 25,
            "source": {
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/24/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/25/retained.json",
            "previousStrictTick": 1613248400869,
            "nextDispatchTick": 1613301375920,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 297.5051000000003,
            "budgetExceeded": true
        },
        {
            "journalRecord": {
                "sequence": 398,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100206",
                "value": {
                    "previousTransitionId": 100205,
                    "transitionId": 100206,
                    "terminalToDispatchBeyondPacingMs": 230.83970000000045,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 25,
            "ordinal": 26,
            "source": {
                "method": "dlss",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/25/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/26/retained.json",
            "previousStrictTick": 1613338105858,
            "nextDispatchTick": 1613390414255,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 230.83970000000045,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 403,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100207",
                "value": {
                    "previousTransitionId": 100206,
                    "transitionId": 100207,
                    "terminalToDispatchBeyondPacingMs": 174.69760000000042,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 26,
            "ordinal": 27,
            "source": {
                "method": "fsr",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "target": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/26/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/27/retained.json",
            "previousStrictTick": 1613402553917,
            "nextDispatchTick": 1613454300893,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 174.69760000000042,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 408,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100208",
                "value": {
                    "previousTransitionId": 100207,
                    "transitionId": 100208,
                    "terminalToDispatchBeyondPacingMs": 325.8886000000002,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 27,
            "ordinal": 28,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/27/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/28/retained.json",
            "previousStrictTick": 1613463086737,
            "nextDispatchTick": 1613516345623,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 325.8886000000002,
            "budgetExceeded": true
        },
        {
            "journalRecord": {
                "sequence": 413,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100209",
                "value": {
                    "previousTransitionId": 100208,
                    "transitionId": 100209,
                    "terminalToDispatchBeyondPacingMs": 185.57510000000002,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 28,
            "ordinal": 29,
            "source": {
                "method": "fsr",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/28/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/29/retained.json",
            "previousStrictTick": 1613532771019,
            "nextDispatchTick": 1613584626770,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 185.57510000000002,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 420,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100210",
                "value": {
                    "previousTransitionId": 100209,
                    "transitionId": 100210,
                    "terminalToDispatchBeyondPacingMs": 182.08230000000003,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 29,
            "ordinal": 30,
            "source": {
                "method": "dlss",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "target": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/29/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/30/retained.json",
            "previousStrictTick": 1613604832327,
            "nextDispatchTick": 1613656653150,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 182.08230000000003,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 425,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100211",
                "value": {
                    "previousTransitionId": 100210,
                    "transitionId": 100211,
                    "terminalToDispatchBeyondPacingMs": 174.27080000000024,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 30,
            "ordinal": 31,
            "source": {
                "method": "taa",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/30/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/31/retained.json",
            "previousStrictTick": 1613666251688,
            "nextDispatchTick": 1613717994396,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 174.27080000000024,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 430,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100212",
                "value": {
                    "previousTransitionId": 100211,
                    "transitionId": 100212,
                    "terminalToDispatchBeyondPacingMs": 164.69570000000022,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 31,
            "ordinal": 32,
            "source": {
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/31/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/32/retained.json",
            "previousStrictTick": 1613724633198,
            "nextDispatchTick": 1613776280155,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 164.69570000000022,
            "budgetExceeded": false
        },
        {
            "journalRecord": {
                "sequence": 435,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100213",
                "value": {
                    "previousTransitionId": 100212,
                    "transitionId": 100213,
                    "terminalToDispatchBeyondPacingMs": 170.3164999999999,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 32,
            "ordinal": 33,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/32/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/33/retained.json",
            "previousStrictTick": 1613781558153,
            "nextDispatchTick": 1613833261318,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 170.3164999999999,
            "budgetExceeded": false
        }
    ],
    "exceedances": [
        {
            "journalRecord": {
                "sequence": 386,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100204",
                "value": {
                    "previousTransitionId": 100203,
                    "transitionId": 100204,
                    "terminalToDispatchBeyondPacingMs": 344.03690000000006,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 23,
            "ordinal": 24,
            "source": {
                "method": "dlss",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/23/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/24/retained.json",
            "previousStrictTick": 1613169982785,
            "nextDispatchTick": 1613223423154,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 344.03690000000006,
            "budgetExceeded": true
        },
        {
            "journalRecord": {
                "sequence": 391,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100205",
                "value": {
                    "previousTransitionId": 100204,
                    "transitionId": 100205,
                    "terminalToDispatchBeyondPacingMs": 297.5051000000003,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 24,
            "ordinal": 25,
            "source": {
                "method": "fsr",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "dlssProfile": "K",
                "method": "dlss",
                "qualityMode": 1,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/24/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/25/retained.json",
            "previousStrictTick": 1613248400869,
            "nextDispatchTick": 1613301375920,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 297.5051000000003,
            "budgetExceeded": true
        },
        {
            "journalRecord": {
                "sequence": 408,
                "receiptKey": "renderscale-tuning-nvidia-20260911T155459454Z:cadence:100208",
                "value": {
                    "previousTransitionId": 100207,
                    "transitionId": 100208,
                    "terminalToDispatchBeyondPacingMs": 325.8886000000002,
                    "pacingMilliseconds": 5000,
                    "definition": "next dispatch QPC minus previous strict terminal QPC minus prescribed wait"
                }
            },
            "pass": 2,
            "previousOrdinal": 27,
            "ordinal": 28,
            "source": {
                "method": "none",
                "qualityMode": 0,
                "renderScaleMode": false
            },
            "target": {
                "fsrRuntime": "fsr3",
                "method": "fsr",
                "qualityMode": 6,
                "renderScaleMode": true
            },
            "previousReceipt": "raw/lane-nvidia/pass-2/transitions/27/retained.json",
            "receipt": "raw/lane-nvidia/pass-2/transitions/28/retained.json",
            "previousStrictTick": 1613463086737,
            "nextDispatchTick": 1613516345623,
            "qpcFrequencyHz": 10000000,
            "recomputedBeyondPacingMs": 325.8886000000002,
            "budgetExceeded": true
        }
    ],
    "interpretation": "EXCEEDED is set by either client response-to-next-call delay or server QPC previous strict-terminal-to-next-dispatch delay after subtracting the prescribed 5000 ms wait. The published maximumClientDispatchGapMs summarizes only the former. These are different intervals; the flag is supported by retained QPC cadence evidence.",
    "limitations": [
        "Cadence includes work after the previous strict terminal and before the next dispatch, including same-scenario trace work, transport and scheduling; these observations do not isolate the delay cause.",
        "The diagnostic does not change terminal render or Task 2 classifications and does not authorize a replay."
    ],
    "sourceAudit": {
        "path": "C:\\Users\\quartus\\.codex\\plugins\\cache\\skyrim-vr-tools\\skyrim-vr-automation\\0.9.0+codex.20260911050803\\tools\\renderscale-tuning-live\\durable-worker.js",
        "sha256": "5a0bc1be8abcf500e5445f1fc9fd5e554fbe6482659a9d5e008f8d5fac3d286f",
        "clientGapLines": [333, 339],
        "qpcCadenceLines": [357, 370]
    },
    "recomputedAllIntervalsExactlyWithinMs": 1e-7,
    "cleanupVerified": true,
    "evidencePending": 0
}
```

Passes without resolved profiler samples: 1, 2. Zero unresolved totals
are unavailable CPU/GPU timings and cannot support GPU-cost or FPS claims.
Actual CPU/GPU, resource and memory observations remain in the complete
reports. Unavailable provider drain or retry intervals retain their stated
reasons instead of zero-cost substitutions.

Current-run feedback evidence:

-   [AUTO-20260911-160853983-FFE590F5: pacing-feedback-note.json](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/pacing-feedback-note.json)
-   [AUTO-20260911-154621563-951D86E2: profiler-feedback-note.json](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/profiler-feedback-note.json)

The [canonical ledger](vr-render-scale-ledger.md) retains
the complete summary, primary and same-build comparisons, all passes
and transitions, provenance and supplemental diagnostics. Exact
field-for-field reconstruction passed for all retained objects, and
every historical cell is preserved. The timing audit verified
1,056 cells for PR66 and
1,056 cells for the previous-run
comparison, covering 1,584 distinct cells across
the three runs. Candidate coverage and ledger dimensions remain in the
[complete validation receipt](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/complete-ledger-validation.json).

The primary wrapper took 27.879 s; the
same-build wrapper took 17.169 s.
Complete ledger reporting took 79.393 s
including those comparisons. Exact stage timings follow.

```json
{
    "primaryComparison": {
        "ok": true,
        "status": "COMPLETE",
        "comparisonReused": false,
        "finalization": null,
        "verifiedNumericCells": 1056,
        "ledgerUnchanged": true,
        "assessment": {
            "status": "INCONCLUSIVE",
            "scope": "improvement_or_neutral",
            "changesTestResult": false,
            "reasons": [
                "retained_context_not_matched:scene",
                "retained_context_not_matched:toolchain",
                "matching_fixture_fingerprint_unavailable",
                "explicit_versioned_tolerance_policy_missing"
            ],
            "slowerOutsideTolerance": []
        },
        "comparison": "C:\\src\\skyrim-community-shaders\\artifacts\\renderscale-tuning\\renderscale-tuning-nvidia-20260911T155459454Z-comparison",
        "stagesMs": {
            "toolIdentityMs": 285.4516000079457,
            "comparisonMs": 16311.851199978264,
            "ledgerAuditAndUpdateMs": 10609.187099995324
        },
        "elapsedMs": 27878.706699993927
    },
    "previousRunComparison": {
        "ok": true,
        "status": "COMPLETE",
        "comparisonReused": false,
        "finalization": null,
        "verifiedNumericCells": 1056,
        "ledgerUnchanged": true,
        "assessment": {
            "status": "INCONCLUSIVE",
            "scope": "improvement_or_neutral",
            "changesTestResult": false,
            "reasons": [
                "retained_context_not_matched:scene",
                "matching_fixture_fingerprint_unavailable",
                "explicit_versioned_tolerance_policy_missing"
            ],
            "slowerOutsideTolerance": []
        },
        "comparison": "C:\\src\\skyrim-community-shaders\\artifacts\\renderscale-tuning\\renderscale-tuning-nvidia-20260911T155459454Z-previous-run-comparison",
        "stagesMs": {
            "toolIdentityMs": 175.04540001391433,
            "comparisonMs": 10329.319400014356,
            "ledgerAuditAndUpdateMs": 6230.787700013025
        },
        "elapsedMs": 17168.87590000988
    },
    "ledgerStages": {
        "preparation": 13542.001900001196,
        "comparison": {
            "ok": true,
            "status": "COMPLETE",
            "comparisonReused": false,
            "finalization": null,
            "verifiedNumericCells": 1056,
            "ledgerUnchanged": true,
            "assessment": {
                "status": "INCONCLUSIVE",
                "scope": "improvement_or_neutral",
                "changesTestResult": false,
                "reasons": [
                    "retained_context_not_matched:scene",
                    "retained_context_not_matched:toolchain",
                    "matching_fixture_fingerprint_unavailable",
                    "explicit_versioned_tolerance_policy_missing"
                ],
                "slowerOutsideTolerance": []
            },
            "comparison": "C:\\src\\skyrim-community-shaders\\artifacts\\renderscale-tuning\\renderscale-tuning-nvidia-20260911T155459454Z-comparison",
            "stagesMs": {
                "toolIdentityMs": 285.4516000079457,
                "comparisonMs": 16311.851199978264,
                "ledgerAuditAndUpdateMs": 10609.187099995324
            },
            "elapsedMs": 27878.706699993927
        },
        "comparisons": {
            "pr66": {
                "ok": true,
                "status": "COMPLETE",
                "comparisonReused": false,
                "finalization": null,
                "verifiedNumericCells": 1056,
                "ledgerUnchanged": true,
                "assessment": {
                    "status": "INCONCLUSIVE",
                    "scope": "improvement_or_neutral",
                    "changesTestResult": false,
                    "reasons": [
                        "retained_context_not_matched:scene",
                        "retained_context_not_matched:toolchain",
                        "matching_fixture_fingerprint_unavailable",
                        "explicit_versioned_tolerance_policy_missing"
                    ],
                    "slowerOutsideTolerance": []
                },
                "comparison": "C:\\src\\skyrim-community-shaders\\artifacts\\renderscale-tuning\\renderscale-tuning-nvidia-20260911T155459454Z-comparison",
                "stagesMs": {
                    "toolIdentityMs": 285.4516000079457,
                    "comparisonMs": 16311.851199978264,
                    "ledgerAuditAndUpdateMs": 10609.187099995324
                },
                "elapsedMs": 27878.706699993927
            },
            "previousRun": {
                "ok": true,
                "status": "COMPLETE",
                "comparisonReused": false,
                "finalization": null,
                "verifiedNumericCells": 1056,
                "ledgerUnchanged": true,
                "assessment": {
                    "status": "INCONCLUSIVE",
                    "scope": "improvement_or_neutral",
                    "changesTestResult": false,
                    "reasons": [
                        "retained_context_not_matched:scene",
                        "matching_fixture_fingerprint_unavailable",
                        "explicit_versioned_tolerance_policy_missing"
                    ],
                    "slowerOutsideTolerance": []
                },
                "comparison": "C:\\src\\skyrim-community-shaders\\artifacts\\renderscale-tuning\\renderscale-tuning-nvidia-20260911T155459454Z-previous-run-comparison",
                "stagesMs": {
                    "toolIdentityMs": 175.04540001391433,
                    "comparisonMs": 10329.319400014356,
                    "ledgerAuditAndUpdateMs": 6230.787700013025
                },
                "elapsedMs": 17168.87590000988
            }
        }
    }
}
```

Instrumented finalization took 42.975 s. The
[exact finalizer result](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/finalization-command-result.json) and
[command](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/finalization-command.json) retain the elapsed
measurement and outcome; finalization was performed once.

-   [Finalized summary](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/summary.json), [transitions](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/transitions.csv), [receipt index](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/receipt-index.json)
-   [Full scalar evidence](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z/evidence-values.csv)
-   [PR66 comparison JSON](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z-comparison/comparison.json), [CSV](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z-comparison/comparison.csv), [stage timings](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z-comparison/reporting-performance.json)
-   [Previous-run comparison JSON](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z-previous-run-comparison/comparison.json), [CSV](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z-previous-run-comparison/comparison.csv), [stage timings](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-20260911T155459454Z-previous-run-comparison/reporting-performance.json)

Raw evidence remains local; PR inclusion is the user's decision.

## Pinned PR66 comparison: per-pass distributions and endpoint deltas

B/C denotes this comparison's baseline / candidate. Passes remain
separate. Deltas are candidate minus baseline; all times are
milliseconds excluding the server wait before dispatch. Full
precision remains in the ledger and saved comparison JSON.

| Pass | Statistic | Baseline ms | Candidate ms |  Delta ms | Delta % |
| ---- | --------- | ----------: | -----------: | --------: | ------: |
| 1    | total     |   26180.089 |    29878.493 | +3698.404 | +14.127 |
| 1    | mean      |     793.336 |      905.409 |  +112.073 | +14.127 |
| 1    | median    |     751.644 |      867.739 |  +116.095 | +15.445 |
| 1    | p95       |    1389.267 |     1977.652 |  +588.385 | +42.352 |
| 1    | maximum   |    1856.757 |     2161.205 |  +304.448 | +16.397 |
| 2    | total     |   26635.148 |    32944.289 | +6309.140 | +23.687 |
| 2    | mean      |     807.126 |      998.312 |  +191.186 | +23.687 |
| 2    | median    |     771.931 |      871.898 |   +99.966 | +12.950 |
| 2    | p95       |    1444.193 |     2211.442 |  +767.249 | +53.126 |
| 2    | maximum   |    1932.147 |     3672.994 | +1740.847 | +90.099 |

Rows slower in every retained pass: 2, 3, 4, 6, 9, 10, 11, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 28, 29, 30, 31, 32.

The largest observed strict-time increases in each pass are shown
below. A missing tolerance policy does not hide observed deltas.

| Pass | Row | Route                          |  Delta ms |  Delta % |
| ---- | --- | ------------------------------ | --------: | -------: |
| 1    | 26  | DLSS Hoshipa -> FSR3 Hoshipa   | +1123.653 | +120.446 |
| 1    | 29  | FSR3 UP -> DLSS UP             |  +714.512 |  +49.389 |
| 1    | 4   | DLAA -> DLSS Hoshipa           |  +339.936 |  +37.293 |
| 2    | 25  | FSR3 Native AA -> DLSS Hoshipa | +2129.381 | +137.948 |
| 2    | 24  | DLAA -> FSR3 Native AA         | +1364.318 | +120.368 |
| 2    | 28  | NONE -> FSR3 UP                |  +658.042 |  +66.840 |

The complete comparison also retains each endpoint's B/C values,
phase durations, request/epoch ownership, retry waits, actual
relatch and strict frames/milliseconds, stretch and health gates.
Here n.d. percentage preserves an unavailable comparison such as
a zero baseline; it does not imply zero change.

| Pass | Row | Presentation delta ms |  Delta % | Cleanup delta ms |  Delta % | Cleanup tail delta ms |  Delta % |
| ---- | --- | --------------------: | -------: | ---------------: | -------: | --------------------: | -------: |
| 1    | 1   |               +32.178 |   +6.415 |          +77.185 |  +11.347 |               +45.007 |  +25.197 |
| 1    | 2   |               +23.577 |  +13.912 |          +23.577 |  +13.912 |                +0.000 |     n.d. |
| 1    | 3   |               +35.184 |  +14.061 |          +35.184 |  +14.061 |                +0.000 |     n.d. |
| 1    | 4   |              +214.910 |  +27.223 |         +328.854 |  +39.634 |              +113.944 | +282.879 |
| 1    | 5   |               -36.358 |   -3.847 |          -65.790 |   -6.158 |               -29.431 |  -23.878 |
| 1    | 6   |              +185.457 |  +21.133 |         +158.501 |  +15.769 |               -26.956 |  -21.125 |
| 1    | 7   |              -119.765 |  -10.961 |          -98.628 |   -8.034 |               +21.137 |  +15.660 |
| 1    | 8   |               -57.101 |   -5.447 |         -100.602 |   -8.476 |               -43.501 |  -31.395 |
| 1    | 9   |               +28.222 |   +2.735 |          +41.548 |   +3.537 |               +13.325 |   +9.330 |
| 1    | 10  |               +79.635 |  +13.484 |          +99.429 |  +13.068 |               +19.794 |  +11.625 |
| 1    | 11  |                -2.402 |   -1.218 |          +48.717 |  +10.517 |               +51.119 |  +19.214 |
| 1    | 12  |               +36.703 |  +21.434 |          +36.703 |  +21.434 |                +0.000 |     n.d. |
| 1    | 13  |               -99.689 |  -13.342 |          -99.689 |  -13.342 |                +0.000 |     n.d. |
| 1    | 14  |              -352.198 |  -28.811 |         -234.312 |  -17.938 |              +117.887 | +140.690 |
| 1    | 15  |                +7.683 |   +0.850 |         +143.690 |  +25.569 |                +0.000 |     n.d. |
| 1    | 16  |              +135.901 |  +18.570 |          +61.007 |  +10.125 |                +0.000 |     n.d. |
| 1    | 17  |              +141.934 |  +19.728 |         +126.672 |  +21.565 |                +0.000 |     n.d. |
| 1    | 18  |               +35.630 |   +4.683 |          +55.933 |   +8.869 |                +0.000 |     n.d. |
| 1    | 19  |              +133.962 |  +17.145 |         +111.850 |  +17.196 |                +0.000 |     n.d. |
| 1    | 20  |               +62.230 |  +11.722 |          +48.790 |   +6.595 |               -13.440 |   -6.433 |
| 1    | 21  |               +50.321 |  +24.484 |         +110.472 |  +23.836 |               +60.151 |  +23.320 |
| 1    | 22  |               +60.687 |  +35.583 |          +60.687 |  +35.583 |                +0.000 |     n.d. |
| 1    | 23  |               +78.647 |  +29.890 |          +78.647 |  +29.890 |                +0.000 |     n.d. |
| 1    | 24  |              -294.224 |  -29.629 |         -284.447 |  -24.011 |                +9.777 |   +5.102 |
| 1    | 25  |               -19.794 |   -1.203 |          +46.408 |   +2.615 |               +66.202 |  +51.473 |
| 1    | 26  |             +1253.099 | +155.963 |        +1108.002 | +124.650 |               -85.433 | -100.000 |
| 1    | 27  |              +157.581 |  +32.100 |         +193.926 |  +25.800 |               +36.345 |  +13.940 |
| 1    | 28  |               +92.322 |  +11.392 |         +139.757 |  +16.233 |               +47.435 |  +93.861 |
| 1    | 29  |              +678.066 |  +55.653 |         +702.343 |  +51.795 |               +24.276 |  +17.639 |
| 1    | 30  |              +149.391 |  +30.354 |         +229.675 |  +33.724 |               +80.284 |  +42.505 |
| 1    | 31  |              +268.210 |  +46.452 |         +268.210 |  +46.452 |                +0.000 |     n.d. |
| 1    | 32  |               +94.780 |  +51.375 |         +149.811 |  +33.265 |               +55.032 |  +20.699 |
| 1    | 33  |               -19.149 |   -6.931 |          -19.149 |   -6.931 |                +0.000 |     n.d. |
| 2    | 1   |                -9.492 |   -1.809 |          -20.275 |   -2.652 |               -10.783 |   -4.494 |
| 2    | 2   |               +16.547 |   +9.869 |          +16.547 |   +9.869 |                +0.000 |     n.d. |
| 2    | 3   |               +95.141 |  +38.462 |          +95.141 |  +38.462 |                +0.000 |     n.d. |
| 2    | 4   |               +11.387 |   +1.395 |          +12.497 |   +1.306 |                +1.110 |   +0.791 |
| 2    | 5   |                -6.367 |   -0.784 |           -6.460 |   -0.678 |                -0.094 |   -0.067 |
| 2    | 6   |              +132.783 |  +13.779 |         +125.979 |  +11.365 |                -6.804 |   -4.699 |
| 2    | 7   |               +66.690 |   +6.905 |          +65.325 |   +5.867 |                -1.366 |   -0.925 |
| 2    | 8   |               +11.105 |   +1.175 |           +3.110 |   +0.283 |                -7.995 |   -5.225 |
| 2    | 9   |              +343.607 |  +38.503 |         +319.029 |  +30.881 |               -24.578 |  -17.470 |
| 2    | 10  |               +57.166 |   +9.455 |          +66.864 |   +8.580 |                +9.698 |   +5.552 |
| 2    | 11  |               +44.049 |  +26.967 |          +53.282 |  +12.231 |                +9.233 |   +3.391 |
| 2    | 12  |               -11.359 |   -6.189 |          -11.359 |   -6.189 |                +0.000 |     n.d. |
| 2    | 13  |               -39.549 |   -6.167 |          -39.549 |   -6.167 |                +0.000 |     n.d. |
| 2    | 14  |               +91.890 |   +7.843 |          +76.640 |   +5.790 |               -15.251 |  -10.027 |
| 2    | 15  |              -122.099 |  -13.625 |          +78.672 |  +14.074 |                +0.000 |     n.d. |
| 2    | 16  |               +19.826 |   +2.820 |          +17.918 |   +2.929 |                +0.000 |     n.d. |
| 2    | 17  |               +99.966 |  +12.950 |         +120.498 |  +20.242 |                +0.000 |     n.d. |
| 2    | 18  |              +281.250 |  +37.442 |         +217.514 |  +35.103 |                +0.000 |     n.d. |
| 2    | 19  |              +219.698 |  +30.206 |         +223.467 |  +40.169 |                +0.000 |     n.d. |
| 2    | 20  |              -332.764 |  -36.898 |         -306.173 |  -28.401 |               +26.591 |  +15.092 |
| 2    | 21  |               +61.789 |  +30.464 |          +86.802 |  +17.310 |               +25.012 |   +8.376 |
| 2    | 22  |               +75.841 |  +44.639 |          +75.841 |  +44.639 |                +0.000 |     n.d. |
| 2    | 23  |              +210.412 |  +83.926 |         +210.412 |  +83.926 |                +0.000 |     n.d. |
| 2    | 24  |             +1001.699 | +104.838 |        +1364.318 | +120.368 |              +362.619 | +203.740 |
| 2    | 25  |             +1930.019 | +145.807 |        +2042.279 | +140.084 |              +112.260 |  +83.642 |
| 2    | 26  |              +292.484 |  +38.321 |         +312.605 |  +34.681 |               +20.120 |  +14.568 |
| 2    | 27  |              +103.625 |  +18.510 |          +59.246 |   +7.231 |               -44.379 |  -17.101 |
| 2    | 28  |              +793.401 |  +93.436 |         +650.161 |  +69.299 |               -89.058 | -100.000 |
| 2    | 29  |               +11.422 |   +0.648 |          +80.783 |   +4.366 |               +69.361 |  +79.115 |
| 2    | 30  |              +256.358 |  +55.167 |         +263.240 |  +37.788 |                +6.882 |   +2.967 |
| 2    | 31  |                +3.383 |   +0.512 |           +3.383 |   +0.512 |                +0.000 |     n.d. |
| 2    | 32  |               +36.936 |  +19.365 |          +59.901 |  +12.802 |               +22.965 |   +8.286 |
| 2    | 33  |               +28.392 |  +11.221 |          +28.392 |  +11.221 |                +0.000 |     n.d. |

## Previous same-build run comparison: per-pass distributions and endpoint deltas

B/C denotes this comparison's baseline / candidate. Passes remain
separate. Deltas are candidate minus baseline; all times are
milliseconds excluding the server wait before dispatch. Full
precision remains in the ledger and saved comparison JSON.

| Pass | Statistic | Baseline ms | Candidate ms |  Delta ms | Delta % |
| ---- | --------- | ----------: | -----------: | --------: | ------: |
| 1    | total     |   31117.846 |    29878.493 | -1239.353 |  -3.983 |
| 1    | mean      |     942.965 |      905.409 |   -37.556 |  -3.983 |
| 1    | median    |     877.031 |      867.739 |    -9.292 |  -1.060 |
| 1    | p95       |    1857.869 |     1977.652 |  +119.782 |  +6.447 |
| 1    | maximum   |    2176.467 |     2161.205 |   -15.262 |  -0.701 |
| 2    | total     |   26663.773 |    32944.289 | +6280.516 | +23.554 |
| 2    | mean      |     807.993 |      998.312 |  +190.319 | +23.554 |
| 2    | median    |     788.261 |      871.898 |   +83.637 | +10.610 |
| 2    | p95       |    1307.828 |     2211.442 |  +903.614 | +69.093 |
| 2    | maximum   |    1871.062 |     3672.994 | +1801.932 | +96.305 |

Rows slower in every retained pass: 4, 17, 19, 21, 22, 23, 25, 26, 27, 28, 29, 30, 33.

The largest observed strict-time increases in each pass are shown
below. A missing tolerance policy does not hide observed deltas.

| Pass | Row | Route                          |  Delta ms |  Delta % |
| ---- | --- | ------------------------------ | --------: | -------: |
| 1    | 26  | DLSS Hoshipa -> FSR3 Hoshipa   | +1123.757 | +120.471 |
| 1    | 29  | FSR3 UP -> DLSS UP             |  +331.947 |  +18.147 |
| 1    | 30  | DLSS UP -> TAA                 |  +204.419 |  +28.943 |
| 2    | 25  | FSR3 Native AA -> DLSS Hoshipa | +2611.324 | +245.964 |
| 2    | 24  | DLAA -> FSR3 Native AA         | +1626.694 | +186.745 |
| 2    | 28  | NONE -> FSR3 UP                |  +586.228 |  +55.498 |

The complete comparison also retains each endpoint's B/C values,
phase durations, request/epoch ownership, retry waits, actual
relatch and strict frames/milliseconds, stretch and health gates.
Here n.d. percentage preserves an unavailable comparison such as
a zero baseline; it does not imply zero change.

| Pass | Row | Presentation delta ms |  Delta % | Cleanup delta ms |  Delta % | Cleanup tail delta ms |  Delta % |
| ---- | --- | --------------------: | -------: | ---------------: | -------: | --------------------: | -------: |
| 1    | 1   |               +62.362 |  +13.228 |          +49.458 |   +6.986 |               -12.904 |   -5.456 |
| 1    | 2   |                -5.031 |   -2.540 |           -5.031 |   -2.540 |                +0.000 |     n.d. |
| 1    | 3   |               -96.363 |  -25.241 |          -96.363 |  -25.241 |                +0.000 |     n.d. |
| 1    | 4   |               +68.639 |   +7.335 |          +76.021 |   +7.022 |                +7.382 |   +5.027 |
| 1    | 5   |              -222.427 |  -19.662 |         -284.030 |  -22.075 |               -61.603 |  -39.634 |
| 1    | 6   |               -58.135 |   -5.185 |         -116.271 |   -9.084 |               -58.136 |  -36.614 |
| 1    | 7   |              -173.565 |  -15.140 |         -160.687 |  -12.460 |               +12.878 |   +8.991 |
| 1    | 8   |              -233.585 |  -19.070 |         -293.086 |  -21.247 |               -59.502 |  -38.498 |
| 1    | 9   |               -74.668 |   -6.580 |          -55.445 |   -4.360 |               +19.223 |  +14.039 |
| 1    | 10  |               -56.191 |   -7.735 |         -112.152 |  -11.533 |               -55.961 |  -22.746 |
| 1    | 11  |               -22.726 |  -10.448 |          -18.950 |   -3.569 |                +3.776 |   +1.205 |
| 1    | 12  |               +22.368 |  +12.053 |          +22.368 |  +12.053 |                +0.000 |     n.d. |
| 1    | 13  |              -407.998 |  -38.655 |         -407.998 |  -38.655 |                +0.000 |     n.d. |
| 1    | 14  |             -1306.223 |  -60.016 |        -1051.676 |  -49.523 |              +201.679 |     n.d. |
| 1    | 15  |              -436.710 |  -32.396 |         -386.127 |  -35.367 |                +0.000 |     n.d. |
| 1    | 16  |               +32.312 |   +3.868 |          +59.941 |   +9.931 |                +0.000 |     n.d. |
| 1    | 17  |               +69.953 |   +8.839 |          +61.145 |   +9.365 |                +0.000 |     n.d. |
| 1    | 18  |              -621.129 |  -43.815 |         -596.169 |  -46.477 |                +0.000 |     n.d. |
| 1    | 19  |               +38.290 |   +4.366 |          +23.780 |   +3.220 |                +0.000 |     n.d. |
| 1    | 20  |               +88.992 |  +17.653 |          +45.842 |   +6.172 |               -43.149 |  -18.084 |
| 1    | 21  |               +41.506 |  +19.365 |          +82.913 |  +16.886 |               +41.406 |  +14.965 |
| 1    | 22  |               +56.560 |  +32.380 |          +56.560 |  +32.380 |                +0.000 |     n.d. |
| 1    | 23  |               +83.415 |  +32.287 |          +83.415 |  +32.287 |                +0.000 |     n.d. |
| 1    | 24  |              -124.927 |  -15.166 |         -130.179 |  -12.634 |                -5.252 |   -2.542 |
| 1    | 25  |                -5.096 |   -0.312 |          +21.672 |   +1.205 |               +26.768 |  +15.929 |
| 1    | 26  |             +1265.515 | +159.981 |        +1111.834 | +125.622 |               -94.017 | -100.000 |
| 1    | 27  |               +64.957 |  +11.132 |          +87.842 |  +10.241 |               +22.885 |   +8.346 |
| 1    | 28  |              -108.972 |  -10.771 |          +41.472 |   +4.323 |               +97.973 |     n.d. |
| 1    | 29  |              +283.236 |  +17.557 |         +311.465 |  +17.830 |               +28.229 |  +21.117 |
| 1    | 30  |              +171.373 |  +36.449 |         +204.419 |  +28.943 |               +33.045 |  +13.995 |
| 1    | 31  |               +96.740 |  +12.918 |          +96.740 |  +12.918 |                +0.000 |     n.d. |
| 1    | 32  |               +16.876 |   +6.432 |          +50.687 |   +9.225 |               +33.811 |  +11.777 |
| 1    | 33  |                +1.510 |   +0.591 |           +1.510 |   +0.591 |                +0.000 |     n.d. |
| 2    | 1   |               -34.575 |   -6.289 |          -43.888 |   -5.568 |                -9.313 |   -3.905 |
| 2    | 2   |                -4.398 |   -2.332 |           -4.398 |   -2.332 |                +0.000 |     n.d. |
| 2    | 3   |               +81.216 |  +31.083 |          +81.216 |  +31.083 |                +0.000 |     n.d. |
| 2    | 4   |               +23.530 |   +2.925 |          +32.914 |   +3.515 |                +9.384 |   +7.110 |
| 2    | 5   |               -13.174 |   -1.609 |           -8.241 |   -0.863 |                +4.933 |   +3.632 |
| 2    | 6   |              +122.595 |  +12.588 |         +124.140 |  +11.181 |                +1.545 |   +1.133 |
| 2    | 7   |               +23.541 |   +2.333 |          +78.453 |   +7.131 |               +54.912 |  +60.154 |
| 2    | 8   |               -86.312 |   -8.281 |          -88.898 |   -7.471 |                -2.586 |   -1.752 |
| 2    | 9   |              +239.830 |  +24.075 |         +218.565 |  +19.281 |               -21.264 |  -15.479 |
| 2    | 10  |               +29.605 |   +4.683 |          +16.904 |   +2.039 |               -12.701 |   -6.445 |
| 2    | 11  |               +31.851 |  +18.145 |          +25.446 |   +5.490 |                -6.405 |   -2.225 |
| 2    | 12  |                -5.527 |   -3.110 |           -5.527 |   -3.110 |                +0.000 |     n.d. |
| 2    | 13  |               -12.948 |   -2.106 |          -12.948 |   -2.106 |                +0.000 |     n.d. |
| 2    | 14  |              +444.745 |  +54.319 |         +534.188 |  +61.672 |               +89.443 | +188.667 |
| 2    | 15  |              -566.517 |  -42.259 |         -567.885 |  -47.106 |                +0.000 |     n.d. |
| 2    | 16  |               -44.552 |   -5.806 |           -1.217 |   -0.193 |                +0.000 |     n.d. |
| 2    | 17  |              +105.369 |  +13.746 |          +86.093 |  +13.672 |                +0.000 |     n.d. |
| 2    | 18  |              +259.350 |  +33.548 |         +200.212 |  +31.433 |                +0.000 |     n.d. |
| 2    | 19  |              +161.133 |  +20.503 |         +125.761 |  +19.229 |                +0.000 |     n.d. |
| 2    | 20  |              -304.710 |  -34.872 |         -285.686 |  -27.014 |               +19.023 |  +10.353 |
| 2    | 21  |               +64.882 |  +32.483 |         +112.739 |  +23.709 |               +47.857 |  +17.354 |
| 2    | 22  |               +65.021 |  +35.979 |          +65.021 |  +35.979 |                +0.000 |     n.d. |
| 2    | 23  |              +196.613 |  +74.331 |         +196.613 |  +74.331 |                +0.000 |     n.d. |
| 2    | 24  |             +1270.912 | +185.194 |        +1626.694 | +186.745 |              +355.783 | +192.504 |
| 2    | 25  |             +2463.842 | +311.935 |        +2543.427 | +265.841 |               +79.584 |  +47.687 |
| 2    | 26  |              +181.099 |  +20.706 |         +236.462 |  +24.190 |               +55.362 |  +53.816 |
| 2    | 27  |              +144.085 |  +27.742 |         +119.295 |  +15.711 |               -24.790 |  -10.333 |
| 2    | 28  |              +752.018 |  +84.447 |         +590.669 |  +59.204 |              -107.168 | -100.000 |
| 2    | 29  |              +127.532 |   +7.746 |         +144.323 |   +8.078 |               +16.792 |  +11.973 |
| 2    | 30  |              +151.632 |  +26.629 |         +109.151 |  +12.831 |               -42.481 |  -15.103 |
| 2    | 31  |               -24.689 |   -3.586 |          -24.689 |   -3.586 |                +0.000 |     n.d. |
| 2    | 32  |               -12.072 |   -5.035 |          -58.006 |   -9.902 |               -45.934 |  -13.273 |
| 2    | 33  |               +22.408 |   +8.651 |          +22.408 |   +8.651 |                +0.000 |     n.d. |

## Retained finalizer report

-   Assay execution: **COMPLETE**
-   Transitions dispatched: **66/66**
-   Terminal render verdict: **PASS** (terminal condition only)
-   Lane qualification: **NOT_APPLICABLE**
-   Full-history switch health: **NO_COUNTED_FAILURES**
-   Change assessment: **INCONCLUSIVE**
-   Non-stable terminal notes: **0**
-   Task 2/evidence: **per transition** (66 PASS, 0 FAIL, 0 INCONCLUSIVE)
-   Reporting completeness: **COMPLETE**
-   Deployment verification: **COMPLETE**
-   Memory confirmation: **inconclusive**
-   Presentation stretch: **31 selected, 31 recovered PASS, 0 unrecovered**

Task 2 is deliberately not aggregated. Reporting failure does not rewrite the render result, and a render pass does not hide missing per-transition evidence. Every raw JSON value is available in `evidence-values.csv`.

### Per-pass switch health and performance

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms   | Max ms   | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | -------- | -------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 905.409        | 1977.652 | 2161.205 | 14.939             | 829.055         | 13.280              | 19               | 79             | 5865.714   | 8       | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 998.312        | 2211.442 | 3672.994 | 15.030             | 938.918         | 13.440              | 17               | 73             | 5357.555   | 10      | 0        | 0                   | MET             | COMPLETE |

#### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":49214,"leftPath":"NativeOriginal","referenceFrame":49564,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

#### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":53209,"leftPath":"NativeOriginal","referenceFrame":53574,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

### Memory confirmation

#### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    16966.098 |  16755.965 |     -210.133 |      17083.668 |     17069.43 |        -14.238 |     17126.34 |  16806.391 |     -319.949 |                   n.d. |
| System commit MiB                  |    57475.785 |  57171.406 |     -304.379 |       57503.43 |    57693.449 |         190.02 |    57683.699 |  57712.359 |        28.66 |                   n.d. |
| DXGI process usage MiB             |     4181.164 |   3520.434 |      -660.73 |       3777.996 |     3762.211 |        -15.785 |     3821.449 |   3397.313 |     -424.137 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        218 |          218 |            218 |          218 |              0 |            0 |        221 |          221 |                  1.014 |
| Estimated live tracked texture MiB |            0 |   2280.306 |     2280.306 |       2280.306 |     2280.306 |              0 |            0 |   2308.609 |     2308.609 |                  1.012 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -210.1328125,
        "systemCommitMiB": -304.37890625,
        "dxgiUsageMiB": -660.73046875,
        "liveTextures": 218,
        "liveTextureMiB": 2280.3062477111816
    },
    "pass2": {
        "processPrivateMiB": -319.94921875,
        "systemCommitMiB": 28.66015625,
        "dxgiUsageMiB": -424.13671875,
        "liveTextures": 221,
        "liveTextureMiB": 2308.6085243225098
    },
    "positivePass1PrivateAndCommit": false,
    "pass2ResourceGrowth": true,
    "retentionPredicate": false,
    "initializationPredicate": false
}
```

Conclusion: classification_is_not_proof_for_or_against_a_leak. Growth is relative to the freshly reset tracker in each pass; session IDs are retained.

Evidence gaps: none.

### Owned provider drain and commit intervals

Cells show frames / milliseconds from exact retained QPC endpoints. Ready means the last observed provider-ready event before the first commit; the required-provider set is not exposed. Commit-to-shared-cleanup is elapsed time to that marker, not isolated cleanup cost. Subsequent commit attempts, raw poll counts and missing identity fields remain in summary.json and CSV. The six-frame settling guard is reported separately.

No owned drain attempts were derived; see each row's diagnostic status.

### Transitions

Retry waits end at the first observed preparation-ready result. Ready-to-candidate includes stereo qualification and the settling guard; it is not isolated retry overhead. Overlapping viewport waits are not added. Unavailable results are n.d. and the affected reporting outcome is n/a. All retrieved values remain in raw evidence and evidence-values.csv; reporting provenance never aborts or replays measurements.

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                            | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery   | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | -------------------------------------------------------- | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ------------------ | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 1              | 45803/533.8072 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                            | 133.980 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 46151/1004.3561 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                            | 111.919 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 2              | 46276/908.8209 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 57.720 ms; SubmitStageFoveatedCenter: 57.681 ms | 247.066 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 46407/1063.0187 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 58.162 ms; SubmitStageFoveatedCenter: 58.078 ms | 224.628 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 46539/972.8491 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 57.631 ms; SubmitStageFoveatedCenter: 57.584 ms | 222.982 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 46665/991.2657 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 62.489 ms; SubmitStageFoveatedCenter: 62.444 ms | 259.176 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 46795/1060.0656 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 47369/870.244 ms   | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 47490/911.3329 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 47611/867.7389 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 47730/861.3705 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 47847/796.4951 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 47964/915.3212 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 336.510 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 48627/1625.9483 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 48753/2056.5585 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 346.296 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 49105/1896.441 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                            | 122.775 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 50284/827.9806 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                            | 113.375 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 50407/805.7692 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 71.091 ms; SubmitStageFoveatedCenter: 71.065 ms | 248.716 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 50528/1096.4666 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 65.451 ms; SubmitStageFoveatedCenter: 65.393 ms | 272.084 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 50657/1032.4749 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.078 ms                                        | 304.490 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 50789/955.9836 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 73.297 ms; SubmitStageFoveatedCenter: 73.220 ms | 296.384 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 50908/1236.0135 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 6              | 51515/1263.5101 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 51646/774.0728 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 51767/722.8166 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 51885/871.8975 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 51999/1032.4122 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 52112/947.0217 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 541.609 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 52655/3253.6999 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 52746/1055.7305 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 285.698 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 53091/1773.9243 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |

### Presentation stretch anomalies

| Lane   | Pass | Row | From                                                      | To                                                                          | Consecutive frames | Recovered PASS |
| ------ | ---: | --: | --------------------------------------------------------- | --------------------------------------------------------------------------- | -----------------: | -------------- |
| nvidia |    1 |   1 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"method":"none","qualityMode":0,"renderScaleMode":false}                   |                  1 | yes            |
| nvidia |    1 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    1 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    1 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |                  2 | yes            |
| nvidia |    1 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |        not_exposed | yes            |
| nvidia |    1 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |        not_exposed | yes            |
| nvidia |    2 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |                  6 | yes            |
| nvidia |    2 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |                  2 | yes            |
| nvidia |    2 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |        not_exposed | yes            |

## Complete comparison with pinned PR66

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                        | Candidate                                                                                                  |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z                                                              | renderscale-tuning-nvidia-20260911T155459454Z                                                              |
| Renderer base           | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | bc077786db08637eec8b4c3f718e971e58700a60                                                                   |
| Main-VR base/equivalent | bf4ae54a7d49620c41cb32ee9ecfd44657688ead                                                                        | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                   |
| Compiled source         | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | bc077786db08637eec8b4c3f718e971e58700a60                                                                   |
| Build ID                | 9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757                                                | 0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1                                           |
| DLL SHA-256             | aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461                                                | 47fe6e5f0283ad05d5bd8b9c8ddfe4e22b801dd091bca81050600b9fd2a8e116                                           |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-20260911T155459454Z |

Assessment limits: retained_context_not_matched:scene; retained_context_not_matched:toolchain; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

### Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 793.336/905.409 | 14.127       | 9/8         | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 807.126/998.312 | 23.687       | 10/10       | 0/0          | 0/0                 | none             | MET/MET             |

### Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 735.095   | 829.055   | 93.960   | 12.782  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.400    | 13.280    | -0.120   | -0.896  |
| nvidia | 1    | Relatch proof total        | ms          | 18377.376 | 20726.370 | 2348.994 | 12.782  |
| nvidia | 1    | Relatch proof total        | frames      | 335       | 332       | -3       | -0.896  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 793.336   | 905.409   | 112.073  | 14.127  |
| nvidia | 1    | Strict completion mean     | frames      | 15.091    | 14.939    | -0.152   | -1.004  |
| nvidia | 1    | Strict completion total    | ms          | 26180.089 | 29878.493 | 3698.404 | 14.127  |
| nvidia | 1    | Strict completion total    | frames      | 498       | 493       | -5       | -1.004  |
| nvidia | 1    | Stretch completed episodes | episodes    | 17        | 19        | 2        | 11.765  |
| nvidia | 1    | Stretch completed total    | frames      | 69        | 79        | 10       | 14.493  |
| nvidia | 1    | Stretch completed total    | ms          | 4143.532  | 5865.714  | 1722.181 | 41.563  |
| nvidia | 1    | Stretch longest episode    | ms          | 376.746   | 546.633   | 169.887  | 45.093  |
| nvidia | 2    | Relatch proof mean         | ms          | 747.347   | 938.918   | 191.571  | 25.634  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.760    | 13.440    | -0.320   | -2.326  |
| nvidia | 2    | Relatch proof total        | ms          | 18683.664 | 23472.950 | 4789.285 | 25.634  |
| nvidia | 2    | Relatch proof total        | frames      | 344       | 336       | -8       | -2.326  |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 807.126   | 998.312   | 191.186  | 23.687  |
| nvidia | 2    | Strict completion mean     | frames      | 15.303    | 15.030    | -0.273   | -1.782  |
| nvidia | 2    | Strict completion total    | ms          | 26635.148 | 32944.289 | 6309.140 | 23.687  |
| nvidia | 2    | Strict completion total    | frames      | 505       | 496       | -9       | -1.782  |
| nvidia | 2    | Stretch completed episodes | episodes    | 18        | 17        | -1       | -5.556  |
| nvidia | 2    | Stretch completed total    | frames      | 78        | 73        | -5       | -6.410  |
| nvidia | 2    | Stretch completed total    | ms          | 4955.678  | 5357.555  | 401.877  | 8.109   |
| nvidia | 2    | Stretch longest episode    | ms          | 408.503   | 707.471   | 298.968  | 73.186  |

### nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 680.250 / 757.434   | 77.185   | 11.347  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.466 / 193.043   | 23.577   | 13.912  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 250.227 / 285.411   | 35.184   | 14.061  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 911.526 / 1251.462  | 339.936  | 37.293  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1151.036 / 1094.711 | -56.325  | -4.893  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1085.145 / 1253.378 | 168.232  | 15.503  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1311.362 / 1216.294 | -95.068  | -7.250  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1267.597 / 1185.304 | -82.293  | -6.492  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1257.584 / 1326.106 | 68.522   | 5.449   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 760.848 / 860.277   | 99.429   | 13.068  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 463.236 / 511.953   | 48.717   | 10.517  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 171.239 / 207.942   | 36.703   | 21.434  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 747.166 / 647.477   | -99.689  | -13.342 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1350.983 / 1145.884 | -205.100 | -15.181 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 903.650 / 911.333   | 7.683    | 0.850   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 731.838 / 867.739   | 135.901  | 18.570  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 719.437 / 861.370   | 141.934  | 19.728  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 760.866 / 796.495   | 35.630   | 4.683   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 781.359 / 915.321   | 133.962  | 17.145  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 739.768 / 788.558   | 48.790   | 6.595   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 463.465 / 573.937   | 110.472  | 23.836  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 170.549 / 231.235   | 60.687   | 35.583  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 263.125 / 341.772   | 78.647   | 29.890  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1184.660 / 900.212  | -284.447 | -24.011 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1856.757 / 1925.047 | 68.290   | 3.678   | 2/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 932.906 / 2056.559  | 1123.653 | 120.446 | 0/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 751.644 / 945.570   | 193.926  | 25.800  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 910.661 / 1051.869  | 141.207  | 15.506  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1446.692 / 2161.205 | 714.512  | 49.389  | 1/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 681.036 / 910.711   | 229.675  | 33.724  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 577.394 / 845.604   | 268.210  | 46.452  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 450.355 / 600.166   | 149.811  | 33.265  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 276.262 / 257.113   | -19.149  | -6.931  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 501.629 / 533.807   | 680.250 / 757.434   | 178.621 / 223.627 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4113,"dispatchToBlockedOrPreparationMs":331.8357,"firstNewGenerationToCleanupDrainedMs":228.8014,"firstPhysicalMutationToFirstNewGenerationMs":116.2013,"presentationToStrictCompletionMs":178.6206}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8482,"dispatchToBlockedOrPreparationMs":409.0275,"firstNewGenerationToCleanupDrainedMs":223.8745,"firstPhysicalMutationToFirstNewGenerationMs":120.6843,"presentationToStrictCompletionMs":223.6273}   |
| 2   | 169.466 / 193.043   | 169.466 / 193.043   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":193.0429,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 250.227 / 285.411   | 250.227 / 285.411   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2275,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":285.4112,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 789.446 / 1004.356  | 829.726 / 1158.580  | 40.280 / 154.224  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3058,"dispatchToBlockedOrPreparationMs":343.3656,"firstNewGenerationToCleanupDrainedMs":159.3035,"firstPhysicalMutationToFirstNewGenerationMs":323.7512,"presentationToStrictCompletionMs":122.0799}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.7992,"dispatchToBlockedOrPreparationMs":528.422,"firstNewGenerationToCleanupDrainedMs":212.0908,"firstPhysicalMutationToFirstNewGenerationMs":412.2678,"presentationToStrictCompletionMs":247.1063}    |
| 5   | 945.179 / 908.821   | 1068.436 / 1002.647 | 123.257 / 93.826  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4538,"dispatchToBlockedOrPreparationMs":359.0552,"firstNewGenerationToCleanupDrainedMs":163.5892,"firstPhysicalMutationToFirstNewGenerationMs":542.3382,"presentationToStrictCompletionMs":205.8572}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9882,"dispatchToBlockedOrPreparationMs":439.8709,"firstNewGenerationToCleanupDrainedMs":188.51,"firstPhysicalMutationToFirstNewGenerationMs":370.2777,"presentationToStrictCompletionMs":185.8898}     |
| 6   | 877.562 / 1063.019  | 1005.163 / 1163.664 | 127.602 / 100.646 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9724,"dispatchToBlockedOrPreparationMs":353.8686,"firstNewGenerationToCleanupDrainedMs":174.4377,"firstPhysicalMutationToFirstNewGenerationMs":472.8845,"presentationToStrictCompletionMs":207.5839}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4113,"dispatchToBlockedOrPreparationMs":396.8915,"firstNewGenerationToCleanupDrainedMs":200.565,"firstPhysicalMutationToFirstNewGenerationMs":561.7966,"presentationToStrictCompletionMs":190.3591}    |
| 7   | 1092.614 / 972.849  | 1227.587 / 1128.959 | 134.973 / 156.110 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4825,"dispatchToBlockedOrPreparationMs":365.9809,"firstNewGenerationToCleanupDrainedMs":192.2705,"firstPhysicalMutationToFirstNewGenerationMs":665.8528,"presentationToStrictCompletionMs":218.7486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2862,"dispatchToBlockedOrPreparationMs":417.6047,"firstNewGenerationToCleanupDrainedMs":204.3556,"firstPhysicalMutationToFirstNewGenerationMs":502.7127,"presentationToStrictCompletionMs":243.4451}   |
| 8   | 1048.367 / 991.266  | 1186.924 / 1086.323 | 138.557 / 95.057  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8603,"dispatchToBlockedOrPreparationMs":343.9502,"firstNewGenerationToCleanupDrainedMs":190.2907,"firstPhysicalMutationToFirstNewGenerationMs":648.823,"presentationToStrictCompletionMs":219.2301}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7721,"dispatchToBlockedOrPreparationMs":392.209,"firstNewGenerationToCleanupDrainedMs":187.6797,"firstPhysicalMutationToFirstNewGenerationMs":502.6618,"presentationToStrictCompletionMs":194.0379}    |
| 9   | 1031.843 / 1060.066 | 1174.671 / 1216.218 | 142.827 / 156.153 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3977,"dispatchToBlockedOrPreparationMs":360.4948,"firstNewGenerationToCleanupDrainedMs":184.3618,"firstPhysicalMutationToFirstNewGenerationMs":626.4163,"presentationToStrictCompletionMs":225.7405}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5864,"dispatchToBlockedOrPreparationMs":427.5854,"firstNewGenerationToCleanupDrainedMs":205.0836,"firstPhysicalMutationToFirstNewGenerationMs":578.9627,"presentationToStrictCompletionMs":266.04}     |
| 10  | 590.580 / 670.215   | 760.848 / 860.277   | 170.268 / 190.063 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5661,"dispatchToBlockedOrPreparationMs":360.7792,"firstNewGenerationToCleanupDrainedMs":214.9309,"firstPhysicalMutationToFirstNewGenerationMs":181.5719,"presentationToStrictCompletionMs":170.2681}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.6364,"dispatchToBlockedOrPreparationMs":403.98,"firstNewGenerationToCleanupDrainedMs":241.8405,"firstPhysicalMutationToFirstNewGenerationMs":208.8204,"presentationToStrictCompletionMs":190.0626}     |
| 11  | 197.187 / 194.785   | 463.236 / 511.953   | 266.049 / 317.168 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6416,"dispatchToBlockedOrPreparationMs":149.8394,"firstNewGenerationToCleanupDrainedMs":267.2946,"firstPhysicalMutationToFirstNewGenerationMs":41.4606,"presentationToStrictCompletionMs":266.0491}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4395,"dispatchToBlockedOrPreparationMs":144.2386,"firstNewGenerationToCleanupDrainedMs":317.742,"firstPhysicalMutationToFirstNewGenerationMs":45.5327,"presentationToStrictCompletionMs":317.1677}     |
| 12  | 171.239 / 207.942   | 171.239 / 207.942   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":171.239,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":207.942,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 747.166 / 647.477   | 747.166 / 647.477   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":192.6286,"dispatchToBlockedOrPreparationMs":394.74,"firstNewGenerationToCleanupDrainedMs":41.0968,"firstPhysicalMutationToFirstNewGenerationMs":118.7009,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":57.6277,"dispatchToBlockedOrPreparationMs":433.2875,"firstNewGenerationToCleanupDrainedMs":49.0728,"firstPhysicalMutationToFirstNewGenerationMs":107.4891,"presentationToStrictCompletionMs":0}          |
| 14  | 1222.442 / 870.244  | 1306.234 / 1071.923 | 83.792 / 201.679  | {"blockedOrPreparationToFirstPhysicalMutationMs":260.1297,"dispatchToBlockedOrPreparationMs":447.4412,"firstNewGenerationToCleanupDrainedMs":172.4308,"firstPhysicalMutationToFirstNewGenerationMs":426.2326,"presentationToStrictCompletionMs":128.541}  | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8621,"dispatchToBlockedOrPreparationMs":453.8777,"firstNewGenerationToCleanupDrainedMs":257.3939,"firstPhysicalMutationToFirstNewGenerationMs":354.789,"presentationToStrictCompletionMs":275.6399}    |
| 15  | 903.650 / 911.333   | 561.963 / 705.653   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0595,"dispatchToBlockedOrPreparationMs":360.1568,"firstNewGenerationToCleanupDrainedMs":0.3022,"firstPhysicalMutationToFirstNewGenerationMs":197.4448,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.7879,"dispatchToBlockedOrPreparationMs":404.7304,"firstNewGenerationToCleanupDrainedMs":57.0855,"firstPhysicalMutationToFirstNewGenerationMs":238.0491,"presentationToStrictCompletionMs":0}           |
| 16  | 731.838 / 867.739   | 602.543 / 663.550   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4262,"dispatchToBlockedOrPreparationMs":343.501,"firstNewGenerationToCleanupDrainedMs":42.7009,"firstPhysicalMutationToFirstNewGenerationMs":211.9146,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7508,"dispatchToBlockedOrPreparationMs":386.5211,"firstNewGenerationToCleanupDrainedMs":47.4045,"firstPhysicalMutationToFirstNewGenerationMs":224.8732,"presentationToStrictCompletionMs":0}           |
| 17  | 719.437 / 861.370   | 587.405 / 714.076   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4142,"dispatchToBlockedOrPreparationMs":350.0745,"firstNewGenerationToCleanupDrainedMs":42.2475,"firstPhysicalMutationToFirstNewGenerationMs":190.6686,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.4347,"dispatchToBlockedOrPreparationMs":423.2445,"firstNewGenerationToCleanupDrainedMs":49.3265,"firstPhysicalMutationToFirstNewGenerationMs":236.0707,"presentationToStrictCompletionMs":0}           |
| 18  | 760.866 / 796.495   | 630.628 / 686.561   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9396,"dispatchToBlockedOrPreparationMs":378.3542,"firstNewGenerationToCleanupDrainedMs":46.4289,"firstPhysicalMutationToFirstNewGenerationMs":200.9055,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.4813,"dispatchToBlockedOrPreparationMs":409.4054,"firstNewGenerationToCleanupDrainedMs":50.38,"firstPhysicalMutationToFirstNewGenerationMs":221.2945,"presentationToStrictCompletionMs":0}             |
| 19  | 781.359 / 915.321   | 650.456 / 762.306   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.493,"dispatchToBlockedOrPreparationMs":388.6542,"firstNewGenerationToCleanupDrainedMs":44.3308,"firstPhysicalMutationToFirstNewGenerationMs":211.9777,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8292,"dispatchToBlockedOrPreparationMs":442.1284,"firstNewGenerationToCleanupDrainedMs":58.5194,"firstPhysicalMutationToFirstNewGenerationMs":255.8287,"presentationToStrictCompletionMs":0}           |
| 20  | 530.869 / 593.099   | 739.768 / 788.558   | 208.899 / 195.459 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5574,"dispatchToBlockedOrPreparationMs":341.9606,"firstNewGenerationToCleanupDrainedMs":261.9474,"firstPhysicalMutationToFirstNewGenerationMs":131.3026,"presentationToStrictCompletionMs":208.8989}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8803,"dispatchToBlockedOrPreparationMs":379.3848,"firstNewGenerationToCleanupDrainedMs":259.2834,"firstPhysicalMutationToFirstNewGenerationMs":144.0099,"presentationToStrictCompletionMs":195.4594}   |
| 21  | 205.525 / 255.847   | 463.465 / 573.937   | 257.940 / 318.091 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":141.074,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":257.9402}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.8579,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":318.0908}             |
| 22  | 170.549 / 231.235   | 170.549 / 231.235   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.5487,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":231.2353,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 263.125 / 341.772   | 263.125 / 341.772   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":263.1251,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":341.7719,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 993.040 / 698.816   | 1184.660 / 900.212  | 191.620 / 201.396 | {"blockedOrPreparationToFirstPhysicalMutationMs":302.0873,"dispatchToBlockedOrPreparationMs":459.3461,"firstNewGenerationToCleanupDrainedMs":249.2883,"firstPhysicalMutationToFirstNewGenerationMs":173.9381,"presentationToStrictCompletionMs":191.6197} | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9671,"dispatchToBlockedOrPreparationMs":474.9865,"firstNewGenerationToCleanupDrainedMs":249.5582,"firstPhysicalMutationToFirstNewGenerationMs":171.7007,"presentationToStrictCompletionMs":201.3963}   |
| 25  | 1645.743 / 1625.948 | 1774.359 / 1820.767 | 128.616 / 194.819 | {"blockedOrPreparationToFirstPhysicalMutationMs":691.2409,"dispatchToBlockedOrPreparationMs":433.694,"firstNewGenerationToCleanupDrainedMs":170.4138,"firstPhysicalMutationToFirstNewGenerationMs":479.0101,"presentationToStrictCompletionMs":211.0144}  | {"blockedOrPreparationToFirstPhysicalMutationMs":44.6293,"dispatchToBlockedOrPreparationMs":898.1418,"firstNewGenerationToCleanupDrainedMs":263.3455,"firstPhysicalMutationToFirstNewGenerationMs":614.6503,"presentationToStrictCompletionMs":299.0989}  |
| 26  | 803.459 / 2056.559  | 888.892 / 1996.895  | 85.433 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7511,"dispatchToBlockedOrPreparationMs":381.6439,"firstNewGenerationToCleanupDrainedMs":167.6765,"firstPhysicalMutationToFirstNewGenerationMs":334.8207,"presentationToStrictCompletionMs":129.4467}   | {"blockedOrPreparationToFirstPhysicalMutationMs":318.5488,"dispatchToBlockedOrPreparationMs":505.2653,"firstNewGenerationToCleanupDrainedMs":315.9017,"firstPhysicalMutationToFirstNewGenerationMs":857.1787,"presentationToStrictCompletionMs":0}        |
| 27  | 490.910 / 648.490   | 751.644 / 945.570   | 260.734 / 297.079 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.59,"dispatchToBlockedOrPreparationMs":345.411,"firstNewGenerationToCleanupDrainedMs":261.5421,"firstPhysicalMutationToFirstNewGenerationMs":140.1008,"presentationToStrictCompletionMs":260.7342}      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9428,"dispatchToBlockedOrPreparationMs":458.9175,"firstNewGenerationToCleanupDrainedMs":297.881,"firstPhysicalMutationToFirstNewGenerationMs":183.8285,"presentationToStrictCompletionMs":297.0793}    |
| 28  | 810.416 / 902.738   | 860.954 / 1000.711  | 50.538 / 97.973   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5332,"dispatchToBlockedOrPreparationMs":391.0506,"firstNewGenerationToCleanupDrainedMs":187.5037,"firstPhysicalMutationToFirstNewGenerationMs":278.8664,"presentationToStrictCompletionMs":100.2448}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7527,"dispatchToBlockedOrPreparationMs":442.2494,"firstNewGenerationToCleanupDrainedMs":200.0396,"firstPhysicalMutationToFirstNewGenerationMs":354.6692,"presentationToStrictCompletionMs":149.1303}   |
| 29  | 1218.375 / 1896.441 | 1356.004 / 2058.347 | 137.629 / 161.906 | {"blockedOrPreparationToFirstPhysicalMutationMs":292.5704,"dispatchToBlockedOrPreparationMs":402.8201,"firstNewGenerationToCleanupDrainedMs":182.5617,"firstPhysicalMutationToFirstNewGenerationMs":478.0519,"presentationToStrictCompletionMs":228.3174} | {"blockedOrPreparationToFirstPhysicalMutationMs":755.5254,"dispatchToBlockedOrPreparationMs":491.9573,"firstNewGenerationToCleanupDrainedMs":218.6266,"firstPhysicalMutationToFirstNewGenerationMs":592.2375,"presentationToStrictCompletionMs":264.7636} |
| 30  | 492.154 / 641.545   | 681.036 / 910.711   | 188.881 / 269.166 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2695,"dispatchToBlockedOrPreparationMs":336.9345,"firstNewGenerationToCleanupDrainedMs":235.4251,"firstPhysicalMutationToFirstNewGenerationMs":104.4067,"presentationToStrictCompletionMs":188.8815}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6578,"dispatchToBlockedOrPreparationMs":502.5192,"firstNewGenerationToCleanupDrainedMs":270.2535,"firstPhysicalMutationToFirstNewGenerationMs":133.2804,"presentationToStrictCompletionMs":269.1657}   |
| 31  | 577.394 / 845.604   | 577.394 / 845.604   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1296,"dispatchToBlockedOrPreparationMs":401.3615,"firstNewGenerationToCleanupDrainedMs":39.3942,"firstPhysicalMutationToFirstNewGenerationMs":86.5087,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":91.5916,"dispatchToBlockedOrPreparationMs":558.7396,"firstNewGenerationToCleanupDrainedMs":52.792,"firstPhysicalMutationToFirstNewGenerationMs":142.4809,"presentationToStrictCompletionMs":0}           |
| 32  | 184.484 / 279.264   | 450.355 / 600.166   | 265.870 / 320.902 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.8185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":265.8704}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":175.8172,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":320.9022}             |
| 33  | 276.262 / 257.113   | 276.262 / 257.113   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":276.2618,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":257.1133,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 11                  | 1            | 451.448 / 533.560    | 82.112   | 15 / 17           | 2            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 12 / 13                  | 1            | 670.423 / 946.489    | 276.066  | 17 / 18           | 1            |
| 5   | 12 / 13                  | 1            | 904.847 / 814.137    | -90.710  | 17 / 18           | 1            |
| 6   | 16 / 17                  | 1            | 830.726 / 963.099    | 132.374  | 21 / 22           | 1            |
| 7   | 16 / 17                  | 1            | 1035.316 / 924.604   | -110.713 | 21 / 22           | 1            |
| 8   | 16 / 16                  | 0            | 996.634 / 898.643    | -97.991  | 21 / 21           | 0            |
| 9   | 16 / 17                  | 1            | 990.309 / 1011.135   | 20.826   | 21 / 22           | 1            |
| 10  | 10 / 10                  | 0            | 545.917 / 618.437    | 72.520   | 15 / 15           | 0            |
| 11  | 4 / 4                    | 0            | 195.942 / 194.211    | -1.731   | 11 / 10           | -1           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 13  | 9 / 9                    | 0            | 706.069 / 598.404    | -107.665 | 10 / 10           | 0            |
| 14  | 24 / 13                  | -11          | 1133.803 / 814.529   | -319.275 | 29 / 18           | -11          |
| 15  | 11 / 12                  | 1            | 561.661 / 648.567    | 86.906   | 19 / 17           | -2           |
| 16  | 12 / 11                  | -1           | 559.842 / 616.145    | 56.303   | 16 / 16           | 0            |
| 17  | 12 / 12                  | 0            | 545.157 / 664.750    | 119.593  | 16 / 16           | 0            |
| 18  | 12 / 12                  | 0            | 584.199 / 636.181    | 51.982   | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 606.125 / 703.786    | 97.661   | 16 / 16           | 0            |
| 20  | 10 / 10                  | 0            | 477.821 / 529.275    | 51.454   | 15 / 15           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 5             | 1            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 24  | 16 / 9                   | -7           | 935.371 / 650.654    | -284.717 | 21 / 14           | -7           |
| 25  | 29 / 23                  | -6           | 1603.945 / 1557.421  | -46.524  | 34 / 28           | -6           |
| 26  | 13 / 23                  | 10           | 721.216 / 1680.993   | 959.777  | 18 / 28           | 10           |
| 27  | 10 / 10                  | 0            | 490.102 / 647.689    | 157.587  | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 673.450 / 800.671    | 127.221  | 16 / 16           | 0            |
| 29  | 23 / 29                  | 6            | 1173.442 / 1839.720  | 666.278  | 28 / 34           | 6            |
| 30  | 10 / 9                   | -1           | 445.611 / 640.457    | 194.847  | 15 / 15           | 0            |
| 31  | 9 / 9                    | 0            | 538.000 / 792.812    | 254.812  | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 119.890 / 124.580  | 4.689    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 199.802 / 265.001  | 65.199   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 199.320 / 238.770  | 39.451   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 371.552 / 439.571  | 68.019   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 376.746 / 388.933  | 12.187   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 366.685 / 390.251  | 23.566   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 370.248 / 457.127  | 86.880   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 2              | -4    | 268.924 / 141.929  | -126.995 |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 142.698 / 164.519  | 21.822   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 160.236 / 150.851  | -9.386   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 140.158 / 178.861  | 38.702   |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 148.345 / 162.848  | 14.502   |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 154.845 / 194.113  | 39.268   |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 6 / 6              | 0     | 363.912 / 472.051  | 108.140  |
| 26  | 1 / 2                | 1     | 2 / 11             | 9     | 94.387 / 933.125   | 838.738  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 2 / 3                | 1     | 11 / 16            | 5     | 665.784 / 1163.184 | 497.400  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

### nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 764.649 / 744.373   | -20.275  | -2.652  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 167.665 / 184.212   | 16.547   | 9.869   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 247.366 / 342.507   | 95.141   | 38.462  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1043.867 / 1070.643 | 26.776   | 2.565   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1038.184 / 1037.714 | -0.470   | -0.045  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1196.438 / 1320.258 | 123.819  | 10.349  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1202.543 / 1269.478 | 66.935   | 5.566   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1191.568 / 1191.512 | -0.056   | -0.005  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1123.080 / 1452.539 | 329.459  | 29.335  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 779.290 / 846.154   | 66.864   | 8.580   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 435.637 / 488.919   | 53.282   | 12.231  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 183.527 / 172.169   | -11.359  | -6.189  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 641.300 / 601.751   | -39.549  | -6.167  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.913 / 1447.499 | 69.586   | 5.050   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 896.172 / 774.073   | -122.099 | -13.625 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 702.990 / 722.817   | 19.826   | 2.820   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 771.931 / 871.898   | 99.966   | 12.950  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 751.162 / 1032.412  | 281.250  | 37.442  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 727.324 / 947.022   | 219.698  | 30.206  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1078.028 / 771.855  | -306.173 | -28.401 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 501.448 / 588.250   | 86.802   | 17.310  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 169.901 / 245.742   | 75.841   | 44.639  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 250.712 / 461.123   | 210.412  | 83.926  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1133.454 / 2497.771 | 1364.318 | 120.368 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1543.613 / 3672.994 | 2129.381 | 137.948 | 1/2         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 901.361 / 1213.966  | 312.605  | 34.681  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 819.338 / 878.584   | 59.246   | 7.231   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 984.498 / 1642.540  | 658.042  | 66.840  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1932.147 / 2020.556 | 88.409   | 4.576   | 2/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 696.614 / 959.854   | 263.240  | 37.788  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 660.497 / 663.880   | 3.383    | 0.512   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 467.899 / 527.800   | 59.901   | 12.802  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 253.033 / 281.425   | 28.392   | 11.221  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                          |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 524.712 / 515.220   | 764.649 / 744.373   | 239.937 / 229.153 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6788,"dispatchToBlockedOrPreparationMs":398.9273,"firstNewGenerationToCleanupDrainedMs":240.7526,"firstPhysicalMutationToFirstNewGenerationMs":121.2898,"presentationToStrictCompletionMs":239.9367}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8588,"dispatchToBlockedOrPreparationMs":393.1572,"firstNewGenerationToCleanupDrainedMs":229.9917,"firstPhysicalMutationToFirstNewGenerationMs":117.3653,"presentationToStrictCompletionMs":229.1534}    |
| 2   | 167.665 / 184.212   | 167.665 / 184.212   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.665,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":184.212,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                      |
| 3   | 247.366 / 342.507   | 247.366 / 342.507   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":247.3658,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":342.5066,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 4   | 816.593 / 827.981   | 956.843 / 969.341   | 140.250 / 141.360 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6999,"dispatchToBlockedOrPreparationMs":408.322,"firstNewGenerationToCleanupDrainedMs":184.2716,"firstPhysicalMutationToFirstNewGenerationMs":359.55,"presentationToStrictCompletionMs":227.2739}      | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9868,"dispatchToBlockedOrPreparationMs":417.5757,"firstNewGenerationToCleanupDrainedMs":188.1501,"firstPhysicalMutationToFirstNewGenerationMs":359.628,"presentationToStrictCompletionMs":242.6629}     |
| 5   | 812.136 / 805.769   | 952.986 / 946.526   | 140.851 / 140.757 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0876,"dispatchToBlockedOrPreparationMs":414.3874,"firstNewGenerationToCleanupDrainedMs":191.1592,"firstPhysicalMutationToFirstNewGenerationMs":343.3521,"presentationToStrictCompletionMs":226.0486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8728,"dispatchToBlockedOrPreparationMs":398.5518,"firstNewGenerationToCleanupDrainedMs":186.791,"firstPhysicalMutationToFirstNewGenerationMs":357.3104,"presentationToStrictCompletionMs":231.9448}     |
| 6   | 963.683 / 1096.467  | 1108.473 / 1234.452 | 144.789 / 137.985 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1046,"dispatchToBlockedOrPreparationMs":402.0685,"firstNewGenerationToCleanupDrainedMs":193.2912,"firstPhysicalMutationToFirstNewGenerationMs":508.0083,"presentationToStrictCompletionMs":232.7553}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4328,"dispatchToBlockedOrPreparationMs":469.5229,"firstNewGenerationToCleanupDrainedMs":182.7132,"firstPhysicalMutationToFirstNewGenerationMs":577.7829,"presentationToStrictCompletionMs":223.7912}    |
| 7   | 965.785 / 1032.475  | 1113.348 / 1178.673 | 147.564 / 146.198 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1118,"dispatchToBlockedOrPreparationMs":391.181,"firstNewGenerationToCleanupDrainedMs":195.7517,"firstPhysicalMutationToFirstNewGenerationMs":522.3038,"presentationToStrictCompletionMs":236.7582}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4597,"dispatchToBlockedOrPreparationMs":401.2807,"firstNewGenerationToCleanupDrainedMs":192.3058,"firstPhysicalMutationToFirstNewGenerationMs":580.6267,"presentationToStrictCompletionMs":237.0027}    |
| 8   | 944.879 / 955.984   | 1097.893 / 1101.004 | 153.015 / 145.020 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.849,"dispatchToBlockedOrPreparationMs":386.0721,"firstNewGenerationToCleanupDrainedMs":201.7934,"firstPhysicalMutationToFirstNewGenerationMs":506.1788,"presentationToStrictCompletionMs":246.6891}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9803,"dispatchToBlockedOrPreparationMs":367.1517,"firstNewGenerationToCleanupDrainedMs":194.4339,"firstPhysicalMutationToFirstNewGenerationMs":534.4376,"presentationToStrictCompletionMs":235.528}     |
| 9   | 892.407 / 1236.014  | 1033.092 / 1352.122 | 140.686 / 116.108 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0101,"dispatchToBlockedOrPreparationMs":350.1775,"firstNewGenerationToCleanupDrainedMs":184.5374,"firstPhysicalMutationToFirstNewGenerationMs":494.3675,"presentationToStrictCompletionMs":230.6732}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8577,"dispatchToBlockedOrPreparationMs":483.5375,"firstNewGenerationToCleanupDrainedMs":219.7109,"firstPhysicalMutationToFirstNewGenerationMs":644.0156,"presentationToStrictCompletionMs":216.5259}    |
| 10  | 604.626 / 661.792   | 779.290 / 846.154   | 174.664 / 184.362 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5596,"dispatchToBlockedOrPreparationMs":361.0118,"firstNewGenerationToCleanupDrainedMs":220.7107,"firstPhysicalMutationToFirstNewGenerationMs":193.0078,"presentationToStrictCompletionMs":174.6636}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2584,"dispatchToBlockedOrPreparationMs":391.0439,"firstNewGenerationToCleanupDrainedMs":251.9323,"firstPhysicalMutationToFirstNewGenerationMs":198.9192,"presentationToStrictCompletionMs":184.3616}    |
| 11  | 163.343 / 207.391   | 435.637 / 488.919   | 272.294 / 281.528 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6609,"dispatchToBlockedOrPreparationMs":121.2179,"firstNewGenerationToCleanupDrainedMs":273.0555,"firstPhysicalMutationToFirstNewGenerationMs":37.7029,"presentationToStrictCompletionMs":272.2943}    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.4479,"dispatchToBlockedOrPreparationMs":106.8648,"firstNewGenerationToCleanupDrainedMs":282.789,"firstPhysicalMutationToFirstNewGenerationMs":93.8174,"presentationToStrictCompletionMs":281.5277}      |
| 12  | 183.527 / 172.169   | 183.527 / 172.169   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":183.5274,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.1687,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 641.300 / 601.751   | 641.300 / 601.751   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0337,"dispatchToBlockedOrPreparationMs":429.1853,"firstNewGenerationToCleanupDrainedMs":47.1866,"firstPhysicalMutationToFirstNewGenerationMs":105.8945,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":52.0166,"dispatchToBlockedOrPreparationMs":413.9298,"firstNewGenerationToCleanupDrainedMs":43.1241,"firstPhysicalMutationToFirstNewGenerationMs":92.681,"presentationToStrictCompletionMs":0}             |
| 14  | 1171.620 / 1263.510 | 1323.721 / 1400.361 | 152.101 / 136.851 | {"blockedOrPreparationToFirstPhysicalMutationMs":262.493,"dispatchToBlockedOrPreparationMs":397.1475,"firstNewGenerationToCleanupDrainedMs":203.3643,"firstPhysicalMutationToFirstNewGenerationMs":460.7159,"presentationToStrictCompletionMs":206.2935}  | {"blockedOrPreparationToFirstPhysicalMutationMs":292.7758,"dispatchToBlockedOrPreparationMs":461.5449,"firstNewGenerationToCleanupDrainedMs":179.8478,"firstPhysicalMutationToFirstNewGenerationMs":466.1922,"presentationToStrictCompletionMs":183.9888}  |
| 15  | 896.172 / 774.073   | 558.987 / 637.659   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1702,"dispatchToBlockedOrPreparationMs":352.6839,"firstNewGenerationToCleanupDrainedMs":0.2574,"firstPhysicalMutationToFirstNewGenerationMs":201.8751,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5792,"dispatchToBlockedOrPreparationMs":360.5536,"firstNewGenerationToCleanupDrainedMs":43.9976,"firstPhysicalMutationToFirstNewGenerationMs":228.5281,"presentationToStrictCompletionMs":0}            |
| 16  | 702.990 / 722.817   | 611.687 / 629.605   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7991,"dispatchToBlockedOrPreparationMs":368.7131,"firstNewGenerationToCleanupDrainedMs":43.5961,"firstPhysicalMutationToFirstNewGenerationMs":194.5788,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2062,"dispatchToBlockedOrPreparationMs":381.4671,"firstNewGenerationToCleanupDrainedMs":43.5947,"firstPhysicalMutationToFirstNewGenerationMs":199.337,"presentationToStrictCompletionMs":0}             |
| 17  | 771.931 / 871.898   | 595.296 / 715.794   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0713,"dispatchToBlockedOrPreparationMs":350.708,"firstNewGenerationToCleanupDrainedMs":42.6388,"firstPhysicalMutationToFirstNewGenerationMs":195.8778,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2205,"dispatchToBlockedOrPreparationMs":409.551,"firstNewGenerationToCleanupDrainedMs":50.599,"firstPhysicalMutationToFirstNewGenerationMs":250.4234,"presentationToStrictCompletionMs":0}              |
| 18  | 751.162 / 1032.412  | 619.652 / 837.165   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5809,"dispatchToBlockedOrPreparationMs":376.7105,"firstNewGenerationToCleanupDrainedMs":43.5683,"firstPhysicalMutationToFirstNewGenerationMs":194.792,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":6.3908,"dispatchToBlockedOrPreparationMs":494.6882,"firstNewGenerationToCleanupDrainedMs":53.7296,"firstPhysicalMutationToFirstNewGenerationMs":282.3569,"presentationToStrictCompletionMs":0}            |
| 19  | 727.324 / 947.022   | 556.316 / 779.783   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8053,"dispatchToBlockedOrPreparationMs":356.7758,"firstNewGenerationToCleanupDrainedMs":0.3603,"firstPhysicalMutationToFirstNewGenerationMs":194.375,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8876,"dispatchToBlockedOrPreparationMs":469.0307,"firstNewGenerationToCleanupDrainedMs":51.0687,"firstPhysicalMutationToFirstNewGenerationMs":253.7963,"presentationToStrictCompletionMs":0}            |
| 20  | 901.838 / 569.075   | 1078.028 / 771.855  | 176.190 / 202.780 | {"blockedOrPreparationToFirstPhysicalMutationMs":267.4793,"dispatchToBlockedOrPreparationMs":463.6941,"firstNewGenerationToCleanupDrainedMs":224.7261,"firstPhysicalMutationToFirstNewGenerationMs":122.1281,"presentationToStrictCompletionMs":176.1895} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5177,"dispatchToBlockedOrPreparationMs":376.173,"firstNewGenerationToCleanupDrainedMs":256.5439,"firstPhysicalMutationToFirstNewGenerationMs":134.6204,"presentationToStrictCompletionMs":202.7804}     |
| 21  | 202.830 / 264.619   | 501.448 / 588.250   | 298.618 / 323.631 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.706,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":298.6185}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.7131,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":323.6308}              |
| 22  | 169.901 / 245.742   | 169.901 / 245.742   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.9006,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":245.7421,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 23  | 250.712 / 461.123   | 250.712 / 461.123   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.7117,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":461.1234,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 24  | 955.472 / 1957.170  | 1133.454 / 2497.771 | 177.982 / 540.601 | {"blockedOrPreparationToFirstPhysicalMutationMs":266.0538,"dispatchToBlockedOrPreparationMs":500.9016,"firstNewGenerationToCleanupDrainedMs":221.3993,"firstPhysicalMutationToFirstNewGenerationMs":145.0989,"presentationToStrictCompletionMs":177.9818} | {"blockedOrPreparationToFirstPhysicalMutationMs":5.9913,"dispatchToBlockedOrPreparationMs":1425.7687,"firstNewGenerationToCleanupDrainedMs":660.2786,"firstPhysicalMutationToFirstNewGenerationMs":405.7329,"presentationToStrictCompletionMs":540.6011}   |
| 25  | 1323.681 / 3253.700 | 1457.896 / 3500.175 | 134.215 / 246.475 | {"blockedOrPreparationToFirstPhysicalMutationMs":321.4216,"dispatchToBlockedOrPreparationMs":449.9428,"firstNewGenerationToCleanupDrainedMs":178.1383,"firstPhysicalMutationToFirstNewGenerationMs":508.3934,"presentationToStrictCompletionMs":219.9317} | {"blockedOrPreparationToFirstPhysicalMutationMs":1339.7583,"dispatchToBlockedOrPreparationMs":899.7312,"firstNewGenerationToCleanupDrainedMs":329.8646,"firstPhysicalMutationToFirstNewGenerationMs":930.8206,"presentationToStrictCompletionMs":419.2939} |
| 26  | 763.246 / 1055.730  | 901.361 / 1213.966  | 138.115 / 158.236 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1998,"dispatchToBlockedOrPreparationMs":367.3261,"firstNewGenerationToCleanupDrainedMs":226.1821,"firstPhysicalMutationToFirstNewGenerationMs":303.6534,"presentationToStrictCompletionMs":138.1153}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5478,"dispatchToBlockedOrPreparationMs":540.0148,"firstNewGenerationToCleanupDrainedMs":259.6154,"firstPhysicalMutationToFirstNewGenerationMs":409.7882,"presentationToStrictCompletionMs":158.2357}    |
| 27  | 559.830 / 663.455   | 819.338 / 878.584   | 259.509 / 215.130 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7194,"dispatchToBlockedOrPreparationMs":406.2385,"firstNewGenerationToCleanupDrainedMs":260.7611,"firstPhysicalMutationToFirstNewGenerationMs":147.6192,"presentationToStrictCompletionMs":259.5085}   | {"blockedOrPreparationToFirstPhysicalMutationMs":6.4703,"dispatchToBlockedOrPreparationMs":429.8494,"firstNewGenerationToCleanupDrainedMs":304.1098,"firstPhysicalMutationToFirstNewGenerationMs":138.1549,"presentationToStrictCompletionMs":215.1296}    |
| 28  | 849.139 / 1642.540  | 938.197 / 1588.359  | 89.058 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3027,"dispatchToBlockedOrPreparationMs":443.3656,"firstNewGenerationToCleanupDrainedMs":175.5081,"firstPhysicalMutationToFirstNewGenerationMs":315.0207,"presentationToStrictCompletionMs":135.3592}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8302,"dispatchToBlockedOrPreparationMs":895.1427,"firstNewGenerationToCleanupDrainedMs":232.1489,"firstPhysicalMutationToFirstNewGenerationMs":457.2367,"presentationToStrictCompletionMs":0}           |
| 29  | 1762.502 / 1773.924 | 1850.173 / 1930.956 | 87.671 / 157.032  | {"blockedOrPreparationToFirstPhysicalMutationMs":717.4208,"dispatchToBlockedOrPreparationMs":454.3886,"firstNewGenerationToCleanupDrainedMs":172.8374,"firstPhysicalMutationToFirstNewGenerationMs":505.5261,"presentationToStrictCompletionMs":169.6445} | {"blockedOrPreparationToFirstPhysicalMutationMs":732.2706,"dispatchToBlockedOrPreparationMs":483.413,"firstNewGenerationToCleanupDrainedMs":203.964,"firstPhysicalMutationToFirstNewGenerationMs":511.3084,"presentationToStrictCompletionMs":246.6314}    |
| 30  | 464.694 / 721.052   | 696.614 / 959.854   | 231.920 / 238.802 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7447,"dispatchToBlockedOrPreparationMs":350.0409,"firstNewGenerationToCleanupDrainedMs":232.4266,"firstPhysicalMutationToFirstNewGenerationMs":110.402,"presentationToStrictCompletionMs":231.9199}    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1877,"dispatchToBlockedOrPreparationMs":484.1465,"firstNewGenerationToCleanupDrainedMs":309.0197,"firstPhysicalMutationToFirstNewGenerationMs":161.4999,"presentationToStrictCompletionMs":238.8019}    |
| 31  | 660.497 / 663.880   | 660.497 / 663.880   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":61.1083,"dispatchToBlockedOrPreparationMs":461.9829,"firstNewGenerationToCleanupDrainedMs":42.7901,"firstPhysicalMutationToFirstNewGenerationMs":94.6154,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":68.5485,"dispatchToBlockedOrPreparationMs":447.0338,"firstNewGenerationToCleanupDrainedMs":45.8072,"firstPhysicalMutationToFirstNewGenerationMs":102.4907,"presentationToStrictCompletionMs":0}           |
| 32  | 190.736 / 227.672   | 467.899 / 527.800   | 277.163 / 300.128 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.4716,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":277.1627}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.3843,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":300.1278}              |
| 33  | 253.033 / 281.425   | 253.033 / 281.425   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":253.0332,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":281.4253,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 9                   | -1           | 523.896 / 514.381    | -9.515   | 16 / 15           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 4   | 14 / 12                  | -2           | 772.572 / 781.191    | 8.619    | 19 / 17           | -2           |
| 5   | 14 / 12                  | -2           | 761.827 / 759.735    | -2.092   | 19 / 17           | -2           |
| 6   | 18 / 18                  | 0            | 915.181 / 1051.739   | 136.557  | 23 / 23           | 0            |
| 7   | 17 / 16                  | -1           | 917.597 / 986.367    | 68.771   | 22 / 21           | -1           |
| 8   | 17 / 16                  | -1           | 896.100 / 906.570    | 10.470   | 22 / 21           | -1           |
| 9   | 16 / 16                  | 0            | 848.555 / 1132.411   | 283.856  | 21 / 21           | 0            |
| 10  | 9 / 9                    | 0            | 558.579 / 594.221    | 35.642   | 14 / 14           | 0            |
| 11  | 3 / 3                    | 0            | 162.582 / 206.130    | 43.548   | 10 / 9            | -1           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 13  | 9 / 9                    | 0            | 594.114 / 558.627    | -35.486  | 10 / 10           | 0            |
| 14  | 23 / 23                  | 0            | 1120.356 / 1220.513  | 100.157  | 28 / 28           | 0            |
| 15  | 12 / 12                  | 0            | 558.729 / 593.661    | 34.932   | 19 / 16           | -3           |
| 16  | 12 / 12                  | 0            | 568.091 / 586.010    | 17.919   | 16 / 16           | 0            |
| 17  | 12 / 12                  | 0            | 552.657 / 665.195    | 112.538  | 17 / 16           | -1           |
| 18  | 12 / 12                  | 0            | 576.083 / 783.436    | 207.352  | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 555.956 / 728.715    | 172.759  | 16 / 16           | 0            |
| 20  | 16 / 10                  | -6           | 853.302 / 515.311    | -337.990 | 21 / 15           | -6           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 10           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 15 / 11                  | -4           | 912.054 / 1837.493   | 925.439  | 20 / 16           | -4           |
| 25  | 23 / 29                  | 6            | 1279.758 / 3170.310  | 1890.552 | 28 / 34           | 6            |
| 26  | 12 / 13                  | 1            | 675.179 / 954.351    | 279.172  | 17 / 18           | 1            |
| 27  | 10 / 10                  | 0            | 558.577 / 574.475    | 15.898   | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 762.689 / 1356.210   | 593.521  | 16 / 16           | 0            |
| 29  | 29 / 29                  | 0            | 1677.335 / 1726.992  | 49.657   | 34 / 34           | 0            |
| 30  | 9 / 11                   | 2            | 464.188 / 650.834    | 186.647  | 15 / 17           | 2            |
| 31  | 9 / 9                    | 0            | 617.707 / 618.073    | 0.366    | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C      | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 237.463 / 232.040   | -5.423   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 232.347 / 226.017   | -6.331   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 400.832 / 425.558   | 24.726   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 401.917 / 452.051   | 50.134   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 401.944 / 417.095   | 15.151   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 390.033 / 500.111   | 110.079  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 307.782 / 304.308   | -3.474   |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 143.903 / 162.425   | 18.523   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 141.176 / 144.516   | 3.340    |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 144.864 / 188.803   | 43.940   |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 144.350 / 210.518   | 66.168   |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 143.964 / 189.366   | 45.401   |
| 20  | 1 / 0                | -1    | 5 / 0              | -5    | 334.871 / 0         | -334.871 |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 25  | 1 / 1                | 0     | 6 / 6              | 0     | 395.523 / 707.471   | 311.948  |
| 26  | 1 / 1                | 0     | 2 / 2              | 0     | 93.461 / 132.871    | 39.410   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 29  | 3 / 3                | 0     | 16 / 16            | 0     | 1041.248 / 1064.405 | 23.156   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |

### Cumulative gates and other health evidence

#### renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":18078,"leftPath":"NativeOriginal","referenceFrame":18480,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

#### renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":22718,"leftPath":"NativeOriginal","referenceFrame":23111,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

#### renderscale-tuning-nvidia-20260911T155459454Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":49214,"leftPath":"NativeOriginal","referenceFrame":49564,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

#### renderscale-tuning-nvidia-20260911T155459454Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":53209,"leftPath":"NativeOriginal","referenceFrame":53574,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

### Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16521.3203125 / 16407.21484375 / -114.10546875 | 16966.09765625 / 16755.96484375 / -210.1328125 | -96.027               |
| nvidia | 1    | systemCommitMiB   | 54414.625 / 54313.40234375 / -101.22265625     | 57475.78515625 / 57171.40625 / -304.37890625   | -203.156              |
| nvidia | 1    | dxgiUsageMiB      | 4201.6171875 / 3527.0078125 / -674.609375      | 4181.1640625 / 3520.43359375 / -660.73046875   | 13.879                |
| nvidia | 1    | liveTextures      | 0 / 256 / 256                                  | 0 / 218 / 218                                  | -38                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2424.0259971618652 / 2424.0259971618652    | 0 / 2280.3062477111816 / 2280.3062477111816    | -143.720              |
| nvidia | 2    | processPrivateMiB | 16831.234375 / 16617.78515625 / -213.44921875  | 17126.33984375 / 16806.390625 / -319.94921875  | -106.500              |
| nvidia | 2    | systemCommitMiB   | 54507.67578125 / 54547.8203125 / 40.14453125   | 57683.69921875 / 57712.359375 / 28.66015625    | -11.484               |
| nvidia | 2    | dxgiUsageMiB      | 3889.8828125 / 3566.79296875 / -323.08984375   | 3821.44921875 / 3397.3125 / -424.13671875      | -101.047              |
| nvidia | 2    | liveTextures      | 0 / 233 / 233                                  | 0 / 221 / 221                                  | -12                   |
| nvidia | 2    | liveTextureMiB    | 0 / 2349.4316596984863 / 2349.4316596984863    | 0 / 2308.6085243225098 / 2308.6085243225098    | -40.823               |

### Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2222        | 1979        | -243        |
| cpu/compactPresentationContract/reuses                  | 2200        | 1957        | -243        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 151         | 149         | -2          |
| cpu/generationResourceValidation/fullValidations        | 568         | 554         | -14         |
| cpu/generationResourceValidation/stableChecks           | 8397        | 7561        | -836        |
| cpu/generationResourceValidation/stableHits             | 8316        | 7482        | -834        |
| cpu/generationResourceValidation/stableMisses           | 81          | 79          | -2          |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 1           | 3           | 2           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4328        | 3773        | -555        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4328        | 3773        | -555        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4286        | 3731        | -555        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4328        | 3773        | -555        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4307        | 3752        | -555        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4294        | 3739        | -555        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4238        | 3684        | -554        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 90          | 0           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4313        | 3758        | -555        |
| cpu/strongStereoPacket/captures                         | 4628        | 4106        | -522        |
| cpu/strongStereoPacket/commitAccepts                    | 4424        | 3937        | -487        |
| cpu/strongStereoPacket/commitRejects                    | 66          | 67          | 1           |
| cpu/strongStereoPacket/commitValidations                | 4490        | 4004        | -486        |
| cpu/strongStereoPacket/cycleReuses                      | 2275        | 2012        | -263        |
| cpu/strongStereoPacket/fastSkips                        | 4028        | 3440        | -588        |
| cpu/strongStereoPacket/invalidations                    | 4882        | 4318        | -564        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 100         | 102         | 2           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2253        | 1992        | -261        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.855       | 1.843       | -0.012      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 68.100      | 22.300      | -45.800     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.147       | 0.120       | -0.027      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 11.900      | 1.600       | -10.300     |
| cpu/window/currentFrame                                 | 18480       | 49566       | 31086       |
| cpu/window/elapsedFrames                                | 4328        | 3774        | -554        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 14152       | 45792       | 31640       |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 18482       | 49566       | 31084       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6230998224  | 5421514032  | -809484192  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4911        | 4273        | -638        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12474725760 | 10854103680 | -1620622080 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.307       | 0.000       |
| gpu/item5ActiveFSRCopies/activePixels                   | 12047940400 | 10406941600 | -1640998800 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27299138000 | 23529596000 | -3769542000 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15490       | 13360       | -2130       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2045        | 1763        | -282        |
| gpu/item7EarlyHAM/executedClears                        | 2052        | 1824        | -228        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2052        | 1824        | -228        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4256        | 3746        | -510        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4330        | 3774        | -556        |
| gpu/startFrame                                          | 14152       | 45792       | 31640       |
| profiler/available                                      | true        | true        | n/a         |
| profiler/capabilities                                   | 63          | 63          | 0           |
| profiler/capturing                                      | false       | false       | n/a         |
| profiler/enabled                                        | true        | true        | n/a         |
| profiler/frame/acquiredSlots                            | 0           | 0           | 0           |
| profiler/frame/captured                                 | 0           | 0           | 0           |
| profiler/frame/peakAcquiredSlots                        | 0           | 0           | 0           |
| profiler/frame/slotRefusals                             | 0           | 0           | 0           |
| profiler/limits/frameLatency                            | 3           | 3           | 0           |
| profiler/limits/historyCapacity                         | 300         | 300         | 0           |
| profiler/limits/maximumTimers                           | 128         | 128         | 0           |
| profiler/timerCount                                     | 0           | 0           | 0           |
| profiler/totalsMs/cpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/gpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a         | n/a         |
| texture/active                                          | false       | false       | n/a         |
| texture/attachFailures                                  | 0           | 0           | 0           |
| texture/createdCount                                    | 3974        | 3916        | -58         |
| texture/createdEstimatedBytes                           | 38786366328 | 38655572240 | -130794088  |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3718        | 3698        | -20         |
| texture/destroyedEstimatedBytes                         | 36244590844 | 36264497836 | 19906992    |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 896         | 869         | -27         |
| texture/liveTextureRecordCount                          | 256         | 218         | -38         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 47          | 11          | -36         |
| texture/niSourceTextureMatchedEstimatedBytes            | 166123904   | 15423024    | -150700880  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1473        | 1502        | 29          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 256         | 218         | -38         |
| texture/outstandingEstimatedBytes                       | 2541775484  | 2391074404  | -150701080  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 1           | 3           | 2           |
| texture/supported                                       | true        | true        | n/a         |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2189        | 1931        | -258        |
| cpu/compactPresentationContract/reuses                  | 2167        | 1909        | -258        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 156         | 148         | -8          |
| cpu/generationResourceValidation/fullValidations        | 575         | 559         | -16         |
| cpu/generationResourceValidation/stableChecks           | 8303        | 7317        | -986        |
| cpu/generationResourceValidation/stableHits             | 8226        | 7240        | -986        |
| cpu/generationResourceValidation/stableMisses           | 77          | 77          | 0           |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 2           | 4           | 2           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4221        | 3633        | -588        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4221        | 3633        | -588        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4179        | 3591        | -588        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4221        | 3633        | -588        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4200        | 3612        | -588        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4187        | 3599        | -588        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4130        | 3543        | -587        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 90          | 0           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4206        | 3618        | -588        |
| cpu/strongStereoPacket/captures                         | 4548        | 4036        | -512        |
| cpu/strongStereoPacket/commitAccepts                    | 4359        | 3844        | -515        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 64          | -1          |
| cpu/strongStereoPacket/commitValidations                | 4424        | 3908        | -516        |
| cpu/strongStereoPacket/cycleReuses                      | 2233        | 1978        | -255        |
| cpu/strongStereoPacket/fastSkips                        | 3894        | 3230        | -664        |
| cpu/strongStereoPacket/invalidations                    | 4767        | 4204        | -563        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 104         | 1           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2212        | 1954        | -258        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.879       | 1.887       | 0.008       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 25.400      | 26.900      | 1.500       |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.143       | 0.123       | -0.020      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | 0.800       | -0.800      |
| cpu/window/currentFrame                                 | 23112       | 53575       | 30463       |
| cpu/window/elapsedFrames                                | 4220        | 3633        | -587        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 18892       | 49942       | 31050       |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 23113       | 53575       | 30462       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6071131440  | 5053566672  | -1017564768 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4785        | 3983        | -802        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12154665600 | 10117457280 | -2037208320 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.309       | 0.320       | 0.011       |
| gpu/item5ActiveFSRCopies/activePixels                   | 11865705520 | 10669136880 | -1196568640 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26516112080 | 22632360720 | -3883751360 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15110       | 13110       | -2000       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 1991        | 1771        | -220        |
| gpu/item7EarlyHAM/executedClears                        | 2016        | 1732        | -284        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2016        | 1732        | -284        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4168        | 3664        | -504        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4221        | 3633        | -588        |
| gpu/startFrame                                          | 18892       | 49942       | 31050       |
| profiler/available                                      | true        | true        | n/a         |
| profiler/capabilities                                   | 63          | 63          | 0           |
| profiler/capturing                                      | false       | false       | n/a         |
| profiler/enabled                                        | true        | true        | n/a         |
| profiler/frame/acquiredSlots                            | 0           | 0           | 0           |
| profiler/frame/captured                                 | 0           | 0           | 0           |
| profiler/frame/peakAcquiredSlots                        | 0           | 0           | 0           |
| profiler/frame/slotRefusals                             | 0           | 0           | 0           |
| profiler/limits/frameLatency                            | 3           | 3           | 0           |
| profiler/limits/historyCapacity                         | 300         | 300         | 0           |
| profiler/limits/maximumTimers                           | 128         | 128         | 0           |
| profiler/timerCount                                     | 0           | 0           | 0           |
| profiler/totalsMs/cpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/gpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a         | n/a         |
| texture/active                                          | false       | false       | n/a         |
| texture/attachFailures                                  | 0           | 0           | 0           |
| texture/createdCount                                    | 3968        | 3926        | -42         |
| texture/createdEstimatedBytes                           | 38826844112 | 38665682656 | -161161456  |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3735        | 3705        | -30         |
| texture/destroyedEstimatedBytes                         | 36363286460 | 36244931164 | -118355296  |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 887         | 875         | -12         |
| texture/liveTextureRecordCount                          | 233         | 221         | -12         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 26          | 14          | -12         |
| texture/niSourceTextureMatchedEstimatedBytes            | 87906272    | 45100112    | -42806160   |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1506        | 1503        | -3          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 233         | 221         | -12         |
| texture/outstandingEstimatedBytes                       | 2463557652  | 2420751492  | -42806160   |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 2           | 4           | 2           |
| texture/supported                                       | true        | true        | n/a         |

</details>

### Context, memory, CPU/GPU and evidence

| Retained context matches | Result |
| ------------------------ | ------ |
| adapter                  | true   |
| scene                    | false  |
| foveation                | true   |
| toolchain                | false  |
| dependencies             | true   |
| shaderCompiler           | true   |

Full start/end memory evidence, CPU/GPU/resource counters, phase timings, retry details, native execution proofs, every gate and raw receipt hash are in comparison.json. comparison.csv contains every paired or missing transition with both original row payloads. Missing samples remain null/n/a. Current and baseline failures are preserved separately; a persistent gate failure is not automatically a newly introduced regression. The fixed stretch-frame cutoff is diagnostic only because settling imposes stretch. Compare its actual frames and duration, together with time to applied relatch and strict completion. Request-to-applied frames begin at the producer request, whereas strict frames/ms begin at qualification dispatch; these intervals must not be conflated.

-   Headset refresh, driver version, power state and full modlist/cache equality require retained proof.
-   One process and ordered repeats do not establish causal or statistically significant gains.
-   Missing profiler samples are unavailable time evidence, never zero cost.

## Complete descriptive comparison with the previous same-build run

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                   | Candidate                                                                                                  |
| ----------------------- | ---------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-20260911T153415138Z                                                              | renderscale-tuning-nvidia-20260911T155459454Z                                                              |
| Renderer base           | bc077786db08637eec8b4c3f718e971e58700a60                                                                   | bc077786db08637eec8b4c3f718e971e58700a60                                                                   |
| Main-VR base/equivalent | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                   | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                   |
| Compiled source         | bc077786db08637eec8b4c3f718e971e58700a60                                                                   | bc077786db08637eec8b4c3f718e971e58700a60                                                                   |
| Build ID                | 0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1                                           | 0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1                                           |
| DLL SHA-256             | 47fe6e5f0283ad05d5bd8b9c8ddfe4e22b801dd091bca81050600b9fd2a8e116                                           | 47fe6e5f0283ad05d5bd8b9c8ddfe4e22b801dd091bca81050600b9fd2a8e116                                           |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-20260911T153415138Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-20260911T155459454Z |

Assessment limits: retained_context_not_matched:scene; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

### Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 942.965/905.409 | -3.983       | 10/8        | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 807.993/998.312 | 23.554       | 9/10        | 0/0          | 0/0                 | none             | MET/MET             |

### Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta     | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | --------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 898.438   | 829.055   | -69.383   | -7.723  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.840    | 13.280    | -0.560    | -4.046  |
| nvidia | 1    | Relatch proof total        | ms          | 22460.946 | 20726.370 | -1734.576 | -7.723  |
| nvidia | 1    | Relatch proof total        | frames      | 346       | 332       | -14       | -4.046  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 942.965   | 905.409   | -37.556   | -3.983  |
| nvidia | 1    | Strict completion mean     | frames      | 15.333    | 14.939    | -0.394    | -2.569  |
| nvidia | 1    | Strict completion total    | ms          | 31117.846 | 29878.493 | -1239.353 | -3.983  |
| nvidia | 1    | Strict completion total    | frames      | 506       | 493       | -13       | -2.569  |
| nvidia | 1    | Stretch completed episodes | episodes    | 18        | 19        | 1         | 5.556   |
| nvidia | 1    | Stretch completed total    | frames      | 84        | 79        | -5        | -5.952  |
| nvidia | 1    | Stretch completed total    | ms          | 5988.474  | 5865.714  | -122.760  | -2.050  |
| nvidia | 1    | Stretch longest episode    | ms          | 726.413   | 546.633   | -179.779  | -24.749 |
| nvidia | 2    | Relatch proof mean         | ms          | 740.277   | 938.918   | 198.641   | 26.833  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.280    | 13.440    | 0.160     | 1.205   |
| nvidia | 2    | Relatch proof total        | ms          | 18506.929 | 23472.950 | 4966.021  | 26.833  |
| nvidia | 2    | Relatch proof total        | frames      | 332       | 336       | 4         | 1.205   |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 807.993   | 998.312   | 190.319   | 23.554  |
| nvidia | 2    | Strict completion mean     | frames      | 14.909    | 15.030    | 0.121     | 0.813   |
| nvidia | 2    | Strict completion total    | ms          | 26663.773 | 32944.289 | 6280.516  | 23.554  |
| nvidia | 2    | Strict completion total    | frames      | 492       | 496       | 4         | 0.813   |
| nvidia | 2    | Stretch completed episodes | episodes    | 18        | 17        | -1        | -5.556  |
| nvidia | 2    | Stretch completed total    | frames      | 80        | 73        | -7        | -8.750  |
| nvidia | 2    | Stretch completed total    | ms          | 5198.280  | 5357.555  | 159.274   | 3.064   |
| nvidia | 2    | Stretch longest episode    | ms          | 682.294   | 707.471   | 25.177    | 3.690   |

### nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms  | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | --------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 707.976 / 757.434   | 49.458    | 6.986   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 198.074 / 193.043   | -5.031    | -2.540  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 381.775 / 285.411   | -96.363   | -25.241 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1179.536 / 1251.462 | 71.926    | 6.098   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1379.204 / 1094.711 | -284.493  | -20.627 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1384.794 / 1253.378 | -131.416  | -9.490  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1377.838 / 1216.294 | -161.544  | -11.724 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1472.654 / 1185.304 | -287.350  | -19.512 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1355.919 / 1326.106 | -29.814   | -2.199  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 972.429 / 860.277   | -112.152  | -11.533 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 530.903 / 511.953   | -18.950   | -3.569  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 185.574 / 207.942   | 22.368    | 12.053  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 1055.475 / 647.477  | -407.998  | -38.655 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 2176.467 / 1145.884 | -1030.583 | -47.351 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 1348.043 / 911.333  | -436.710  | -32.396 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 835.427 / 867.739   | 32.312    | 3.868   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 791.418 / 861.370   | 69.953    | 8.839   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1417.624 / 796.495  | -621.129  | -43.815 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 877.031 / 915.321   | 38.290    | 4.366   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 742.716 / 788.558   | 45.842    | 6.172   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 491.025 / 573.937   | 82.913    | 16.886  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 174.675 / 231.235   | 56.560    | 32.380  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 258.357 / 341.772   | 83.415    | 32.287  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1030.392 / 900.212  | -130.179  | -12.634 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1900.786 / 1925.047 | 24.261    | 1.276   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 932.801 / 2056.559  | 1123.757  | 120.471 | 0/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 857.728 / 945.570   | 87.842    | 10.241  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 1011.710 / 1051.869 | 40.158    | 3.969   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1829.258 / 2161.205 | 331.947   | 18.147  | 2/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 706.292 / 910.711   | 204.419   | 28.943  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 748.864 / 845.604   | 96.740    | 12.918  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 549.479 / 600.166   | 50.687    | 9.225   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 255.603 / 257.113   | 1.510     | 0.591   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 471.445 / 533.807   | 707.976 / 757.434   | 236.532 / 223.627 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7737,"dispatchToBlockedOrPreparationMs":347.7917,"firstNewGenerationToCleanupDrainedMs":236.9509,"firstPhysicalMutationToFirstNewGenerationMs":119.46,"presentationToStrictCompletionMs":236.5316}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8482,"dispatchToBlockedOrPreparationMs":409.0275,"firstNewGenerationToCleanupDrainedMs":223.8745,"firstPhysicalMutationToFirstNewGenerationMs":120.6843,"presentationToStrictCompletionMs":223.6273}   |
| 2   | 198.074 / 193.043   | 198.074 / 193.043   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":198.0738,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":193.0429,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 381.775 / 285.411   | 381.775 / 285.411   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":381.7747,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":285.4112,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 935.717 / 1004.356  | 1082.559 / 1158.580 | 146.842 / 154.224 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2346,"dispatchToBlockedOrPreparationMs":513.2194,"firstNewGenerationToCleanupDrainedMs":195.7392,"firstPhysicalMutationToFirstNewGenerationMs":369.3653,"presentationToStrictCompletionMs":243.819}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.7992,"dispatchToBlockedOrPreparationMs":528.422,"firstNewGenerationToCleanupDrainedMs":212.0908,"firstPhysicalMutationToFirstNewGenerationMs":412.2678,"presentationToStrictCompletionMs":247.1063}    |
| 5   | 1131.248 / 908.821  | 1286.677 / 1002.647 | 155.429 / 93.826  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8716,"dispatchToBlockedOrPreparationMs":438.4427,"firstNewGenerationToCleanupDrainedMs":206.8728,"firstPhysicalMutationToFirstNewGenerationMs":637.4898,"presentationToStrictCompletionMs":247.9556}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9882,"dispatchToBlockedOrPreparationMs":439.8709,"firstNewGenerationToCleanupDrainedMs":188.51,"firstPhysicalMutationToFirstNewGenerationMs":370.2777,"presentationToStrictCompletionMs":185.8898}     |
| 6   | 1121.153 / 1063.019 | 1279.935 / 1163.664 | 158.782 / 100.646 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4136,"dispatchToBlockedOrPreparationMs":445.4328,"firstNewGenerationToCleanupDrainedMs":216.4203,"firstPhysicalMutationToFirstNewGenerationMs":613.6686,"presentationToStrictCompletionMs":263.6403}  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4113,"dispatchToBlockedOrPreparationMs":396.8915,"firstNewGenerationToCleanupDrainedMs":200.565,"firstPhysicalMutationToFirstNewGenerationMs":561.7966,"presentationToStrictCompletionMs":190.3591}    |
| 7   | 1146.414 / 972.849  | 1289.646 / 1128.959 | 143.232 / 156.110 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4567,"dispatchToBlockedOrPreparationMs":385.1714,"firstNewGenerationToCleanupDrainedMs":202.0943,"firstPhysicalMutationToFirstNewGenerationMs":698.9233,"presentationToStrictCompletionMs":231.4245}  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2862,"dispatchToBlockedOrPreparationMs":417.6047,"firstNewGenerationToCleanupDrainedMs":204.3556,"firstPhysicalMutationToFirstNewGenerationMs":502.7127,"presentationToStrictCompletionMs":243.4451}   |
| 8   | 1224.850 / 991.266  | 1379.409 / 1086.323 | 154.559 / 95.057  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9449,"dispatchToBlockedOrPreparationMs":428.605,"firstNewGenerationToCleanupDrainedMs":204.3079,"firstPhysicalMutationToFirstNewGenerationMs":742.5509,"presentationToStrictCompletionMs":247.8036}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7721,"dispatchToBlockedOrPreparationMs":392.209,"firstNewGenerationToCleanupDrainedMs":187.6797,"firstPhysicalMutationToFirstNewGenerationMs":502.6618,"presentationToStrictCompletionMs":194.0379}    |
| 9   | 1134.734 / 1060.066 | 1271.663 / 1216.218 | 136.929 / 156.153 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0818,"dispatchToBlockedOrPreparationMs":416.3309,"firstNewGenerationToCleanupDrainedMs":179.359,"firstPhysicalMutationToFirstNewGenerationMs":671.8909,"presentationToStrictCompletionMs":221.1857}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5864,"dispatchToBlockedOrPreparationMs":427.5854,"firstNewGenerationToCleanupDrainedMs":205.0836,"firstPhysicalMutationToFirstNewGenerationMs":578.9627,"presentationToStrictCompletionMs":266.04}     |
| 10  | 726.406 / 670.215   | 972.429 / 860.277   | 246.024 / 190.063 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8964,"dispatchToBlockedOrPreparationMs":433.3119,"firstNewGenerationToCleanupDrainedMs":303.892,"firstPhysicalMutationToFirstNewGenerationMs":230.3289,"presentationToStrictCompletionMs":246.0236}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.6364,"dispatchToBlockedOrPreparationMs":403.98,"firstNewGenerationToCleanupDrainedMs":241.8405,"firstPhysicalMutationToFirstNewGenerationMs":208.8204,"presentationToStrictCompletionMs":190.0626}     |
| 11  | 217.511 / 194.785   | 530.903 / 511.953   | 313.392 / 317.168 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0133,"dispatchToBlockedOrPreparationMs":163.4681,"firstNewGenerationToCleanupDrainedMs":314.1798,"firstPhysicalMutationToFirstNewGenerationMs":48.2413,"presentationToStrictCompletionMs":313.3916}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4395,"dispatchToBlockedOrPreparationMs":144.2386,"firstNewGenerationToCleanupDrainedMs":317.742,"firstPhysicalMutationToFirstNewGenerationMs":45.5327,"presentationToStrictCompletionMs":317.1677}     |
| 12  | 185.574 / 207.942   | 185.574 / 207.942   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":185.5742,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":207.942,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 1055.475 / 647.477  | 1055.475 / 647.477  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":331.8593,"dispatchToBlockedOrPreparationMs":525.0831,"firstNewGenerationToCleanupDrainedMs":49.9863,"firstPhysicalMutationToFirstNewGenerationMs":148.5463,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":57.6277,"dispatchToBlockedOrPreparationMs":433.2875,"firstNewGenerationToCleanupDrainedMs":49.0728,"firstPhysicalMutationToFirstNewGenerationMs":107.4891,"presentationToStrictCompletionMs":0}          |
| 14  | 2176.467 / 870.244  | 2123.598 / 1071.923 | 0 / 201.679       | {"blockedOrPreparationToFirstPhysicalMutationMs":317.6467,"dispatchToBlockedOrPreparationMs":796.3105,"firstNewGenerationToCleanupDrainedMs":203.9615,"firstPhysicalMutationToFirstNewGenerationMs":805.6797,"presentationToStrictCompletionMs":0}       | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8621,"dispatchToBlockedOrPreparationMs":453.8777,"firstNewGenerationToCleanupDrainedMs":257.3939,"firstPhysicalMutationToFirstNewGenerationMs":354.789,"presentationToStrictCompletionMs":275.6399}    |
| 15  | 1348.043 / 911.333  | 1091.780 / 705.653  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":8.1734,"dispatchToBlockedOrPreparationMs":670.6568,"firstNewGenerationToCleanupDrainedMs":55.3568,"firstPhysicalMutationToFirstNewGenerationMs":357.5931,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":5.7879,"dispatchToBlockedOrPreparationMs":404.7304,"firstNewGenerationToCleanupDrainedMs":57.0855,"firstPhysicalMutationToFirstNewGenerationMs":238.0491,"presentationToStrictCompletionMs":0}           |
| 16  | 835.427 / 867.739   | 603.608 / 663.550   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4995,"dispatchToBlockedOrPreparationMs":382.8237,"firstNewGenerationToCleanupDrainedMs":0.1826,"firstPhysicalMutationToFirstNewGenerationMs":216.1024,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7508,"dispatchToBlockedOrPreparationMs":386.5211,"firstNewGenerationToCleanupDrainedMs":47.4045,"firstPhysicalMutationToFirstNewGenerationMs":224.8732,"presentationToStrictCompletionMs":0}           |
| 17  | 791.418 / 861.370   | 652.932 / 714.076   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.742,"dispatchToBlockedOrPreparationMs":382.4265,"firstNewGenerationToCleanupDrainedMs":45.3268,"firstPhysicalMutationToFirstNewGenerationMs":220.4365,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.4347,"dispatchToBlockedOrPreparationMs":423.2445,"firstNewGenerationToCleanupDrainedMs":49.3265,"firstPhysicalMutationToFirstNewGenerationMs":236.0707,"presentationToStrictCompletionMs":0}           |
| 18  | 1417.624 / 796.495  | 1282.730 / 686.561  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":319.6063,"dispatchToBlockedOrPreparationMs":512.453,"firstNewGenerationToCleanupDrainedMs":44.6283,"firstPhysicalMutationToFirstNewGenerationMs":406.0426,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":5.4813,"dispatchToBlockedOrPreparationMs":409.4054,"firstNewGenerationToCleanupDrainedMs":50.38,"firstPhysicalMutationToFirstNewGenerationMs":221.2945,"presentationToStrictCompletionMs":0}             |
| 19  | 877.031 / 915.321   | 738.526 / 762.306   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.5759,"dispatchToBlockedOrPreparationMs":404.8708,"firstNewGenerationToCleanupDrainedMs":48.3438,"firstPhysicalMutationToFirstNewGenerationMs":279.7356,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8292,"dispatchToBlockedOrPreparationMs":442.1284,"firstNewGenerationToCleanupDrainedMs":58.5194,"firstPhysicalMutationToFirstNewGenerationMs":255.8287,"presentationToStrictCompletionMs":0}           |
| 20  | 504.108 / 593.099   | 742.716 / 788.558   | 238.609 / 195.459 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1109,"dispatchToBlockedOrPreparationMs":372.7511,"firstNewGenerationToCleanupDrainedMs":238.7998,"firstPhysicalMutationToFirstNewGenerationMs":127.0543,"presentationToStrictCompletionMs":238.6086}  | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8803,"dispatchToBlockedOrPreparationMs":379.3848,"firstNewGenerationToCleanupDrainedMs":259.2834,"firstPhysicalMutationToFirstNewGenerationMs":144.0099,"presentationToStrictCompletionMs":195.4594}   |
| 21  | 214.340 / 255.847   | 491.025 / 573.937   | 276.685 / 318.091 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":131.7941,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":276.6846}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.8579,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":318.0908}             |
| 22  | 174.675 / 231.235   | 174.675 / 231.235   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":174.6751,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":231.2353,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 258.357 / 341.772   | 258.357 / 341.772   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":258.3567,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":341.7719,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 823.743 / 698.816   | 1030.392 / 900.212  | 206.649 / 201.396 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7149,"dispatchToBlockedOrPreparationMs":526.5686,"firstNewGenerationToCleanupDrainedMs":274.0909,"firstPhysicalMutationToFirstNewGenerationMs":226.0173,"presentationToStrictCompletionMs":206.6488}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9671,"dispatchToBlockedOrPreparationMs":474.9865,"firstNewGenerationToCleanupDrainedMs":249.5582,"firstPhysicalMutationToFirstNewGenerationMs":171.7007,"presentationToStrictCompletionMs":201.3963}   |
| 25  | 1631.044 / 1625.948 | 1799.095 / 1820.767 | 168.051 / 194.819 | {"blockedOrPreparationToFirstPhysicalMutationMs":42.5328,"dispatchToBlockedOrPreparationMs":778.2024,"firstNewGenerationToCleanupDrainedMs":229.0936,"firstPhysicalMutationToFirstNewGenerationMs":749.2658,"presentationToStrictCompletionMs":269.7421} | {"blockedOrPreparationToFirstPhysicalMutationMs":44.6293,"dispatchToBlockedOrPreparationMs":898.1418,"firstNewGenerationToCleanupDrainedMs":263.3455,"firstPhysicalMutationToFirstNewGenerationMs":614.6503,"presentationToStrictCompletionMs":299.0989}  |
| 26  | 791.043 / 2056.559  | 885.060 / 1996.895  | 94.017 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9527,"dispatchToBlockedOrPreparationMs":377.3538,"firstNewGenerationToCleanupDrainedMs":182.6476,"firstPhysicalMutationToFirstNewGenerationMs":321.1063,"presentationToStrictCompletionMs":141.7579}  | {"blockedOrPreparationToFirstPhysicalMutationMs":318.5488,"dispatchToBlockedOrPreparationMs":505.2653,"firstNewGenerationToCleanupDrainedMs":315.9017,"firstPhysicalMutationToFirstNewGenerationMs":857.1787,"presentationToStrictCompletionMs":0}        |
| 27  | 583.533 / 648.490   | 857.728 / 945.570   | 274.195 / 297.079 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8659,"dispatchToBlockedOrPreparationMs":408.2371,"firstNewGenerationToCleanupDrainedMs":275.1607,"firstPhysicalMutationToFirstNewGenerationMs":168.4642,"presentationToStrictCompletionMs":274.1947}  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9428,"dispatchToBlockedOrPreparationMs":458.9175,"firstNewGenerationToCleanupDrainedMs":297.881,"firstPhysicalMutationToFirstNewGenerationMs":183.8285,"presentationToStrictCompletionMs":297.0793}    |
| 28  | 1011.710 / 902.738  | 959.239 / 1000.711  | 0 / 97.973        | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6996,"dispatchToBlockedOrPreparationMs":411.5543,"firstNewGenerationToCleanupDrainedMs":193.7838,"firstPhysicalMutationToFirstNewGenerationMs":350.2012,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7527,"dispatchToBlockedOrPreparationMs":442.2494,"firstNewGenerationToCleanupDrainedMs":200.0396,"firstPhysicalMutationToFirstNewGenerationMs":354.6692,"presentationToStrictCompletionMs":149.1303}   |
| 29  | 1613.205 / 1896.441 | 1746.882 / 2058.347 | 133.677 / 161.906 | {"blockedOrPreparationToFirstPhysicalMutationMs":651.0355,"dispatchToBlockedOrPreparationMs":428.8006,"firstNewGenerationToCleanupDrainedMs":182.667,"firstPhysicalMutationToFirstNewGenerationMs":484.3786,"presentationToStrictCompletionMs":216.0534} | {"blockedOrPreparationToFirstPhysicalMutationMs":755.5254,"dispatchToBlockedOrPreparationMs":491.9573,"firstNewGenerationToCleanupDrainedMs":218.6266,"firstPhysicalMutationToFirstNewGenerationMs":592.2375,"presentationToStrictCompletionMs":264.7636} |
| 30  | 470.172 / 641.545   | 706.292 / 910.711   | 236.120 / 269.166 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6798,"dispatchToBlockedOrPreparationMs":354.623,"firstNewGenerationToCleanupDrainedMs":236.3138,"firstPhysicalMutationToFirstNewGenerationMs":111.6756,"presentationToStrictCompletionMs":236.1203}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6578,"dispatchToBlockedOrPreparationMs":502.5192,"firstNewGenerationToCleanupDrainedMs":270.2535,"firstPhysicalMutationToFirstNewGenerationMs":133.2804,"presentationToStrictCompletionMs":269.1657}   |
| 31  | 748.864 / 845.604   | 748.864 / 845.604   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":54.4848,"dispatchToBlockedOrPreparationMs":557.0639,"firstNewGenerationToCleanupDrainedMs":45.0075,"firstPhysicalMutationToFirstNewGenerationMs":92.3075,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":91.5916,"dispatchToBlockedOrPreparationMs":558.7396,"firstNewGenerationToCleanupDrainedMs":52.792,"firstPhysicalMutationToFirstNewGenerationMs":142.4809,"presentationToStrictCompletionMs":0}           |
| 32  | 262.388 / 279.264   | 549.479 / 600.166   | 287.091 / 320.902 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":160.7632,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":287.0913}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":175.8172,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":320.9022}             |
| 33  | 255.603 / 257.113   | 255.603 / 257.113   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":255.6029,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":257.1133,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms  | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | --------- | ----------------- | ------------ |
| 1   | 10 / 11                  | 1            | 471.025 / 533.560    | 62.535    | 15 / 17           | 2            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a       | 3 / 4             | 1            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a       | 3 / 3             | 0            |
| 4   | 13 / 13                  | 0            | 886.819 / 946.489    | 59.670    | 18 / 18           | 0            |
| 5   | 13 / 13                  | 0            | 1079.804 / 814.137   | -265.667  | 18 / 18           | 0            |
| 6   | 17 / 17                  | 0            | 1063.515 / 963.099   | -100.416  | 22 / 22           | 0            |
| 7   | 17 / 17                  | 0            | 1087.551 / 924.604   | -162.948  | 22 / 22           | 0            |
| 8   | 17 / 16                  | -1           | 1175.101 / 898.643   | -276.458  | 22 / 21           | -1           |
| 9   | 18 / 17                  | -1           | 1092.304 / 1011.135  | -81.169   | 23 / 22           | -1           |
| 10  | 11 / 10                  | -1           | 668.537 / 618.437    | -50.100   | 17 / 15           | -2           |
| 11  | 3 / 4                    | 1            | 216.723 / 194.211    | -22.512   | 9 / 10            | 1            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a       | 4 / 3             | -1           |
| 13  | 9 / 9                    | 0            | 1005.489 / 598.404   | -407.084  | 10 / 10           | 0            |
| 14  | 23 / 13                  | -10          | 1919.637 / 814.529   | -1105.108 | 28 / 18           | -10          |
| 15  | 12 / 12                  | 0            | 1036.423 / 648.567   | -387.856  | 17 / 17           | 0            |
| 16  | 12 / 11                  | -1           | 603.426 / 616.145    | 12.719    | 17 / 16           | -1           |
| 17  | 12 / 12                  | 0            | 607.605 / 664.750    | 57.145    | 16 / 16           | 0            |
| 18  | 22 / 12                  | -10          | 1238.102 / 636.181   | -601.921  | 26 / 16           | -10          |
| 19  | 12 / 12                  | 0            | 690.182 / 703.786    | 13.604    | 16 / 16           | 0            |
| 20  | 10 / 10                  | 0            | 503.916 / 529.275    | 25.359    | 15 / 15           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a       | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a       | 4 / 5             | 1            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a       | 3 / 4             | 1            |
| 24  | 11 / 9                   | -2           | 756.301 / 650.654    | -105.646  | 16 / 14           | -2           |
| 25  | 23 / 23                  | 0            | 1570.001 / 1557.421  | -12.580   | 28 / 28           | 0            |
| 26  | 12 / 23                  | 11           | 702.413 / 1680.993   | 978.580   | 17 / 28           | 11           |
| 27  | 10 / 10                  | 0            | 582.567 / 647.689    | 65.122    | 16 / 16           | 0            |
| 28  | 12 / 11                  | -1           | 765.455 / 800.671    | 35.216    | 17 / 16           | -1           |
| 29  | 29 / 29                  | 0            | 1564.215 / 1839.720  | 275.505   | 34 / 34           | 0            |
| 30  | 9 / 9                    | 0            | 469.978 / 640.457    | 170.479   | 15 / 15           | 0            |
| 31  | 9 / 9                    | 0            | 703.856 / 792.812    | 88.956    | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a       | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a       | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 123.173 / 124.580  | 1.407    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 236.631 / 265.001  | 28.370   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 244.304 / 238.770  | -5.534   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 479.642 / 439.571  | -40.072  |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 389.290 / 388.933  | -0.357   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 440.955 / 390.251  | -50.704  |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 407.488 / 457.127  | 49.639   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 2              | -4    | 518.001 / 141.929  | -376.072 |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 224.534 / 164.519  | -60.015  |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 151.957 / 150.851  | -1.106   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 166.582 / 178.861  | 12.279   |
| 18  | 1 / 1                | 0     | 13 / 3             | -10   | 726.413 / 162.848  | -563.565 |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 207.084 / 194.113  | -12.971  |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 6 / 6              | 0     | 593.009 / 472.051  | -120.958 |
| 26  | 1 / 2                | 1     | 2 / 11             | 9     | 102.075 / 933.125  | 831.050  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 3                | 0     | 16 / 16            | 0     | 977.337 / 1163.184 | 185.847  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

### nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 788.261 / 744.373   | -43.888  | -5.568  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 188.609 / 184.212   | -4.398   | -2.332  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 261.291 / 342.507   | 81.216   | 31.083  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1038.610 / 1070.643 | 32.034   | 3.084   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1039.456 / 1037.714 | -1.742   | -0.168  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1203.006 / 1320.258 | 117.252  | 9.747   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1186.420 / 1269.478 | 83.058   | 7.001   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1285.986 / 1191.512 | -94.475  | -7.346  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1217.829 / 1452.539 | 234.710  | 19.273  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 829.250 / 846.154   | 16.904   | 2.039   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 463.473 / 488.919   | 25.446   | 5.490   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 177.695 / 172.169   | -5.527   | -3.110  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 614.699 / 601.751   | -12.948  | -2.106  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 915.325 / 1447.499  | 532.174  | 58.140  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 1340.590 / 774.073  | -566.517 | -42.259 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 767.369 / 722.817   | -44.552  | -5.806  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 766.529 / 871.898   | 105.369  | 13.746  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 773.062 / 1032.412  | 259.350  | 33.548  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 785.889 / 947.022   | 161.133  | 20.503  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1057.541 / 771.855  | -285.686 | -27.014 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 475.511 / 588.250   | 112.739  | 23.709  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 180.721 / 245.742   | 65.021   | 35.979  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 264.511 / 461.123   | 196.613  | 74.331  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 871.077 / 2497.771  | 1626.694 | 186.745 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1061.670 / 3672.994 | 2611.324 | 245.964 | 0/2         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1038.635 / 1213.966 | 175.332  | 16.881  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 759.289 / 878.584   | 119.295  | 15.711  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 1056.312 / 1642.540 | 586.228  | 55.498  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1871.062 / 2020.556 | 149.493  | 7.990   | 2/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 850.703 / 959.854   | 109.151  | 12.831  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 688.570 / 663.880   | -24.689  | -3.586  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 585.806 / 527.800   | -58.006  | -9.902  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 259.017 / 281.425   | 22.408   | 8.651   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                          |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 549.794 / 515.220   | 788.261 / 744.373   | 238.467 / 229.153 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3573,"dispatchToBlockedOrPreparationMs":428.9674,"firstNewGenerationToCleanupDrainedMs":238.6696,"firstPhysicalMutationToFirstNewGenerationMs":116.2666,"presentationToStrictCompletionMs":238.4666}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8588,"dispatchToBlockedOrPreparationMs":393.1572,"firstNewGenerationToCleanupDrainedMs":229.9917,"firstPhysicalMutationToFirstNewGenerationMs":117.3653,"presentationToStrictCompletionMs":229.1534}    |
| 2   | 188.609 / 184.212   | 188.609 / 184.212   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":188.6095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":184.212,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                      |
| 3   | 261.291 / 342.507   | 261.291 / 342.507   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.2905,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":342.5066,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 4   | 804.450 / 827.981   | 936.426 / 969.341   | 131.976 / 141.360 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9939,"dispatchToBlockedOrPreparationMs":410.5675,"firstNewGenerationToCleanupDrainedMs":176.2961,"firstPhysicalMutationToFirstNewGenerationMs":345.5687,"presentationToStrictCompletionMs":234.1596}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9868,"dispatchToBlockedOrPreparationMs":417.5757,"firstNewGenerationToCleanupDrainedMs":188.1501,"firstPhysicalMutationToFirstNewGenerationMs":359.628,"presentationToStrictCompletionMs":242.6629}     |
| 5   | 818.943 / 805.769   | 954.767 / 946.526   | 135.824 / 140.757 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8184,"dispatchToBlockedOrPreparationMs":421.1279,"firstNewGenerationToCleanupDrainedMs":179.569,"firstPhysicalMutationToFirstNewGenerationMs":350.2513,"presentationToStrictCompletionMs":220.5132}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8728,"dispatchToBlockedOrPreparationMs":398.5518,"firstNewGenerationToCleanupDrainedMs":186.791,"firstPhysicalMutationToFirstNewGenerationMs":357.3104,"presentationToStrictCompletionMs":231.9448}     |
| 6   | 973.871 / 1096.467  | 1110.311 / 1234.452 | 136.440 / 137.985 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7294,"dispatchToBlockedOrPreparationMs":411.3054,"firstNewGenerationToCleanupDrainedMs":180.0578,"firstPhysicalMutationToFirstNewGenerationMs":515.2187,"presentationToStrictCompletionMs":229.1348}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4328,"dispatchToBlockedOrPreparationMs":469.5229,"firstNewGenerationToCleanupDrainedMs":182.7132,"firstPhysicalMutationToFirstNewGenerationMs":577.7829,"presentationToStrictCompletionMs":223.7912}    |
| 7   | 1008.934 / 1032.475 | 1100.220 / 1178.673 | 91.286 / 146.198  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0656,"dispatchToBlockedOrPreparationMs":403.5784,"firstNewGenerationToCleanupDrainedMs":179.5065,"firstPhysicalMutationToFirstNewGenerationMs":513.0697,"presentationToStrictCompletionMs":177.4858}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4597,"dispatchToBlockedOrPreparationMs":401.2807,"firstNewGenerationToCleanupDrainedMs":192.3058,"firstPhysicalMutationToFirstNewGenerationMs":580.6267,"presentationToStrictCompletionMs":237.0027}    |
| 8   | 1042.295 / 955.984  | 1189.901 / 1101.004 | 147.606 / 145.020 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.59,"dispatchToBlockedOrPreparationMs":423.2783,"firstNewGenerationToCleanupDrainedMs":195.9647,"firstPhysicalMutationToFirstNewGenerationMs":566.0685,"presentationToStrictCompletionMs":243.6909}     | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9803,"dispatchToBlockedOrPreparationMs":367.1517,"firstNewGenerationToCleanupDrainedMs":194.4339,"firstPhysicalMutationToFirstNewGenerationMs":534.4376,"presentationToStrictCompletionMs":235.528}     |
| 9   | 996.184 / 1236.014  | 1133.556 / 1352.122 | 137.373 / 116.108 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8973,"dispatchToBlockedOrPreparationMs":420.0474,"firstNewGenerationToCleanupDrainedMs":183.8236,"firstPhysicalMutationToFirstNewGenerationMs":525.7879,"presentationToStrictCompletionMs":221.6455}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8577,"dispatchToBlockedOrPreparationMs":483.5375,"firstNewGenerationToCleanupDrainedMs":219.7109,"firstPhysicalMutationToFirstNewGenerationMs":644.0156,"presentationToStrictCompletionMs":216.5259}    |
| 10  | 632.187 / 661.792   | 829.250 / 846.154   | 197.063 / 184.362 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6215,"dispatchToBlockedOrPreparationMs":360.1838,"firstNewGenerationToCleanupDrainedMs":258.9863,"firstPhysicalMutationToFirstNewGenerationMs":206.4579,"presentationToStrictCompletionMs":197.0625}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2584,"dispatchToBlockedOrPreparationMs":391.0439,"firstNewGenerationToCleanupDrainedMs":251.9323,"firstPhysicalMutationToFirstNewGenerationMs":198.9192,"presentationToStrictCompletionMs":184.3616}    |
| 11  | 175.540 / 207.391   | 463.473 / 488.919   | 287.933 / 281.528 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6074,"dispatchToBlockedOrPreparationMs":132.1942,"firstNewGenerationToCleanupDrainedMs":288.7995,"firstPhysicalMutationToFirstNewGenerationMs":38.872,"presentationToStrictCompletionMs":287.9328}     | {"blockedOrPreparationToFirstPhysicalMutationMs":5.4479,"dispatchToBlockedOrPreparationMs":106.8648,"firstNewGenerationToCleanupDrainedMs":282.789,"firstPhysicalMutationToFirstNewGenerationMs":93.8174,"presentationToStrictCompletionMs":281.5277}      |
| 12  | 177.695 / 172.169   | 177.695 / 172.169   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.6954,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.1687,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 614.699 / 601.751   | 614.699 / 601.751   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":53.4671,"dispatchToBlockedOrPreparationMs":423.697,"firstNewGenerationToCleanupDrainedMs":44.7161,"firstPhysicalMutationToFirstNewGenerationMs":92.8189,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":52.0166,"dispatchToBlockedOrPreparationMs":413.9298,"firstNewGenerationToCleanupDrainedMs":43.1241,"firstPhysicalMutationToFirstNewGenerationMs":92.681,"presentationToStrictCompletionMs":0}             |
| 14  | 818.765 / 1263.510  | 866.172 / 1400.361  | 47.408 / 136.851  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9372,"dispatchToBlockedOrPreparationMs":385.2048,"firstNewGenerationToCleanupDrainedMs":189.3994,"firstPhysicalMutationToFirstNewGenerationMs":287.631,"presentationToStrictCompletionMs":96.5605}     | {"blockedOrPreparationToFirstPhysicalMutationMs":292.7758,"dispatchToBlockedOrPreparationMs":461.5449,"firstNewGenerationToCleanupDrainedMs":179.8478,"firstPhysicalMutationToFirstNewGenerationMs":466.1922,"presentationToStrictCompletionMs":183.9888}  |
| 15  | 1340.590 / 774.073  | 1205.543 / 637.659  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":302.0083,"dispatchToBlockedOrPreparationMs":478.9954,"firstNewGenerationToCleanupDrainedMs":44.633,"firstPhysicalMutationToFirstNewGenerationMs":379.9068,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5792,"dispatchToBlockedOrPreparationMs":360.5536,"firstNewGenerationToCleanupDrainedMs":43.9976,"firstPhysicalMutationToFirstNewGenerationMs":228.5281,"presentationToStrictCompletionMs":0}            |
| 16  | 767.369 / 722.817   | 630.822 / 629.605   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2853,"dispatchToBlockedOrPreparationMs":369.3391,"firstNewGenerationToCleanupDrainedMs":44.1843,"firstPhysicalMutationToFirstNewGenerationMs":212.0138,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2062,"dispatchToBlockedOrPreparationMs":381.4671,"firstNewGenerationToCleanupDrainedMs":43.5947,"firstPhysicalMutationToFirstNewGenerationMs":199.337,"presentationToStrictCompletionMs":0}             |
| 17  | 766.529 / 871.898   | 629.701 / 715.794   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8678,"dispatchToBlockedOrPreparationMs":372.615,"firstNewGenerationToCleanupDrainedMs":44.854,"firstPhysicalMutationToFirstNewGenerationMs":207.364,"presentationToStrictCompletionMs":0}              | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2205,"dispatchToBlockedOrPreparationMs":409.551,"firstNewGenerationToCleanupDrainedMs":50.599,"firstPhysicalMutationToFirstNewGenerationMs":250.4234,"presentationToStrictCompletionMs":0}              |
| 18  | 773.062 / 1032.412  | 636.953 / 837.165   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":7.6245,"dispatchToBlockedOrPreparationMs":372.8308,"firstNewGenerationToCleanupDrainedMs":44.4943,"firstPhysicalMutationToFirstNewGenerationMs":212.0034,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":6.3908,"dispatchToBlockedOrPreparationMs":494.6882,"firstNewGenerationToCleanupDrainedMs":53.7296,"firstPhysicalMutationToFirstNewGenerationMs":282.3569,"presentationToStrictCompletionMs":0}            |
| 19  | 785.889 / 947.022   | 654.022 / 779.783   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.5567,"dispatchToBlockedOrPreparationMs":404.0456,"firstNewGenerationToCleanupDrainedMs":44.696,"firstPhysicalMutationToFirstNewGenerationMs":199.7237,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8876,"dispatchToBlockedOrPreparationMs":469.0307,"firstNewGenerationToCleanupDrainedMs":51.0687,"firstPhysicalMutationToFirstNewGenerationMs":253.7963,"presentationToStrictCompletionMs":0}            |
| 20  | 873.784 / 569.075   | 1057.541 / 771.855  | 183.757 / 202.780 | {"blockedOrPreparationToFirstPhysicalMutationMs":285.0914,"dispatchToBlockedOrPreparationMs":409.5131,"firstNewGenerationToCleanupDrainedMs":234.0775,"firstPhysicalMutationToFirstNewGenerationMs":128.8593,"presentationToStrictCompletionMs":183.7569} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5177,"dispatchToBlockedOrPreparationMs":376.173,"firstNewGenerationToCleanupDrainedMs":256.5439,"firstPhysicalMutationToFirstNewGenerationMs":134.6204,"presentationToStrictCompletionMs":202.7804}     |
| 21  | 199.737 / 264.619   | 475.511 / 588.250   | 275.774 / 323.631 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":134.8106,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":275.7737}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.7131,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":323.6308}              |
| 22  | 180.721 / 245.742   | 180.721 / 245.742   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":180.7207,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":245.7421,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 23  | 264.511 / 461.123   | 264.511 / 461.123   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":264.5108,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":461.1234,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 24  | 686.259 / 1957.170  | 871.077 / 2497.771  | 184.818 / 540.601 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6459,"dispatchToBlockedOrPreparationMs":474.8769,"firstNewGenerationToCleanupDrainedMs":232.3723,"firstPhysicalMutationToFirstNewGenerationMs":160.182,"presentationToStrictCompletionMs":184.8184}    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.9913,"dispatchToBlockedOrPreparationMs":1425.7687,"firstNewGenerationToCleanupDrainedMs":660.2786,"firstPhysicalMutationToFirstNewGenerationMs":405.7329,"presentationToStrictCompletionMs":540.6011}   |
| 25  | 789.857 / 3253.700  | 956.748 / 3500.175  | 166.891 / 246.475 | {"blockedOrPreparationToFirstPhysicalMutationMs":29.7825,"dispatchToBlockedOrPreparationMs":369.6542,"firstNewGenerationToCleanupDrainedMs":213.9043,"firstPhysicalMutationToFirstNewGenerationMs":343.407,"presentationToStrictCompletionMs":271.8125}   | {"blockedOrPreparationToFirstPhysicalMutationMs":1339.7583,"dispatchToBlockedOrPreparationMs":899.7312,"firstNewGenerationToCleanupDrainedMs":329.8646,"firstPhysicalMutationToFirstNewGenerationMs":930.8206,"presentationToStrictCompletionMs":419.2939} |
| 26  | 874.631 / 1055.730  | 977.505 / 1213.966  | 102.873 / 158.236 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1474,"dispatchToBlockedOrPreparationMs":418.8753,"firstNewGenerationToCleanupDrainedMs":200.1484,"firstPhysicalMutationToFirstNewGenerationMs":353.3335,"presentationToStrictCompletionMs":164.0034}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5478,"dispatchToBlockedOrPreparationMs":540.0148,"firstNewGenerationToCleanupDrainedMs":259.6154,"firstPhysicalMutationToFirstNewGenerationMs":409.7882,"presentationToStrictCompletionMs":158.2357}    |
| 27  | 519.370 / 663.455   | 759.289 / 878.584   | 239.919 / 215.130 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5625,"dispatchToBlockedOrPreparationMs":361.7554,"firstNewGenerationToCleanupDrainedMs":241.0267,"firstPhysicalMutationToFirstNewGenerationMs":151.9449,"presentationToStrictCompletionMs":239.9193}   | {"blockedOrPreparationToFirstPhysicalMutationMs":6.4703,"dispatchToBlockedOrPreparationMs":429.8494,"firstNewGenerationToCleanupDrainedMs":304.1098,"firstPhysicalMutationToFirstNewGenerationMs":138.1549,"presentationToStrictCompletionMs":215.1296}    |
| 28  | 890.521 / 1642.540  | 997.690 / 1588.359  | 107.168 / 0       | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9094,"dispatchToBlockedOrPreparationMs":453.2262,"firstNewGenerationToCleanupDrainedMs":203.2425,"firstPhysicalMutationToFirstNewGenerationMs":337.3116,"presentationToStrictCompletionMs":165.7904}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8302,"dispatchToBlockedOrPreparationMs":895.1427,"firstNewGenerationToCleanupDrainedMs":232.1489,"firstPhysicalMutationToFirstNewGenerationMs":457.2367,"presentationToStrictCompletionMs":0}           |
| 29  | 1646.392 / 1773.924 | 1786.633 / 1930.956 | 140.240 / 157.032 | {"blockedOrPreparationToFirstPhysicalMutationMs":671.9087,"dispatchToBlockedOrPreparationMs":435.9349,"firstNewGenerationToCleanupDrainedMs":186.4797,"firstPhysicalMutationToFirstNewGenerationMs":492.3093,"presentationToStrictCompletionMs":224.6697} | {"blockedOrPreparationToFirstPhysicalMutationMs":732.2706,"dispatchToBlockedOrPreparationMs":483.413,"firstNewGenerationToCleanupDrainedMs":203.964,"firstPhysicalMutationToFirstNewGenerationMs":511.3084,"presentationToStrictCompletionMs":246.6314}    |
| 30  | 569.420 / 721.052   | 850.703 / 959.854   | 281.283 / 238.802 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4877,"dispatchToBlockedOrPreparationMs":435.4832,"firstNewGenerationToCleanupDrainedMs":282.5012,"firstPhysicalMutationToFirstNewGenerationMs":128.2311,"presentationToStrictCompletionMs":281.2834}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1877,"dispatchToBlockedOrPreparationMs":484.1465,"firstNewGenerationToCleanupDrainedMs":309.0197,"firstPhysicalMutationToFirstNewGenerationMs":161.4999,"presentationToStrictCompletionMs":238.8019}    |
| 31  | 688.570 / 663.880   | 688.570 / 663.880   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":60.0548,"dispatchToBlockedOrPreparationMs":464.6423,"firstNewGenerationToCleanupDrainedMs":50.5027,"firstPhysicalMutationToFirstNewGenerationMs":113.3698,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":68.5485,"dispatchToBlockedOrPreparationMs":447.0338,"firstNewGenerationToCleanupDrainedMs":45.8072,"firstPhysicalMutationToFirstNewGenerationMs":102.4907,"presentationToStrictCompletionMs":0}           |
| 32  | 239.744 / 227.672   | 585.806 / 527.800   | 346.062 / 300.128 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":158.2448,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":346.0616}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.3843,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":300.1278}              |
| 33  | 259.017 / 281.425   | 259.017 / 281.425   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":259.0173,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":281.4253,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 9                   | -1           | 549.591 / 514.381    | -35.210  | 16 / 15           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 4   | 14 / 12                  | -2           | 760.130 / 781.191    | 21.060   | 19 / 17           | -2           |
| 5   | 14 / 12                  | -2           | 775.198 / 759.735    | -15.463  | 19 / 17           | -2           |
| 6   | 18 / 18                  | 0            | 930.254 / 1051.739   | 121.485  | 23 / 23           | 0            |
| 7   | 17 / 16                  | -1           | 920.714 / 986.367    | 65.653   | 22 / 21           | -1           |
| 8   | 17 / 16                  | -1           | 993.937 / 906.570    | -87.367  | 22 / 21           | -1           |
| 9   | 17 / 16                  | -1           | 949.733 / 1132.411   | 182.678  | 22 / 21           | -1           |
| 10  | 10 / 9                   | -1           | 570.263 / 594.221    | 23.958   | 15 / 14           | -1           |
| 11  | 3 / 3                    | 0            | 174.674 / 206.130    | 31.457   | 9 / 9             | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 569.983 / 558.627    | -11.356  | 10 / 10           | 0            |
| 14  | 13 / 23                  | 10           | 676.773 / 1220.513   | 543.740  | 18 / 28           | 10           |
| 15  | 22 / 12                  | -10          | 1160.910 / 593.661   | -567.250 | 26 / 16           | -10          |
| 16  | 12 / 12                  | 0            | 586.638 / 586.010    | -0.628   | 16 / 16           | 0            |
| 17  | 12 / 12                  | 0            | 584.847 / 665.195    | 80.348   | 16 / 16           | 0            |
| 18  | 12 / 12                  | 0            | 592.459 / 783.436    | 190.977  | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 609.326 / 728.715    | 119.389  | 16 / 16           | 0            |
| 20  | 16 / 10                  | -6           | 823.464 / 515.311    | -308.153 | 21 / 15           | -6           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 10 / 11                  | 1            | 638.705 / 1837.493   | 1198.788 | 15 / 16           | 1            |
| 25  | 12 / 29                  | 17           | 742.844 / 3170.310   | 2427.466 | 17 / 34           | 17           |
| 26  | 13 / 13                  | 0            | 777.356 / 954.351    | 176.995  | 18 / 18           | 0            |
| 27  | 10 / 10                  | 0            | 518.263 / 574.475    | 56.212   | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 794.447 / 1356.210   | 561.762  | 16 / 16           | 0            |
| 29  | 29 / 29                  | 0            | 1600.153 / 1726.992  | 126.839  | 34 / 34           | 0            |
| 30  | 10 / 11                  | 1            | 568.202 / 650.834    | 82.632   | 16 / 17           | 1            |
| 31  | 9 / 9                    | 0            | 638.067 / 618.073    | -19.994  | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 229.396 / 232.040  | 2.644    |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 223.886 / 226.017  | 2.130    |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 402.426 / 425.558  | 23.132   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 401.994 / 452.051  | 50.058   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 443.357 / 417.095  | -26.262  |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 413.973 / 500.111  | 86.138   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 2 / 6              | 4     | 115.583 / 304.308  | 188.725  |
| 15  | 1 / 1                | 0     | 13 / 3             | -10   | 682.294 / 162.425  | -519.869 |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 151.037 / 144.516  | -6.521   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 155.286 / 188.803  | 33.517   |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 153.936 / 210.518  | 56.582   |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 147.624 / 189.366  | 41.742   |
| 20  | 1 / 0                | -1    | 5 / 0              | -5    | 347.497 / 0        | -347.497 |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 222.710 / 707.471  | 484.761  |
| 26  | 1 / 1                | 0     | 2 / 2              | 0     | 108.487 / 132.871  | 24.383   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 3                | 0     | 16 / 16            | 0     | 998.794 / 1064.405 | 65.610   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

### Cumulative gates and other health evidence

#### renderscale-tuning-nvidia-20260911T153415138Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":23018,"leftPath":"NativeOriginal","referenceFrame":23405,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

#### renderscale-tuning-nvidia-20260911T153415138Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":27488,"leftPath":"NativeOriginal","referenceFrame":27855,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

#### renderscale-tuning-nvidia-20260911T155459454Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":49214,"leftPath":"NativeOriginal","referenceFrame":49564,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

#### renderscale-tuning-nvidia-20260911T155459454Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":53209,"leftPath":"NativeOriginal","referenceFrame":53574,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

### Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16677.78515625 / 16563.484375 / -114.30078125  | 16966.09765625 / 16755.96484375 / -210.1328125 | -95.832               |
| nvidia | 1    | systemCommitMiB   | 56653.96484375 / 56801.75 / 147.78515625       | 57475.78515625 / 57171.40625 / -304.37890625   | -452.164              |
| nvidia | 1    | dxgiUsageMiB      | 4248.890625 / 3696.67578125 / -552.21484375    | 4181.1640625 / 3520.43359375 / -660.73046875   | -108.516              |
| nvidia | 1    | liveTextures      | 0 / 267 / 267                                  | 0 / 218 / 218                                  | -49                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2454.422275543213 / 2454.422275543213      | 0 / 2280.3062477111816 / 2280.3062477111816    | -174.116              |
| nvidia | 2    | processPrivateMiB | 16961.67578125 / 16603.90625 / -357.76953125   | 17126.33984375 / 16806.390625 / -319.94921875  | 37.820                |
| nvidia | 2    | systemCommitMiB   | 57297.87890625 / 56803.0234375 / -494.85546875 | 57683.69921875 / 57712.359375 / 28.66015625    | 523.516               |
| nvidia | 2    | dxgiUsageMiB      | 3917.17578125 / 3556.42578125 / -360.75        | 3821.44921875 / 3397.3125 / -424.13671875      | -63.387               |
| nvidia | 2    | liveTextures      | 0 / 209 / 209                                  | 0 / 221 / 221                                  | 12                    |
| nvidia | 2    | liveTextureMiB    | 0 / 2270.93111038208 / 2270.93111038208        | 0 / 2308.6085243225098 / 2308.6085243225098    | 37.677                |

### Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2024        | 1979        | -45         |
| cpu/compactPresentationContract/reuses                  | 2000        | 1957        | -43         |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 148         | 149         | 1           |
| cpu/generationResourceValidation/fullValidations        | 567         | 554         | -13         |
| cpu/generationResourceValidation/stableChecks           | 7729        | 7561        | -168        |
| cpu/generationResourceValidation/stableHits             | 7650        | 7482        | -168        |
| cpu/generationResourceValidation/stableMisses           | 79          | 79          | 0           |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 1           | 3           | 2           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 3914        | 3773        | -141        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 3914        | 3773        | -141        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 3872        | 3731        | -141        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 3914        | 3773        | -141        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 3893        | 3752        | -141        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 3880        | 3739        | -141        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 3819        | 3684        | -135        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 95          | 90          | -5          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 3899        | 3758        | -141        |
| cpu/strongStereoPacket/captures                         | 4208        | 4106        | -102        |
| cpu/strongStereoPacket/commitAccepts                    | 4024        | 3937        | -87         |
| cpu/strongStereoPacket/commitRejects                    | 68          | 67          | -1          |
| cpu/strongStereoPacket/commitValidations                | 4092        | 4004        | -88         |
| cpu/strongStereoPacket/cycleReuses                      | 2063        | 2012        | -51         |
| cpu/strongStereoPacket/fastSkips                        | 3620        | 3440        | -180        |
| cpu/strongStereoPacket/invalidations                    | 4471        | 4318        | -153        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 102         | -1          |
| cpu/strongStereoPacket/lifetimeReuses                   | 2042        | 1992        | -50         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.841       | 1.843       | 0.002       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 150.900     | 22.300      | -128.600    |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.119       | 0.120       | 0.001       |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.500       | 1.600       | 0.100       |
| cpu/window/currentFrame                                 | 23405       | 49566       | 26161       |
| cpu/window/elapsedFrames                                | 3914        | 3774        | -140        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 19491       | 45792       | 26301       |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 23407       | 49566       | 26159       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5588993520  | 5421514032  | -167479488  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4405        | 4273        | -132        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11189404800 | 10854103680 | -335301120  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.300       | 0.307       | 0.006       |
| gpu/item5ActiveFSRCopies/activePixels                   | 10522243280 | 10406941600 | -115301680  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 24531964720 | 23529596000 | -1002368720 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 13800       | 13360       | -440        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 1819        | 1763        | -56         |
| gpu/item7EarlyHAM/executedClears                        | 1846        | 1824        | -22         |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1846        | 1824        | -22         |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 3824        | 3746        | -78         |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 3916        | 3774        | -142        |
| gpu/startFrame                                          | 19491       | 45792       | 26301       |
| profiler/available                                      | true        | true        | n/a         |
| profiler/capabilities                                   | 63          | 63          | 0           |
| profiler/capturing                                      | false       | false       | n/a         |
| profiler/enabled                                        | true        | true        | n/a         |
| profiler/frame/acquiredSlots                            | 0           | 0           | 0           |
| profiler/frame/captured                                 | 0           | 0           | 0           |
| profiler/frame/peakAcquiredSlots                        | 0           | 0           | 0           |
| profiler/frame/slotRefusals                             | 0           | 0           | 0           |
| profiler/limits/frameLatency                            | 3           | 3           | 0           |
| profiler/limits/historyCapacity                         | 300         | 300         | 0           |
| profiler/limits/maximumTimers                           | 128         | 128         | 0           |
| profiler/timerCount                                     | 0           | 0           | 0           |
| profiler/totalsMs/cpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/gpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a         | n/a         |
| texture/active                                          | false       | false       | n/a         |
| texture/attachFailures                                  | 0           | 0           | 0           |
| texture/createdCount                                    | 4021        | 3916        | -105        |
| texture/createdEstimatedBytes                           | 38958703920 | 38655572240 | -303131680  |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3754        | 3698        | -56         |
| texture/destroyedEstimatedBytes                         | 36385055628 | 36264497836 | -120557792  |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 906         | 869         | -37         |
| texture/liveTextureRecordCount                          | 267         | 218         | -49         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 56          | 11          | -45         |
| texture/niSourceTextureMatchedEstimatedBytes            | 197996512   | 15423024    | -182573488  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1506        | 1502        | -4          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 267         | 218         | -49         |
| texture/outstandingEstimatedBytes                       | 2573648292  | 2391074404  | -182573888  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 1           | 3           | 2           |
| texture/supported                                       | true        | true        | n/a         |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2125        | 1931        | -194        |
| cpu/compactPresentationContract/reuses                  | 2103        | 1909        | -194        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 153         | 148         | -5          |
| cpu/generationResourceValidation/fullValidations        | 555         | 559         | 4           |
| cpu/generationResourceValidation/stableChecks           | 8078        | 7317        | -761        |
| cpu/generationResourceValidation/stableHits             | 7999        | 7240        | -759        |
| cpu/generationResourceValidation/stableMisses           | 79          | 77          | -2          |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 2           | 4           | 2           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4063        | 3633        | -430        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4063        | 3633        | -430        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4021        | 3591        | -430        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4063        | 3633        | -430        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4042        | 3612        | -430        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4029        | 3599        | -430        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 3976        | 3543        | -433        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 86          | 90          | 4           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4048        | 3618        | -430        |
| cpu/strongStereoPacket/captures                         | 4388        | 4036        | -352        |
| cpu/strongStereoPacket/commitAccepts                    | 4231        | 3844        | -387        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 64          | -1          |
| cpu/strongStereoPacket/commitValidations                | 4296        | 3908        | -388        |
| cpu/strongStereoPacket/cycleReuses                      | 2153        | 1978        | -175        |
| cpu/strongStereoPacket/fastSkips                        | 3738        | 3230        | -508        |
| cpu/strongStereoPacket/invalidations                    | 4606        | 4204        | -402        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 102         | 104         | 2           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2133        | 1954        | -179        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.742       | 1.887       | 0.144       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 82.300      | 26.900      | -55.400     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.114       | 0.123       | 0.009       |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.400       | 0.800       | -0.600      |
| cpu/window/currentFrame                                 | 27855       | 53575       | 25720       |
| cpu/window/elapsedFrames                                | 4062        | 3633        | -429        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 23793       | 49942       | 26149       |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 27856       | 53575       | 25719       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5878276272  | 5053566672  | -824709600  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4633        | 3983        | -650        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11768561280 | 10117457280 | -1651104000 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.303       | 0.320       | 0.017       |
| gpu/item5ActiveFSRCopies/activePixels                   | 11187774560 | 10669136880 | -518637680  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 25695348640 | 22632360720 | -3062987920 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14520       | 13110       | -1410       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 1921        | 1771        | -150        |
| gpu/item7EarlyHAM/executedClears                        | 1954        | 1732        | -222        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1954        | 1732        | -222        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4036        | 3664        | -372        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4063        | 3633        | -430        |
| gpu/startFrame                                          | 23793       | 49942       | 26149       |
| profiler/available                                      | true        | true        | n/a         |
| profiler/capabilities                                   | 63          | 63          | 0           |
| profiler/capturing                                      | false       | false       | n/a         |
| profiler/enabled                                        | true        | true        | n/a         |
| profiler/frame/acquiredSlots                            | 0           | 0           | 0           |
| profiler/frame/captured                                 | 0           | 0           | 0           |
| profiler/frame/peakAcquiredSlots                        | 0           | 0           | 0           |
| profiler/frame/slotRefusals                             | 0           | 0           | 0           |
| profiler/limits/frameLatency                            | 3           | 3           | 0           |
| profiler/limits/historyCapacity                         | 300         | 300         | 0           |
| profiler/limits/maximumTimers                           | 128         | 128         | 0           |
| profiler/timerCount                                     | 0           | 0           | 0           |
| profiler/totalsMs/cpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/gpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a         | n/a         |
| texture/active                                          | false       | false       | n/a         |
| texture/attachFailures                                  | 0           | 0           | 0           |
| texture/createdCount                                    | 3910        | 3926        | 16          |
| texture/createdEstimatedBytes                           | 38640278112 | 38665682656 | 25404544    |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3701        | 3705        | 4           |
| texture/destroyedEstimatedBytes                         | 36259034252 | 36244931164 | -14103088   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 863         | 875         | 12          |
| texture/liveTextureRecordCount                          | 209         | 221         | 12          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 2           | 14          | 12          |
| texture/niSourceTextureMatchedEstimatedBytes            | 5592480     | 45100112    | 39507632    |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1502        | 1503        | 1           |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 209         | 221         | 12          |
| texture/outstandingEstimatedBytes                       | 2381243860  | 2420751492  | 39507632    |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 2           | 4           | 2           |
| texture/supported                                       | true        | true        | n/a         |

</details>

### Context, memory, CPU/GPU and evidence

| Retained context matches | Result |
| ------------------------ | ------ |
| adapter                  | true   |
| scene                    | false  |
| foveation                | true   |
| toolchain                | true   |
| dependencies             | true   |
| shaderCompiler           | true   |

Full start/end memory evidence, CPU/GPU/resource counters, phase timings, retry details, native execution proofs, every gate and raw receipt hash are in comparison.json. comparison.csv contains every paired or missing transition with both original row payloads. Missing samples remain null/n/a. Current and baseline failures are preserved separately; a persistent gate failure is not automatically a newly introduced regression. The fixed stretch-frame cutoff is diagnostic only because settling imposes stretch. Compare its actual frames and duration, together with time to applied relatch and strict completion. Request-to-applied frames begin at the producer request, whereas strict frames/ms begin at qualification dispatch; these intervals must not be conflated.

-   Headset refresh, driver version, power state and full modlist/cache equality require retained proof.
-   One process and ordered repeats do not establish causal or statistically significant gains.
-   Missing profiler samples are unavailable time evidence, never zero cost.
