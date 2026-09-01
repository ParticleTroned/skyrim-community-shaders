# NVIDIA tuning interruption: nvidia-mtu6cpo8

The updated NVIDIA protocol completed its first 33-transition pass, then
stopped at the second baseline. Execution is **INTERRUPTED**, reporting is
**INCOMPLETE**, and all 33 dispatched matrix rows retain render **PASS**.
Task 2 counts are 33 PASS, zero FAIL, and zero INCONCLUSIVE; no aggregate
Task 2 verdict is calculated. All 33 second-pass destinations are NOT RUN.

The run used source `89e3964582730b92c1885eaef8f865e2d1d02ea9` (clean),
Build ID `880b0ef46dd92fc519bc53f577a21d2986b438133952822f350f18d11360169e`,
DLSS profile K, and explicit FSR3 in Dragonsreach. Output was 1512x1680 per
eye, with the 0.3/0.3/0.7 foveation fixture. The deployed DLL matched the
adjacent build manifest in SHA-256 and byte length.

## Second-baseline failure

After the required 10-second cooldown, DLSS Hoshipa activation timed out
at 20001.5645 ms. Memory admission remained deferred, the controller was
WaitingForSafePoint, operation 35 remained active, and physical mutation
had not started. The game advanced 311 frames during the waiter. This is
a qualification timeout, without independent evidence of a game stall.

The runtime reported zero device-loss, OOM, and producer terminal failures.
Vendor-native qualification failures and credible liveness timeouts were
also zero. The scaled second-baseline timeout is recorded separately.

The runner stopped its baseline stress session. Later read-only inspection
found operation zero and DLSS Hoshipa active, with stress, qualification,
CPU, GPU, texture, load-presentation, and DLSS trace owners inactive. The
first-pass cleanup retained successful profiler disable. No apply or
matrix row was replayed, and the game was left running.

## Available measurements

| Measurement                              |               Pass 1 | Pass 2        |
| ---------------------------------------- | -------------------: | ------------- |
| Dispatched and retained matrix rows      |                33/33 | 0/33; NOT RUN |
| Mean strict completion                   |          2569.260 ms | n/a           |
| Worst strict completion                  |         16264.205 ms | n/a           |
| Mean presentation completion             |          2438.510 ms | n/a           |
| Mean / worst presentation-to-strict tail | 130.749 / 280.117 ms | n/a           |
| Producer retry-event deltas              |                   21 | n/a           |
| Runner recovery applies                  |                    0 | 0             |
| Presentation-stretch rows / recovered    |              17 / 17 | n/a           |
| Process-private memory delta             |         +417.285 MiB | n/a           |
| System-commit delta                      |        +3855.988 MiB | n/a           |
| DXGI usage delta                         |        -1684.695 MiB | n/a           |

These are transition latencies, not steady-state GPU render times. Memory
confirmation is `repeat_not_completed`; retention and initialization
predicates cannot be evaluated without the repeat. The freshly reset
texture tracker ended with 272 tracked textures and 2454.551 estimated
MiB. This capture-scoped growth does not establish a leak.

## Evidence and validation

The local evidence root is
`artifacts/renderscale-tuning/nvidia-mtu6cpo8/`. It contains the complete
160-entry append-only journal, startup and baseline receipts, all 33
terminal matrix receipts and their required DLSS trace windows, four
available memory boundaries, cumulative telemetry, and cleanup evidence.

The packaged `tools/renderscale-tuning-finalizer/finalizer.js` completed
with `--variant nvidia --run-id nvidia-mtu6cpo8 --expected-rows 66`, the
exact Build ID above, and the resolved DLL/manifest paths. It produced
`summary.json`, `transitions.csv`, `evidence-values.csv`, `report.md`, and
`receipt-index.json`. The report includes separate provider and native
tables, per-row repeat NOT RUN entries, and the complete memory table.

Optional cumulative operation history and standalone probe reads were
unsupported; the profiler status request lacked clientId. Probe status
remains in render status and separate CPU/GPU captures are retained. The
cumulative event page contains its first 100 events and is partial. These
limitations do not rewrite completed transition classifications.

The comparison ledger received one partial-result column named
`89e3964582730b92c1885eaef8f865e2d1d02ea9__nvidia-mtu6cpo8`.
Post-write CSV validation preserved all existing parsed cells and metric
order, with 358 nonempty rows and columns increasing from 17 to 18.
Repository `git diff --check` passed.

Local feedback: `AUTO-20260909-142710384-ABB8A748`.

## Follow-up diagnosis

The retained runtime log identifies system commit admission as the
specific memory blocker. Epoch 36 needed an estimated 448 MiB target,
projected to require 1792 MiB of additional commit. Its projected total
exceeded the 60137 MiB admission limit. GPU pressure remained Normal.
System commit reached 63118 MiB while Skyrim private memory remained
16928 MiB. At 16:23:15 system commit fell to 58060 MiB, admission
succeeded, and both eyes stabilized at 16:23:15.822. The approximately
1849 MiB process-private increase at recreation supports the allocation
projection. The source of the external/system commit fluctuation is
unidentified; the log does not establish a game allocation leak.

The machine has a 4096 MiB automatically managed page file. A prepared
machine remediation proposes a 16384 MiB initial and 32768 MiB maximum
page file, preserving the existing CSX safety limits. This persistent
Windows change has not been applied and may require a restart.
The exact log excerpt, sizing calculation, and rollback plan are in
`artifacts/renderscale-tuning/nvidia-mtu6cpo8-diagnosis/`. No source
change or repeat validation is claimed, and the assay remains
`INTERRUPTED`.
