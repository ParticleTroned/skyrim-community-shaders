# NR native lifetime isolation, 2026-09-28

The [Task 3A live record](nr-task3a-live-20260928.md) contains two device
removals with different preceding mode sequences. The first failed D3D11
copy observes asynchronous device removal; it does not identify the
responsible GPU command. Neither run proves a DevBench-only defect.

## Source inspection and diagnostic scope

Slot replacement already waits for interop completion before releasing its
native feature. Backend teardown also drains D3D12 work and acknowledges
the D3D11 tail before releasing ownership. Ordinary ROI changes retain
full-eye resource capacity. This inspection does not establish that the
queue ordering, native feature implementation or resource reuse is correct
under every observed sequence.

The diagnostic addition records a bounded 64-entry CPU history of batch,
slot-retirement and backend-retirement operations. It retains resource
identities and serials, dimensions, old/new inference rectangles, history
identity, effective reset, native creation/evaluation outcomes and shared
fence observations. The first observed failure freezes the history across
NR disable/reset; another fault campaign needs a fresh process. Retirement
frame IDs refer to the last renderer request, not the configuration frame.
Opaque identities and fence values serialize as decimal strings. Unknown
completion is null; device removal is explicit. Queue acceptance does not
prove GPU completion or identify the faulting command.

All added native diagnostics, storage, counters and entry points compile
only with `DEVBENCH_BRIDGE_ENABLED`. Recording additionally requires the
existing `captureFrameEvidence` switch. With capture off, the guard does
not initialize its record, inspect fences or copy resource snapshots.
Serialization occurs only when NR status is requested. No GPU resource,
dispatch, flush, wait, barrier or retirement policy is added. The API
description and output schema expose `lifetimeDiagnostics` in NR status.

The user's performance constraint applies to subsequent fixes as well:
keep tooling-only behavior behind the bridge; require comparable evidence
before claiming performance neutrality for changed runtime behavior.
Capture-enabled CPU diagnostics have a cost and are not a performance
baseline. A production build excludes this addition, but this alone does
not qualify the performance of the earlier Task 3A implementation.

## Fresh-process isolation

Use the same Dragonsreach scene and comparable visible characters, DLSS K
Quality with Render Scale on, FOV `0.95`, character selection on and
`legacy_raw`. Record exact producer identity and starting settings first.
Keep mode and multi-ROI fixed within each independent process:

| Lane | NR mode           | Multi-ROI |
| ---- | ----------------- | --------- |
| A1   | A/full resolution | off       |
| A2   | A/full resolution | on        |
| B1   | B/foveated        | off       |
| B2   | B/foveated        | on        |

Separate capture-off controls from capture-on diagnostic repeats. If the
fault tracks capture or bridge activity, reproduce with a bridge-disabled
producer under equivalent settings before assigning the defect to public
rendering. Do not change native lifetime policy solely because a copy
reported device removal. Stop mutations on a fault, preserve the frozen
history and logs, and leave NR disabled in that process.

After a targeted fix survives these controls, repeat A/B, B/C and A/C
transitions, including NoWork and authored-selection recovery. Restore
the initial profile only while the device remains healthy. Complete the
remaining Task 3A/3B visual and performance qualification before Task 3C.

## Validation and package

`pwsh -File ./tools/validate-local.ps1 -OutputDirectory
build/validation/nr-lifetime-diagnostics-20260928-final` passed in 395.60
seconds. The universal SE/AE/VR Release DLL compiled with DevBench ON,
Tracy OFF and automatic deployment OFF. All **208/208 CTests** passed,
with zero failures, skips or missing executables; the shader suite passed
191 assertions. Preset-generator tests, preset verification, diff checks,
DLL manifest verification and source-snapshot consistency passed.

The new regressions exercise bounded chronological retention, first-failure
pinning, capture-disable visibility and exact JSON identities. Actual MSVC
preprocessing of the six affected native units, with includes suppressed,
proves that the new diagnostic identifiers disappear when the bridge is
disabled and remain in the positive control. This is a compilation-boundary
check, not a separately built bridge-disabled DLL or a live cost measurement.

The initial validation had 207/208 passes: one existing source-contract
assertion still required the old API-v9 description, while NR status already
reported API version 10. The assertion now matches v10 and checks the new
diagnostic description/serialization wiring. Its focused rerun and the final
complete run passed. The initial evidence remains under
`build/validation/nr-lifetime-diagnostics-20260928-build/`. The existing
CMake CMP0116 deprecation warning remains in the configure logs.

The compiled producer, before this validation-note update, is:

-   Build ID: `490462394cae63ac3f49cee6d3adca293ed8dfd202b71f66259655896c9030b7`.
-   Source: `7c4effa2749bb8d3cf123818ad6cf06bdc03a7cf`, dirty digest
    `ff66918ffd95f7ab0a7662ad9f70d1058686871f98be724cef7241eef595cf73`.
-   DLL: 31,065,600 bytes, SHA-256
    `48a133756b21b6c92e09e15c39c4adac0d29de72e8a49f0b56f1f7f14cd2ce21`.

The test archive is
`dist/CSX_AIO-main-vr-nr-NRLifetime-DevBench-20260928-490462394cae.7z`,
198,962,927 bytes, SHA-256
`9055ec2e59ada03207d5d283300e5b1e89c83492fd6baf1d0ecc3f80439186a9`.
Archive integrity and all 385 extracted file sizes/hashes match fresh AIO
staging. The archived DLL and manifest match the validated producer. The NR
runtime is included; shader caches are not. All 23 pre-existing distribution
files remain byte-identical, and prior AIO staging is preserved under
`build/validation/nr-lifetime-diagnostics-20260928-package/prior-aio/`.
The adjacent archive receipt and SHA file retain the package identity;
full packaging evidence is in that package evidence directory.

The user reserves installation and game restarts for explicit instruction.
Package preparation did not deploy or restart the game. The quarantined
producer `3f647eeab4cb` in PID `21648` did not qualify this addition. The
following fresh-process test used the user's manually installed archive.

## User-installed A1 live check

On September 28, the user authorized checking the installed DLL in an
already running game. Direct DevBench attached to fresh Skyrim VR PID
`27212`, port `8921`, in `WhiterunDragonsreach`. Its producer matched Build
ID `490462394cae` above. The physical DLL, adjacent manifest and AIO receipt
matched the recorded SHA-256, size and compiled source identity. The exact
selected MO2 profile had one enabled loose provider and no Overwrite or
unmanaged Data shadow. No installation, restart or new build was performed.

Only lane A1 ran: full-resolution NR, character selection on, multi-ROI
off, DLSS K Quality with Render Scale on, FOV `0.95`, and `legacy_raw`.
The user kept a nearby NPC visible after a temporary departure from view.
Capture off/on/off controls remained in this process with its mode fixed;
the captured interval is not an independent fresh-process reproduction.
The following durations are observation windows, not frame-time or cost
measurements. Counters are successful eye evaluations, not stereo frames.

| Capture interval | Seconds | Successful evaluations | Recorder total |
| ---------------- | ------- | ---------------------- | -------------- |
| Off              | 72.203  | 1,228 to 5,864         | 0 to 0         |
| On               | 80.882  | 8,996 to 13,630        | 643 to 2,960   |
| Off again        | 46.074  | 18,238 to 21,418       | 5,061 to 5,061 |

The first interval ended with both eyes reporting `empty_bypass` after the
NPC left view. Those endpoint frames are NoWork, not successful evaluation.
Both eyes evaluated at the captured interval endpoints and the second
capture-off endpoints. Changing ROI bounds retained full-eye capacity.
Across the whole check, including calls between the tabulated windows,
there were **23,198 successful eye evaluations / 11,599 stereo pairs**.
All sampled NR failure, validation-failure, stereo-failure, device-removal,
quarantine and reset-failure counters remained zero.

The recorder retained 64 chronological entries out of 5,061, with 4,997
overwrites, zero diagnostic failures and no frozen first failure. Retained
batch records had successful native evaluation, resource identities and
finite known completion observations. With capture disabled, the complete
history stayed unchanged while another 3,180 eye evaluations succeeded.
Colour input epochs stayed `[1, 1]` across both capture toggles. The offline
audit passed 13 snapshots and three distinct captured ROI transactions
(frames 44,429, 46,821 and 48,922), including role containment, allocation
capacity, guide mapping and private-output commitment. Capture-off status
retains the earlier frozen transaction; it does not create current evidence.

Restoration completed while healthy: NR off with B/foveated selected,
character selection off, multi-ROI off, FOV off with its original `0.3`
centre scale, capture off, and DLSS K Native AA with Render Scale off.
The original FSR runtime preference, colour settings and persisted profile
also matched. Public upscaling operation 2 completed successfully; final
configured/effective/stable profiles and dimensions matched the initial
snapshot. The final health receipt still identified PID `27212`; all
20 reset attempts succeeded and no fault followed restoration.

Evidence is under `build/validation/nr-lifetime-live-20260928-pid27212/`:
raw tool receipts, exact physical DLL verification, `CommunityShaders.log`,
`structural-audit.json`, timestamps and `runtime-outcome.json`. The offline
`audit_live.py` and `finalize_live.py` checks passed. The active interval
started at 10:33:48.220 UTC; final verification ended at 10:43:41.321 UTC.

This is a **passed bounded A1 smoke check**, not complete Task 3A
qualification. Cached mask-coverage zeros did not match current policy and
evaluation, so they cannot establish current coverage or visible NR effect.
No native HMD visual comparison or comparable performance measurement was
made. The hang was not reproduced and its cause remains unresolved. A2,
B1 and B2 need separate fresh processes; cross-mode transitions, bridge-off
reproduction, SE/AE live behavior, visual equivalence and performance
neutrality remain unqualified. No commit or push was performed.

The subsequent [Bannered Mare continuation](nr-task3a-bannered-mare-20260928.md)
passed multi-ROI and all six directed A/B/C transitions in the same healthy
PID, with no reproduced device removal. It adds functional coverage but
does not replace independent fresh-process A2/B1/B2 controls. The earlier
fault cause remains unresolved.
