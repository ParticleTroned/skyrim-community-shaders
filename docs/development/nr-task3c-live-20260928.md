# Task 3C live evaluation, 2026-09-28

The later [Task 3 closeout](nr-task3-completion-20260928.md) records a
tracer-free restarted process, the remaining debug-view check, bounded
timing samples and native stereo sequences. It closes the implementation
milestone while retaining inconclusive quality/cost qualification and the
unresolved cost-scaling problem. The results below belong to PID 22896.

The user-installed Task 3C DevBench build passed the bounded Skyrim VR
functional evaluation in the Bannered Mare. The current-context policy
reached native inference and private-output commitment, preserved C's
per-evaluation reset, and retained the baseline in excluded configurations.
There were 45,069 successful native region evaluations and 22,395 successful
stereo submissions, with zero NR failures, device removals, quarantines,
validation failures, stereo failures or reset failures.

This closes the sampled Task 3C VR functional check. Task 3 implementation
and local validation are complete; full runtime qualification remains open.
Matched image quality and GPU cost are unmeasured. The experiment remains
default-off and DevBench-only. This run does not establish a fix for the
earlier intermittent driver hang or promote tighter contexts to production.

## Producer and fixture

Branch: `main-vr-nr`. The compiled source and dirty digest are unchanged from
the [Task 3C implementation/build record](nr-task3c-current-context-20260928.md):

-   Source: `7c4effa2749bb8d3cf123818ad6cf06bdc03a7cf`.
-   Dirty digest: `bb3ce892f02f620e4410febc3842bb2efa0f99e647d06f955330dcfdc16eff0a`.
-   Build ID: `40689ffbf63b58479b81ee74ef687137fabf3c2c87585307526f4e980bf919f1`.
-   DLL: 31,068,160 bytes; SHA-256
    `31f6ac810d437531a34cec5a67e47e7770d6ab2f08994d13f6d7683ba3a1ff76`.
-   Runtime: Skyrim VR, PID `22896`, DevBench port `8921`, version
    `1.22.0+pt.1.17.0`.

The physical DLL in the enabled
`CSX_AIO-main-vr-nr-Task3C-DevBench-20260928-40689ffbf63b` mod matched its
adjacent manifest, AIO receipt and runtime producer. The selected MO2
profile was `Codex Task - 20260919t055629z-tracy-guardian-main-vr-1bf5803a`.
Enabled loose providers, Overwrite and unmanaged Data were checked for
shadowing. The physical verification receipt is preserved with this run.
The previously authorized universal build passed 211/211 local tests;
those tests were not rerun or represented as live SE/AE evidence here.

The runtime-only fixture used DLSS K Quality with Render Scale enabled,
1008 x 1120 render pixels and 1512 x 1680 display pixels per eye. FOV was
enabled at 0.95 without periphery TAA. Character selection retained the
starting face/skin/hair settings, authored mask, debug-off and raw colour.
C used a full-eye source except for the explicit optional-FOV check.
Frame evidence was enabled for structural samples and disabled for one
separate observation window. No profiler or colour measurement passes
were enabled. Direct MCP was the single control transport.

## Observed behavior

| Check                                      | Evidence and result                                                                                                                                                                                                                                                                                                 |
| ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Default-off baseline                       | Frame 53747 used the retained envelope in both eyes; actual preparation, inference and output descriptors matched.                                                                                                                                                                                                  |
| Current required context                   | Frame 56142 used 832 x 320 = 266,240 pixels per eye inside a 960 x 800 = 768,000-pixel retained envelope. Evaluated area was 65.33% smaller; allocation capacity stayed 1008 x 1120 = 1,128,960 pixels. This is an area observation, not a timing result.                                                           |
| Allocation stability                       | Resource-rebuild counter remained 2 from the baseline through opt-in, empty bypass and recovery. Later mode/multi-ROI transitions raised the counter to 10; these are separate configuration changes.                                                                                                               |
| Spatial context and resets                 | Current support remained contained in inference, inference stayed within retained history and capacity, guide mappings matched, and every audited C region requested effective reset. Full-size current contexts also occurred; opt-in does not imply a smaller rectangle.                                          |
| Empty and recovery                         | Forced-zero frames 57493 and 57724 had two NoWork eyes and no executions. Native evaluations stayed at 7,796 until authored selection resumed. Frame 57876 committed both eyes successfully.                                                                                                                        |
| Multi-ROI exclusion                        | Frame 59383 retained the baseline with multi-ROI enabled. The sampled frame used the single fallback; it is not evidence of two simultaneous regions.                                                                                                                                                               |
| A/B exclusion                              | Frames 59703 and 60538 retained the baseline in full-resolution and foveated modes despite the selected experiment flag. Their effective resets were false.                                                                                                                                                         |
| C FOV choice                               | Frame 60907 applied current context with optional C FOV enabled. Frame 61691 applied it after full-eye C was restored.                                                                                                                                                                                              |
| Capture-off window                         | Between status requests at 12:08:56.872Z and 12:10:27.864Z, native successes increased by 6,426 and stereo successes by 3,213, with no faults or additional resource rebuilds. Retained frozen evidence was stale during this window and was not counted as new captures. These counters do not measure frame time. |
| Return to baseline                         | Frame 68037 committed both eyes using retained context with the experiment disabled. Earlier return snapshots were NoWork because visible eligible geometry changed.                                                                                                                                                |
| Diagnostic-mask exclusion                  | Frames 68176 and 68356 used full-capacity baseline context with `force_half`, despite the selected flag. Temporal envelopes were explicitly unavailable.                                                                                                                                                            |
| Authored recovery and containment fallback | Frame 76033 committed both eyes: one used current context and the other retained the envelope. Each eye's descriptor passed containment and guide checks.                                                                                                                                                           |

The offline audit passed 15 unique committed ROI transactions and four
NoWork transactions across 30 NR snapshots. It reused the maintained
transaction validator and checked preparation/execution descriptor equality,
support containment, capacity, guide mapping, private output, effective
reset, and the captured configuration's policy eligibility. Two stereo
observation PNGs matched their terminal receipt sizes and SHA-256 hashes.
They show the scene but are not a matched quality comparison.

Five unique settings-boundary samples are retained separately. Three
contain successful prior-configuration neural pairs; two, at multi-ROI
enable and final NR disable, report `settings_changed_during_frame` and
normal-DLSS fallback with no committed neural eyes. Their native execution
succeeded and fault counters stayed zero. These are not clean samples of
the newly requested configuration and were excluded from the ROI pass count.

## Restoration and evidence

NR was disabled before restoring the original upscaler profile. Retirement
succeeded. Operation 2 completed with DLSS K NativeAA and Render Scale off;
the original FSR4 runtime preference was preserved. NR, character, FOV and
colour settings and all upscaling profiles compare equal to the initial
snapshots. Frame capture is off; no screenshot acquisitions, sequences,
pending operations or capture jobs remain. The same PID remained loaded
and responsive after restoration. Reset successes reached 13/13, from
2/2 at entry. The experiment is off.

Raw responses, command arguments/timestamps, image receipts, identity and
audits are preserved under
`build/validation/nr-task3c-live-20260928-pid22896/`.
Key files are `call-journal.json`, `physical-dll-identity.json`,
`structural-audit.json`, `restoration-verification.json`,
`final-profile.json`, `final-capture-status.json` and `final-state.json`.
The offline check ran as
`python build/validation/nr-task3c-live-20260928-pid22896/audit_live.py`
and exited 0 with no audit errors. No source implementation was changed,
compiled, installed, committed or pushed during this evaluation.

## Remaining qualification

The installed profiler-control skill requires standalone
`skyrimvrupscaler.temporalProbe` status to establish a neutral physical
probe and ownership epoch before profiling. The direct callable catalog
exposed no such tool. Consequently no GPU cost comparison was started;
unqualified cumulative timing fields were not substituted. Local feedback
was recorded as `AUTO-20260928-121741055-E0450666`; the complete catalog and
receipt are retained. This was the qualification blocker at the time of the
run; the follow-up below corrects its cause. It is independent of the earlier
native driver-hang investigation.

### Profiling guard correction

On the user's subsequent request to fix the blocker, fresh direct
`inspect registrants` evidence showed no standalone-upscaler consumer or
temporal-probe registration. The bundled controller already treats this
case as not applicable; the direct skill instructions omitted that case.
The earlier diagnosis of a missing required callable tool was too broad.

The automation source now has an offline
`Get-DevBenchDirectPerformanceGuard` helper and corrected instructions.
Fresh direct state/registration evidence proves absence; missing callable
metadata alone does not. Registered probes still require their exact typed
status action and neutral physical ownership evidence. Missing, legacy,
inconsistent or changed evidence remains rejected. Direct window validation
also checks process identity, frame direction and the registration ledger.
There is no game DLL, rendering-path or synchronization change. Automation
commit `ec47a8c` was fast-forwarded into local `dev`; its source and packaged
copies match. The installed plugin cache was not rotated. The repository
reserves that refresh for an authorized `main` release and host reload;
the corrected helper was used directly from source for this verification.

The user then explicitly requested the marketplace update and cache rotation.
The guarded local installer registered
`0.9.0+codex.20260928130304`; all 204 source, rebuilt, marketplace and installed
files matched by SHA-256. Existing GameFT additions were preserved. Skyrim
was closed and no automation worker was running. The installer requires a
full Codex/VS Code host reload before another protocol; the current host's
pickup is not inferred from successful installation. Rotation receipts and
the prior package are retained under
`build/validation/automation-marketplace-rotation-20260928/`.

Validation passed 29 focused direct-guard cases, 177 DevBench controller
checks and 47 profiler checks. Both edited skills passed validation, and
all six changed source/package file pairs matched. Applied to fresh
before/after observations from PID 22896, the guard returned a valid,
not-applicable temporal-probe window with unchanged registration identity.
Evidence: `build/validation/nr-task3c-probe-guard-fix-20260928/`.

A separate direct `lifetimetracer.capture` status read reported
`hooksInstalled: true`, active recording and `performanceDistorted: true`.
The session therefore remains unsuitable for a performance-neutral cost
comparison even after fixing probe applicability. Stopping the recorder
leaves its startup-installed hooks in place. No tracer mutation, installation
or restart was performed. A clean cost comparison requires a separately
authorized session without those hooks, plus matched scene/capture evidence.
The previous functional results remain valid within their documented scope.

Task 3C promotion still needs matched native stereo image sequences and
controlled cost evidence. The current NPC view changed during functional
testing; it cannot establish facial detail, hair, temporal stability,
stereo consistency, colour or Lighting preservation equivalence. Live mono
SE/AE, controlled ready/pending/ready and same-source reuse, mixed empty
eyes, and native-failure paths were not qualified by this run. Existing
compiled regressions cover relevant contracts without replacing that
runtime evidence. Debug-view exclusion was not separately exercised live.

Task 3A's call-site integration and Task 3B's preserved jitter/feather/shared
envelope contracts remain in place. Task 3D already carries independent
ownership fields and rejects overlapping provider regions. Overlap,
transport sharing and larger region counts remain later tasks. Full Task 3
runtime qualification and the separate fresh-process hang isolation remain
open; the default-off experiment can remain implemented without promotion.
