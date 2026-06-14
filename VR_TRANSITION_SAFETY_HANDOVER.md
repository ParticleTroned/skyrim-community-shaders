# Codex Handover: VR Render-Scale Transition Safety

Date opened: 2026-06-01
Branch/context: `cs-1.6-PL-VR`

This document tracks the investigation into VR render-scale transition safety, with special focus on the black block/triangle shape seen near the top-left after interior/exterior transitions. It is intended for future Codex sessions after chat compaction, so log evidence, conclusions, code changes, and test outcomes stay in one place.

Do not treat this file as final diagnosis. Append new findings chronologically at the bottom after each new log/test round.

## Current Goal

Find where the top-left block shape comes from and improve transition safety/fidelity without adding unnecessary wait time.

Logging is only an investigation tool, not the purpose of this work. The purpose is to improve transition safety and fix the black square/block artifact. Keep logs focused on decisions that can change the implementation, prove a guard is correct, or show why a previous assumption was wrong.

The target behavior is:

- Render-scale transitions recover as soon as the unsafe D3D/vendor window is actually over.
- DLSS and FSR both handle `DLSS -> FSR` and `FSR -> DLSS` while render-scale mode is involved.
- HAM/HMD mask clear and projected HAM/FOV mask repair do not run while render-scale state is unstable.
- Interior/exterior transitions do not crash, freeze longer than necessary, or leave persistent stutter.
- Save/load behavior is not changed unless a specific later test explicitly targets it.

## Current Test Scope

The next expected log should cover only:

- Exterior Hosh... plus render-scale mode
- Transition into interior DLAA
- API-driven transition path
- VR path active

The first received log actually contains multiple API-driven exterior/interior and interior/exterior transitions. Interpret each leg separately. The user reports no crash in this run, and notes that the black square is especially visible on interior -> exterior transitions.

Additional testing setup now tracked separately:

- CS menu switch tests.
- Purpose: stress render-scale changes made from the Community Shaders menu, including DLSS -> FSR, FSR -> DLSS, render-scale on/off, perf on/off, and vendor runtime teardown/rebuild while no cell transition is happening.
- Key issue under investigation: a requested method can become `kNONE` while the boot-latched runtime vendor method is still active. Safety gates must therefore consider both requested method and runtime active vendor state.

Current timing under test:

- Recent-loading render-scale relatch delay: `3` frames.
- Render-target relatch retry backoff after busy vendor resources, D3D recreate retry, or common texture recreation failure: `60` frames.
- Render-target relatch retry backoff after D3D allocation failure / `8007000E`: `300` frames.
- Door/API render-scale D3D relatch post-close gate: normally `120` frames; scoped to `600` frames only when the last save/load ended in an interior and the current relatch is in an exterior.
- Post-D3D render-scale entry recovery: `10` stretch frames plus `20` stable full-eye frames before FOV/foveated dispatch resumes.
- Post-D3D render-scale exit recovery: `10` frames.
- Save/load handling is not part of this timing trial.
- New logs should go into the `3`-frame recent-loading trial table if the log itself shows recent-loading relatches using `relatchAge=0/3`. The trial now includes relatch-frame CS-upscaled-submit bypass with original OpenVR submit forwarding, plus the `60`-frame busy retry backoff.

## Current Instrumentation Context

The current head contains VR transition diagnostics in `src/Features/Upscaling.cpp`.

The diagnostics are centralized behind:

- `VR_TRANSITION_DIAG_ENABLED`
- `VR_TRANSITION_DIAG_LOG_LEVEL`
- `VR_TRANSITION_DIAG_LOG(...)`

The intent is that the logging can later be moved to debug-only by changing the central log level or disabling the macro path, instead of editing every call site.

The transition logs should expose:

- loading menu open/close timing
- cell transition context
- render-scale relevance
- pending VR/render-scale setting changes
- relatch pending/in-progress/settling state
- DLSS/FSR/vendor reset pending state
- HAM/HMD mask clear guard state
- projected HAM/FOV mask repair guard state
- foveated vendor bypass state
- FSR runtime defer state
- D3D render-target relatch/recreate breadcrumbs

## How To Read The Next Log

Use the log to answer these questions in order:

1. Did the render-scale transition actually become relevant for this test?
2. Did the loading/menu close grace start and end at the expected frames?
3. Was a render-target relatch queued, retried, deferred, or completed?
4. Did DLSS/FSR teardown/recreate happen before, during, or after the unsafe transition window?
5. Did HAM/HMD mask clear or projected HAM/FOV mask repair become allowed before the relatch was stable?
6. Did the black block appear before relatch success, during post-relatch settling, or after all guards ended?
7. Did CPU/GPU stutter continue after the transition guards ended?
8. Was FSR different from DLSS in reset timing, repeated dispatch skips, or pending reset behavior?

## Log Recording Format

Every log interpretation must start with a numbered log record:

- `Log NNN`: running number for the captured `CommunityShaders.log`.
- `File timestamp`: exact `LastWriteTime` of the log file when analyzed.
- `Log time range`: first and last relevant in-log timestamps used for the transition analysis.
- `Build/change context`: code state or relevant patch under test.
- `Summary`: short result summary.
- `Try table`: running try numbers inside that log.

Try numbering rules:

- Use `Try NNN` for each intentional transition attempt in that log.
- Number tries from `001` within each log.
- Do not count boot/setup loads as tries unless the user explicitly identifies them as part of the test.
- Each try row must include exact in-log timestamps for menu close/resource change, relatch completion if present, recovery/FOV resume if present, visible result if known, and conclusion.
- Every submitted log must update the running timing statistics table for the active timing regime, not only the per-log narrative. Add all measured transition periods to the matching per-try timeline, then update `n`, mean/SE, range, and `Max` for each affected marker. The `Max` values are the primary reference for deciding whether safety windows can be shortened, must be lengthened, or should stay unchanged.
- If a timing constant changes, start a new running table instead of adding samples to the older table. Keep older tables as historical baselines.
- If the same `CommunityShaders.log` path is overwritten later, record it as a new `Log NNN` using the new file timestamp.

## Code Change Ledger

Append every later code change here with rationale, evidence, and result. Do not only describe the patch; record why the log justified it.

### 2026-06-01

- Change: none in this document entry.
- Reason: new transition log is not available yet.
- Result: handover structure created only.
- Conclusion: wait for the intended log before changing transition behavior.

### 2026-06-01 - First Transition Log Review

- Change: none.
- Reason: current log answers phase timing, but does not yet pinpoint black-square timing relative to a named recovery phase.
- Result: no code change made.
- Conclusion: use the next visual observation to map the black square to one of the observed phases before changing behavior.

### 2026-06-01 - Phase Narrowing From Same Log

- Change: none.
- Reason: follow-up log review found an unsafe-looking pre-recovery window.
- Result: no code change made yet.
- Conclusion: the strongest log-derived suspect is premature FOV/foveated dispatch re-enable while relatch is still pending/retrying/in progress, not the final FOV resume after stable full-eye recovery.

### 2026-06-01 - Visual Timing Added

- Change: none.
- Reason: user reports the square is very early in the transition, before or during stretch mode, visible for only about one second, then gone.
- Result: no code change made yet.
- Conclusion: this rules against the late 360-stable-frame FOV resume as the primary cause. Focus on pre-recovery relatch and first stretch frames.

### 2026-06-01 - Working Diagnosis

- Change: none.
- Reason: code/log comparison found that `perfModeRenderTargetRecreateInProgress` is not included in the main visual safety guard.
- Result: no code change made yet.
- Conclusion: the square is most likely a stale or mismatched VR projection/mask/foveated output frame escaping during the D3D render-target relatch window, before post-relatch stretch/full-eye recovery takes control.

### 2026-06-01 - First Fix: Guard Relatch-In-Progress

- Change: `src/Features/Upscaling.cpp`
- Implementation: `IsVRRenderScaleRelatchVisualSafetyActive(...)` now treats render-scale-relevant relatch pending and `perfModeRenderTargetRecreateInProgress` as visually unsafe. `ShouldBypassVRFoveatedVendorDispatchForTransition(...)` now uses that visual-safety predicate instead of only the fixed relatch delay.
- Reason: the log showed early one-second square timing and snapshots with `relatchInProgress=yes` while `hmdDefer=no`, `projectedDefer=no`, and `foveatedBypass=no`.
- Result: pending runtime validation.
- Conclusion: this should close the early relatch window without increasing the late `600/360` recovery windows.

### 2026-06-01 - Follow-Up Fix: Bound Foveated Bypass During Relatch Retry

- Change: `src/Features/Upscaling.cpp`
- Implementation: split the relatch guard into two meanings. `IsVRRenderScaleRelatchVisualSafetyActive(...)` still treats pending/in-progress relatch as unsafe for HAM/HMD and projected mask repair. New `IsVRRenderScaleRelatchFoveatedBypassActive(...)` only bypasses foveated vendor dispatch during the bounded safety tail or actual relatch-in-progress state, not for the whole pending retry loop. Transition diagnostics now compute `foveatedBypass` through `ShouldBypassVRFoveatedVendorDispatchForTransition(...)`.
- Reason: `Log 003` showed the first fix could leave DLSS in a retry loop where render-target relatch never completed and performance never settled. Holding full-eye/foveated-bypass for an unbounded pending relatch can keep vendor resources busy and prevent the teardown from becoming idle.
- Result: pending runtime validation.
- Conclusion: keep the mask-repair safety from the first fix, but do not let an old pending relatch force full-eye DLSS/FSR indefinitely.

### 2026-06-01 - Crash Fix: Delay D3D Relatch After Recent Transition

- Change: `src/Features/Upscaling.cpp`
- Implementation: split render-scale relatch timing into two delays. The mandatory transition grace remains `20` frames for guards/vendor scheduling. The actual D3D render-target relatch now uses the `120` frame recent-transition delay after a loading-menu close, while CS menu relatches keep the shorter menu delay. Save/load context is explicitly excluded from the new relatch-delay helper.
- Reason: `Log 004` showed the retry loop was fixed and load settled, but the first transition crashed after D3D relatch completed at close age `49`. The black square was briefly visible while all HAM/FOV/foveated guards were still active, then the log stopped during first stretch presentation texture creation.
- Result: runtime validated by `Log 005` for crash stability. No crash occurred; relatches completed much later after loading close (`249`, `255`, and `270` frames for exterior entries; `276` and `304` frames for interior exits). The black square remained very early, so this fixes the crash path but not the visual artifact.
- Conclusion: keep the `120` frame D3D relatch delay for recent loading transitions. The remaining square is earlier than D3D relatch and belongs to the loading-tail/resource-change handoff, not the post-relatch stretch/FOV recovery.

### 2026-06-01 - Logging Hygiene: Summarize Repeated Deferrals

- Change: `src/Features/Upscaling.cpp`
- Implementation: added repeat categories for noisy transition diagnostics such as vendor reset deferrals, vendor teardown deferrals, relatch deferrals, wait states, and skipped FSR dispatch during reset. The logger now keeps the first full snapshot, then suppresses identical repeat snapshots and emits bounded summary lines with occurrence count, frame span, last state, relatch age, pending reset state, and device-lost flag. Repeated render-target relatch retry warnings are also first-occurrence only; the repeat summaries carry the ongoing retry count/state. Diagnostics now call `ShouldDeferVRTransitionMaskRepair(...)` and `ShouldDeferVRProjectedMaskRepair(...)` directly, so logged `hmdDefer` and `projectedDefer` match runtime behavior instead of duplicating partial guard logic.
- Reason: logs should remain readable while still preserving troubleshooting data. Full per-retry snapshots can hide the actual transition sequence when a resource remains busy for many frames.
- Result: pending runtime validation.
- Conclusion: no transition information is intentionally removed; repeated identical retry snapshots are condensed into first occurrence plus summaries.

### 2026-06-01 - Early Square Fix: Keep Projected Mask Repair Deferred During Foveated Bypass Tail

- Change: `src/Features/Upscaling.cpp`
- Implementation: `ShouldDeferVRProjectedMaskRepair(...)` now also defers while `ShouldBypassVRFoveatedVendorDispatchForTransition(...)` is active.
- Reason: `Log 005` showed no crash after restoring the 120-frame D3D relatch delay, but the black square was still visible very early. The suspicious gap is before D3D relatch: projected HAM/FOV repair exits at closeAge `4` while render-scale is not yet marked relevant, then render-scale becomes relevant at closeAge `15` to `30`. Foveated/full-eye bypass was already active through that gap, so projected mask repair should not be allowed to run there.
- Result: failed by `Log 006` and reverted. The new log kept `projectedDefer=yes` through closeAge `4`, closeAge `30`, and relatch closeAge `249`, but the black square still appeared and the game crashed.
- Conclusion: do not pursue projected HAM/FOV repair as the primary cause without new evidence. The remaining issue is more likely in VR intermediate/presentation texture recreation around render-scale entry and post-relatch stretch.

### 2026-06-01 - Crash Fix: Skip Submit-Stage Presentation Texture Recreate On Relatch Frame

- Change: `src/Features/Upscaling.cpp`
- Implementation: record the frame when `MarkVRRenderScaleRelatchSettling(...)` arms post-relatch recovery. During submit-stage stretch fallback, return to vanilla submit for that same frame instead of creating presentation textures immediately. Add a single diagnostic line when this skip happens. Also remove the failed projected-mask/foveated-bypass extension from `ShouldDeferVRProjectedMaskRepair(...)`.
- Reason: `Log 006` crashed immediately after a successful D3D relatch and vendor recreate, with the final line `13:22:45.778` starting presentation texture recreation on the same frame (`7660`) as the relatch. This matches the older crash signature, now delayed to closeAge `249` instead of closeAge `49`.
- Result: runtime validated for crash stability by `Log 007`. The skip appeared at relatch closeAge `249` and `255` in captured complete entries, and also at closeAge `262` and `268` in repeated captured entries. No crash occurred; presentation textures were created on the next submit-stage call instead.
- Conclusion: keep the one-frame presentation texture skip. It addresses the immediate crash path but does not fix the black-square artifact.

### 2026-06-01 - Rapid-Reversal Fix: Keep Relatch Retry Cadence During Active Recovery

- Change: `src/Features/Upscaling.cpp`
- Implementation: added `GetVRRenderScaleRelatchRetryDelayFrames(...)` and routed `ApplyPendingPerfModeRenderTargetRecreate(...)` retry scheduling through it. When render-scale transition safety is relevant and post-relatch recovery is still settling, vendor-busy relatch retries keep at least the `120` frame recent-transition cadence instead of falling back to the short `6` frame retry.
- Reason: `Log 008` showed that rapid out-and-back cell transitions can overlap the previous `600/360` recovery. Once the recent-close context expires, deferred relatches can retry every `6` frames while `settling=yes`, repeatedly rebuilding DLSS and recreating presentation textures.
- Result: validated by `Log 009` for the specific long-settle mechanism. The post-fix run had zero `relatchAge=*/6` entries while `settling=yes`; repeated overlap retries used `0/120`, and the user reported no long settle or crashes.
- Conclusion: keep this fix. It addresses the rapid-reversal retry collapse, but it does not address the black square, which still appears on interior -> exterior transitions.

### 2026-06-01 - Short Post-D3D Recovery Trial

- Change: `src/Features/Upscaling.cpp`
- Implementation: shortened post-relatch recovery after D3D completion. Render-scale entry now uses `10` frames stretch fallback plus up to `10` stable full-eye DLSS/FSR frames before FOV/foveated dispatch resumes (`20` total frames). Render-scale exit now uses `10` frames instead of the old `180` frame tail.
- Reason: logs show the post-D3D crash was fixed by skipping same-frame presentation texture recreation, and later relatches complete without crashing. The black square appears before the old long recovery completes, so the `600 + 360` frame tail is not proving useful for the remaining artifact.
- Result: runtime validated by `Log 010` for crash/stutter stability. Ten D3D relatches completed, six render-scale entries resumed after `10` stable full-eye frames, and the user reported no crashes during normal or rapid transitions.
- Conclusion: keep pre-D3D relatch timing, the one-frame presentation skip, and the 120-frame rapid-reversal retry cadence unchanged. The short post-D3D recovery appears safe so far, but the black square remains and is therefore not caused by the old long `600 + 360` frame tail.

### 2026-06-01 - Recent-Loading Relatch Delay 60-Frame Trial

- Change: `src/Features/Upscaling.cpp`
- Implementation: changed `kVRRenderScaleRelatchRecentTransitionFrames` from `120` to `60`.
- Reason: `120` frames was stable but conservative. This test checks whether the first post-loading D3D relatch and active-recovery retry cadence can happen earlier while keeping the one-frame presentation skip and the short `10 + 10` post-D3D recovery.
- Result: paused before valid runtime validation. `Log 011` still showed `relatchAge=0/120`, so that crash did not test the 60-frame source change.
- Conclusion: do not add samples to the 60-frame table unless the log itself shows `relatchAge=0/60`. After `Log 011`, stability work takes priority and the source delay was restored to `120`.

### 2026-06-01 - Crash Fix: Suppress OpenVR Submit On Relatch Frame

- Change: `src/Features/Upscaling.cpp`, `src/Features/Upscaling.h`, `src/Features/VR/InSceneOverlay.cpp`
- Implementation: restored `kVRRenderScaleRelatchRecentTransitionFrames` to `120` for controlled crash validation. Added `ShouldSuppressVRCompositorSubmitForRenderScaleRelatchFrame()`. When the one-frame relatch skip is active, the IVRCompositor submit hook now returns `VRCompositorError_None` instead of falling through to submit the just-relatched texture to OpenVR.
- Reason: `Log 011` crashed immediately after D3D relatch completed at closeAge `249` and after the existing `Skipping submit-stage presentation texture recreate` line. That means skipping presentation texture creation alone was not sufficient; the normal OpenVR submit on the same rebuilt render-target frame can still lock/crash the HMD.
- Result: runtime validated by `Log 012`. The new suppress line appeared on completed relatch frames, including after the existing skip line for render-scale entries, and the next frame logged normally with no crash.
- Conclusion: keep relatch-frame OpenVR compositor-submit suppression. It directly addresses the `Log 011` crash shape.

### 2026-06-01 - Resume 60-Frame Relatch Trial With Submit Suppression

- Change: `src/Features/Upscaling.cpp`
- Implementation: changed `kVRRenderScaleRelatchRecentTransitionFrames` back from `120` to `60`.
- Reason: user requested the next test return to the 60-frame timing after adding relatch-frame OpenVR compositor-submit suppression.
- Result: runtime validated by `Log 012` for a first stability pass. Multiple normal and short in/out transitions completed without crashes.
- Conclusion: the 60-frame trial is a successful stability baseline with submit suppression. It is now closed because the next requested test reduces the delay further to 6 frames.

### 2026-06-01 - Recent-Loading Relatch Delay 6-Frame Trial

- Change: `src/Features/Upscaling.cpp`
- Implementation: changed `kVRRenderScaleRelatchRecentTransitionFrames` from `60` to `6`.
- Reason: user requested an aggressive stability trial after the 60-frame build completed multiple normal and short in/out transitions without crashes.
- Result: partially validated by `Log 013`: normal in/out transitions worked, but a rapid repeated in/out stress case crashed after repeated 6-frame relatch retries.
- Conclusion: `6` frames can work for normal transitions, but it is too aggressive as a retry cadence when resources are still busy. Keep the initial 6-frame trial separate from retry safety.

### 2026-06-01 - Back Off Busy Relatch Retries And Defer Common Texture Recreate

- Change: `src/Features/Upscaling.cpp`
- Implementation: added `kVRRenderScaleRelatchBusyRetryFrames = 60`. The initial recent-loading relatch delay remains `6`, but retries after vendor-resource-busy deferral, D3D recreate retry/failure, or render-target relatch exceptions now requeue with at least `60` frames. `CheckResources` now defers missing common vendor texture recreation while render-target relatch or vendor reset state is pending, and catches VR render-scale common-texture recreation failures by requeueing the relatch instead of letting `DX::com_exception` terminate the game.
- Reason: `Log 013` showed the crash after a rapid transition stress loop, not after normal transitions. The captured tail had relatches at frame `12495`, `12501`, and `12507`, followed by D3D recreate failure `HRESULT 8007000E` and then a crash stack inside `Upscaling::CreateUpscalingTextureResources` from `CheckResources`.
- Result: pending runtime validation.
- Conclusion: this fix separates the initial relatch delay from the retry cadence. It was introduced while the first attempt was still 6 frames, and remains required now that the first attempt is being reduced to 3 frames.

### 2026-06-01 - Recent-Loading Relatch Delay 3-Frame Trial

- Change: `src/Features/Upscaling.cpp`
- Implementation: changed `kVRRenderScaleRelatchRecentTransitionFrames` from `6` to `3` while keeping `kVRRenderScaleRelatchBusyRetryFrames = 60`.
- Reason: user wants to reduce the chance of two visible pauses after a door transition. The first relatch attempt should happen as close to the door pause as possible, while any busy/failing relatch retries still back off instead of creating rapid repeated stutters or allocation pressure.
- Result: pending runtime validation.
- Conclusion: this is a new timing trial. Do not mix its samples into the 6-frame table; only logs showing `relatchAge=0/3` belong in the new table.

### 2026-06-01 - Pending-Relatch Submit Fallback And Allocation Cooldown

- Change: `src/Features/Upscaling.cpp`
- Implementation: while a render-scale relatch is pending, submit-stage VR now uses the stretch/presentation fallback instead of running `CheckResources`, submit-stage DLSS/FSR runtime reset, or full vendor intermediate allocation. A render-scale-only cooldown also starts after an `8007000E` submit-stage intermediate allocation failure and keeps submit-stage on the same fallback for up to the `60`-frame busy retry window.
- Reason: `Log 015` shows the `60`-frame backoff engaged, but the submit-stage path kept creating vendor intermediates during that pending window and hit `8007000E` 240 times before the final crash. A later overwritten `CommunityShaders.log` also shows repeated `FSR`/`DLSS` runtime resets while relatch remains pending, which is the same conceptual problem without the intermediate-allocation spam.
- Result: pending runtime validation.
- Conclusion: the retry backoff must also be a submit-stage no-vendor-allocation window. Otherwise the code waits 60 frames for relatch while still doing the allocations/resets that keep vendor resources busy.

### 2026-06-01 - D3D Allocation Failure Backoff

- Change: `src/Features/Upscaling.cpp`
- Implementation: added `kVRRenderScaleRelatchD3DFailureRetryFrames = 300`. If `Hooks::RecreateRenderTargets()` throws a D3D out-of-memory exception during render-target relatch, the relatch is requeued for at least `300` frames instead of the normal `60`-frame busy retry. Also fixed pending-relatch fallback diagnostics so the same frame/eye pair does not log repeatedly.
- Reason: `Log 017` shows the new pending-relatch submit-stage fallback was active, so the remaining crash was not the older submit-stage rebuild spam. The final hard-stress crash followed multiple handled D3D `8007000E` render-target recreate failures, then crashed on a later D3D recreate attempt before the failure could be logged.
- Result: pending runtime validation.
- Conclusion: normal transitions should still use the fast `3`-frame first attempt and `60`-frame busy retry, but a real D3D allocation failure means the render-target allocator is under heavier pressure and needs a longer cool-down before retrying.

### 2026-06-01 - CS Menu Switch Validation

- Change: none.
- Reason: `Log 018` validates the CS menu switch path that previously got FSR stuck with `req=kNONE runtime=kFSR renderScaleRelevant=no relatchPending=yes`.
- Result: no crash, no `8007000E`, no submit device-lost marker, and no stuck render-target relatch. DLSS and FSR both completed render-scale on/off relatches and recovered.
- Conclusion: the runtime-aware render-scale relevance fix is working for the CS menu switch class. Long vendor-busy periods can still happen, especially with FSR, but the fallback path preserves stability and lets the relatch complete.

### 2026-06-02 - Render-Scale Exit Recovery Vendor-Evaluate Bypass

- Change: `src/Features/Upscaling.cpp`
- Implementation: added `ShouldBypassVRRenderScaleRelatchVendorEvaluation(...)` and an early return in `Upscale()` before DLSS/FSR color evaluation. The bypass is active only in VR when render scale is no longer requested, perf mode is no longer active, and the relatch/recovery submit guard is active. It logs `Skipping VR vendor color upscale during render-scale exit recovery guard`.
- Reason: `Log 034` showed the handoff bypass working (`inputHandoff=no`), but after render-scale-off relatch completed with `renderScaleRequested=no` and `perfActive=no`, normal VR DLSS still created full-size per-eye intermediates and called `slEvaluateFeature` during the first recovery/stretch frame. That moved the crash from OpenVR submit handoff to immediate post-exit DLSS evaluation.
- Result: pending runtime validation. `git diff --check` is clean; no build run.
- Conclusion: render-scale exit recovery should be a short vanilla/guarded window before normal DLSS/FSR resumes. Same-resolution quality/preset changes with render scale off remain unrestricted and do not use this guard.

## Failure Ledger: Do Not Repeat Without New Evidence

- Do not blindly extend transition waits as the first response to every artifact. The goal is to use the log to place the rebuild after the dangerous period, not to hide uncertainty behind a longer fixed delay.
- Do not touch save/load handling for this investigation unless the test explicitly changes from interior/exterior transition testing to save/load testing.
- Do not let HAM/FOV repair run only because the fixed grace expired if relatch or vendor reset state still shows unstable render-scale resources.
- Do not conclude that shader compile or asset load noise is the root cause unless the transition log shows the unsafe render-scale state had already fully settled.
- Do not mix old logs into the new test conclusion unless explicitly labeled as historical context.
- Do not treat exterior -> interior and interior -> exterior as equivalent. In the current log, the black-square-prone direction is reported as interior -> exterior, which corresponds to entering render-scale mode and uses the long recovery path.
- Do not only inspect the post-relatch `stretch -> full-eye -> FOV` phases. The log shows an earlier window before post-relatch recovery is armed, where `foveatedBypass` can be off while relatch state is not stable.
- Do not use a single broad "visual safety" predicate for every guard. HAM/FOV mask repair can stay blocked while relatch is pending, but foveated vendor bypass must remain bounded or it can hold DLSS/FSR in a slow path and prevent relatch teardown from ever becoming idle.
- Do not keep blaming HAM/FOV repair after `Log 004` without new evidence. The black square appeared while `hmdDefer=yes`, `projectedDefer=yes`, and `foveatedBypass=yes`; the active suspicious path was first post-relatch stretch/presentation.
- Do not restore per-frame retry snapshot spam unless a specific missing data point is identified. Prefer first occurrence plus repeat summaries so crash-relevant breadcrumbs stay visible.
- Do not assume the early square is caused by the delayed D3D relatch after `Log 005`. The relatch now completes much later and no crash occurred; the remaining visual artifact appears before relatch, during the loading-tail/resource-change handoff.
- Do not keep extending projected HAM/FOV repair deferral after `Log 006` without new evidence. The square and crash remained while `projectedDefer=yes` stayed active through the full suspect window.
- Do not interpret the black square as the whole returned world view being stale or disappearing. Latest user correction says the real world view stays and only the square disappears immediately.
- Do not mix rapid back-and-forth door stress cases into the clean transition averages. Track them separately, because their relatches overlap an existing recovery window and answer a different question.
- Do not allow render-target relatch retries to collapse to the short 6-frame retry cadence while a previous render-scale recovery is still active. `Log 008` shows that this can create repeated DLSS reset/intermediate/presentation rebuild loops.
- Do not use the rapid-reversal retry fix as an explanation for the black square after `Log 009`. The long-settle mechanism improved, but the square remained.
- Do not use the old long post-D3D recovery as an explanation for the black square after `Log 010`. Shortening recovery to `10 + 10` preserved stability, but the square still appeared on outside/Hoshipa entry.
- Do not count a log as part of the 60-frame trial unless the log itself shows `relatchAge=0/60`. `Log 011` still showed `0/120`.
- Do not assume the one-frame submit-stage presentation texture skip fully protects the relatch frame. `Log 011` reached the skip line and still crashed before the next frame.
- Do not allow failed/busy relatch retries to keep using the short initial recent-loading delay. `Log 013` and `Log 014` show repeated attempts every 6 frames can reach D3D/common texture recreation while resources are still unstable. The same rule applies to the newer 3-frame first-attempt trial.
- Do not allow missing common vendor texture recreation from `CheckResources` to run while a VR render-scale relatch/reset is still pending. `Log 013` crashed in `CreateUpscalingTextureResources` after the relatch path had already reported `HRESULT 8007000E`.
- Do not ignore repeated submit-stage intermediate allocation failures before the final crash. `Log 014` shows 48 `8007000E` intermediate creation failures before the same `CheckResources -> CreateUpscalingTextureResources` crash. This supports backing off the relatch/reset loop instead of repeatedly trying more allocations while memory/RT state is already stressed.
- Do not treat a 60-frame relatch retry as safe if submit-stage still performs full vendor reset/rebuild or intermediate allocation during the pending window. `Log 015` shows the backoff was active, but allocation pressure continued and the eventual visible crash moved to `SubsurfaceScattering::EnsureBlurHorizontalTemp`.
- Do not treat pending-relatch submit-stage fallback as the complete stability fix after `Log 017`. It prevented the old submit-stage rebuild spam, but repeated D3D `8007000E` render-target recreate failures still need a longer allocation-failure backoff.
- Do not reclassify the `Log 018` CS-menu FSR delays as stuck relatch. The longest FSR waits eventually completed and ended with `relatchPending=no`, no device-lost marker, and no crash.
- Do not assume the relatch/recovery submit guard is effective if `original-submit-relatch-guard` still shows `inputHandoff=yes`. `Log 033` shows OpenComposite can fail inside swapchain creation while the guard forwards a CS handoff texture.
- Do not assume `inputHandoff=no` proves the render-scale exit frame is safe. `Log 034` shows SteamVR can still crash after handoff bypass succeeds because normal VR DLSS evaluates on the first render-scale-off recovery frame.
- Do not treat closeAge `120` as universally safe for D3D relatch after `Log 039`. It worked as a general improvement after `Log 038`, but an interior-loaded save followed by first exterior entry still crashed when D3D started at closeAge `132` and overlapped exterior terrain/resource initialization.
- Do not treat the scoped `300`-frame D3D gate as a visual artifact fix. `Log 040` shows it can prevent the crash, but HAM/HMD clear and projected mask repair still resume immediately after the short `10` stable full-eye threshold and the square/HAM artifact remains visible.
- Do not treat the scoped `300`-frame D3D gate as universally stable. `Log 041` shows a tester still crashed after D3D completed at closeAge `312`; first-interior-load exterior relatch now needs a more conservative `600`-frame test.
- Do not keep treating the remaining black square as HAM/FOV after `Log 044` without new evidence. The HAM-like transparent mask is gone, while the square remains; the strongest matching signal is original-submit fallback while a render-scale profile is pending but the relatch has not applied yet.
- Do not use the verbose Info submit-diagnostic build for final CPU frametime judgement. `Log 044` is `52 MB` for one short test and logs per-eye submit state heavily enough that CPU ms can be distorted by the diagnostic tool itself.
- Do not fall through to `original-submit-fallback` after submit-stage D3D device removal while render-scale/perf mode is still active. `Log 046` shows that path handed OpenComposite a low-resolution render-scale texture after DLSS sharpening removed the device, then crashed in `vrclient_x64.dll` / OpenComposite swapchain handling.

## Success Ledger

Append successes only when the log explains why the outcome is meaningful.

### 2026-06-01 - No-Crash Transition Run

- Success: multiple API-driven transitions completed without a crash.
- Why this matters: the current teardown/relatch sequencing did not device-lost or hard-crash across repeated direction changes.
- Success: DLSS teardown/rebuild deferrals resolved instead of hanging indefinitely.
- Why this matters: Streamline resource-in-use deferrals appeared during relatch attempts, but later retries completed and vendor resources were recreated.
- Success: transitions into render-scale mode completed the intended staged recovery: stretch fallback, full-eye vendor recovery, then foveated/FOV dispatch.
- Why this matters: the observed GPU-ms pattern has a matching implementation/log explanation instead of indicating an unknown hidden state.

### 2026-06-01 - Relatch-Frame Presentation Skip Prevents Crash

- Success: one-frame submit-stage presentation texture skip on the relatch frame avoided the previous immediate post-relatch crash.
- Why this matters: `Log 007` captured successful recovery after skips at closeAge `249` and `255` in complete samples, plus partial captured skip sightings at closeAge `262` and `268`. Presentation textures were created on the following submit-stage call instead of the relatch frame.

### 2026-06-01 - Pre-Fix Rapid-Reversal Repeat Survived

- Success: the `Log 008` repeat stress run, still before the rapid-reversal retry-cadence fix, survived multiple immediate in/out transitions without a crash.
- Why this matters: the rapid-reversal crash is not deterministic on every repeat. The same old build can survive, but the black square remains visible and the log still shows the short 6-frame retry loop that explains long recovery/stutter.

### 2026-06-01 - Post-Fix Rapid-Reversal Cadence Holds

- Success: `Log 009` shows no crash and no user-visible long settle after the rapid-reversal retry-cadence fix.
- Why this matters: the exact old failure signature is absent. The log has `0` `relatchAge=*/6` entries while `settling=yes`, and repeated overlapping relatch attempts stay on the `120` frame retry cadence.

### 2026-06-01 - Short Post-D3D Recovery Holds

- Success: `Log 010` shows no crash with post-D3D recovery shortened to `10` stretch frames plus `10` stable full-eye frames on render-scale entry.
- Why this matters: the former `600 + 360` frame tail is not required for the crash path fixed so far. The remaining black square persists, so it should be investigated before or at first returned-world presentation, not in the long post-D3D tail.

### 2026-06-01 - CS Menu FSR Switches Complete

- Success: `Log 018` shows CS-menu DLSS and FSR render-scale switches completing without crash or persistent pending relatch.
- Why this matters: this directly validates the earlier FSR stuck-relatch fix. The old failing signature `req=kNONE runtime=kFSR renderScaleRelevant=no relatchPending=yes` is absent; FSR render-scale exit remains relevant and finishes.

### 2026-06-03 - Scoped Interior-Load Exterior Gate Holds

- Success: `Log 040` validates the scoped first-exterior-after-interior-load `300`-frame D3D gate for one SteamVR run.
- Why this matters: the same transition family crashed when D3D started around closeAge `132` in `Log 039`; with the scoped `300` gate, D3D started at closeAge `312`, completed, and consumed the special guard without making later exterior relatches use `300`.

### 2026-06-03 - Clean-Latch Relatches Complete Without HAM Mask

- Success: `Log 044` completed `14` relatches without a crash marker and without render-scale HAM/HMD/projected/foveated guard activation outside normal save/load safe-mode.
- Why this matters: the remaining visual problem has narrowed from "black square plus HAM mask" to "brief black square only", so future fixes should target the pending profile/original-submit/fade boundary rather than reintroducing broad HAM repair deferrals.

## Open Hypotheses

These are hypotheses to check against the next log, not conclusions.

- The top-left block appears when HAM/HMD mask clear or projected HAM/FOV repair resumes while render-scale relatch/vendor resources are still unstable.
- DLSS and FSR may differ because DLSS exposes clearer teardown/resource-in-use signals, while FSR mostly tears down/recreates directly.
- Persistent stutter after switching into FSR may indicate repeated runtime reset deferral, repeated full-frame dispatch skip, or an incomplete transition out of pending render-scale state.
- A visually normal world image can return before vendor/runtime resources are fully stable, so visual recovery alone is not enough to release all guards.

## Testing Guidance

- Preserve the log immediately after the transition test.
- Note the visible timing of the black block: before the new cell appears, immediately after image recovery, during the first movement, or after movement continues.
- Note whether the stutter settles or accumulates.
- Keep the first log narrowly scoped to the exterior render-scale to interior DLAA transition.
- Do not combine unrelated mode switches, save/load, or multiple tests in one first evidence log unless clearly separated by restart.

## Append-Only Log Interpretations

Add new interpretations below this line, newest at the bottom. Each entry should include the test setup, observed transition sequence, black block timing, code changes made, and conclusion.

### Log 001 - 2026-06-01 12:13:06 Capture

File: `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`

File timestamp: `2026-06-01 12:13:06` local time.

Log time range used: `12:11:24.233` through `12:13:06.625`.

Build/change context: pre-first-fix transition diagnostics; before `perfModeRenderTargetRecreateInProgress` was included in relatch visual safety.

Summary:

- Multiple API-driven exterior/interior and interior/exterior render-scale transitions were captured.
- No crash was reported.
- Interior -> exterior / entering render-scale mode used the long recovery path.
- Corrected user visual report: after the typical full-HMD black transition, the real world view and the black square pop in at the same time. The world view stays; only the square disappears immediately. Treat this as a transient subregion/overlay/presentation artifact on the first returned world frame, not as the whole returned world view being stale or disappearing.
- Log-derived diagnosis: the likely unsafe window is before post-relatch recovery is armed, when relatch is pending/retrying/in progress.

Try table:

| Try | Direction / Mode | Key timestamps | Result / conclusion |
| --- | --- | --- | --- |
| Try 001 | Render-scale -> DLAA/full-size | Resource change `12:11:24.233`; queued relatch `12:11:24.289`; relatch complete `12:11:25.956`; recovery exit `12:11:28.829` | Short 180-frame recovery. No crash. Not the strongest black-square direction. |
| Try 002 | DLAA/full-size -> render-scale | Menu opened `12:11:34.086`; menu closed `12:11:41.788`; queued relatch `12:11:42.699`; relatch complete `12:11:44.549`; stretch ends `12:11:50.010`; FOV resumes `12:11:56.059` | Long `600 + 360` recovery. Matches the direction where the square is most visible. |
| Try 003 | Render-scale -> DLAA/full-size | Menu opened `12:11:58.440`; menu closed `12:11:59.100`; resource change `12:11:59.748`; queued relatch `12:11:59.836`; relatch complete `12:12:02.099`; recovery exit `12:12:04.963` | Short recovery. No crash. |
| Try 004 | DLAA/full-size -> render-scale | Menu opened `12:12:10.545`; menu closed `12:12:11.163`; queued relatch `12:12:11.825`; relatch complete `12:12:14.100`; stretch ends `12:12:19.823`; FOV resumes `12:12:25.879` | Long recovery. Same artifact-prone direction. |
| Try 005 | Render-scale -> DLAA/full-size | Menu opened `12:12:36.744`; menu closed `12:12:36.900`; resource change `12:12:37.766`; queued relatch `12:12:37.850`; relatch complete `12:12:40.139`; recovery exit `12:12:43.285` | Short recovery. No crash. |
| Try 006 | DLAA/full-size -> render-scale | Menu opened `12:12:50.933`; menu closed `12:12:51.555`; resource change `12:12:52.272`; queued relatch `12:12:52.336`; relatch complete `12:12:54.776`; stretch ends `12:13:00.566`; FOV resumes `12:13:06.625` | Long recovery. Same artifact-prone direction. |

Setup events not counted as tries:

- Initial render-scale world/menu close around `12:10:48.549`.
- Save/load-adjacent event around `12:11:17.879` -> `12:11:22.247`.

Code changes made from this log:

- First fix implemented later in `src/Features/Upscaling.cpp`: relatch-in-progress is now part of render-scale visual safety, and foveated/FOV dispatch consumes the shared visual-safety predicate.

Conclusion:

- The black square should be validated against `Try 002`, `Try 004`, and `Try 006` style transitions.
- For the next log, verify that `relatchInProgress=yes` no longer coincides with visual guards being off.

### Log 002 - 2026-06-01 12:29:33 Capture

File: `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`

File timestamp: `2026-06-01 12:29:33` local time.

Log time range currently inspected: `12:29:07.570` through `12:29:24.573`.

Build/change context: current on-disk log after Log 001 was overwritten. Runtime result/visual report has not been provided yet.

Summary:

- Treat this as a pending capture until paired with a user visual result.
- Do not use it as final evidence for the first fix until the user reports whether the square changed.

Try table:

| Try | Direction / Mode | Key timestamps | Result / conclusion |
| --- | --- | --- | --- |
| Try 001 | Render-scale -> DLAA/full-size candidate | Resource change `12:29:23.240`; queued relatch `12:29:23.293`; repeated relatch retries from `12:29:23.539` onward | Pending visual result. Not interpreted as success/failure yet. |

Setup events not counted as tries:

- Initial loading/menu close around `12:29:07.570`.
- Save/load-adjacent event around `12:29:17.776` -> `12:29:22.141`.

Code changes made from this log: none.

Conclusion: pending visual result and full interpretation.

### 2026-06-01 - Setup Only

Test setup: pending new log.

Observed transition sequence: not analyzed.

Black block timing: unknown.

Code changes made from this entry: none.

Conclusion: document created from the existing handover style. Await the new `CommunityShaders.log` before interpreting transition behavior.

### 2026-06-01 - API Transition Log, Multiple Directions

Test setup: API-driven transitions between exterior Hosh... with render-scale mode and interior DLAA/full-size mode. User reports no crashes. User also reports the black square is especially visible on interior -> exterior transitions.

Observed transition sequence:

- Boot/current render-scale state starts around `4194x2329` internal render size for `kDLSS` render-scale mode.
- Full-size/DLAA interior state appears as `4936x2740`.
- Exterior/render-scale -> interior/DLAA legs recreate toward `4936x2740` and arm short recovery: `stretch fallback 180 frames, foveated bypass up to 180 frames`.
- Interior/DLAA -> exterior/render-scale legs recreate toward `4194x2329` and arm long recovery: `stretch fallback 600 frames, foveated bypass up to 1200 frames or 360 stable full-eye frames`.
- On the long recovery legs, the log shows three visible performance phases:
  - Phase 1: `stretch=yes`, `foveatedBypass=yes` for 600 frames. This should be cheap stretch fallback and matches the user's initial low GPU-ms observation.
  - Phase 2: `stretch=no`, `settling=yes`, `foveatedBypass=yes` for 360 stable full-eye vendor frames. This should be full-eye DLSS without FOV/foveated dispatch and matches the higher GPU-ms observation.
  - Phase 3: `settling=no`, `foveatedBypass=no` after the stable-frame threshold. FOV/foveated dispatch can resume and matches the later lower GPU-ms observation around 10 seconds after transition.
- Examples of the long path:
  - `12:11:44.549` relatch to `4194x2329`, stretch ends at `12:11:50.010`, FOV resumes at `12:11:56.059`.
  - `12:12:14.100` relatch to `4194x2329`, stretch ends at `12:12:19.823`, FOV resumes at `12:12:25.879`.
  - `12:12:54.776` relatch to `4194x2329`, stretch ends at `12:13:00.566`, FOV resumes at `12:13:06.625`.
- Examples of the short path:
  - `12:11:25.956` relatch to `4936x2740`, recovery exits at `12:11:28.829`.
  - `12:12:02.099` relatch to `4936x2740`, recovery exits at `12:12:04.963`.
  - `12:12:40.139` relatch to `4936x2740`, recovery exits at `12:12:43.285`.

Black block timing:

- The log cannot contain the human-visible square directly, but it can rule in/out active paths.
- Important distinction: the user reports it is strongest on interior -> exterior transitions, which are the long `600 + 360` render-scale entry path.
- The first guess was to map the square to post-relatch Phase 1/2/3. A closer pass shows a more important pre-recovery gap:
  - On some interior -> exterior legs, `FOV foveated vendor bypass exited` before the successful D3D relatch.
  - The snapshots then show `foveatedBypass=no` while `relatchPending=yes` or `relatchInProgress=yes`.
  - At the same time, HAM/HMD and projected HAM/FOV guards can still be active (`hmdDefer=yes`, `projectedDefer=yes`), so the gap is specifically FOV/foveated vendor dispatch, not necessarily HAM repair.
- Example: around `12:12:52.336` -> `12:12:54.776`, relatch is queued/retried for the interior -> exterior render-scale entry. `foveatedBypass` exits at `12:12:53.059`, the first relatch is deferred because vendor resources are still in use, and recovery is not armed until `12:12:54.776`.
- Therefore the current strongest log-derived suspect is not the final FOV resume after 360 stable full-eye frames. It is earlier: FOV/foveated dispatch can become active during the relatch retry/in-progress window before stretch/full-eye recovery has started.
- If the square only appears exactly after `VR render-scale post-relatch recovery observed 360 stable full-eye vendor frames`, then revisit final FOV resume. The current log makes that less likely than the pre-recovery gap.

Code changes made from this entry: none.

Conclusion:

- The user's GPU-ms observation is consistent with the intended staged recovery: stretch fallback -> full-eye vendor -> FOV/foveated dispatch.
- The black-square investigation should now focus on interior -> exterior / entering render-scale mode, especially mapping the visible block to Phase 1, Phase 2, or Phase 3.
- A useful next implementation candidate, if visual timing supports it, is to make the recovery phase names explicit in the log and/or keep the mask-repair guard logically active during `perfModeRenderTargetRecreateInProgress` as well as pending/settling. Do not change this yet without correlating the visible block timing.

### 2026-06-01 - Follow-Up: Phase Narrowing From Log Alone

Test setup: same multi-transition API-driven log.

Observed transition sequence:

- The long post-relatch phases are real and explain the GPU-ms profile.
- However, the log also shows a pre-recovery window before post-relatch recovery is armed.
- In this window, `foveatedBypass` can be `no` while relatch is still pending/retrying/in progress.
- HAM/HMD and projected HAM/FOV guards are generally still held during pending relatch retry, so this is narrower than the earlier HAM/FOV repair hypothesis.

Black block timing:

- The log cannot prove the exact visible frame, but it does identify the most suspicious active path.
- The likely problematic window is before stretch/full-eye recovery, when FOV/foveated dispatch has been allowed again but the render-target relatch has not successfully completed.
- This is more consistent with the black square being tied to FOV/foveated transition handling than with the final FOV resume after stable full-eye recovery.

Code changes made from this entry: none.

Conclusion:

- Do not first change stretch fallback or the 360 stable full-eye threshold.
- First implementation candidate should be narrow: keep FOV/foveated vendor dispatch bypassed while render-scale relatch is pending, retrying, or in progress, but avoid holding full-eye mode indefinitely after relatch has actually settled.
- This should be scoped to render-scale transitions only.

### 2026-06-01 - Visual Timing: Early One-Second Square

Test setup: same multi-transition API-driven log plus user visual observation.

Observed visual timing:

- The square appears very early in the transition.
- It is before stretch mode or during the first stretch-mode frames.
- It lasts about one second, then disappears.
- It is especially visible on interior -> exterior transitions.

Interpretation:

- This rules out Phase 3/final FOV resume as the main source, because FOV resumes much later after 360 stable full-eye frames.
- It also makes the long full-eye recovery phase less likely, because the square is gone before the full-eye-only phase dominates.
- The best match is the pre-recovery relatch window and/or the first stretch fallback frames.
- The log shows that on some interior -> exterior legs the D3D render-target recreate itself takes roughly one second, and `foveatedBypass` can be off before post-relatch recovery is armed.
- Therefore the highest-value fix candidate is to make the transition enter its visual-safe presentation state before relatch/D3D recreate can expose a frame, not only after relatch succeeds.

Code changes made from this entry: none.

Conclusion:

- The next code change should target the early window only. Do not extend the late recovery windows first.
- Candidate: keep FOV/foveated vendor dispatch bypassed while render-scale relatch is pending/retrying/in progress, and consider arming the stretch-safe visual state before calling D3D recreate so the first visible post-recreate frames cannot use stale/foveated projection state.

### 2026-06-01 - Working Diagnosis And Fix Direction

Test setup: same multi-transition API-driven log plus code review.

Observed code/log mismatch:

- The log shows `applying render-target relatch ... relatchInProgress=yes ... hmdDefer=no projectedDefer=no foveatedBypass=no` during several relatch attempts.
- `IsVRRenderScaleRelatchVisualSafetyActive(...)` currently checks pending relatch delay/pending relatch, but not `perfModeRenderTargetRecreateInProgress`.
- `ShouldBypassVRFoveatedVendorDispatchForTransition(...)` checks only delay safety and settling, not the full visual-safety predicate and not `perfModeRenderTargetRecreateInProgress`.
- After relatch succeeds, `MarkVRRenderScaleRelatchSettling(...)` correctly arms stretch and FOV bypass. The artifact is earlier than that, so the late recovery logic is not the first fix target.

Working diagnosis:

- The black square is probably not "stretch mode" itself and not the final FOV resume.
- It is most likely one or more frames where stale/mismatched VR projection, HAM/FOV mask, or foveated vendor output is allowed during the D3D render-target relatch window.
- The one-second duration matches the render-target recreate/resource reload window seen in the log.

Fix direction:

- Treat `perfModeRenderTargetRecreateInProgress` as visually unsafe when render-scale transition safety is relevant.
- Make `ShouldBypassVRFoveatedVendorDispatchForTransition(...)` use that same visual-safety predicate, so foveated/FOV dispatch cannot resume during pending/retrying/in-progress relatch.
- Keep HAM/HMD mask clear and projected HAM/FOV repair deferred during relatch-in-progress, not only pending/settling.
- Do not increase the 600/360 late recovery windows for this issue yet. The evidence points earlier.
- Scope this only to VR render-scale-relevant transitions.

Expected effect:

- No added fixed wait after the transition has settled.
- The unsafe early relatch frames should use the conservative presentation/full-eye path instead of FOV/foveated or HAM/FOV repair paths.
- If the square remains after this, the next suspect is the first stretch frame's presentation texture contents rather than foveated/HAM gating.

### 2026-06-01 - First Fix Implemented

Test setup: no new runtime test yet.

Code changes made from this entry:

- `src/Features/Upscaling.cpp`: `IsVRRenderScaleRelatchVisualSafetyActive(...)` now returns true when the transition is render-scale relevant and either the pending relatch safety delay, pending relatch flag, or relatch-in-progress flag is active.
- `src/Features/Upscaling.cpp`: `ShouldBypassVRFoveatedVendorDispatchForTransition(...)` now consumes `IsVRRenderScaleRelatchVisualSafetyActive(...)`, so foveated/FOV dispatch remains bypassed through pending/retry/in-progress relatch windows.

Why:

- The square is early and short-lived.
- The log showed the early unsafe state: `relatchInProgress=yes` with visual guards off.
- This fix targets that exact gap instead of increasing late recovery duration.

Expected next log result:

- During relatch attempts, diagnostics should show `hmdDefer=yes`, `projectedDefer=yes`, and `foveatedBypass=yes` whenever `relatchInProgress=yes` and render-scale transition safety is relevant.
- The late phase durations should remain unchanged.

Conclusion:

- If the square disappears, the cause was the early relatch visual guard gap.
- If the square remains but the new log shows guards active through relatch-in-progress, inspect the first stretch fallback frame and presentation texture source next.

## Log 003 - 2026-06-01 12:31:08 Capture

File timestamp: `2026-06-01 12:31:08`

Log time range: `12:28:34.076` through `12:31:08.842`; relevant relatch loop from `12:29:23.293` through `12:31:08.678`.

Build/change context: first fix under test, where pending render-scale relatch was included in `IsVRRenderScaleRelatchVisualSafetyActive(...)` and that predicate was also used for foveated vendor bypass.

Summary:

- User result: no crash reported in this run, but GPU/CPU ms did not settle from the start and did not settle after transitions in/out. User did not notice the black square, but this is uncertain and must not be recorded as proof that the square is fixed.
- Log result: render-target relatch never completed. There are `303` relatch-pending entries and `302` `Render-target relatch deferred because vendor resources are still in use` warnings.
- No successful completion markers appear: no `Relatch step: D3D render-target recreate complete`, no `Applied render-target relatch`, no `Armed VR render-scale post-relatch recovery`, no stretch end, and no foveated dispatch resume.
- The first relatch attempt begins at `12:29:23.539` and immediately defers because DLSS resources are still in use. The same retry pattern continues until the end of the log.
- The requested state changes while the old relatch is still pending:
  - `12:29:23.240`: `renderScaleRequested=no`, `perfRequested=no`, `perfActive=yes`
  - `12:30:04.760`: `renderScaleRequested=yes`, `perfRequested=yes`, `perfActive=yes`
  - `12:30:47.291`: `renderScaleRequested=no`, `perfRequested=no`, `perfActive=yes`
  - `12:30:57.794`: `renderScaleRequested=yes`, `perfRequested=yes`, `perfActive=yes`
- This explains the user's "engine stuck in a slow setting" observation better than the normal staged recovery model. The log never reaches the staged recovery model.

Try table:

| Try | Direction / Mode | Key timestamps | Visible result | Result / conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | Render-scale -> DLAA/full-size candidate | Resource/request change `12:29:23.240`; relatch queued `12:29:23.293`; first apply attempt `12:29:23.539`; repeated vendor-resource deferrals begin immediately | Performance did not settle; black square not confirmed either way | Failed relatch. No D3D recreate complete and no post-relatch recovery armed. |
| Try 002 | DLAA/full-size candidate -> render-scale | Requested state flips back at `12:30:04.760` while old relatch is still pending | Performance still did not settle | Old pending relatch survives the direction change. This should not be treated as a normal transition recovery phase. |
| Try 003 | Render-scale -> DLAA/full-size candidate | Requested state flips again at `12:30:47.291` while relatch is still pending | Performance still did not settle | Retry loop continues; no successful relatch markers. |
| Try 004 | DLAA/full-size candidate -> render-scale | Requested state flips at `12:30:57.794`; last observed relatch retry `12:31:08.678`; last state marker `12:31:08.842` | Performance still did not settle; black square absence uncertain | Retry loop remains active at end of log. |

Setup events not counted as tries:

- Initial state/loading marker: `12:28:34.076`, `renderScaleRequested=yes`, `perfActive=yes`.
- Loading/menu close sequence: `12:29:07.573` through `12:29:22.141`.
- During setup, vendor reset deferral appears briefly and clears by `12:29:22.141`; the long stuck state starts after the first render-scale setting change at `12:29:23.240`.

Code changes made from this log:

- `src/Features/Upscaling.cpp`: added `IsVRRenderScaleRelatchFoveatedBypassActive(...)`.
- `src/Features/Upscaling.cpp`: `ShouldBypassVRFoveatedVendorDispatchForTransition(...)` now uses the bounded foveated relatch guard instead of the broad visual-safety guard.
- `src/Features/Upscaling.cpp`: transition diagnostics now compute `foveatedBypass` through `ShouldBypassVRFoveatedVendorDispatchForTransition(...)` so runtime and log semantics stay DRY.

Conclusion:

- The first fix was too broad for foveated vendor dispatch. It is still reasonable for HAM/FOV mask repair to remain deferred while relatch is pending, but full-eye/foveated-bypass must not be held for an unbounded pending retry loop.
- The next test should verify that after loading and after the first API transition, relatch either completes or the log no longer shows a continuous vendor-resource retry loop.
- Do not record the black square as fixed from this run. The performance failure invalidates the visual conclusion.

## Log 004 - 2026-06-01 12:44:00 Capture

File timestamp: `2026-06-01 12:44:00`

Log time range: `12:42:45.651` through `12:44:00.647`; relevant crash transition from `12:43:49.119` through `12:44:00.647`.

Build/change context: bounded foveated-bypass fix under test. Pending relatch still guarded HAM/HMD and projected mask repair, but did not hold foveated vendor bypass indefinitely.

Summary:

- User result: after load, ms settled in the interior. On the first transition, a black square was briefly visible, then the game crashed/locked the HMD image and SteamVR needed restart.
- Load result: this confirms the `Log 003` infinite slow-state was improved. The runtime no longer stayed in a permanent relatch retry loop after load.
- Crash result: the first transition did not fail during vendor teardown. The relatch retry at `12:43:58.526` deferred once because vendor resources were still in use, then the next relatch at `12:43:59.129` succeeded.
- The D3D render-target recreate completed at `12:44:00.334`; post-relatch recovery was armed with `stretch fallback 600 frames` and `foveated bypass up to 1200 frames or 360 stable full-eye frames`.
- The last line before the lock is `12:44:00.647` while creating presentation textures: `per-eye in 2097x2329, out 2468x2740`.

Try table:

| Try | Direction / Mode | Key timestamps | Visible result | Result / conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | Initial load into interior / render-scale state | Loading close `12:43:24.922`; grace exit `12:43:25.077`; post-load reset defers once at `12:43:31.563` and then succeeds at `12:43:31.573`; settled by user observation | ms settled | Success for the bounded foveated-bypass fix: no indefinite retry/slow state after load. |
| Try 002 | Render-scale -> DLAA/full-size candidate before the reported crash transition | Resource change `12:43:35.608`; relatch queued `12:43:35.654`; D3D recreate called `12:43:36.097`; D3D recreate complete `12:43:37.291`; recovery armed `12:43:37.291`; recovery exited `12:43:40.296` | no reported crash here | Successful short/full-size relatch. |
| Try 003 | DLAA/full-size -> render-scale on first exterior transition | Loading opened `12:43:49.119`; render-scale request visible `12:43:50.618`; loading closed `12:43:57.011`; relatch queued `12:43:57.944`; first relatch deferred `12:43:58.526`; second relatch applied `12:43:59.129`; D3D recreate called `12:43:59.136`; D3D recreate complete and recovery armed `12:44:00.334`; last line `12:44:00.647` during presentation texture creation | black square briefly visible before crash; HMD image locked | Failure. Crash occurs after successful D3D relatch, during first post-relatch stretch/presentation setup. |

Black-square interpretation:

- This log makes early HAM/FOV repair unlikely as the immediate cause. At the failure point the guards are active: `hmdDefer=yes`, `projectedDefer=yes`, `foveatedBypass=yes`.
- The square appears in the same window as first post-relatch stretch recovery. Therefore the better current hypothesis is stale or mismatched presentation source being stretched after render-target recreate, or relatch happening too early while the engine is still finishing transition resource work.
- The crash at presentation texture creation supports this: the first post-relatch presentation path is touching new per-eye `2097x2329 -> 2468x2740` textures immediately after D3D recreate, with close age only `49` frames.

Code changes made from this log:

- `src/Features/Upscaling.cpp`: added `GetPostTransitionVRRenderScaleRelatchDelayFrames(...)`.
- `src/Features/Upscaling.cpp`: kept the mandatory render-scale transition grace at `20` frames for guards/vendor scheduling.
- `src/Features/Upscaling.cpp`: restored the `120` frame recent-transition delay for the actual D3D render-target relatch after loading-menu close.
- `src/Features/Upscaling.cpp`: kept CS menu relatch delay at the shorter menu delay and explicitly excluded save/load context from the new relatch-delay helper.

Conclusion:

- The previous bounded-foveated fix helped the load/settling problem.
- The first transition crash shows the D3D relatch itself is still too early at close age `49`.
- Next validation should check whether delaying only the D3D relatch to the old `120` frame recent-transition window removes both the crash and the early black square without bringing back the indefinite slow-state.

## Log 005 - 2026-06-01 12:58:18 Capture

File timestamp: `2026-06-01 12:58:18`

Log time range: `12:55:03.477` through `12:58:18.629`; relevant transition attempts from `12:56:07.030` through `12:58:18.629`.

Build/change context: 120-frame recent-transition D3D relatch delay under test, with bounded foveated bypass and repeat-summary logging.

Summary:

- User result: no crash. Black square still present very early in the transition.
- Crash result: improved. D3D relatch no longer happens near closeAge `49`; render-scale relatches now complete later, e.g. closeAge `249`, `255`, and `270`, and the HMD does not lock.
- Black-square result: not fixed. The remaining artifact is earlier than D3D relatch and earlier than post-relatch stretch/FOV recovery.
- Key pattern: on render-scale entry from DLAA/full-size, loading closes while `renderScaleRelevant=no`. Projected HAM/FOV repair exits at closeAge `4`, then render-scale becomes relevant later at closeAge `15` to `30`, and only then the render-scale guards re-enter.
- Therefore the likely artifact window is the loading-tail/resource-change handoff before relatch, not D3D relatch completion and not FOV resume.

Try table:

| Try | Direction / Mode | Key timestamps | Visible result | Result / conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | DLAA/full-size -> render-scale | Loading opened `12:56:07.030`; render-scale request already visible at `12:56:08.504`; loading closed `12:56:14.955`; relatch queued `12:56:15.917`; first relatch deferred `12:56:18.632`; D3D complete `12:56:22.208`; recovery/FOV resume `12:56:33.809` | no crash | Successful relatch. Render-scale was known before close, so guards stayed active through the early window. |
| Try 002 | render-scale -> DLAA/full-size | Loading opened `12:56:37.650`; loading closed `12:56:38.414`; HAM/projected/foveated guards exited `12:56:38.966`; render-scale off request `12:56:39.040`; relatch queued `12:56:39.148`; D3D complete `12:56:45.387`; exit recovery ended `12:56:48.340` | no crash | Successful exit relatch. No evidence this is the black-square-prone direction. |
| Try 003 | DLAA/full-size -> render-scale | Loading opened `12:56:53.340`; loading closed `12:56:53.966`; projected guard exited at closeAge `4` at `12:56:54.006`; render-scale request appears at `12:56:54.540`; relatch queued `12:56:54.605`; first relatch deferred `12:56:57.385`; D3D complete `12:57:00.846`; recovery/FOV resume `12:57:12.647` | black square likely in this class of early entry transition | Suspicious early unguarded gap before relatch: projected repair can run after closeAge `4`, before render-scale guards re-enter. |
| Try 004 | render-scale -> DLAA/full-size | Loading opened `12:57:30.126`; loading closed `12:57:30.283`; guards exited `12:57:30.967`; render-scale off request `12:57:31.549`; relatch queued `12:57:31.634`; first relatch deferred `12:57:34.151`; D3D complete `12:57:37.762`; exit recovery ended `12:57:40.967` | no crash | Successful exit relatch. D3D completion is safe at closeAge `304`; not the early-square source. |
| Try 005 | DLAA/full-size -> render-scale | Loading opened `12:57:59.623`; loading closed `12:58:00.247`; projected guard exited at closeAge `4` at `12:58:00.288`; render-scale request appears at closeAge `24` at `12:58:01.051`; relatch queued at closeAge `30` at `12:58:01.158`; first relatch deferred at closeAge `150` at `12:58:03.681`; D3D complete at closeAge `270` at `12:58:06.944`; recovery/FOV resume `12:58:18.629` | black square visible very early; no crash | Strongest evidence. The square appears before relatch; projected mask repair had been allowed for roughly 20 frames before render-scale safety became visible. |

Timing statistics - historical 120-frame baseline:

Notes:

- Times below are seconds plus frames, relative to `loading menu closed`, except `Load` which is `loading opened -> loading closed`.
- `Exterior` means DLAA/full-size -> render-scale entry (`renderScale 0 -> 1`), matching exterior target in this test setup.
- `Interior` means render-scale -> DLAA/full-size exit (`renderScale 1 -> 0`), matching interior target.
- `FullEye` means post-stretch full-eye/foveated-bypass time before FOV/foveated dispatch resumes.
- `n/a` means the log line had no reliable frame marker for that specific event.
- This was the running table for the older 120-frame / long-recovery baseline. Do not add 60-frame trial logs here.
- `SE` is standard error. Samples are small, so use it as a variation hint. Use the `Max` columns as the practical safety-window reference.

Per-try timeline:

| Try | Target | Open | Close | Load | Render-scale change | Relatch queued | First relatch apply | Final relatch apply | D3D complete | Stretch off | FOV resumes | Stretch length | FullEye | Projected exit before change |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 001 | Exterior | `12:56:07.030` | `12:56:14.955` | 7.925s / 942 fr | -6.451s / -775 fr | 0.962s / 9 fr | 3.677s / 129 fr | 6.008s / 249 fr | 7.253s / 249 fr | 12.771s / 849 fr | 18.854s / 1209 fr | 5.518s / 600 fr | 6.083s / 360 fr | n/a |
| 002 | Interior | `12:56:37.650` | `12:56:38.414` | 0.764s / 88 fr | 0.626s / 30 fr | 0.734s / 36 fr | 3.335s / 156 fr | 5.740s / 276 fr | 6.973s / 276 fr | 9.926s / 456 fr | 9.926s / 456 fr | 2.953s / 180 fr | 0.000s / 0 fr | 0.552s / 30 fr |
| 003 | Exterior | `12:56:53.340` | `12:56:53.966` | 0.626s / 72 fr | 0.574s / 10 fr | 0.639s / 15 fr | 3.419s / 135 fr | 5.627s / 255 fr | 6.880s / 255 fr | 12.414s / 855 fr | 18.681s / 1215 fr | 5.534s / 600 fr | 6.267s / 360 fr | 0.040s / 4 fr |
| 004 | Interior | `12:57:30.126` | `12:57:30.283` | 0.157s / 16 fr | 1.266s / 58 fr | 1.351s / 64 fr | 3.868s / 184 fr | 6.237s / 304 fr | 7.478s / 304 fr | 10.684s / 484 fr | 10.684s / 484 fr | 3.205s / 180 fr | 0.000s / 0 fr | 0.684s / 30 fr |
| 005 | Exterior | `12:57:59.623` | `12:58:00.247` | 0.624s / 70 fr | 0.804s / 24 fr | 0.911s / 30 fr | 3.434s / 150 fr | 5.460s / 270 fr | 6.697s / 270 fr | 12.173s / 870 fr | 18.382s / 1230 fr | 5.476s / 600 fr | 6.209s / 360 fr | 0.041s / 4 fr |
| 006 | Exterior | `13:22:30.338` | `13:22:38.425` | 8.087s / 969 fr | -6.921s / -831 fr | 0.936s / 9 fr | 3.651s / 129 fr | 5.813s / 249 fr | 7.041s / 249 fr | n/a crash | n/a crash | n/a crash | n/a crash | n/a |
| 007 | Exterior | `13:37:41.925` | `13:37:49.870` | 7.945s / 951 fr | -6.696s / -804 fr | 0.904s / 9 fr | 3.702s / 129 fr | 6.039s / 249 fr | 7.248s / 249 fr | 12.605s / 849 fr | 18.676s / 1209 fr | 5.357s / 600 fr | 6.071s / 360 fr | n/a |
| 008 | Exterior | `13:40:39.675` | `13:40:40.290` | 0.615s / 69 fr | 0.473s / 10 fr | 0.628s / 15 fr | 3.382s / 135 fr | 5.442s / 255 fr | 6.678s / 255 fr | 12.056s / 855 fr | 18.151s / 1215 fr | 5.378s / 600 fr | 6.095s / 360 fr | 0.034s / 4 fr |

Running grouped statistics - Exterior / render-scale entry:

| Metric | n | Mean +/- SE | Range | Max |
| --- | ---: | ---: | ---: | ---: |
| Load duration | 6 | 4.304 +/- 1.647s / 512.2 +/- 197.6 fr | 0.615..8.087s / 69..969 fr | 8.087s / 969 fr |
| Render-scale change from open | 6 | 1.268 +/- 0.062s / 117.8 +/- 15.3 fr | 1.088..1.474s / 79..167 fr | 1.474s / 167 fr |
| Render-scale change from close | 6 | -3.036 +/- 1.635s / -394.3 +/- 183.1 fr | -6.921..0.804s / -831..24 fr | 0.804s / 24 fr |
| Relatch queued from close | 6 | 0.830 +/- 0.063s / 14.5 +/- 3.3 fr | 0.628..0.962s / 9..30 fr | 0.962s / 30 fr |
| First relatch apply from close | 6 | 3.544 +/- 0.060s / 134.5 +/- 3.3 fr | 3.382..3.702s / 129..150 fr | 3.702s / 150 fr |
| Final relatch apply from close | 6 | 5.732 +/- 0.107s / 254.5 +/- 3.3 fr | 5.442..6.039s / 249..270 fr | 6.039s / 270 fr |
| D3D complete from close | 6 | 6.966 +/- 0.105s / 254.5 +/- 3.3 fr | 6.678..7.253s / 249..270 fr | 7.253s / 270 fr |
| Crash/final presentation line from close | 1 | 7.353s / 249 fr | 7.353s / 249 fr | 7.353s / 249 fr |
| Stretch off from close | 5 | 12.404 +/- 0.132s / 855.6 +/- 3.8 fr | 12.056..12.771s / 849..870 fr | 12.771s / 870 fr |
| FOV resumes from close | 5 | 18.549 +/- 0.125s / 1215.6 +/- 3.8 fr | 18.151..18.854s / 1209..1230 fr | 18.854s / 1230 fr |
| Stretch length after D3D complete | 5 | 5.453 +/- 0.036s / 600.0 +/- 0.0 fr | 5.357..5.534s / 600..600 fr | 5.534s / 600 fr |
| FullEye after stretch | 5 | 6.145 +/- 0.039s / 360.0 +/- 0.0 fr | 6.071..6.267s / 360..360 fr | 6.267s / 360 fr |
| FOV after D3D complete | 5 | 11.598 +/- 0.068s / 960.0 +/- 0.0 fr | 11.428..11.801s / 960..960 fr | 11.801s / 960 fr |
| Projected exit before render-scale change | 4 | 0.039 +/- 0.002s / 4.0 +/- 0.0 fr | 0.034..0.041s / 4..4 fr | 0.041s / 4 fr |

Running grouped statistics - Interior / full-size exit:

| Metric | n | Mean +/- SE | Range | Max |
| --- | ---: | ---: | ---: | ---: |
| Load duration | 2 | 0.461 +/- 0.303s / 52.0 +/- 36.0 fr | 0.157..0.764s / 16..88 fr | 0.764s / 88 fr |
| Render-scale change from open | 2 | 1.407 +/- 0.017s / 96.0 +/- 22.0 fr | 1.390..1.423s / 74..118 fr | 1.423s / 118 fr |
| Render-scale change from close | 2 | 0.946 +/- 0.320s / 44.0 +/- 14.0 fr | 0.626..1.266s / 30..58 fr | 1.266s / 58 fr |
| Relatch queued from close | 2 | 1.042 +/- 0.308s / 50.0 +/- 14.0 fr | 0.734..1.351s / 36..64 fr | 1.351s / 64 fr |
| First relatch apply from close | 2 | 3.602 +/- 0.266s / 170.0 +/- 14.0 fr | 3.335..3.868s / 156..184 fr | 3.868s / 184 fr |
| Final relatch apply from close | 2 | 5.988 +/- 0.248s / 290.0 +/- 14.0 fr | 5.740..6.237s / 276..304 fr | 6.237s / 304 fr |
| D3D complete from close | 2 | 7.226 +/- 0.252s / 290.0 +/- 14.0 fr | 6.973..7.478s / 276..304 fr | 7.478s / 304 fr |
| Stretch off from close | 2 | 10.305 +/- 0.379s / 470.0 +/- 14.0 fr | 9.926..10.684s / 456..484 fr | 10.684s / 484 fr |
| FOV resumes from close | 2 | 10.305 +/- 0.379s / 470.0 +/- 14.0 fr | 9.926..10.684s / 456..484 fr | 10.684s / 484 fr |
| Stretch length after D3D complete | 2 | 3.079 +/- 0.126s / 180.0 +/- 0.0 fr | 2.953..3.205s / 180..180 fr | 3.205s / 180 fr |
| FullEye after stretch | 2 | 0.000 +/- 0.000s / 0.0 +/- 0.0 fr | 0.000..0.000s / 0..0 fr | 0.000s / 0 fr |
| FOV after D3D complete | 2 | 3.079 +/- 0.126s / 180.0 +/- 0.0 fr | 2.953..3.205s / 180..180 fr | 3.205s / 180 fr |
| Projected exit before render-scale change | 2 | 0.618 +/- 0.066s / 30.0 +/- 0.0 fr | 0.552..0.684s / 30..30 fr | 0.684s / 30 fr |

Running timing statistics - 60-frame recent-loading trial:

Notes:

- This table starts after `kVRRenderScaleRelatchRecentTransitionFrames` changed from `120` to `60`.
- This table is closed after `Log 012` because the next requested timing trial changes the delay to `6` frames.
- Do not add these samples to the historical 120-frame table above or the later 6-frame table below.
- Keep rapid back-and-forth stress attempts separate from clean single-leg transition averages unless the log clearly gives isolated timing markers.

Per-try timeline:

| Try | Log | Target | Open | Close | Load | Render-scale change | Relatch queued | First relatch apply | Final relatch apply | D3D complete | Stretch off | FOV resumes | Visible result |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 001 | Log 012 | Exterior/render-scale | `15:04:02.228` | `15:04:02.894` | `0.666s / 79 fr` | `0.782s / 28 fr` | `0.948s / 33 fr` | `2.258s / 93 fr` | `3.543s / 153 fr` | `4.755s / 153 fr` | n/a | `5.457s / 173 fr` | no crash |
| 002 | Log 012 | Interior/full-size | `15:04:12.660` | `15:04:12.816` | `0.156s / 18 fr` | `0.854s / 41 fr` | `0.918s / 46 fr` | `2.321s / 106 fr` | `3.454s / 166 fr` | `4.697s / 166 fr` | n/a | `4.950s / 176 fr` | no crash |
| 003 | Log 012 | Exterior/render-scale | `15:04:22.800` | `15:04:23.463` | `0.663s / 68 fr` | `0.720s / 18 fr` | `0.826s / 23 fr` | `2.441s / 83 fr` | `3.692s / 143 fr` | `4.944s / 143 fr` | n/a | `5.977s / 163 fr` | no crash |
| 004 | Log 012 | Interior/full-size | `15:04:39.364` | `15:04:39.519` | `0.155s / 19 fr` | `1.176s / 58 fr` | `1.261s / 64 fr` | `1.781s / 84 fr` | `2.304s / 104 fr` | `3.525s / 104 fr` | n/a | `3.752s / 114 fr` | no crash |
| 005 | Log 012 | Exterior/render-scale | `15:04:49.721` | `15:04:50.347` | `0.626s / 69 fr` | `0.840s / 25 fr` | `0.905s / 30 fr` | `2.240s / 90 fr` | `3.452s / 150 fr` | `4.690s / 150 fr` | n/a | `5.448s / 170 fr` | no crash |
| 006 | Log 012 | Interior/full-size | `15:05:01.328` | `15:05:01.487` | `0.159s / 17 fr` | `1.205s / 64 fr` | `1.300s / 70 fr` | `1.517s / 76 fr` | `1.738s / 82 fr` | `2.984s / 82 fr` | n/a | `3.219s / 92 fr` | no crash |
| 007 | Log 012 | Exterior/render-scale | `15:05:13.093` | `15:05:13.767` | `0.674s / 79 fr` | `0.584s / 10 fr` | `0.671s / 15 fr` | `2.186s / 75 fr` | `3.179s / 135 fr` | `4.417s / 135 fr` | n/a | `5.030s / 155 fr` | no crash |
| 008 | Log 012 | Interior/full-size | `15:05:19.341` | `15:05:19.491` | `0.150s / 9 fr` | `1.215s / 44 fr` | `1.300s / 50 fr` | `2.542s / 110 fr` | `3.476s / 170 fr` | `4.689s / 170 fr` | n/a | `4.899s / 180 fr` | no crash |
| 009 | Log 012 | Rapid interior/full-size | `15:05:30.268` | `15:05:30.414` | `0.146s / 9 fr` | `1.408s / 31 fr` | n/a | `2.297s / 63 fr` | `2.297s / 63 fr` | `3.556s / 63 fr` | n/a | `4.197s / 83 fr` | no crash; rapid overlap |
| 010 | Log 012 | Exterior/render-scale | `15:05:35.957` | `15:05:36.621` | `0.664s / 78 fr` | `0.587s / 33 fr` | `0.655s / 38 fr` | `1.936s / 98 fr` | `2.967s / 158 fr` | `4.229s / 158 fr` | n/a | `4.943s / 178 fr` | no crash |
| 011 | Log 012 | Interior/full-size | `15:05:43.215` | `15:05:43.373` | `0.158s / 17 fr` | `1.347s / 67 fr` | `1.431s / 73 fr` | `1.648s / 79 fr` | `1.864s / 85 fr` | `3.107s / 85 fr` | n/a | `3.389s / 95 fr` | no crash |
| 012 | Log 012 | Exterior/render-scale | `15:05:50.871` | `15:05:51.542` | `0.671s / 67 fr` | `0.681s / 19 fr` | `0.750s / 24 fr` | `2.134s / 84 fr` | `3.336s / 144 fr` | `4.577s / 144 fr` | n/a | `5.352s / 164 fr` | no crash |
| 013 | Log 012 | Interior/full-size | `15:05:59.553` | `15:05:59.707` | `0.154s / 18 fr` | `1.035s / 31 fr` | `1.095s / 36 fr` | `2.520s / 96 fr` | `3.763s / 156 fr` | `4.995s / 156 fr` | n/a | `5.360s / 166 fr` | no crash |

Grouped statistics:

| Target | Metric | n | Mean +/- SE | Range | Max |
| --- | --- | ---: | ---: | ---: | ---: |
| Exterior/render-scale | Render-scale change from close | 6 | `0.699 +/- 0.042s / 22.2 +/- 3.3 fr` | `0.584..0.840s / 10..33 fr` | `0.840s / 33 fr` |
| Exterior/render-scale | Relatch queued from close | 6 | `0.793 +/- 0.049s / 27.2 +/- 3.3 fr` | `0.655..0.948s / 15..38 fr` | `0.948s / 38 fr` |
| Exterior/render-scale | First relatch apply from close | 6 | `2.199 +/- 0.068s / 87.2 +/- 3.3 fr` | `1.936..2.441s / 75..98 fr` | `2.441s / 98 fr` |
| Exterior/render-scale | Final relatch apply from close | 6 | `3.362 +/- 0.106s / 147.2 +/- 3.3 fr` | `2.967..3.692s / 135..158 fr` | `3.692s / 158 fr` |
| Exterior/render-scale | D3D complete from close | 6 | `4.602 +/- 0.104s / 147.2 +/- 3.3 fr` | `4.229..4.944s / 135..158 fr` | `4.944s / 158 fr` |
| Exterior/render-scale | Submit suppression from close | 6 | `4.920 +/- 0.104s / 147.2 +/- 3.3 fr` | `4.543..5.260s / 135..158 fr` | `5.260s / 158 fr` |
| Exterior/render-scale | FOV/guard exit from close | 6 | `5.368 +/- 0.151s / 167.2 +/- 3.3 fr` | `4.943..5.977s / 155..178 fr` | `5.977s / 178 fr` |
| Interior/full-size | Render-scale change from close | 6 | `1.139 +/- 0.070s / 50.8 +/- 5.8 fr` | `0.854..1.347s / 31..67 fr` | `1.347s / 67 fr` |
| Interior/full-size | Relatch queued from close | 6 | `1.218 +/- 0.074s / 56.5 +/- 6.0 fr` | `0.918..1.431s / 36..73 fr` | `1.431s / 73 fr` |
| Interior/full-size | First relatch apply from close | 6 | `2.055 +/- 0.187s / 91.8 +/- 5.8 fr` | `1.517..2.542s / 76..110 fr` | `2.542s / 110 fr` |
| Interior/full-size | Final relatch apply from close | 6 | `2.767 +/- 0.368s / 127.2 +/- 16.9 fr` | `1.738..3.763s / 82..170 fr` | `3.763s / 170 fr` |
| Interior/full-size | D3D complete from close | 6 | `4.000 +/- 0.365s / 127.2 +/- 16.9 fr` | `2.984..4.995s / 82..170 fr` | `4.995s / 170 fr` |
| Interior/full-size | Submit suppression from close | 6 | `4.108 +/- 0.366s / 127.2 +/- 16.9 fr` | `3.100..5.107s / 82..170 fr` | `5.107s / 170 fr` |
| Interior/full-size | FOV/guard exit from close | 6 | `4.262 +/- 0.374s / 137.2 +/- 16.9 fr` | `3.219..5.360s / 92..180 fr` | `5.360s / 180 fr` |

Current 60-frame trial maximums:

| Window / marker | Observed max | Current interpretation |
| --- | ---: | --- |
| Latest successful D3D complete after close | `4.995s / 170 fr` | First 60-frame stability pass completed without crashes. |
| Latest final relatch apply after close | `3.763s / 170 fr` | 60-frame delay plus vendor-resource deferrals can still place final apply up to 170 frames after close. |
| Latest relatch-frame compositor suppression | `5.260s / 158 fr` | Suppression appeared on relatch frames and was followed by normal recovery. |
| Long-settle / rapid-reversal retry collapse | no crash or long-settle report; one rapid overlap completed | Continue monitoring; this run did not show the old pre-fix active-settling retry collapse. |
| Black square timing | not assessed in this stability-focused result | Cosmetic issue remains tracked separately; stability is the current priority. |

Running timing statistics - 6-frame recent-loading trial:

Notes:

- This table starts after `kVRRenderScaleRelatchRecentTransitionFrames` changed from `60` to `6`.
- Do not add these samples to the historical 120-frame or 60-frame tables.
- This is an aggressive stability trial. Treat crash/no-crash and relatch-frame submit suppression as primary outcomes.
- `Log 013` is a partial transition-log sample because the live `CommunityShaders.log` was overwritten by the next launch before full extraction. Use only the captured tail and crash log facts; do not include it in clean grouped timing averages.
- This table is closed after `Log 014`; the next requested timing trial changes the initial delay to `3` frames and keeps the `60`-frame busy retry backoff.

Per-try timeline:

| Try | Log | Target | Open | Close | Load | Render-scale change | Relatch queued | First relatch apply | Final relatch apply | D3D complete | Submit suppression | FOV resumes | Visible result |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 001 | Log 013 | Rapid stress / render-scale relatch retry loop | not recovered | not recovered | n/a | n/a | n/a | relatch attempts captured at closeAge `218`, `224`, `230` | closeAge `230` | failed at closeAge `236` with `8007000E` | none captured after failure | no | normal transitions first; crash after repeated fast in/out |
| 002 | Log 014 | Pre-backoff rapid stress repeat | `15:24:21.597` | `15:24:22.213` | `0.616s / 69 fr` | `0.681s / 14 fr` | `0.681s / 14 fr` | `0.822s / 20 fr` | `3.594s / 140 fr` | failed `3.649s / 140 fr` with `8007000E` | none; no successful relatch | no | same old build; crash reproduced with full logs |

Grouped statistics:

| Target | Metric | n | Mean +/- SE | Range | Max |
| --- | --- | ---: | ---: | ---: | ---: |
| Exterior/render-scale | Relatch queued from close | 0 | n/a | n/a | n/a |
| Exterior/render-scale | First relatch apply from close | 0 | n/a | n/a | n/a |
| Exterior/render-scale | Final relatch apply from close | 0 | n/a | n/a | n/a |
| Exterior/render-scale | D3D complete from close | 0 | n/a | n/a | n/a |
| Exterior/render-scale | Submit suppression from close | 0 | n/a | n/a | n/a |
| Interior/full-size | Relatch queued from close | 0 | n/a | n/a | n/a |
| Interior/full-size | First relatch apply from close | 0 | n/a | n/a | n/a |
| Interior/full-size | Final relatch apply from close | 0 | n/a | n/a | n/a |
| Interior/full-size | D3D complete from close | 0 | n/a | n/a | n/a |
| Interior/full-size | Submit suppression from close | 0 | n/a | n/a | n/a |

6-frame trial maximums:

| Window / marker | Observed max | Current interpretation |
| --- | ---: | --- |
| Latest successful D3D complete after close | n/a | No complete clean 6-frame sample extracted yet. |
| Latest relatch-frame compositor suppression | n/a | No successful relatch-frame submit suppression captured in the partial stress tail. |
| Crash/final line after close | closeAge `140` / `15:24:25.862` | Full Log 014 supersedes the partial Log 013 timing for the same pre-backoff failure shape. |
| Long-settle / active-settling retry collapse | 21 relatch attempts every `6` frames after the final close in Log 014 | Needs retry backoff; do not use the initial 6-frame delay as the busy retry cadence. |

Running timing statistics - 3-frame recent-loading trial:

Notes:

- This table starts after `kVRRenderScaleRelatchRecentTransitionFrames` changed from `6` to `3`.
- Busy or failed relatch retries should still use the `60`-frame backoff.
- Do not add these samples to the 6-frame table. Only logs showing `relatchAge=0/3` belong here.

Per-try timeline:

| Try | Log | Target | Open | Close | Load | Render-scale change | Relatch queued | First relatch apply | Final relatch apply | D3D complete | Submit suppression | FOV resumes | Visible result |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| pending | pending | pending | pending | pending | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | pending first 3-frame log |

Grouped statistics:

| Target | Metric | n | Mean +/- SE | Range | Max |
| --- | --- | ---: | ---: | ---: | ---: |
| Exterior/render-scale | Relatch queued from close | 0 | n/a | n/a | n/a |
| Exterior/render-scale | First relatch apply from close | 0 | n/a | n/a | n/a |
| Exterior/render-scale | Final relatch apply from close | 0 | n/a | n/a | n/a |
| Exterior/render-scale | D3D complete from close | 0 | n/a | n/a | n/a |
| Exterior/render-scale | Submit suppression from close | 0 | n/a | n/a | n/a |
| Interior/full-size | Relatch queued from close | 0 | n/a | n/a | n/a |
| Interior/full-size | First relatch apply from close | 0 | n/a | n/a | n/a |
| Interior/full-size | Final relatch apply from close | 0 | n/a | n/a | n/a |
| Interior/full-size | D3D complete from close | 0 | n/a | n/a | n/a |
| Interior/full-size | Submit suppression from close | 0 | n/a | n/a | n/a |

Current 3-frame trial maximums:

| Window / marker | Observed max | Current interpretation |
| --- | ---: | --- |
| Latest successful D3D complete after close | n/a | Pending first 3-frame log. |
| Latest relatch-frame compositor suppression | n/a | Pending first 3-frame log. |
| Crash/final line after close | n/a | Pending first 3-frame log. |
| Busy retry collapse | n/a | This should no longer repeat every 3 frames; busy/failing retries should show the 60-frame backoff. |

Historical maximums for 120-frame baseline safety-window tuning:

| Window / marker | Observed max | Current interpretation |
| --- | ---: | --- |
| Latest render-scale change after loading close | 1.266s / 58 fr | Interior exit can receive the change much later than the old 20-frame grace. Exterior entry can also receive it before close, so this marker is direction-dependent. |
| Latest render-scale entry change after close | 0.804s / 24 fr | Exterior entry barely exceeds the 20-frame grace in the current sample set. |
| Latest relatch queue after close | 1.351s / 64 fr | Queue timing is not the crash point; it indicates when the staged relatch pipeline starts. |
| Latest first relatch apply after close | 3.868s / 184 fr | First attempt can happen after the fixed 120-frame base because timing also depends on request arrival. |
| Latest successful D3D complete after close | 7.478s / 304 fr | This is the current largest completed relatch marker across both directions. |
| Exterior recovery to stretch-off | 12.771s / 870 fr | Completed exterior entries held stretch for the intended 600 frames after D3D complete. |
| Exterior recovery to FOV resume | 18.854s / 1230 fr | Completed exterior entries held full-eye for the intended 360 frames after stretch. |
| Interior recovery to FOV resume | 10.684s / 484 fr | Completed interior exits held the intended 180-frame recovery after D3D complete. |
| Crash after close | 7.353s / 249 fr | Log 006 crash occurred on same-frame post-relatch presentation texture recreation, not after recovery windows expired. |
| One-frame presentation skip after relatch | closeAge 268 fr captured; complete samples at 249 and 255 fr | Log 007 validates the skip as a crash-stability fix. Keep it, but do not treat it as a black-square fix. |
| Rapid-reversal retry collapse | 6-frame retry cadence while still settling through closeAge `1158` fr; relatch finally applied after settling expired at closeAge `1164` fr; `259` vendor-resource relatch deferrals in `Log 008` | This is not a clean-transition max. It is the stress failure/stutter signature in the pre-fix old build: repeated vendor reset and presentation texture recreation while previous recovery is still active. |
| Post-fix rapid-reversal overlap | no `*/6` relatch retry while `settling=yes`; latest settling deferral closeAge `425` fr with `settleRemaining=9`; final apply after settling closeAge `545` fr; `21` total relatch deferrals, `10` while settling in `Log 009` | This validates the retry-cadence fix for the long-settle mechanism. The remaining black square is separate. |
| Short post-D3D recovery | `Log 010`: 10 D3D completes; 6 render-scale entries armed `10 + 10`; 4 exits armed `10`; 6 relatch-frame skips; 0 device-lost/crash signatures | This validates the short recovery trial for stability. It does not improve the outside-entry black square. |

Statistical interpretation:

- Render-scale change is stable relative to transition start/open by time, but frame deltas show the close/open relationship differs by direction and loading duration. Exterior entry now averages `1.268 +/- 0.062s / 117.8 +/- 15.3 fr` from open across `n=6`.
- Render-scale change remains unstable relative to loading close, especially exterior, because some exterior entries receive the render-scale change before close while later entries receive it shortly after close. Current exterior close-relative range is `-6.921..0.804s / -831..24 fr`.
- D3D completion is now fairly stable by frame: exterior `254.5 +/- 3.3 fr`, interior `290 +/- 14 fr` after loading close. The observed max remains the interior exit at `304 fr`.
- Exterior entry has deterministic recovery markers in frames: stretch is `600 fr`, full-eye is `360 fr`, and FOV resumes `960 fr` after D3D completion.
- Interior exit has deterministic recovery markers in frames: stretch/FOV recovery is `180 fr` after relatch applied, with no separate full-eye period.
- `Log 007` validates the one-frame presentation skip as crash-stability work, not as the black-square fix. The black square still appears.
- Latest corrected visual evidence says the real world view and square appear together after the full HMD-black transition, then the world view stays while only the square disappears. That rules against treating the artifact as the whole returned world frame being stale or disappearing.
- The square is not late FOV resume: it appears far before the `600 + 360` frame exterior recovery is complete. After `Log 006`, it is also no longer enough to blame projected HAM/FOV repair alone.
- Current best suspect is a transient subregion/mask/presentation artifact on the first returned world frames around loading close and render-scale entry, before delayed D3D relatch success.
- Result to carry forward: use frame counts as the primary tuning unit. The stable safety markers are D3D relatch completion around `249..304` frames after close, exterior recovery as `600` frames stretch plus `360` frames full-eye, and interior recovery as `180` frames with no separate full-eye phase.
- Rapid-reversal stress cases need separate interpretation. In `Log 008`, the user changed cells again before the previous `600/360` exterior recovery had settled, so the relevant metric is not normal close-to-FOV timing; it is whether the next relatch waits sanely or starts a reset/retry loop.
- `Log 009` confirms the post-fix stress metric: overlapping relatches can still wait for vendor resources, but the wait is paced by `120` frame retries while settling instead of falling back to `6` frame retries.
- After the short post-D3D recovery trial, the expected recovery markers are no longer `600 + 360` frames for exterior entry or `180` frames for interior exit. The next log should show roughly `10` stretch frames, then roughly `10` stable full-eye frames before FOV/foveated resumes.
- `Log 010` confirms those short post-D3D markers. Keep the old timing table as historical baseline only; current head now expects `10 + 10` on render-scale entry and `10` frames on exit.

Code changes made from this log:

- `src/Features/Upscaling.cpp`: `ShouldDeferVRProjectedMaskRepair(...)` now stays active while `ShouldBypassVRFoveatedVendorDispatchForTransition(...)` is active.
- `src/Features/Upscaling.cpp`: the diagnostics-only helper call alignment was folded into the prior logging commit, not kept as part of this behavior change.

Conclusion:

- The 120-frame D3D relatch delay should stay for recent loading transitions because it removed the crash in this test.
- The remaining black square is probably not caused by post-relatch stretch/FOV recovery. It appears before relatch, during the early loading-tail handoff when render-scale relevance may not yet be visible.
- The next test should check whether projected HAM/FOV guard remains active from loading close through the delayed render-scale request. If the square remains with projected guard active through that whole gap, inspect the VR intermediate texture recreate path at `renderScale 0 -> 1` next.

## Log 006 - 2026-06-01 13:22:45 Capture

File timestamp: `2026-06-01 13:22:45`

Log time range: `13:21:04.461` through `13:22:45.778`; relevant first interior-to-exterior transition from `13:22:30.338` through the final line at `13:22:45.778`.

Build/change context: projected HAM/FOV repair was additionally deferred during the foveated/full-eye bypass tail, on top of the `120` frame recent-transition D3D relatch delay.

Summary:

- User result: black square still visible, then crash after the first interior-to-exterior transition.
- The projected-mask hypothesis is rejected by this log. During the crash transition, `projectedDefer=yes` was active at closeAge `4`, closeAge `30`, and through relatch closeAge `249`.
- The final crash signature is again post-relatch presentation texture recreation. The log reaches `Relatch step: D3D render-target recreate complete`, recreates DLSS resources, arms `600/1200` frame recovery, then stops immediately after `(Re)creating presentation textures: per-eye in 2097x2329, out 2468x2740`.
- The black square remains early. The likely early visual source is no longer projected HAM/FOV repair; the next suspect is VR intermediate recreation during render-scale entry at closeAge `8` to `9`.

Try table:

| Try | Direction / Mode | Key timestamps | Visible result | Result / conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | Interior DLAA/full-size -> exterior render-scale | Loading opened `13:22:30.338`; render-scale request `13:22:31.504`; loading closed `13:22:38.425`; VR intermediates recreated full-size at `13:22:39.236` and render-scale at `13:22:39.396`; relatch queued `13:22:39.361` at closeAge `9`; first relatch attempt `13:22:42.076` at closeAge `129` deferred by DLSS resources; second relatch `13:22:44.238` at closeAge `249`; D3D complete and recovery armed `13:22:45.466`; final line `13:22:45.778` recreating presentation textures | black square early, then crash | Failure. Keeping projected repair deferred did not fix the square. Crash occurs after relatch success, on same-frame presentation texture creation. |

Timing:

- Render-scale request from loading open: `1.166s / 138 fr`.
- Loading duration: `8.087s / 969 fr`.
- Render-scale request from loading close: `-6.921s / -831 fr`.
- Relatch queued from close: `0.936s / 9 fr`.
- First relatch attempt from close: `3.651s / 129 fr`; deferred because DLSS resources were still in use.
- Final relatch attempt from close: `5.813s / 249 fr`.
- D3D relatch complete from close: `7.041s / 249 fr`.
- Crash/final presentation texture line from close: `7.353s / 249 fr`.

Code changes made from this log:

- `src/Features/Upscaling.cpp`: reverted the failed `ShouldDeferVRProjectedMaskRepair(...)` extension that used foveated bypass as an additional projected-mask defer reason.
- `src/Features/Upscaling.cpp`: added a relatch recovery start frame and skip same-frame submit-stage presentation texture recreation during relatch stretch fallback, falling back to vanilla submit for that frame.

Conclusion:

- Do not spend another iteration on projected HAM/FOV repair unless a future log contradicts this run.
- The crash is specifically tied to creating submit-stage presentation textures in the same frame as the render-target relatch. The next test should validate that skipping that one frame avoids the crash.
- If the black square remains but the crash is gone, focus on the early VR intermediate texture recreation at closeAge `8` to `9`, before the delayed D3D relatch.

## Log 007 - 2026-06-01 13:40:58 Capture

File timestamp: `2026-06-01 13:40:58` when first read.

Log overwrite note: during analysis the same `CommunityShaders.log` path was overwritten by a shorter startup log at `2026-06-01 13:44:49`. This entry uses the relevant lines captured before that overwrite.

Log time range used: captured lines from `13:36:36.044` through `13:40:58.441`; complete transition samples from `13:37:41.925` through `13:38:08.546` and from `13:40:39.675` through `13:40:58.441`. Additional partial skip sightings were captured at `13:38:52.960` and `13:39:44.900`.

Build/change context: failed projected-mask/foveated-bypass extension reverted; one-frame submit-stage presentation texture skip on the D3D relatch frame under test; `120` frame recent-transition D3D relatch delay active.

Summary:

- User result: no crash. Full HMD black occurs during transition, then the real world view and black square appear together.
- Corrected visual result: the world view stays; only the black square disappears immediately. This is important because it points to a transient subregion/overlay/mask/presentation artifact on the first returned world frame, not a stale full-world image that disappears with the square.
- Crash result: improved. The one-frame skip fired on relatch-frame submit-stage presentation setup and the runtime continued to presentation texture creation on the following submit-stage call.
- Black-square result: not fixed. The artifact remains earlier than post-relatch FOV recovery and should not be marked as solved.

Try table:

| Try | Direction / Mode | Key timestamps | Visible result | Result / conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | Interior DLAA/full-size -> exterior render-scale | Loading opened `13:37:41.925`; render-scale request `13:37:43.174`; loading closed `13:37:49.870`; relatch queued `13:37:50.774` at closeAge `9`; first relatch attempt `13:37:53.572` at closeAge `129` deferred by vendor resources; final relatch `13:37:55.909` at closeAge `249`; D3D complete `13:37:57.118`; relatch-frame presentation skip `13:37:57.439`; presentation textures created `13:37:57.449`; stretch off `13:38:02.475`; FOV resumes `13:38:08.546` | no crash; black square behavior still present in this test class | Success for crash stability. Not a square fix. |
| Try 002 | Interior DLAA/full-size -> exterior render-scale | Loading opened `13:40:39.675`; loading closed `13:40:40.290`; projected guard exit before render-scale change `13:40:40.324`; render-scale request `13:40:40.763`; relatch queued `13:40:40.918` at closeAge `15`; first relatch attempt `13:40:43.672` at closeAge `135` deferred by vendor resources; final relatch `13:40:45.732` at closeAge `255`; D3D complete `13:40:46.968`; relatch-frame presentation skip `13:40:47.284`; presentation textures created `13:40:47.295`; stretch off `13:40:52.346`; FOV resumes `13:40:58.441` | no crash; user reports square visible very early | Success for crash stability. Confirms square survives the skip. |

Timing:

- Try 001 load duration: `7.945s / 951 fr`.
- Try 001 render-scale change from close: `-6.696s / -804 fr`.
- Try 001 relatch queued from close: `0.904s / 9 fr`.
- Try 001 final relatch apply from close: `6.039s / 249 fr`.
- Try 001 D3D complete from close: `7.248s / 249 fr`.
- Try 001 stretch length after D3D complete: `5.357s / 600 fr`.
- Try 001 full-eye period after stretch: `6.071s / 360 fr`.
- Try 001 FOV resumes from close: `18.676s / 1209 fr`.
- Try 002 load duration: `0.615s / 69 fr`.
- Try 002 render-scale change from close: `0.473s / 10 fr`.
- Try 002 projected guard exit before render-scale change: `0.034s / 4 fr`.
- Try 002 relatch queued from close: `0.628s / 15 fr`.
- Try 002 final relatch apply from close: `5.442s / 255 fr`.
- Try 002 D3D complete from close: `6.678s / 255 fr`.
- Try 002 stretch length after D3D complete: `5.378s / 600 fr`.
- Try 002 full-eye period after stretch: `6.095s / 360 fr`.
- Try 002 FOV resumes from close: `18.151s / 1215 fr`.
- Additional partial evidence: relatch-frame presentation skips were also captured at closeAge `262` and `268`, but those partial transitions were not added to the running means because the log was overwritten before complete extraction.

Code changes made from this log:

- None. This entry records the runtime result of the one-frame presentation skip already implemented from `Log 006`.

Conclusion:

- Keep the one-frame relatch submit-stage presentation skip. It addresses the immediate post-relatch crash path seen in `Log 006`.
- Do not mark the black square fixed. It survives the crash fix.
- The corrected visual report changes the target: the returned world view is valid enough to stay, while only a square-shaped subregion disappears. Focus next on first-returned-world-frame presentation, mask, or intermediate-texture state around loading close/render-scale entry, not on late FOV resume and not on the whole world image being stale.

## Log 008 - 2026-06-01 13:56:14 Pre-Fix Rapid-Reversal Repeat

File timestamp while analyzed: `2026-06-01 13:56:14`.

Log note: the file was still being written during first analysis, then the user clarified the result. This was a repeat attempt before the rapid-reversal retry-cadence fix, meant to see whether the prior crash could be reproduced.

Build/change context: same build as `Log 007`: one-frame relatch presentation skip active; projected-mask/foveated-bypass extension reverted; `120` frame recent-transition D3D relatch delay active.

Summary:

- User result: with the same pre-fix build, repeated immediate in/out transitions before latch and at different times after latch to see whether another crash could be reproduced. This repeat did not crash.
- User visual/perf result: black square appeared in all interior -> exterior transitions. On the final exterior -> interior transition, interior frame time took very long to recover; after recovery, the next transition back to exterior was safe.
- Log result: this is not the old single-transition crash path. It is a rapid-reversal stress pattern where a new cell transition can start while the previous render-scale recovery is still active.
- The code keeps accepting new resource changes, queuing relatches, resetting DLSS, recreating VR intermediates, and recreating presentation textures while `settling=yes` and often `relatchPending=yes`.
- After enough overlap, the relatch retry cadence falls back to the short `6` frame retry window even though the previous render-scale recovery is still active. That creates a tight reset/rebuild loop.
- Because this run was before the rapid-reversal source fix, it does not validate the fix. It validates the failure/stutter diagnosis that the fix is meant to address.

Key captured sequence:

| Marker | Timestamp / frame | Meaning |
| --- | --- | --- |
| Exterior render-scale relatch applied | `13:53:11.568`, frame `6099`, closeAge `249` | Long recovery armed: `settleRemaining=1200`, `stretchRemaining=600`. |
| First rapid re-entry menu opens | `13:53:14.187`, frame `6372` | Only `273` frames after relatch; previous recovery still has `927` settle / `327` stretch frames left. |
| First rapid re-entry menu closes | `13:53:14.849`, frame `6450` | Previous recovery still has `849` settle / `249` stretch frames left. |
| Interior/full-size resource change | `13:53:15.229`, frame `6495`, closeAge `45` | Render-scale target begins flipping while previous stretch is still active. |
| Interior/full-size relatch queued | `13:53:15.270`, frame `6500`, closeAge `50` | Relatch is queued with `settleRemaining=799`, `stretchRemaining=199`. |
| First opposite relatch attempt | `13:53:17.614`, frame `6620`, closeAge `170` | Vendor resources are still busy, so relatch defers while previous recovery is still active. |
| Later collapsed retry loop | captured around `13:54:56.857` -> `13:54:57.090`, frames `12531`, `12537`, `12543` | Relatch retries every `6` frames while `settling=yes`, repeatedly deferring vendor teardown, rebuilding DLSS, and recreating presentation textures. |

Interpretation:

- The single-transition fix still matters: relatch-frame presentation skip is present and works when a transition is allowed to finish normally.
- The new risk pattern is overlap handling. The first re-entry happened while the previous render-scale recovery still had hundreds of frames remaining.
- The dangerous part is not just that a new transition occurs early. It is that a deferred relatch can retry at the short `6` frame cadence once the recent-close context has expired, even though the previous recovery is still active.
- The tight loop is visible as repeated `Applying render-target relatch`, `vendor resources still in use`, `Rebuilt submit-stage DLSS feature after VR reset`, and `(Re)creating presentation textures` within a few frames.
- The final long interior frame-time recovery matches this pattern: the old build was repeatedly trying to relatch every `6` frames until previous recovery had almost fully expired, then finally completed the interior/full-size relatch.

Code changes made from this log:

- `src/Features/Upscaling.cpp`: added `GetVRRenderScaleRelatchRetryDelayFrames(...)`.
- `src/Features/Upscaling.cpp`: `ApplyPendingPerfModeRenderTargetRecreate(...)` now computes retry delay through that helper.
- Rationale: when render-scale transition safety is relevant and post-relatch recovery is still settling, a relatch retry now keeps at least the `120` frame recent-transition cadence instead of falling back to `6` frames. This is meant to prevent the rapid-reversal reset/rebuild loop without adding delay to non-render-scale paths.

Timing/statistics handling:

- Do not add this log to the clean exterior/interior averages above. Those averages describe isolated transitions.
- Use this log as the first rapid-reversal stress sample. The important stress metrics are: first re-entry opened `273` frames after exterior relatch, closed `351` frames after exterior relatch, and the first opposite relatch was queued at closeAge `50` with `199` stretch frames still remaining.
- The finalized captured retry-loop signature reached closeAge `1158` while still settling (`settleRemaining=1`). The relatch then applied at closeAge `1164` after settling expired, and the log contains `259` vendor-resource relatch deferrals.

Conclusion:

- Rapid direction changes need their own guard. The next test with the rebuilt source should repeat the same out-and-immediate-in stress case and verify that deferred relatches show a `120` frame retry cadence while recovery is active, not `6`.
- If the crash remains after this retry-cadence fix, the next suspect is presentation/intermediate texture recreation during pending-relatch recovery, because that still occurs on resource changes while the previous recovery is active.

## Log 009 - 2026-06-01 14:07:59 Post-Fix Rapid-Reversal Validation

File timestamp: `2026-06-01 14:07:59`.

Log time range: setup/save-load noise starts earlier; relevant transition stress period from `14:05:00.038` through `14:07:59.013`.

Build/change context: first run after the rapid-reversal retry-cadence fix. One-frame relatch presentation skip remains active; projected-mask/foveated-bypass extension remains reverted; `120` frame recent-transition D3D relatch delay active.

Summary:

- User result: no crashes, multiple entries, no long settle. Black square remains.
- Fix validation result: the old long-settle signature is gone in this run. There are `0` `relatchAge=*/6` entries while `settling=yes`.
- Post-fix overlap result: repeated relatch attempts during active recovery use the `120` frame cadence. The log has `74` `relatchAge=0/120` entries while `settling=yes`.
- Remaining artifact: black square is not fixed by retry pacing. It remains especially associated with interior -> exterior / render-scale entry.

Try table:

| Try | Direction / Mode | Key timestamps | Visible result | Result / conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | Interior/full-size -> exterior render-scale | Loading opened `14:05:00.038`; render-scale relevance changed while loading at `14:05:01.286`; loading closed `14:05:07.707`; relatch queued `14:05:08.620` closeAge `9`; first apply `14:05:11.192` closeAge `129` deferred; final apply `14:05:13.382` closeAge `249`; D3D complete `14:05:14.594`; relatch-frame presentation skip `14:05:14.909`; FOV resumes `14:05:26.009` | black square remains | No crash. Normal staged exterior recovery. |
| Try 002 | Exterior render-scale -> interior/full-size | Loading opened `14:05:33.716`; loading closed `14:05:34.434`; relatch queued `14:05:34.971` closeAge `28`; first apply `14:05:37.554` closeAge `148` deferred; final apply `14:05:39.880` closeAge `268`; D3D complete `14:05:41.111`; guards exit `14:05:43.940` | no square note for this direction | No crash. Interior/full-size transition settles normally. |
| Try 003 | Interior/full-size -> exterior render-scale | Loading opened `14:05:58.054`; loading closed `14:05:58.684`; relatch queued `14:05:59.378` closeAge `21`; first apply `14:06:01.832` closeAge `141` deferred; final apply `14:06:04.270` closeAge `261`; D3D complete `14:06:05.498`; relatch-frame presentation skip `14:06:05.825`; FOV resumes `14:06:17.317` | black square remains | No crash. Normal staged exterior recovery. |
| Try 004 | Rapid repeated transitions before relatch completion | Multiple loading closes from `14:06:21.560` through `14:06:42.224`; relatch attempts stay on `0/120`; final apply `14:06:47.077` closeAge `243`; D3D complete `14:06:48.308`; relatch-frame presentation skip `14:06:48.315` | not isolated | No crash. Pending relatch survived repeated immediate menu transitions. |
| Try 005 | Transition during active recovery | Loading opened `14:06:52.597`; loading closed `14:06:52.752`; relatch queued `14:06:53.953` closeAge `125` while `settling=yes`; first apply `14:06:54.515` closeAge `145` deferred; retries at closeAge `265` and `385` use `0/120`; recovery completes `14:07:01.215`; final apply `14:07:01.667` closeAge `505`; D3D complete `14:07:02.896` | no long settle | Fix validation. Retry cadence did not collapse to `6` while settling. |
| Try 006 | Interior/full-size -> exterior render-scale | Loading opened `14:07:11.044`; loading closed `14:07:11.667`; relatch queued `14:07:12.493` closeAge `16`; first apply `14:07:15.172` closeAge `136` deferred; final apply `14:07:17.196` closeAge `256`; D3D complete `14:07:18.422`; relatch-frame presentation skip `14:07:18.732` | black square remains | No crash. |
| Try 007 | Rapid overlap after Try 006 | New transition opened/closed at `14:07:22.342`/`14:07:22.500` while exterior recovery still had `775/758` settle frames and `175/158` stretch frames; relatch queued `14:07:23.393` closeAge `101` while `settling=yes`; retries at closeAge `221`, `341`, `123`, and `243` use `0/120`; final apply `14:07:35.420` closeAge `363`; D3D complete `14:07:36.643`; relatch-frame presentation skip `14:07:36.648` | no long settle | No crash. Repeated immediate reversals are paced, not tight-looped. |
| Try 008 | Final active-recovery overlap | Loading opened/closed `14:07:44.193`/`14:07:44.353` while `settling=yes`; relatch queued `14:07:45.446` closeAge `65`; retries at closeAge `185`, `305`, and `425` use `0/120` while settling; recovery completes `14:07:52.630`; final apply `14:07:54.789` closeAge `545`; D3D complete `14:07:56.001`; guards exit `14:07:59.013` | no long settle | No crash. Latest settling deferral was closeAge `425` with `settleRemaining=9`; final apply happened after recovery expired. |

Post-fix stress metrics:

- `relatchAge=*/6` while `settling=yes`: `0`.
- `relatchAge=0/120` while `settling=yes`: `74` entries.
- Vendor-resource relatch deferrals: `21` total, `10` while `settling=yes`.
- Relatch applies while `settling=yes`: `10`.
- Latest observed deferral while still settling: closeAge `425`, `settleRemaining=9`.
- Latest final relatch apply in an active-recovery overlap: closeAge `545`, after recovery had ended.

Code changes made from this log:

- None.

Conclusion:

- The rapid-reversal retry-cadence fix works for the long-wait/stutter mechanism seen in `Log 008`. Keep it.
- The black square is independent of that retry collapse. Continue investigating early returned-world-frame presentation/mask/intermediate state on interior -> exterior render-scale entry.

## Log 010 - 2026-06-01 14:32:39 Short Post-D3D Recovery Validation

File timestamp: `2026-06-01 14:32:39`.

Log time range: setup/load noise starts earlier; relevant transition period from `14:29:34.079` through `14:32:39.534`.

Build/change context: first run after shortening post-D3D recovery. Render-scale entry should use `10` frames stretch plus `10` stable full-eye DLSS/FSR frames before FOV/foveated resumes. Render-scale exit should use a `10` frame recovery. Pre-D3D relatch timing, 120-frame retry cadence, and one-frame relatch presentation skip are unchanged.

Summary:

- User result: no crashes. Several normal transitions worked at the beginning, then many rapid inside/outside transitions worked at the end.
- User visual result: black square is always visible when transitioning outside to Hoshipa. It may also be visible going inside, but that is not confirmed.
- Log result: short post-D3D recovery is active and stable. Ten D3D relatches completed; six render-scale entries armed `10 + 10`, four exits armed `10`, and six render-scale entries hit the `10 stable full-eye` resume marker.
- No device-lost/crash signature appeared. No `relatchAge=*/6` while `settling=yes` appeared, though this run did not keep a new relatch pending during active post-relatch settling the way `Log 009` did.

Try table:

| Try | Direction / Mode | Key timestamps | Visible result | Result / conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | Exterior/render-scale -> interior/full-size | Loading opened `14:29:34.079`; loading closed `14:29:36.598`; relatch queued `14:29:38.574` closeAge `139`; final apply `14:29:39.005` closeAge `151`; D3D complete `14:29:40.213`; exit recovery armed `10` frames; guards exit `14:29:40.480` | not isolated | No crash. Exit recovery is short. |
| Try 002 | Interior/full-size -> exterior render-scale | Loading opened `14:29:50.168`; render-scale relevance changes during loading `14:29:51.533`; loading closed `14:29:57.951`; relatch queued `14:29:58.857` closeAge `9`; first apply `14:30:01.522` closeAge `129` deferred; final apply `14:30:03.646` closeAge `249`; D3D complete `14:30:04.886`; skip `14:30:05.200`; FOV resumes after `10` stable full-eye frames at `14:30:05.543` | black square visible on outside entry | No crash. Entry recovery is now short. |
| Try 003 | Exterior/render-scale -> interior/full-size | Loading opened `14:30:20.997`; loading closed `14:30:21.708`; relatch queued `14:30:22.200` closeAge `28`; final apply `14:30:27.016` closeAge `268`; D3D complete `14:30:28.220`; exit guards clear `14:30:28.477` | not confirmed | No crash. |
| Try 004 | Interior/full-size -> exterior render-scale | Loading opened `14:30:39.636`; loading closed `14:30:40.250`; relatch queued `14:30:41.210` closeAge `34`; first apply `14:30:43.572` closeAge `154` deferred; final apply `14:30:45.599` closeAge `274`; D3D complete `14:30:46.814`; skip `14:30:47.142`; FOV resumes `14:30:47.525` | black square visible on outside entry | No crash. |
| Try 005 | Exterior/render-scale -> interior/full-size | Loading opened `14:30:59.700`; loading closed `14:30:59.849`; relatch queued `14:31:01.164` closeAge `62`; final apply `14:31:05.958` closeAge `302`; D3D complete `14:31:07.197`; exit guards clear `14:31:07.411` | not confirmed | No crash. |
| Try 006 | Interior/full-size -> exterior render-scale | Loading opened `14:31:13.906`; loading closed `14:31:14.558`; relatch queued `14:31:15.205` closeAge `15`; final apply `14:31:20.229` closeAge `255`; D3D complete `14:31:21.439`; skip `14:31:21.758`; FOV resumes `14:31:22.073` | black square visible on outside entry | No crash. |
| Try 007 | Exterior/render-scale -> interior/full-size | Loading opened `14:31:28.962`; loading closed `14:31:29.111`; final D3D complete `14:31:35.803`; exit guards clear `14:31:36.027` | not confirmed | No crash. |
| Try 008 | Interior/full-size -> exterior render-scale | Loading opened `14:31:39.772`; loading closed `14:31:40.317`; relatch queued `14:31:41.223` closeAge `29`; final apply `14:31:45.674` closeAge `269`; D3D complete `14:31:46.914`; skip `14:31:47.239`; FOV resumes `14:31:47.620` | black square visible on outside entry | No crash. |
| Try 009 | Rapid repeated transitions before final relatch | Repeated loading open/close cycles from `14:31:53.492` through `14:32:06.534`; relatch remains paced at `0/120`; final apply `14:32:10.683` closeAge `243`; D3D complete `14:32:11.916`; skip `14:32:11.920`; FOV resumes `14:32:12.203` | black square visible on outside entry | No crash. Rapid transitions remain stable. |
| Try 010 | Rapid repeated transitions before final relatch | Repeated loading open/close cycles from `14:32:12.571` through `14:32:33.458`; relatch remains paced at `0/120`; final apply `14:32:38.009` closeAge `243`; D3D complete `14:32:39.233`; skip `14:32:39.239`; FOV resumes `14:32:39.533` | black square visible on outside entry | No crash. Rapid transitions remain stable. |

Latch/D3D timing from loading close:

| Try | Target | Relatch queued | First relatch apply | Final relatch apply | D3D complete |
| --- | --- | ---: | ---: | ---: | ---: |
| 001 | Interior/full-size | `1.976s / 139 fr` | `2.200s / 145 fr` | `2.407s / 151 fr` | `3.615s / 151 fr` |
| 002 | Exterior/render-scale | `0.906s / 9 fr` | `3.571s / 129 fr` | `5.695s / 249 fr` | `6.935s / 249 fr` |
| 003 | Interior/full-size | `0.492s / 28 fr` | `2.993s / 148 fr` | `5.308s / 268 fr` | `6.512s / 268 fr` |
| 004 | Exterior/render-scale | `0.960s / 34 fr` | `3.322s / 154 fr` | `5.349s / 274 fr` | `6.564s / 274 fr` |
| 005 | Interior/full-size | `1.315s / 62 fr` | `3.785s / 182 fr` | `6.109s / 302 fr` | `7.348s / 302 fr` |
| 006 | Exterior/render-scale | `0.647s / 15 fr` | `3.645s / 135 fr` | `5.671s / 255 fr` | `6.881s / 255 fr` |
| 007 | Interior/full-size | `1.066s / 56 fr` | `3.443s / 176 fr` | `5.473s / 296 fr` | `6.692s / 296 fr` |
| 008 | Exterior/render-scale | `0.906s / 29 fr` | `3.322s / 149 fr` | `5.357s / 269 fr` | `6.597s / 269 fr` |
| 009 | Rapid exterior/render-scale | n/a multi-close | n/a multi-close | `4.149s / 243 fr` | `5.382s / 243 fr` |
| 010 | Rapid exterior/render-scale | n/a multi-close | n/a multi-close | `4.551s / 243 fr` | `5.775s / 243 fr` |

Clean latch/D3D grouped statistics from this log:

| Target | Metric | n | Mean | Max |
| --- | --- | ---: | ---: | ---: |
| Exterior/render-scale | Relatch queued | 4 | `0.855s / 21.8 fr` | `0.960s / 34 fr` |
| Exterior/render-scale | First relatch apply | 4 | `3.465s / 141.8 fr` | `3.645s / 154 fr` |
| Exterior/render-scale | Final relatch apply | 4 | `5.518s / 261.8 fr` | `5.695s / 274 fr` |
| Exterior/render-scale | D3D complete | 4 | `6.744s / 261.8 fr` | `6.935s / 274 fr` |
| Interior/full-size | Relatch queued | 4 | `1.212s / 71.3 fr` | `1.976s / 139 fr` |
| Interior/full-size | First relatch apply | 4 | `3.105s / 162.8 fr` | `3.785s / 182 fr` |
| Interior/full-size | Final relatch apply | 4 | `4.824s / 254.3 fr` | `6.109s / 302 fr` |
| Interior/full-size | D3D complete | 4 | `6.042s / 254.3 fr` | `7.348s / 302 fr` |

Short-recovery validation metrics:

- D3D relatches completed: `10`.
- Render-scale entries armed as `10` stretch + `20` total / `10` stable full-eye: `6`.
- Render-scale exits armed as `10` total: `4`.
- Relatch-frame presentation skips: `6`.
- `10` stable full-eye resume markers: `6`.
- Device-lost/crash signatures: `0`.
- Vendor-resource relatch deferrals: `18`, none while `settling=yes`.
- `relatchAge=*/6` while `settling=yes`: `0`.

Code changes made from this log:

- None.

Conclusion:

- The short post-D3D recovery is stable in this run and should stay for now.
- The black square is not caused by the long post-D3D recovery tail. It remains tied most strongly to outside/Hoshipa render-scale entry and must be investigated in the earlier returned-world / loading-tail / render-scale-entry handoff.

## Log 011 - 2026-06-01 14:49:22 First Outside Transition Crash

File timestamp: `2026-06-01 14:49:22`.

Log time range: relevant transition from loading open `14:49:07.237` through final log line `14:49:22.476`.

Build/change context: despite the source being prepared for a 60-frame trial, the running binary still used the `120`-frame recent-loading relatch delay. Evidence: relatch queue and retry lines show `relatchAge=0/120`, not `0/60`. Short post-D3D recovery was active (`10` stretch, `20` total / `10` stable full-eye).

Summary:

- User result: crash on the first transition outside.
- Log result: not a valid 60-frame timing sample. The first relatch attempt occurred at closeAge `129`, deferred because DLSS/submit-stage resources were still in use, then retried and completed at closeAge `249`.
- Crash signature: D3D relatch completed, vendor resources recreated, post-relatch recovery armed, and the existing one-frame `Skipping submit-stage presentation texture recreate` line fired. The log stopped before the next frame.
- Interpretation: the old same-frame presentation texture recreation crash path was avoided, but allowing the original OpenVR compositor submit on that same relatch frame can still lock/crash. The relatch frame itself needs to suppress compositor submit, not only skip our presentation texture recreate.

Try table:

| Try | Direction / Mode | Key timestamps | Visible result | Result / conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | Interior/full-size -> exterior render-scale | Loading opened `14:49:07.237`; render-scale request while loading `14:49:08.485`; loading closed `14:49:15.098`; relatch queued `14:49:16.002` closeAge `9` with `0/120`; first relatch apply `14:49:18.715` closeAge `129` deferred; final apply `14:49:20.908` closeAge `249`; D3D complete and recovery armed `14:49:22.164`; submit-stage presentation skip `14:49:22.476` closeAge `249`; log stops | crash | One-frame texture-recreate skip is insufficient by itself. Need to suppress OpenVR submit on relatch frame. |

Timing/statistics handling:

- Do not add this to the 60-frame table because the log clearly shows `relatchAge=0/120`.
- Do not merge this into clean success averages because the transition crashed.
- Keep the crash marker: final relatch apply at closeAge `249`, D3D complete at closeAge `249`, final line at closeAge `249` after the skip.

Code changes made from this log:

- `src/Features/Upscaling.cpp`: restored `kVRRenderScaleRelatchRecentTransitionFrames` to `120` for controlled crash validation.
- `src/Features/Upscaling.cpp` / `src/Features/Upscaling.h`: added `ShouldSuppressVRCompositorSubmitForRenderScaleRelatchFrame()`.
- `src/Features/VR/InSceneOverlay.cpp`: after `SubmitVRUpscaledFrame(...)` returns false, the submit hook now returns `VRCompositorError_None` on the relatch frame instead of falling through to the original OpenVR submit.

Conclusion:

- Next test should be a 120-frame crash-validation run, not a 60-frame timing run.
- Expected successful log signature: existing `Skipping submit-stage presentation texture recreate...` followed by new `Suppressing OpenVR compositor submit...`, then a normal next-frame recovery log instead of a stop.
- If this removes the crash, the 60-frame timing trial can be reopened as a separate table later.

## Log 012 - 2026-06-01 15:06:05 60-Frame Stability Pass

File timestamp: `2026-06-01 15:06:05`.

Log time range: relevant transition sequence from `15:04:02.228` through `15:06:05.067`.

Build/change context: first real `60`-frame recent-loading trial with relatch-frame OpenVR compositor-submit suppression active. The log contains `relatchAge=0/60` for recent-loading render-scale relatches.

Summary:

- User result: no crashes across multiple normal and short in/out transitions.
- Log result: relatch-frame compositor-submit suppression is validated in this run. The suppress line appears on completed relatch frames and the next frame logs normally.
- Render-scale entries used `0/60` and completed without crash. Some interior/full-size exits used shorter `0/20` or `0/6` contexts depending on whether they were no longer considered recent-loading render-scale relatches; these did not crash.
- No device-lost signature appears in the transition diagnostics.
- Black square was not evaluated in this stability-focused result.

Try table:

| Try | Direction / Mode | Key timestamps | Visible result | Result / conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | Interior/full-size -> exterior render-scale | close `15:04:02.894`; relatch queued closeAge `33`; first apply `93`; final apply/D3D closeAge `153`; suppress closeAge `153`; guards exit closeAge `173` | no crash | 60-frame exterior entry stable. |
| Try 002 | Exterior/render-scale -> interior/full-size | close `15:04:12.816`; relatch queued `46`; final apply/D3D `166`; suppress `166`; guards exit `176` | no crash | Interior exit stable. |
| Try 003 | Interior/full-size -> exterior render-scale | close `15:04:23.463`; relatch queued `23`; final apply/D3D `143`; suppress `143`; guards exit `163` | no crash | Exterior entry stable. |
| Try 004 | Exterior/render-scale -> interior/full-size | close `15:04:39.519`; relatch queued `64` using `0/20`; final apply/D3D `104`; suppress `104`; guards exit `114` | no crash | Interior exit stable outside the 60-frame recent-loading path. |
| Try 005 | Interior/full-size -> exterior render-scale | close `15:04:50.347`; relatch queued `30`; final apply/D3D `150`; suppress `150`; guards exit `170` | no crash | Exterior entry stable. |
| Try 006 | Exterior/render-scale -> interior/full-size | close `15:05:01.487`; relatch queued `70` using `0/6`; final apply/D3D `82`; suppress `82`; guards exit `92` | no crash | Short-context interior exit stable. |
| Try 007 | Interior/full-size -> exterior render-scale | close `15:05:13.767`; relatch queued `15`; final apply/D3D `135`; suppress `135`; guards exit `155` | no crash | Exterior entry stable. |
| Try 008 | Exterior/render-scale -> interior/full-size | close `15:05:19.491`; relatch queued `50`; final apply/D3D `170`; suppress `170`; guards exit `180` | no crash | Interior exit stable. |
| Try 009 | Rapid overlap / interior-full-size outcome | close `15:05:30.414`; no clean queue marker; final apply/D3D `63`; suppress `63`; guards exit `83` | no crash | Rapid overlap completed; keep separate from clean averages. |
| Try 010 | Interior/full-size -> exterior render-scale | close `15:05:36.621`; relatch queued `38`; final apply/D3D `158`; suppress `158`; guards exit `178` | no crash | Exterior entry stable. |
| Try 011 | Exterior/render-scale -> interior/full-size | close `15:05:43.373`; relatch queued `73` using `0/6`; final apply/D3D `85`; suppress `85`; guards exit `95` | no crash | Short-context interior exit stable. |
| Try 012 | Interior/full-size -> exterior render-scale | close `15:05:51.542`; relatch queued `24`; final apply/D3D `144`; suppress `144`; guards exit `164` | no crash | Exterior entry stable. |
| Try 013 | Exterior/render-scale -> interior/full-size | close `15:05:59.707`; relatch queued `36`; final apply/D3D `156`; suppress `156`; guards exit `166` | no crash | Interior exit stable. |

Timing/statistics handling:

- Added clean non-rapid samples to the 60-frame table only.
- Kept Try 009 separate from grouped clean averages because it is a rapid-overlap case.
- 60-frame table is now closed because the next requested test changes `kVRRenderScaleRelatchRecentTransitionFrames` to `6`.

Code changes made after this log:

- `src/Features/Upscaling.cpp`: changed `kVRRenderScaleRelatchRecentTransitionFrames` from `60` to `6` for the next aggressive stability trial.

Conclusion:

- The compositor-submit suppression fix is meaningful: the old `Log 011` crash point is now followed by normal recovery.
- A `60`-frame recent-loading delay is stable in this first run with multiple normal and short transitions.
- The next test is a new 6-frame timing regime and must be recorded in the separate 6-frame table.

## Log 013 - 2026-06-01 15:19:06 6-Frame Rapid Stress Crash

Source files:

- `CommunityShaders.log` was partially captured live, then overwritten by the next game launch at `15:21`.
- `crash-2026-06-01-14-19-06.log` remains available and shows the crash stack.

User-visible result:

- Normal in/out transitions worked fine.
- Crash was reproduced only after multiple rapid fast in/out rounds, likely while hitting a transition directly during D3D/render-target relatch.

Captured transition tail:

| Marker | Timestamp | Frame / closeAge | Meaning |
| --- | --- | --- | --- |
| Relatch attempt | `15:19:05.551` | frame `12495`, closeAge `218`, `relatchAge=0/6` | Render-target relatch applied; vendor teardown deferred because submit-stage reset was not ready. |
| Vendor reset | `15:19:05.557` | frame `12495` | Submit-stage DLSS reset ran and rebuilt DLSS. |
| Relatch attempt | `15:19:05.609` | frame `12501`, closeAge `224`, `relatchAge=0/6` | Second 6-frame retry; vendor teardown again deferred because submit-stage reset was not ready. |
| Vendor reset | `15:19:05.620` | frame `12501` | Submit-stage DLSS reset ran and rebuilt DLSS. |
| Relatch attempt | `15:19:05.671` | frame `12507`, closeAge `230`, `relatchAge=0/6` | Third 6-frame retry. Vendor teardown completed, then D3D render-target recreate was called. |
| D3D failure | `15:19:05.844` | frame `12513`, closeAge `236` | `Render-target relatch failed: Failure with HRESULT of 8007000E`. |
| Crash stack | `15:19:06` | n/a | Crash in `Upscaling::CreateUpscalingTextureResources`, called via `Upscaling::CheckResources`; stack contains `DX::com_exception` and `8007000E`. |

Interpretation:

- This was a rapid-reversal stress failure, not a normal-transition failure.
- The risky pattern is not just "D3D relatch happened too early after loading close"; it is "failed or busy relatch attempts kept retrying every 6 frames." That created repeated DLSS reset/rebuild work and then a D3D/common-texture recreate failure.
- The relatch exception was logged, but after the failed relatch the normal `CheckResources` path could still see missing common vendor textures and try to recreate them immediately. That second path is what the crash stack points at.

Code changes made after this log:

- `src/Features/Upscaling.cpp`: added `kVRRenderScaleRelatchBusyRetryFrames = 60`.
- `src/Features/Upscaling.cpp`: relatch retries after vendor-resource-busy deferral, D3D recreate retry/failure, or relatch exceptions now wait at least `60` frames instead of repeating every `6`.
- `src/Features/Upscaling.cpp`: missing common vendor texture recreation is deferred while a VR render-scale relatch/reset is pending.
- `src/Features/Upscaling.cpp`: if common texture recreation still fails during a VR render-scale path, the code requeues relatch/reset work and logs the deferral instead of allowing the exception to escape.

Conclusion:

- The 6-frame initial recent-loading delay was later reduced to 3 frames to reduce double-pause risk.
- Do not keep the short first-attempt delay as the retry cadence after a busy/failing relatch attempt. The retry path now uses 60 frames as a stability backoff.
- Next validation should repeat both normal in/out transitions and rapid in/out stress. Success means no crash, no repeated 3-frame relatch loop after a busy resource deferral, and recovery after any D3D/common texture recreate failure.

## Log 014 - 2026-06-01 15:24:25 Pre-Backoff 6-Frame Rapid Stress Repeat

Source files:

- `CommunityShaders.log` timestamp `2026-06-01 15:24:25`
- `crash-2026-06-01-14-24-25.log`
- `CrashLogger.log`

Build state:

- Same old test build as `Log 013`; it does not include the later busy-retry backoff or common-texture recreation deferral.
- Do not treat this as validation of the current code. Treat it as a fuller reproduction of the old failure.

User-visible result:

- Same test pattern as `Log 013`.
- Normal in/out transitions worked, then rapid repeated in/out stress crashed again.

Crash stack:

| Source | Evidence |
| --- | --- |
| Crash log | `CreateUpscalingTextureResources` at `Upscaling.cpp:4350` |
| Crash log | called from `CheckResources` at `Upscaling.cpp:5489` |
| Crash log | `DX::com_exception` with `HRESULT 8007000E` |
| CrashLogger.log | PDB resolution confirms the same two source lines |

Final transition sequence:

| Marker | Timestamp | Frame / closeAge | Meaning |
| --- | --- | --- | --- |
| Loading menu opened | `15:24:21.597` | frame `13239` | Final rapid exterior/interior leg begins. |
| Loading menu closed | `15:24:22.213` | frame `13308`, closeAge `0` | Recent-loading context starts. |
| Render-scale change / relatch queued | `15:24:22.894` | frame `13322`, closeAge `14`, `relatchAge=0/6` | Render-scale entry is queued very soon after close. |
| Initial DLSS submit failure | `15:24:22.930` | frame `13322` | `slEvaluateFeature failed` for both eyes; stretch fallback used for that frame. |
| First relatch apply | `15:24:23.035` | frame `13328`, closeAge `20`, `relatchAge=0/6` | Relatch attempts begin as soon as grace/6-frame delay clear. |
| First vendor-busy deferral | `15:24:23.035` | frame `13328`, closeAge `20` | Submit-stage DLSS resources not idle; relatch requeued. |
| Intermediate allocation failures | `15:24:23.064` -> `15:24:23.934` | closeAge `20` -> `62` | 48 failures creating VR intermediates, all `8007000E`. |
| Foveated allocation failure | `15:24:23.700` | frame `13358`, closeAge `50` | Periphery TAA history texture allocation failed with `8007000E`. |
| Repeated relatch loop | `15:24:23.035` -> `15:24:25.807` | frames `13328` -> `13448`, closeAge `20` -> `140` | 21 relatch attempts at the old `0/6` cadence. |
| Vendor-busy deferrals | `15:24:23.035` -> `15:24:25.613` | frames `13328` -> `13442`, closeAge `20` -> `134` | 20 relatch attempts deferred because vendor resources were still in use. |
| Streamline constant failure | `15:24:25.805` | before final D3D recreate | `Could not set constants for eye 1`. |
| Final D3D recreate call | `15:24:25.811` | frame `13448`, closeAge `140` | Vendor teardown finally completes; D3D render-target recreate begins. |
| D3D recreate failure | `15:24:25.862` | frame `13448`, closeAge `140` | `Render-target relatch failed: Failure with HRESULT of 8007000E`. |
| Crash | `15:24:25`/`15:24:26` | immediately after failed relatch | Normal `CheckResources` recreates missing common resources and throws at `CreateUpscalingTextureResources`. |

Interpretation:

- This is the same root failure as `Log 013`, but the full log proves the build was already in allocation failure before the final D3D recreate.
- The old code did not just relatch "too early" once. It repeatedly cycled: relatch attempt -> vendor-busy deferral -> submit-stage DLSS reset/rebuild -> intermediate allocation pressure -> relatch attempt again 6 frames later.
- `8007000E` appears in three places before the crash: submit-stage intermediates, foveated/Periphery TAA allocation, and final D3D render-target recreate. The crash then occurs through `CheckResources` common texture recreation.
- The current local fix directly targets this pattern: the first attempt is now being tested at 3 frames, but after any busy/failing relatch it should requeue with a 60-frame backoff and prevent `CheckResources` from recreating missing common vendor textures while relatch/reset is pending.

Conclusion:

- `Log 014` strengthens, rather than changes, the `Log 013` conclusion.
- The next meaningful validation is with the current local code, not another old-build repeat.
- Success criteria for the next run: normal transitions still work; rapid stress does not show relatch attempts every 3 frames after a vendor-busy deferral; intermediate `8007000E` failures do not escalate into `CheckResources -> CreateUpscalingTextureResources` crash.

Code changes made after this log:

- `src/Features/Upscaling.cpp`: changed `kVRRenderScaleRelatchRecentTransitionFrames` from `6` to `3`.
- The `60`-frame busy retry backoff remains in place. This is intended to avoid a second visible post-door pause on normal transitions while still backing off hard if vendor resources or D3D allocations are not ready.

## Log 015 - 2026-06-01 15:44:47 Current Backoff Stress Crash

Source files:

- `crash-2026-06-01-14-44-47.log`
- `CommunityShaders.log` was inspected while it still contained the `15:44` crash run, but the file was later overwritten by a separate run ending at `15:53:48`. Do not use the current on-disk `CommunityShaders.log` as the paired transition log for this crash.

User-visible result:

- Multiple normal interior/exterior and exterior/interior transitions worked at the beginning.
- The end-of-run rapid stress test crashed.
- User suspected hitting a transition while D3D/render-scale work was still active.

Crash stack:

| Source | Evidence |
| --- | --- |
| Crash log | `SubsurfaceScattering::EnsureBlurHorizontalTemp` at `SubsurfaceScattering.cpp:459` |
| Crash log | called from `Deferred.cpp:715` during blended decal render hook |
| Crash log | `DX::com_exception`; GPU memory `14.97/22.83 GB` |

Final transition sequence captured before overwrite:

| Marker | Timestamp | Frame / closeAge | Meaning |
| --- | --- | --- | --- |
| Render-scale grace active | `15:44:43.364` | frame `15098`, closeAge `8`, graceRemaining `12` | Final rapid transition is still inside the fixed post-close guard. |
| Relatch queued | `15:44:43.513` | frame `15104`, closeAge `14`, `relatchAge=0/6` | Effective first delay is `6` because the 20-frame grace still has 6 frames left, despite the source trial being 3-frame recent-loading. |
| First relatch apply | `15:44:43.746` | frame `15110`, closeAge `20`, `relatchAge=0/6` | First apply happens as soon as the grace clears. |
| Vendor-busy deferral | `15:44:43.747` | frame `15110` | Submit-stage DLSS resources are not idle; relatch requeues with the new `60`-frame busy retry. |
| Intermediate allocation failures | `15:44:43.765` -> `15:44:46.072` | 240 failures | Submit-stage vendor intermediates repeatedly fail with `HRESULT 8007000E` while the relatch is pending on the 60-frame retry. |
| Retry relatch apply | `15:44:46.073` | frame `15230`, closeAge `140`, `relatchAge=0/60` | Confirms the 60-frame retry backoff engaged correctly. |
| Second vendor-busy deferral | `15:44:46.073` | frame `15230` | Vendor teardown is still not ready; relatch requeues again. |
| Submit-stage reset | `15:44:46.089` -> `15:44:46.105` | frame `15230` -> `15231` | DLSS rebuild is first deferred because Streamline resources are still in use, then rebuilt. |
| Foveated/periphery failure | `15:44:46.108` | frame `15231` | Periphery TAA resource creation fails with `8007000E`. |
| Streamline evaluation failure | `15:44:46.109` -> `15:44:46.111` | frame `15231` | `slEvaluateFeature failed` for both eyes. |
| Crash | `15:44:47` | n/a | Visible exception lands in SSS temp texture allocation, not directly in `Upscaling::CheckResources`. |

Interpretation:

- The 60-frame busy retry backoff did work: the crash was not another tight `0/6` retry loop.
- The remaining problem was that submit-stage still performed full vendor work while relatch was pending. That created 240 intermediate allocation failures during the period that was supposed to let resources become idle.
- The final crash moved to SSS because SSS was the next feature to allocate a render target under the same memory/RT pressure. Treat it as the visible victim, not the root render-scale transition mechanism.
- This also explains why the user asked whether weak systems or GPU-heavy areas could hit a similar issue: yes, a stressed render-target allocation window can expose the same pattern if we keep trying vendor allocations every frame while resources are already busy.

Later overwritten `CommunityShaders.log` note:

- The current on-disk log after overwrite has no paired crash, no intermediate `8007000E`, and no `slEvaluateFeature failed` lines.
- It still shows the same conceptual issue in a different form: repeated 60-frame relatch deferrals between `FSR` and `DLSS` while submit-stage runtime resets continue during `relatchPending=yes`.
- This supports the same fix: while render-scale relatch is pending, submit-stage should present/stretch and avoid vendor reset/rebuild until the relatch path gets a clean retry.

Code changes made after this log:

- `src/Features/Upscaling.cpp`: added render-scale pending-relatch submit-stage stretch fallback.
- `src/Features/Upscaling.cpp`: submit-stage now skips `CheckResources` and `ApplyPendingVendorRuntimeReset(...)` while that pending-relatch fallback is active.
- `src/Features/Upscaling.cpp`: added render-scale-only `8007000E` intermediate-allocation cooldown so a surprise allocation failure falls back for the same `60`-frame busy window instead of retrying every frame.
- No save/load routine was changed.

Conclusion:

- The next runtime test should check that rapid stress no longer produces submit-stage intermediate allocation spam during `relatchPending=yes`.
- A successful log should show `submit-stage stretch fallback: relatch pending` during the wait, then a later clean relatch/rebuild, with no repeated `8007000E` intermediate failures and no SSS allocation crash.

## Log 016 - 2026-06-01 15:54:32 CS Menu Switch FSR Pending Relatch

Source file:

- `CommunityShaders.log`, file timestamp `2026-06-01 15:54:32`, length `729199`.

Build/change context:

- CS menu switch test.
- Same local investigation line as `Log 015`; the log predates the runtime-active render-scale relevance fix recorded below.

User-visible result:

- Many CS menu changes were made before the final crash.
- User reports the important bug is not the crash itself: after switching to FSR and then changing settings within FSR, the relatch stayed pending until the end of the log.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| DLSS render-scale off case | `15:50:53.437` | `req=kDLSS runtime=kDLSS`, `renderScale 1 -> 0`, `perf 1 -> 1`, `renderScaleRelevant=yes` | DLSS stayed requested as `kDLSS`, so the old requested-method gate did not collapse in this path. |
| DLSS perf off follow-up | `15:50:54.171` | `method kDLSS -> kDLSS`, `renderScale 0 -> 0`, `perf 1 -> 0` | DLSS did not reproduce the `req=kNONE/runtime=kDLSS` stuck shape in this log. |
| FSR render-scale off trigger | `15:54:01.312` | frame `22966`, `Resource change: method kFSR -> kFSR`, `renderScale 1 -> 0`, `perf 1 -> 1`; snapshot says `req=kNONE runtime=kFSR`, `perfActive=yes`, `renderScaleRelevant=no`, `relatchPending=yes` | This is the first clear bad state. Requested method had fallen to `kNONE`, but runtime FSR was still active. The shared safety/diagnostic gate incorrectly treated render-scale as irrelevant. |
| FSR perf off follow-up | `15:54:03.540` | frame `23229`, `renderScale 0 -> 0`, `perf 1 -> 0`; snapshot still says `req=kNONE runtime=kFSR`, `perfActive=yes`, `relatchPending=yes` | Turning perf off in settings did not immediately stop the boot-latched runtime; relatch was still required. |
| First stuck relatch attempt | `15:54:04.378` | frame `23283`, relatch exits after `1013` frames and tries `method=kNONE`; FSR teardown says not idle and requeues `0/60` | Relatch target is the requested end state, but the active runtime FSR resources still need a safe idle window. |
| Submit-stage rebuild during pending relatch | `15:54:04.392` -> `15:54:04.400` | `Applying submit-stage vendor runtime reset: method=kFSR`; `Created 2 FSR3 contexts`; `Rebuilt submit-stage FSR resources after VR reset` | Submit-stage rebuilt FSR during the pending relatch, keeping the resources alive and preventing the relatch teardown from becoming idle. |
| Repeated stuck loop | `15:54:26.233`, `15:54:27.531`, `15:54:28.812` | repeated `req=kNONE runtime=kFSR`, `renderScaleRelevant=no`, FSR not idle, FSR rebuilt | The loop repeats every 60 frames instead of completing. |
| Final state | `15:54:32.025` | frame `25701`, `req=kNONE runtime=kFSR`, `perfActive=yes`, `relatchPending=yes`, `relatchAge=0/60` | Relatch is still pending at end of log. |

Interpretation:

- This is a render-scale gating logic bug, not an FSR-only conceptual rule.
- The old shared safety relevance check required the requested method to be render-scale eligible. That is wrong during teardown/exit because requested state can already be `kNONE` while the boot-latched runtime vendor is still active.
- FSR exposes the bug strongly because its teardown poll reports not idle, then submit-stage immediately recreates FSR contexts during the pending relatch.
- DLSS can hit the same class if a CS menu path produces `req=kNONE runtime=kDLSS perfActive=yes`. This specific log did not show that exact DLSS state; DLSS render-scale-off examples kept `req=kDLSS`, so the old gate stayed relevant there.

Code changes made after this log:

- `src/Features/Upscaling.cpp`: `IsVRRenderScaleTransitionSafetyRelevant(...)` now keeps render-scale safety relevant when the requested method is not vendor-eligible but `IsPerfModeActive()` is true and `GetRuntimeUpscaleMethod()` is DLSS or FSR.
- `src/Features/Upscaling.cpp`: pending VR upscaling transitions now block relatch application through the same runtime-aware safety predicate instead of only `IsRenderScaleMethodEligible(GetUpscaleMethod())`.

Conclusion:

- Expected next-log signature for this case: after `renderScale 1 -> 0` inside FSR, diagnostics should remain `renderScaleRelevant=yes` while `runtime=kFSR` and `perfActive=yes`.
- During `relatchPending=yes`, submit-stage should use the pending-relatch stretch fallback instead of rebuilding FSR contexts every 60 frames.
- Success means the relatch either completes cleanly to the requested non-render-scale state or requeues without rebuilding the active runtime vendor every retry.

## Log 017 - 2026-06-01 16:11:27 Hard-Stress D3D Relatch Crash

Source files:

- `crash-2026-06-01-15-11-27.log`, file timestamp `2026-06-01 16:11:27`, length `180`.
- `CommunityShaders.log` was inspected when it still contained the crash run, with file timestamp `2026-06-01 16:11:27` and length `2111040`. It was later overwritten by startup logs. The current on-disk `CommunityShaders.log` no longer contains the `16:11` transition tail and must not be used as the paired log for this crash.

Log time range used: `16:11:18.737` through `16:11:27.295`.

Build/change context:

- Current 3-frame first-attempt trial.
- `60`-frame busy retry backoff active.
- Pending-relatch submit-stage stretch fallback active.
- Runtime-aware render-scale relevance fix active for `req=kNONE` plus active runtime DLSS/FSR.

User-visible result:

- Latest version still crashed after a hard stress test with multiple rapid entries/exits.
- The supplied crash log is minimal and has no stack beyond the exception line.

Try table:

| Try | Target / setup | Key timings | Visible result | Conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | Hard rapid entry/exit stress, render-scale relevant | loading close `16:11:18.737`; relatch queued `16:11:19.438`; D3D failures `16:11:19.638`, `16:11:22.457`, `16:11:24.494`; final D3D call `16:11:27.295` | crash | Submit-stage fallback was active, but repeated D3D `8007000E` failures still retried too soon for this stress case. |

Crash evidence:

| Source | Evidence |
| --- | --- |
| Crash log | `EXCEPTION_ACCESS_VIOLATION` at `CommunityShaders.dll+00DB67B`, instruction `mov rax, [rcx]` |
| Transition log before overwrite | repeated D3D render-target recreate failures with `HRESULT 8007000E` before the final crash |
| Transition log before overwrite | pending-relatch submit-stage fallback lines were present, so submit-stage fallback was active |

Final transition sequence captured before overwrite:

| Marker | Timestamp | Frame / closeAge | Meaning |
| --- | --- | --- | --- |
| Loading menu closed | `16:11:18.737` | frame `15444`, closeAge `0` | Final hard-stress transition window starts. |
| Resource change / render-scale entry | `16:11:19.221` | frame `15454`, closeAge `10` | Render-scale relevance and pending VR/render-scale transition are active. |
| Relatch queued | `16:11:19.438` | frame `15460`, closeAge `16`, `relatchAge=0/6` | Effective first delay is still bounded by the 20-frame post-close grace. |
| First D3D relatch attempt | `16:11:19.601` | frame `15466`, closeAge `22` | Vendor teardown completed; D3D render-target recreate was called. |
| First D3D allocation failure | `16:11:19.638` | frame `15466` | `Render-target relatch failed: Failure with HRESULT of 8007000E`; relatch requeued on the old `60`-frame path. |
| Missing common texture recreate deferred | `16:11:19.638` | frame `15466` | `CheckResources` deferral worked; it did not immediately recreate missing common vendor textures. |
| Later relatch retry | `16:11:22.426` | frame `15586`, closeAge `142` | Another D3D recreate attempt begins. |
| Second D3D allocation failure | `16:11:22.457` | frame `15586` | Another `8007000E` failure. |
| Vendor-busy retry | `16:11:23.447` | frame `15646`, closeAge `202` | Vendor resources still busy; requeued for `60` frames. |
| Third D3D allocation failure | `16:11:24.440` -> `16:11:24.494` | frame `15706`, closeAge `262` | D3D recreate attempt again fails with `8007000E`. |
| Vendor-busy retry | `16:11:25.613` | frame `15766`, closeAge `322` | Vendor resources still busy; requeued for `60` frames. |
| Final D3D recreate call | `16:11:27.295` | frame `15826`, closeAge `382` | Vendor teardown completes and D3D recreate starts. The log stops before success/failure logging. |
| Crash | `16:11:27` | n/a | Access violation occurs during or immediately after that D3D recreate attempt. |

Interpretation:

- This is not the same as `Log 015`. The new pending-relatch submit-stage fallback was active, so the previous submit-stage reset/rebuild spam is not the remaining crash cause.
- The remaining crash path is repeated D3D render-target recreate attempts after real allocation failures. The log had multiple handled `8007000E` failures, then the next D3D recreate attempt access-violated before the normal failure log could run.
- The `60`-frame retry is appropriate for ordinary vendor-busy deferrals, but after an actual D3D allocation failure it is still too short under hard transition stress.
- The current startup `CommunityShaders.log` only has initialization lines and frame-0 diagnostics. It cannot update transition timing statistics.

Code changes made after this log:

- `src/Features/Upscaling.cpp`: added `kVRRenderScaleRelatchD3DFailureRetryFrames = 300`.
- `src/Features/Upscaling.cpp`: D3D out-of-memory exceptions during render-target relatch now requeue the relatch for at least `300` frames, instead of the normal `60`-frame busy retry.
- `src/Features/Upscaling.cpp`: pending-relatch fallback diagnostics were tightened so repeated eye submissions on the same frame do not spam the log.

Conclusion:

- The next validation should repeat the hard rapid entry/exit stress. Success means no crash after an `8007000E` D3D relatch failure, and the log should show `Relatch deferred after D3D allocation failure; retrying in at least 300 frames`.
- Normal no-failure transitions should not become slower because the 300-frame path is only for actual D3D allocation failures.

## Log 018 - 2026-06-01 16:20:18 CS Menu Switch Validation

Source file:

- Original live path: `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, file timestamp `2026-06-01 16:20:18`, length `2117244`.
- Preserved copy: `C:\Users\Win10\Desktop\CommunityShaders.log`, file timestamp `2026-06-01 16:20:18`, length `2117244`. Later analysis should use this copy, because the live SKSE path is overwritten on restart.

Log time range used: `16:17:26.337` through `16:20:09.990`.

Build/change context:

- CS menu switch test using the version that previously crashed or got stuck during CS menu switching.
- Runtime-aware render-scale relevance appears active.
- Pending-relatch submit-stage stretch fallback appears active.
- The log still contains per-frame pending-relatch fallback diagnostics, so this binary either predates the latest logging-throttle cleanup or did not include it.

Summary:

- No crash signature.
- No `8007000E`.
- No device-lost marker.
- No `renderScaleRelevant=no` plus `relatchPending=yes` bad state.
- DLSS and FSR render-scale on/off paths completed their relatches.
- FSR had long vendor-busy waits, but those waits resolved instead of getting stuck.

Preserved-copy counts:

| Check | Count |
| --- | ---: |
| Resource changes | `30` |
| Completed D3D render-target relatches | `8` |
| Vendor-busy relatch deferrals | `14` |
| Pending-relatch submit-stage fallback lines | `1943` |
| Stable full-eye recovery markers | `5` |
| Relatch-frame compositor-submit suppressions | `8` |
| D3D out-of-memory / `8007000E` | `0` |
| Device-lost markers | `0` |
| `renderScaleRelevant=no` plus `relatchPending=yes` | `0` |
| `req=kNONE runtime=kFSR` | `0` |
| Unhandled crash/exception markers | `0` |

CS resource changes extracted from preserved copy:

| Time | Method | Quality | DLSS preset | Render-scale | Perf | Relevant |
| --- | --- | --- | --- | --- | --- | --- |
| `16:17:26.337` | `kDLSS -> kDLSS` | `1 -> 1` | `3 -> 3` | `1 -> 0` | `1 -> 1` | yes |
| `16:17:26.392` | `kDLSS -> kDLSS` | `1 -> 1` | `3 -> 1` | `0 -> 0` | `1 -> 0` | yes |
| `16:17:38.677` | `kDLSS -> kDLSS` | `1 -> 1` | `1 -> 0` | `0 -> 0` | `0 -> 0` | no |
| `16:17:39.676` | `kDLSS -> kDLSS` | `1 -> 1` | `0 -> 4` | `0 -> 0` | `0 -> 0` | no |
| `16:17:40.292` | `kDLSS -> kDLSS` | `1 -> 1` | `4 -> 0` | `0 -> 0` | `0 -> 0` | no |
| `16:17:41.660` | `kDLSS -> kDLSS` | `1 -> 1` | `0 -> 1` | `0 -> 0` | `0 -> 0` | no |
| `16:17:48.781` | `kDLSS -> kDLSS` | `1 -> 5` | `1 -> 1` | `0 -> 0` | `0 -> 0` | no |
| `16:18:00.683` | `kDLSS -> kDLSS` | `5 -> 5` | `1 -> 1` | `0 -> 1` | `0 -> 0` | yes |
| `16:18:02.341` | `kDLSS -> kDLSS` | `5 -> 5` | `1 -> 1` | `1 -> 1` | `0 -> 1` | yes |
| `16:18:08.865` | `kDLSS -> kDLSS` | `5 -> 5` | `1 -> 0` | `1 -> 1` | `1 -> 1` | yes |
| `16:18:10.440` | `kDLSS -> kDLSS` | `5 -> 5` | `0 -> 4` | `1 -> 1` | `1 -> 1` | yes |
| `16:18:11.665` | `kDLSS -> kDLSS` | `5 -> 5` | `4 -> 2` | `1 -> 1` | `1 -> 1` | yes |
| `16:18:12.657` | `kDLSS -> kDLSS` | `5 -> 5` | `2 -> 1` | `1 -> 1` | `1 -> 1` | yes |
| `16:18:13.464` | `kDLSS -> kDLSS` | `5 -> 5` | `1 -> 2` | `1 -> 1` | `1 -> 1` | yes |
| `16:18:15.148` | `kDLSS -> kDLSS` | `5 -> 5` | `2 -> 1` | `1 -> 1` | `1 -> 1` | yes |
| `16:18:15.746` | `kDLSS -> kDLSS` | `5 -> 5` | `1 -> 0` | `1 -> 1` | `1 -> 1` | yes |
| `16:18:16.647` | `kDLSS -> kDLSS` | `5 -> 5` | `0 -> 1` | `1 -> 1` | `1 -> 1` | yes |
| `16:18:22.322` | `kDLSS -> kDLSS` | `5 -> 3` | `1 -> 1` | `1 -> 1` | `1 -> 1` | yes |
| `16:18:26.755` | `kDLSS -> kDLSS` | `3 -> 3` | `1 -> 1` | `1 -> 0` | `1 -> 1` | yes |
| `16:18:28.821` | `kDLSS -> kDLSS` | `3 -> 3` | `1 -> 1` | `0 -> 0` | `1 -> 0` | yes |
| `16:18:31.497` | `kDLSS -> kNONE` | `3 -> 3` | `1 -> 1` | `0 -> 0` | `0 -> 0` | no |
| `16:18:41.493` | `kTAA -> kFSR` | `3 -> 3` | `1 -> 1` | `0 -> 0` | `0 -> 0` | no |
| `16:18:43.077` | `kFSR -> kFSR` | `3 -> 3` | `1 -> 1` | `0 -> 1` | `0 -> 0` | yes |
| `16:18:43.818` | `kFSR -> kFSR` | `3 -> 3` | `1 -> 1` | `1 -> 1` | `0 -> 1` | yes |
| `16:19:02.504` | `kFSR -> kFSR` | `3 -> 1` | `1 -> 1` | `1 -> 1` | `1 -> 1` | yes |
| `16:19:45.404` | `kFSR -> kFSR` | `1 -> 1` | `1 -> 3` | `1 -> 0` | `1 -> 1` | yes |
| `16:19:45.445` | `kFSR -> kFSR` | `1 -> 1` | `3 -> 3` | `0 -> 0` | `1 -> 0` | yes |
| `16:19:50.396` | `kFSR -> kFSR` | `1 -> 0` | `3 -> 3` | `0 -> 0` | `0 -> 0` | no |
| `16:19:52.243` | `kFSR -> kFSR` | `0 -> 1` | `3 -> 3` | `0 -> 1` | `0 -> 1` | yes |

The startup boot change at `16:15:43.642` (`kTAA -> kDLSS`, render-scale/perf already on) is excluded from the CS-menu table above.

Completed D3D render-target relatches extracted from preserved copy:

| Time | Frame | Request / runtime | Quality | Screen | Render-scale | Perf | Meaning |
| --- | ---: | --- | ---: | --- | --- | --- | --- |
| `16:17:34.601` | `12126` | `kDLSS / kDLSS` | `1` | `4936x2740` | no | no | DLSS render-scale/perf off completed. |
| `16:18:05.138` | `15201` | `kDLSS / kDLSS` | `5` | `2468x1370` | yes | yes | DLSS render-scale/perf on completed. |
| `16:18:22.438` | `16909` | `kDLSS / kDLSS` | `3` | `3290x1826` | yes | yes | DLSS active render-scale quality change completed. |
| `16:18:31.611` | `17732` | `kNONE / kNONE` | `3` | `4936x2740` | no | no | DLSS exit to none completed. |
| `16:18:48.862` | `19150` | `kFSR / kFSR` | `3` | `3290x1826` | yes | yes | FSR render-scale/perf on completed. |
| `16:19:02.616` | `20472` | `kFSR / kFSR` | `1` | `4194x2329` | yes | yes | FSR quality/size relatch completed; quality changed during an already pending relatch. |
| `16:19:50.533` | `25447` | `kFSR / kFSR` | `0` | `4936x2740` | no | no | FSR render-scale/perf off completed. |
| `16:19:58.249` | `25965` | `kFSR / kFSR` | `1` | `4194x2329` | yes | yes | FSR render-scale/perf back on completed. |

Vendor/reset wait observations:

| Path | Wait evidence | Interpretation |
| --- | --- | --- |
| DLSS render-scale off / perf off | pending relatch `16:17:26.511` -> `16:17:33.386` (`681` frames), D3D complete `16:17:34.601` | Long because of initial post-load vendor state, but it exited cleanly. |
| DLSS render-scale/perf on | vendor reset `16:18:02.729` -> `16:18:05.138` (`60` frames) | Normal one-retry vendor-busy wait. |
| DLSS active render-scale quality change | vendor reset `16:18:19.995` -> `16:18:22.438` (`60` frames) | Normal one-retry wait. |
| DLSS exit to none | vendor reset `16:18:29.226` -> `16:18:31.497` (`60` frames), D3D complete `16:18:31.611` | Runtime-aware relevance kept the transition protected until runtime became none. |
| FSR render-scale/perf on | FSR defer `16:18:43.077` -> `16:18:48.896` (`289` frames), vendor reset `180` frames | Longer FSR idle/resource wait, but it resolved. |
| FSR active quality/size relatch | FSR defer `16:18:56.150` -> `16:19:02.646` (`453` frames), vendor reset `180` frames | Quality `3 -> 1` at `16:19:02.504` was folded into the pending relatch. |
| FSR render-scale/perf off | FSR defer `16:19:45.404` -> `16:19:50.396` (`191` frames), vendor reset `183` frames | Previous stuck class now completes. |
| FSR render-scale/perf back on | FSR defer `16:19:52.237` -> `16:19:58.278` (`422` frames), vendor reset `422` frames | Long but clean completion. |

Try table:

| Try | Target / setup | Key timings | Visible result | Conclusion |
| --- | --- | --- | --- | --- |
| Try 001 | DLSS render-scale off / perf off from CS menu | resource changes `16:17:26.337` and `16:17:26.392`; relatch queued `16:17:26.391`; first apply/defer `16:17:26.510`; D3D complete/applied `16:17:34.601` | no issue reported | Completed. Initial post-load vendor state caused a long pending period, but it did not crash or remain stuck. |
| Try 002 | DLSS render-scale/perf back on | render-scale on `16:18:00.683`; perf on `16:18:02.341`; D3D complete `16:18:05.132`; applied `16:18:05.138`; stable full-eye recovery `16:18:05.396` | no issue reported | Completed with normal short post-D3D recovery. |
| Try 003 | DLSS active render-scale preset/quality changes | preset changes `16:18:08.865` through `16:18:16.647`; quality change `16:18:22.322`; D3D complete `16:18:22.437`; applied `16:18:22.438`; recovery `16:18:22.668` | no issue reported | DLSS submit-stage resets and the quality relatch completed. |
| Try 004 | DLSS exit to non-vendor / none | render-scale/perf off before this at `16:18:26.755` and `16:18:28.821`; `method kDLSS -> kNONE` at `16:18:31.497`; D3D complete `16:18:31.611`; applied `16:18:31.611` | no issue reported | Runtime-aware relevance handled `req=kNONE runtime=kDLSS` until teardown completed. |
| Try 005 | TAA/non-render-scale to FSR, then FSR render-scale/perf on | `kTAA -> kFSR` non-render-scale at `16:18:41.493`; render-scale on `16:18:43.077`; perf on `16:18:43.818`; first apply/defer `16:18:44.186`; final D3D complete `16:18:48.861`; applied `16:18:48.862`; recovery `16:18:49.056` | no issue reported | Completed. FSR defer lasted `289` frames and vendor reset lasted `180` frames; no crash or stuck pending latch. |
| Try 006 | FSR quality/size relatch while render-scale active | pending VR/render-scale state starts `16:18:56.150`; relatch queued `16:18:58.258`; repeated 60-frame deferrals through `16:19:01.402`; quality `3 -> 1` arrives during the pending relatch at `16:19:02.504`; D3D complete `16:19:02.615`; applied `16:19:02.616`; recovery `16:19:02.804` | no issue reported | Completed. The late quality change was folded into the in-progress pending relatch; no allocation failure or stuck FSR state. |
| Try 007 | FSR render-scale off / perf off from CS menu | render-scale off `16:19:45.404`; perf off `16:19:45.445`; relatch first apply/defer `16:19:45.574`; D3D complete/applied `16:19:50.533`; vendor reset exited `16:19:50.661` | no issue reported | This is the important previous failure class. It stayed `renderScaleRelevant=yes`, used fallback during the busy period, and completed. |
| Try 008 | FSR render-scale/perf back on | resource change `16:19:52.243`; relatch first apply/defer `16:19:55.984`; final apply `16:19:57.015`; D3D complete `16:19:58.248`; applied `16:19:58.249`; recovery `16:19:58.479` | no issue reported | Completed. FSR runtime defer lasted `422` frames, but exited cleanly. |
| Try 009 | FSR vendor reset after later loading/menu close | loading close `16:20:08.204`; FSR reset applied `16:20:09.576`; FSR rebuild `16:20:09.638`; guards exit `16:20:09.990` | no issue reported | Post-menu FSR reset completed quickly and final state was clear. |

Failure-signature checks:

- Search for `8007000E`, D3D out-of-memory, device-lost, unhandled exception, or crash returned no relevant transition failures.
- Search for `renderScaleRelevant=no.*relatchPending=yes` returned no matches.
- Search for `req=kNONE runtime=kFSR` returned no matches.
- The log has `1943` `Using submit-stage stretch fallback while render-target relatch is pending` lines, so diagnostic spam was still present in this tested binary. This is a logging hygiene issue, not a runtime failure in this log.

Interpretation:

- CS menu switching now behaves correctly in both directions covered here.
- The earlier FSR stuck-relatch bug is specifically improved: the FSR render-scale-off path at `16:19:45` no longer loses render-scale relevance while runtime FSR is still active.
- Long FSR waits are caused by resources not becoming idle immediately. The fallback path is doing the right thing: it keeps presenting/stretching, avoids crashing, and retries until teardown can safely proceed.
- No code change is justified from this log.

Conclusion:

- Treat this as a successful CS-menu switch validation.
- The remaining stability priority is still hard rapid entry/exit stress after D3D allocation failure, not ordinary CS-menu switching.

## 2026-06-01 - Post-Commit Review of `9c4ec8de1`

Scope reviewed:

- Commit `9c4ec8de1 fix(rendersize-mode): harden VR relatch recovery`.
- Files reviewed: `src/Features/Upscaling.cpp`, `src/Features/Upscaling.h`, `src/Features/VR/InSceneOverlay.cpp`.

Finding:

- The commit was correctly scoped to VR render-scale relatch/recovery safety.
- The concrete correctness gap was the order inside `SubmitVRUpscaledFrame(...)`: the desktop-mirror reuse fast path could return a prepared submit-stage texture on the same frame that `MarkVRRenderScaleRelatchSettling(...)` armed the one-frame relatch skip. That could bypass the later stretch-fallback skip and prevent `InSceneOverlay` from reaching `ShouldSuppressVRCompositorSubmitForRenderScaleRelatchFrame()`.

Code change made after this review:

- `src/Features/Upscaling.cpp`: moved the relatch-frame submit-stage skip to the front of `SubmitVRUpscaledFrame(...)`, immediately after basic state/context validation and before runtime-plan refresh, desktop-mirror reuse, resource checks, vendor resets, or presentation texture creation.
- `src/Features/Upscaling.cpp`: added `LogVRRenderScaleRelatchSubmitStagePresentationSkip(...)` so the skip logging is centralized instead of duplicated near the stretch fallback.

Rationale:

- The safety rule is frame-based, not path-based: if the current frame is the render-target relatch frame, submit-stage must not hand OpenVR a just-rebuilt texture through any fast path.
- This does not alter transition delays, FSR/DLSS teardown, D3D retry timing, save/load behavior, or the black-square investigation.

Expected validation:

- On a relatch frame, `SubmitVRUpscaledFrame(...)` should return `false` before submit-stage texture reuse/allocation.
- `InSceneOverlay` should then log/suppress the compositor submit for that same frame and return `VRCompositorError_None`.
- Later frames continue through the existing stretch/full-eye recovery path.

## Log 019 - 2026-06-01 17:23:29 User Crash From `9c4ec8de1`

Source file:

- `D:\FireFox-downloads\CommunityShaders(2)\CommunityShaders.log`, file timestamp `2026-06-01 17:23:29`, length `41416374`.

Build/change context:

- User reports this build was compiled from `9c4ec8de129ac12c485b7844749601d5087c3f0b`.
- Therefore this log does not include the later `15062011e` / amended `925aa20a2` submit-guard changes.
- Runtime path is OpenComposite.

Summary:

- Crash happens during a CS-menu DLSS render-scale relatch, not an interior/exterior transition.
- No `8007000E`, no explicit D3D device-lost marker, and no logged exception before the log stops.
- Final logged state is immediately after D3D render-target relatch completion, vendor recreation, post-relatch recovery arming, same-frame submit-stage skip, and same-frame OpenVR compositor submit suppression.

Final sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- |
| CS menu resource change | `18:23:16.780` | frame `19922`, `kDLSS -> kDLSS`, quality `3 -> 4`, renderScale `1 -> 1`, perf `0 -> 1`, relevant yes | User switches DLSS render-scale/perf state from CS menu. |
| Relatch queued | `18:23:16.780` | frame `19922`, `relatchAge=0/20`, safety `20` | Menu-stable delay path queues D3D relatch. |
| First relatch apply | `18:23:17.840` | frame `19942`, closeAge `7350` | Apply starts after 20 frames. |
| Vendor busy defer | `18:23:17.840` | frame `19942`, requeued `0/60` | DLSS resources are still in use; relatch is safely deferred. |
| Second relatch apply | `18:23:21.067` | frame `20002`, `relatchAge=0/60` | Retry begins after 60 frames. |
| D3D recreate call | `18:23:21.067` | frame `20002` | `Hooks::RecreateRenderTargets()` begins. |
| D3D recreate complete | `18:23:28.825` | frame `20002` | D3D relatch completes after a long wall-clock stall on the same frame; shader/material compilation is interleaved in the log. |
| Vendor resources recreated | `18:23:28.826` | frame `20002`, runtime/request `kDLSS`, quality `4`, screen `2988x1385` | DLSS/common resources recreate successfully. |
| Recovery armed | `18:23:28.826` | stretch `10`, full-eye/foveated bypass up to `20` | Post-D3D recovery is armed. |
| Same-frame submit skip | `18:23:29.046` | frame `20002` | Submit-stage presentation texture recreate is skipped on relatch frame. |
| Same-frame compositor suppression | `18:23:29.046` | frame `20002` | OpenVR compositor submit is suppressed on relatch frame. |
| Log ends | `18:23:29.046` | frame `20002` | Crash or hard stop occurs immediately after same-frame suppression. |

Interpretation:

- This crash is not evidence against the amended `925aa20a2` behavior, because the tested binary predates it.
- The crash still occurred even though `9c4ec8de1` logged same-frame submit skip and compositor suppression. That means the older same-frame-only guard was not sufficient for this user path, or the crash happened immediately after that guard while the relatch frame was still being processed by OpenComposite/SteamVR.
- The later `925aa20a2` change is directly relevant because it moves the submit-stage skip before fast-path reuse and adds a one-frame fallback when the original relatch-frame skip/suppression did not already run.
- However, this log does not prove that `925aa20a2` fixes the crash, because the final crash signature has no post-frame evidence. The next user test must use a build at or after `925aa20a2`.

Conclusion:

- Treat this as a crash reproduction for `9c4ec8de1`, not current HEAD.
- Retest the same CS-menu sequence on `925aa20a2` or later: DLSS render-scale active, quality `3 -> 4`, perf `0 -> 1`.
- If it still crashes on the amended build, the next likely mitigation is to extend relatch-frame compositor suppression beyond one missed follow-up frame or avoid doing vendor/common resource recreation and OpenVR submit work in the same rendered frame after a long `RecreateRenderTargets()` stall.

## Log 020 - 2026-06-01 18:23:49 User Exterior Load, Reported Stretch-Looking Image

Source file:

- `D:\FireFox-downloads\CommunityShaders(9).log`, file timestamp `2026-06-01 18:23:49`, length `9342971`.

Build/change context:

- User reports load into exterior where the image probably stayed in stretch mode forever.
- Log starts with render-scale FSR active at boot. Runtime reports `method=kFSR`, quality `1`, display `4936x2604`, render `4194x2213`, final `4936x2604`.

Summary:

- The log does not show a post-D3D relatch stretch state.
- There is no `Applied render-target relatch`, no `settling=yes`, no `stretch=yes`, no same-frame submit skip, and no OpenVR suppression.
- The long visible degraded/stretched period most likely corresponds to FSR/vendor runtime reset defer after exterior load, not the post-relatch stretch fallback.
- The reset eventually exits cleanly at frame `4661`.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- |
| Boot resource change | `20:17:29.211` | frame `0`, `kTAA -> kFSR`, renderScale/perf `1/1`, `req=kDLSS runtime=kFSR` | Startup state is already mixed requested/runtime, but render-scale safety remains relevant. |
| Loading close | `20:18:01.112` | frame `2404`, closeAge `0`, grace `20` | Exterior load transition closes. |
| FSR defer enters | `20:18:01.112` | frame `2404`, `fsrRuntimeDefer=yes`, `pendingReset(FSR=yes)` | FSR runtime reset is queued after loading. |
| Vendor reset pending enters | `20:18:01.330` | frame `2424`, closeAge `20` | Reset begins after transition grace. |
| Second loading close/save-load context | `20:18:25.348` | frame `4537`, `saveLoad=yes`, postLoadReset yes | Another loading/save-load signal occurs while reset is still pending. |
| FSR teardown deferred | `20:18:27.547` | frame `4656`, closeAge `119` | FSR resources are not idle yet. |
| Submit-stage DLSS cleanup deferred | `20:18:27.562` | frame `4657`, `pendingReset(DLSS=yes,FSR=yes)` | Inactive DLSS resources also need cleanup before reset fully completes. |
| Vendor resources recreated | `20:18:27.588` | frame `4659`, method `kFSR` | FSR resources are recreated. |
| Submit-stage FSR rebuild | `20:18:27.652` | frame `4660/4661` area | FSR submit-stage resources rebuild after VR reset. |
| Reset exits | `20:18:27.760` | frame `4661`, FSR defer `2257` frames, vendor reset `2237` frames | Final state is clear: `fsrRuntimeDefer=no`, `vendorPending=no`, `pendingReset=no`, `settling=no`, `stretch=no`. |

Interpretation:

- The user's "stretch forever" observation is not backed by the relatch recovery flags. The transition was not stuck in the explicit post-D3D stretch fallback.
- It was stuck visually, if at all, in a vendor reset/defer phase for about `2257` frames. During this time the diagnostics show no relatch pending/in-progress and no post-relatch recovery. This points to FSR runtime reset and inactive DLSS cleanup, not the relatch stretch window.
- The final state is clean at `20:18:27.760`, but the user may have judged the image during the long pending period or the visible image may not have refreshed even though the internal flags cleared.
- The startup state is suspicious: diagnostics show `req=kDLSS runtime=kFSR` while the resource change says `kTAA -> kFSR`. This mixed requested/runtime state may be normal during boot latch, but it should be watched in later logs if exterior-load visuals stay wrong.

Conclusion:

- Do not tune the `10`-frame stretch recovery based on this log; this was not a stretch-recovery sample.
- If this reproduces, improve logging around submit-stage presentation mode while `fsrRuntimeDefer=yes` and `vendorPending=yes`, because the current log tells us the reset is pending but not exactly what image source is being submitted during that long interval.
- For robustness, consider whether FSR post-load runtime reset should use an explicit bounded presentation fallback or a clearer "reset still pending" visual state, but do not change relatch D3D timing from this evidence alone.

## Log 021 - 2026-06-01 19:57:25 Current HEAD Interior/Exterior CTD

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, file timestamp `2026-06-01 19:57:25`, length `179061`.

Build/change context:

- User reports latest version after the pending-DLSS API commit and compile fix.
- Log still has relatch-frame OpenVR compositor-submit suppression enabled.
- No new CrashLogger crash file was produced for this run; the newest crash file in SKSE remained from `15:11:27`.

Summary:

- Immediate CTD on first interior -> exterior render-scale entry.
- Runtime path was `kFSR`, not DLSS, even though Streamline/DLSS was available at startup.
- This transition did not fail because the D3D relatch was too early: the first apply waited `20` frames, then a vendor-busy deferral requeued for `60` frames, and the final relatch happened at closeAge `89`.
- The final lines show same-frame submit-stage skip plus OpenVR compositor submit suppression, then one next-frame state line, then the log stops.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- |
| Boot latch | `19:56:08.366` | `kFSR`, quality `1`, display `2468x2740`, render `2097x2329` | Startup render-scale path is FSR. |
| Interior DLAA/quality 0 state active | before `19:57:12.702` | `req=kFSR runtime=kFSR quality=0`, renderScale/perf off | Interior profile is effectively non-render-scale. |
| Loading open | `19:57:12.702` | frame `6268`, quality `0` | Interior -> exterior transition begins. |
| API/render-scale pending seen | `19:57:16.742` | frame `6749`, pendingVR yes, pendingRenderScale yes | Exterior profile requests render-scale. |
| Loading close | `19:57:21.137` | frame `7277`, closeAge `0`, grace `20` | Render-scale transition grace starts. |
| Relatch queued | `19:57:21.961` | frame `7286`, closeAge `9`, quality `1`, relatchAge `0/20`, safety `20` | Exterior Hoshipa render-scale entry is queued. |
| First relatch apply | `19:57:22.406` | frame `7306`, closeAge `29` | First D3D relatch attempt begins. |
| Vendor-busy defer | `19:57:22.407` | frame `7306`, requeued `0/60`, `pendingReset(DLSS=yes,FSR=yes)` | Submit-stage DLSS resources were not idle. This is the expected 60-frame retry path. |
| Final relatch apply | `19:57:23.596` | frame `7366`, closeAge `89`, relatchAge `0/60` | Retry starts after 60 frames. |
| Vendor teardown complete | `19:57:23.597` | frame `7366` | FSR/common/periphery teardown completes. |
| D3D recreate call | `19:57:23.597` | frame `7366` | Render targets are recreated. |
| D3D recreate complete | `19:57:24.797` | frame `7366` | D3D relatch completes after a long same-frame stall. |
| Vendor/common recreate | `19:57:24.797` -> `19:57:24.798` | method `kFSR`, `recreateTemporal=no`, pendingReset still `FSR=yes` | Common resources recreated; FSR runtime reset remains pending for submit-stage recovery. |
| Recovery armed | `19:57:24.798` | stretch `10`, full-eye/foveated bypass up to `20` | Post-D3D recovery starts. |
| Same-frame submit skip | `19:57:25.121` | frame `7366`, closeAge `89` | Submit-stage presentation texture recreation is skipped on the relatch frame. |
| Same-frame OpenVR suppression | `19:57:25.121` | frame `7366`, closeAge `89` | Hook returns `VRCompositorError_None`; no original texture is submitted for that frame. |
| Last state | `19:57:25.125` | frame `7367`, settling yes, stretch `9`, `pendingReset(FSR=yes)` | One frame after suppression, FSR reset is still pending and recovery is active. |

Interpretation:

- This is the first log showing a crash with the newer same-frame suppression path in current testing.
- The important difference from the earlier successful idea is semantic: the guard should prevent CS from submitting a just-rebuilt upscaled/presentation texture, but returning `VRCompositorError_None` means OpenVR receives no texture for that eye/frame.
- In this run, the crash/hard stop occurs immediately around that guarded relatch-frame submit. The log does not show a D3D exception, device-lost marker, or allocation failure.
- The next safer mitigation is to keep the relatch-frame CS-upscaling bypass but forward the original Skyrim/OpenVR texture instead of suppressing compositor submit entirely.

Code change made after this log:

- `src/Features/Upscaling.h` / `src/Features/Upscaling.cpp`: renamed the public guard to `ShouldBypassVRCompositorUpscalingForRenderScaleRelatchFrame()`.
- `src/Features/VR/InSceneOverlay.cpp`: call the guard before `SubmitVRUpscaledFrame(...)`; when active, skip CS upscaled submit-stage work and overlay work, then fall through to the original `func(_this, eEye, pTexture, pBounds, nSubmitFlags)`.
- The new diagnostic line says CS upscaled OpenVR submit is bypassed and the original submit is forwarded.

Conclusion:

- Do not keep using `VRCompositorError_None` as the relatch-frame safety path unless a later log proves forwarding the original texture is worse.
- Stability priority remains D3D/submit safety. The black square remains cosmetic until transitions are stable.

## API Revision 2 - Explicit DLSS/FSR Selection

Code change:

- `include/VRAPI/CSinterface001.h`: added `CSInterfaceRevision002 = 2` and made `CSInterfaceRevision` point to revision 2. Revision 1 remains named as `CSInterfaceRevision001`.
- `include/VRAPI/CSinterface001.h`: appended `UpscaleMethod` plus `GetUpscaleMethod()`, `SetUpscaleMethod(...)`, and `SetVRUpscalingTransitionProfileForMethod(...)` to the end of `ICSInterface001`.
- `include/VRAPI/CSpluginapi.h`: bumped `CSBuildNumber` to `7`, implemented the revision-2 calls, and kept `GetApi(...)` accepting revision `1`, revision `2`, and `0` for latest.
- `src/VRAPI/CSinterface001.cpp`: `GetCSInterface001()` now prefers revision `2` and falls back to revision `1` when needed (caller must still gate revision-2 methods by build/revision checks).
- `API.md`: documented revision 2 and the new method-explicit transition call.

Reason:

- VR FPS Stabilizer's existing v6 config/API can set preset, DLSS profile, and render-scale mode, but it cannot force the CS upscaler method to DLSS. If CS is currently configured as FSR, `DLSSProfile + UpscalePreset` resolves through FSR and the DLSS profile is ignored.

Compatibility conclusion:

- Existing compiled v6/revision-1 consumers remain binary compatible because the vtable additions are append-only and the provider still accepts revision `1`.
- Legacy v6/revision-1 upscaling calls are DLSS-preferred on DLSS-capable systems. This preserves old NVIDIA/DLSS expectations from the legacy `DLSSMode`/`DLSSProfile` naming instead of accidentally resolving through persisted FSR settings.
- New consumers that need deterministic DLSS/FSR selection should compile against the revision-2 header and call `SetVRUpscalingTransitionProfileForMethod(...)`, for example `kDLSS + kNativeAA + kK` for DLAA/K interiors and `kDLSS + kHoshipa + kM` for DLSS/Hoshipa/M exteriors. FSR-specific automation should use `kFSR` through this new method-specific call.

## Log 022 - 2026-06-01 22:19 User Crashes From `772bb5bd0`

Source files:

- `D:\FireFox-downloads\CommunityShaders(11).log`, file timestamp `2026-06-01 22:19:15`, length `1329604`.
- `D:\FireFox-downloads\CommunityShaders(12).log`, file timestamp `2026-06-01 22:19:25`, length `905578`.
- `D:\FireFox-downloads\CommunityShaders(13).log`, file timestamp `2026-06-01 22:19:33`, length `1299506`.
- `D:\FireFox-downloads\crash-2026-06-01-21-02-18.log`, file timestamp `2026-06-01 22:19:17`, length `151532`.

Build/change context:

- User reports crashes observed from build `772bb5bd072a4a09c6310384c7bb31b4d9e8db64`.
- That build predates the current `84c65f52a` scoped render-scale transition protection, but this particular crash path is still a real render-scale exit relatch and should remain protected.
- GPU in the crash log is AMD Navi 31 / Radeon RX 7900 XT/XTX.

Summary:

- The paired crash is `CommunityShaders(11).log` plus `crash-2026-06-01-21-02-18.log`; both use thread id `38952`.
- Crash is not in OpenVR submit. The stack is `amdxx64.dll -> d3d11.dll -> SkyrimVR.exe -> BSShaderRenderTargets_Create::thunk -> Upscaling::ApplyPendingPerfModeRenderTargetRecreate`.
- The transition had already waited through FSR idle polling. It crashed immediately after `Relatch step: calling D3D render-target recreate`, before any `D3D render-target recreate complete` line.
- `CommunityShaders(13).log` shows the same pattern without a crash log: after the same FSR teardown and D3D recreate call, the log reports `Failure with HRESULT of 8007000E`.
- `CommunityShaders(12).log` contains successful comparable FSR relatches, proving the path is timing/pressure-sensitive rather than universally broken.

Key sequences:

| Source | Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- | --- |
| Log 11 | Render-scale exit requested | `21:01:37.241` | frame `58535`, `renderScale 1 -> 0`, `perfActive=yes` | A real render-scale relatch is required. |
| Log 11 | Relatch queued | `21:01:52.068` | frame `59507`, `relatchAge=0/20` | Full-size transition relatch starts its guarded delay. |
| Log 11 | First apply deferred | `21:01:52.941` | frame `59527`, FSR not idle, retry `60` | FSR resources still have GPU work in flight. |
| Log 11 | Long wait observed | `21:02:08.742` | frame `60099`, previous pending lasted `572` frames | User/system stall means the 60-frame retry can stretch in wall time. |
| Log 11 | Retry deferred again | `21:02:11.692` | frame `60159`, FSR not idle | FSR still not ready. |
| Log 11 | FSR teardown ready | `21:02:14.670` | frame `60219` | FSR poll finally reports safe to destroy. |
| Log 11 | Vendor teardown complete | `21:02:14.706` | frame `60219` | FSR/common/periphery resources destroyed. |
| Log 11 | D3D recreate call | `21:02:14.706` | frame `60219` | Crash follows before completion. |
| Crash log | Exception | `21:02:18` | AMD driver AV, stack into `BSShaderRenderTargets_Create` and `ApplyPendingPerfModeRenderTargetRecreate` | D3D render-target recreate is the crash site. |
| Log 13 | Similar relatch queued | `21:41:41.118` | frame `82940`, `relatchAge=0/20` | Same render-scale exit shape. |
| Log 13 | FSR teardown ready | `21:41:48.173` | frame `83140` | Poll says resources are idle. |
| Log 13 | D3D recreate call | `21:41:48.195` | frame `83140` | Same handoff point. |
| Log 13 | Allocation failure | `21:41:54.295` | `HRESULT 8007000E` | Same weakness expressed as handled out-of-memory rather than immediate AMD driver AV. |

Interpretation:

- The unsafe boundary is narrower than the whole transition: it is the handoff from completed FSR teardown into Skyrim/D3D render-target recreation.
- `PollFSRResourceTeardownReady(...)` tells us FSR work is idle enough to destroy FSR resources, but on AMD it does not prove the driver has fully retired/released all backing memory/lifetime state needed for immediate large render-target recreation.
- This does not invalidate the scoped render-scale protection rule. The affected transitions are render-scale exits while `perfActive=yes`, so they are exactly within the protected surface.
- The existing OpenVR submit forwarding/suppression work is not the direct fix for this crash, because the crash occurs before submit-stage recovery and before compositor submit.

Code change made after this log:

- `src/Features/Upscaling/FidelityFX.h` / `src/Features/Upscaling/FidelityFX.cpp`: added `HasFSRResourcesPendingTeardown()` and reused it in `PollFSRResourceTeardownReady(...)`, so transition code can ask whether FSR resources actually existed before teardown without duplicating FidelityFX internals.
- `src/Features/Upscaling.cpp`: added a narrow post-FSR-teardown D3D cooldown of `6` frames when the relatch teardown actually destroyed or released FSR/runtime resources. This is based on the pre-teardown resource state, not just the target method.
- After `ResetVRVendorRuntimeResources(...)` succeeds with FSR resources present, the code now flushes the D3D context, records that vendor teardown is staged, and requeues the pending relatch for `6` frames without carrying the old `60`-frame busy retry.
- On the next apply, if the staged method still matches, the code skips redundant vendor teardown and proceeds directly to D3D recreate after the cooldown.
- The staged state is cleared after successful D3D recreate, on device-lost reset, when the method changes, when OpenComposite disables the relatch path, and when the relatch fully completes or aborts due to device loss.
- A review fix corrected the cooldown requeue so the old `60`-frame busy retry cannot silently keep the cooldown at `60` instead of `6`.
- No build run in chat; `git diff --check` is clean.

Failure ledger update:

- Do not assume FSR teardown poll readiness alone makes the immediately following D3D render-target recreate safe on AMD. `Log 11` crashed in the AMD D3D11 driver at that point, and `Log 13` produced `8007000E` at the same point.

Next validation:

- Test the same user scenario on a build containing the FSR post-teardown cooldown.
- Success means the log shows `D3D post-teardown cooldown`, then `resuming after staged vendor teardown`, then `D3D render-target recreate complete`, with no crash and no `8007000E`.
- If it still crashes, increase the cooldown first to `20` frames before broadening the protection surface.

## Log 023 - 2026-06-01 22:49 DLSS Relatch Stability, FSR Cooldown Not Exercised

Source file:

- `D:\FireFox-downloads\CommunityShaders(15).log`, file timestamp `2026-06-01 22:49:12`, length `9310632`.

Summary:

- This run is DLSS-only for the visible transition relatches. The log shows `req=kDLSS`, `runtime=kDLSS`, `fsrRuntimePath no`, and `pendingReset(DLSS=...,FSR=no,...)`.
- No FSR post-teardown cooldown markers appear: no `D3D post-teardown cooldown`, no `staged vendor teardown`, and no `resuming after staged vendor teardown`.
- No transition failure markers were found: no `HRESULT`, no `8007000E`, no `submitDeviceLost=yes`, and no device-removal marker.
- Three D3D relatches complete successfully. This validates the DLSS relatch path in this run, but it does not validate the new FSR/AMD post-teardown cooldown from Log 022.

Key sequences:

| Sequence | Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- | --- |
| 1 | Load/startup relatch pending | `00:44:38.508` | frame `3703`, `relatchAge=0/20`, `saveLoad=yes`, `pendingReset(DLSS=yes,FSR=no,DLSSHistory=yes)` | DLSS render-scale relatch is waiting through load/startup context. |
| 1 | First teardown attempt | `00:44:40.517` | frame `3844`, `closeAge=140`, retry armed as `0/60` | DLSS resources are still vendor-busy, so teardown is deferred. |
| 1 | D3D recreate starts | `00:44:41.213` | frame `3904`, closeAge about `200` | Vendor teardown completed and D3D recreate begins. |
| 1 | D3D recreate complete | `00:44:42.628` | frame `3904`, `settleRemaining=20`, `stretchRemaining=10` | First relatch succeeds and post-relatch recovery starts. |
| 1 | Original submit forwarded | `00:44:42.633` | frame `3904` | Relatch-frame OpenVR submit guard is active and forwards the original submit. |
| 2 | Render-scale/perf exit detected | `00:44:42.515` / `00:44:42.692` | frames `3904` / `3910`, renderScale `1 -> 0`, then perf `1 -> 0` | Transition exits render-scale/perf mode. |
| 2 | Teardown attempt | `00:44:42.767` | frame `3916`, retry `0/60` | DLSS resources again need the vendor-busy retry. |
| 2 | D3D recreate complete | `00:44:44.840` | frame `3976`, `renderScaleRelevant=no`, `perfActive=no` | Full-size/no-render-scale relatch succeeds. |
| 3 | Render-scale entry detected | `00:44:59.087` / `00:45:03.132` | frames `5222` / `5534`, renderScale `0 -> 1`, then quality/perf into active mode | Exterior/loading-style entry into DLSS render-scale mode. |
| 3 | Teardown attempt | `00:45:03.416` | frame `5554`, closeAge `29`, retry `0/60` | DLSS resources are vendor-busy and deferred once. |
| 3 | D3D recreate complete | `00:45:05.572` | frame `5614`, `settleRemaining=20`, `stretchRemaining=10` | Render-scale relatch succeeds. |
| 3 | Original submit forwarded | `00:45:05.894` | frame `5614` | Relatch-frame OpenVR submit guard is active. |

Interpretation:

- This log is good evidence that the DLSS relatch path can complete cleanly with the 60-frame vendor-busy retry and the original-submit relatch guard.
- It is not evidence for or against the FSR/AMD D3D post-teardown crash fix, because FSR resources are never the active runtime path in the recorded relatches.
- The repeated `pendingReset(DLSS=yes,FSR=no,DLSSHistory=yes)` confirms the retry pressure is DLSS-side vendor/runtime work, not FSR teardown pressure.

Next validation:

- To validate the Log 022 fix, capture a run where FSR render-scale resources are active before the relatch and the log shows `D3D post-teardown cooldown` followed by `resuming after staged vendor teardown` and then `D3D render-target recreate complete`.

## Log 024 - 2026-06-01 Interior Save Load Then Exterior Switch Crash Report

Source:

- User report only, no new crash/log file attached in this turn.

Reported scenario:

- Save is inside an interior.
- User loads the interior save.
- User exits to exterior.
- Crash happens on the switch.

Interpretation:

- This is the same class of risk as Log 022 but can be DLSS-side rather than FSR-side: the first post-load profile switch can force a render-scale relatch shortly after load, and the dangerous boundary is the handoff from vendor/runtime teardown into Skyrim/D3D render-target recreation.
- Log 023 showed DLSS-only relatches with `pendingReset(DLSS=yes,FSR=no,DLSSHistory=yes)` and successful D3D recreation, but it also confirmed DLSS can have real vendor teardown/retry pressure in this exact transition family.
- Therefore an FSR-only post-teardown D3D cooldown is too narrow for first post-load interior-to-exterior switches. The cooldown should apply after actual vendor resources existed before teardown, independent of whether those resources were DLSS or FSR.
- This is not primarily an OpenVR submit crash class. The previous crash evidence points at the D3D render-target recreate boundary after vendor teardown. The OpenVR original-submit guard still matters after relatch, but it does not protect the earlier D3D recreate call itself.
- This report does not change the rule that unchanged render-scale settings should behave like normal Flatrim-style transitions. The added protection is tied to a pending render-scale relatch and actual vendor resources that existed before teardown.

Code change made after this report:

- `src/Features/Upscaling/Streamline.h` / `src/Features/Upscaling/Streamline.cpp`: added `Streamline::HasDLSSResourcesPendingTeardown()` to mirror the FSR resource-presence helper.
- `src/Features/Upscaling.cpp`: the staged post-teardown D3D cooldown now uses `vendorResourcesPendingTeardown = dlssResourcesPendingTeardown || fsrResourcesPendingTeardown`.
- The log line now records which vendor resource set caused the cooldown: `DLSS=yes/no`, `FSR=yes/no`.
- Scope remains render-scale relatch only. No-render-scale `None`/`TAA`/`DLAA` transitions without DLSS/FSR render-scale resources should not gain this protection path.
- The cooldown remains `6` frames for now. It is deliberately narrow: it avoids going back to a broad 60/120-frame stall while giving the driver a small handoff gap after real vendor resource teardown.
- The staged-teardown state is cleared on successful D3D recreate, device-lost cleanup, method change, OpenComposite blocker, relatch abort, and final relatch completion. This avoids skipping vendor teardown for a later unrelated relatch.

Scope rules captured here:

- Protected: VR render-scale relatch where DLSS resources existed before teardown.
- Protected: VR render-scale relatch where FSR resources existed before teardown.
- Protected: first post-load profile switch if it forces a render-scale relatch and real DLSS/FSR resources are torn down.
- Not newly protected: interior/exterior transitions where DLSS/FSR render-scale settings remain unchanged and no render-scale relatch is queued.
- Not newly protected: `None`, `TAA`, or `DLAA` only transitions when render-scale mode is off and no DLSS/FSR render-scale vendor resources are involved.
- Not newly protected: generic save/load protection beyond the existing load/reset mechanisms.

Expected log meaning:

- `DLSS=yes, FSR=no`: DLSS resources were present before teardown, so the new broader cooldown was used for a DLSS-side first-switch case.
- `DLSS=no, FSR=yes`: FSR resources were present before teardown, so this validates the earlier AMD/FSR crash fix class.
- `DLSS=no, FSR=no`: no vendor resource teardown was detected; the staged D3D cooldown should not be armed from this path.

Next validation:

- For this specific report, the desired log sequence is `Relatch step: vendor teardown complete before D3D render-target recreate`, then `D3D render-target recreate waits 6 frames after vendor teardown (DLSS=yes, FSR=no)` or `(DLSS=no, FSR=yes)`, then `resuming after staged vendor teardown`, then `D3D render-target recreate complete`.
- If this exact scenario still crashes before `D3D render-target recreate complete`, first try increasing the narrow post-teardown cooldown from `6` to `20` frames before broadening the protected surface.
- If the log does not show `D3D render-target recreate waits ... after vendor teardown`, the crash happened outside the newly staged handoff and needs a fresh crash/log pairing.

Current status:

- Code changes are uncommitted.
- `VR_TRANSITION_SAFETY_HANDOVER.md` remains untracked by design.
- `git diff --check` was clean after the change.
- No build was run in chat.

## Crash Coverage Audit - 2026-06-01 Current HEAD Plus Uncommitted Changes

Scope:

- Source of truth for this audit is this handover plus the current working tree.
- Current source changes are uncommitted.
- This audit checks whether each recorded crash class is covered by current code, not whether every fix is runtime-proven.

Crash/failure classes and current coverage:

| Crash / failure class | Evidence | Current coverage | Adequacy |
| --- | --- | --- | --- |
| Immediate post-D3D relatch crash from same-frame presentation texture creation | `Log 006`; final line started presentation texture recreation on relatch frame | `SubmitVRUpscaledFrame(...)` now skips submit-stage presentation work on the relatch frame before fast-path reuse/resource checks. | Adequately addressed by current code and previously validated by later no-crash logs. |
| Same-frame OpenVR submit crash after D3D relatch | `Log 011`, then `Log 021`; suppression path still crashed or hard-stopped | `InSceneOverlay` now bypasses CS-upscaled submit and forwards the original Skyrim/OpenVR texture instead of returning `VRCompositorError_None`. | Adequately addressed conceptually; requires continued validation but matches the failure mechanism better than suppression. |
| Rapid in/out retry loop with 6-frame retries and common texture recreation crash | `Log 013` / `Log 014`; `8007000E` followed by `CheckResources -> CreateUpscalingTextureResources` crash | Busy/vendor/D3D retry cadence is `60` frames, common vendor texture recreation is deferred while relatch/reset is pending, and failed common recreation requeues the relatch instead of terminating. | Adequately addressed for the recorded path. |
| Submit-stage allocation pressure during pending relatch | `Log 015`; many intermediate `8007000E` failures while relatch backoff was active | Submit-stage uses pending-relatch stretch fallback instead of `CheckResources`, vendor reset/rebuild, or full intermediate allocation while relatch is pending; allocation failures can arm a `60`-frame submit-stage fallback cooldown. | Adequately addressed for the recorded spam/pressure mechanism. |
| Repeated D3D render-target recreate `8007000E` under hard stress | `Log 017`; pending-relatch fallback helped, but D3D recreate itself still failed repeatedly | D3D out-of-memory exceptions in relatch now requeue for `300` frames instead of the normal `60` frame retry. | Adequately targeted; still needs stress validation because this is driver/memory-pressure sensitive. |
| CS-menu FSR stuck/pending relatch and method `kNONE` while runtime FSR remains active | Earlier CS-menu failure; `Log 018` validation | Render-scale relevance considers requested and runtime vendor state; `None`/non-render-scale transitions no longer drop the active FSR/DLSS runtime state prematurely. | Runtime validated by `Log 018`; adequately addressed. |
| Current-head interior -> exterior CTD around relatch-frame OpenVR suppression | `Log 021`; FSR runtime, D3D completed, then suppression/hard stop | Original-submit forwarding replaces compositor suppression; FSR/DLSS vendor teardown-to-D3D cooldown now also delays D3D recreate after real vendor teardown. | Addressed by two current mechanisms. Needs a fresh run to confirm. |
| AMD/FSR crash immediately after FSR teardown and before D3D recreate completion | `Log 022`; crash stack in AMD D3D11 driver / `BSShaderRenderTargets_Create`; `Log 13` had same handoff and `8007000E` | After real FSR resources existed and vendor teardown succeeds, relatch now flushes, stages teardown, waits `6` frames, then resumes D3D recreate without redoing teardown. | Correctly targeted, but unvalidated. If crash persists before D3D complete, first increase this narrow cooldown to `20` frames. |
| Interior-save load, then first exterior switch crash that may be DLSS-side | `Log 024` user report plus `Log 023` DLSS teardown pressure evidence | Added `Streamline::HasDLSSResourcesPendingTeardown()` and made post-teardown D3D cooldown vendor-resource based: `DLSS || FSR`. | Correctly targeted and scoped to render-scale relatch with real vendor resources. Needs runtime validation. |

Current adequacy conclusion:

- Current HEAD plus uncommitted changes covers every crash class recorded in this handover with a mechanism that matches the recorded failure boundary.
- No additional code change is justified from the handover alone right now.
- The only weak point is validation, not obvious implementation scope: the newest vendor-resource post-teardown cooldown is still unproven and may need `6 -> 20` frames if the next log still crashes before `D3D render-target recreate complete`.

What remains outside crash coverage:

- The black square/block artifact is not solved by these crash fixes. It remains a separate visual-fidelity investigation.
- The long FSR post-load reset/defer visual state from `Log 020` is not a hard crash and was not proven to be explicit post-D3D stretch. It may need clearer presentation-state logging if it reproduces.
- A crash without `D3D post-teardown cooldown`, `resuming after staged vendor teardown`, or relatch-frame original-submit markers should be treated as a new class and paired with a crash log.

## Log 025 - 2026-06-02 20:34 Current Head Interior -> Exterior Crash

Source file:

- `D:\FireFox-downloads\CommunityShaders(16).log`, file timestamp `2026-06-02 20:34:35`, length `1662209`.

Build/change context:

- User reports crash with current head after an interior -> exterior transition.
- This run includes the uncommitted vendor-resource D3D post-teardown cooldown.

Summary:

- The new DLSS-side post-teardown cooldown did run: `D3D render-target recreate waits 6 frames after vendor teardown (DLSS=yes, FSR=no)`.
- D3D recreate completed: `Relatch step: D3D render-target recreate complete`.
- Vendor/common resources recreated and `Applied render-target relatch` was logged.
- The relatch-frame original-submit guard ran at frame `9205`.
- The log stopped immediately after the next state change at frame `9206`, while post-relatch stretch recovery was still active (`settleRemaining=19`, `stretchRemaining=9`).

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Exterior loading menu closes | `14:24:40.423` | frame `9110`, `pendingVR=yes`, `pendingRenderScale=yes`, `graceRemaining=20` | Interior -> exterior profile switch is pending and render-scale relevant. |
| Transition applied/relatch queued | `14:24:41.227` | frame `9119`, `relatchAge=0/20`, closeAge `9` | Render-scale settings apply after transition grace; D3D relatch queued. |
| Vendor busy retry | `14:24:41.444` | frame `9139`, retry `0/60` | DLSS submit-stage resources are still in use. |
| Vendor teardown complete | `14:24:42.150` | frame `9199` | DLSS/common/periphery resources are torn down. |
| D3D post-teardown cooldown armed | `14:24:42.150` | frame `9199`, retry `0/6`, `DLSS=yes`, `FSR=no` | New current-head cooldown is active and correctly scoped to DLSS resources. |
| Pending-relatch stretch submits | `14:24:42.161` to `14:24:42.223` | frames `9199` to `9204`, `pendingRelatch=yes` | Before D3D relatch resumes, submit-stage stretch fallback still creates CS presentation textures and submits them. This did not crash. |
| D3D relatch resumes | `14:24:42.224` | frame `9205` | Staged teardown resumes and calls D3D render-target recreate. |
| D3D recreate completes | `14:24:43.367` | frame `9205` | D3D relatch itself survives. |
| Post-relatch recovery armed | `14:24:43.368` | frame `9205`, stretch `10`, full-eye/foveated bypass `20` | Recovery enters stretch fallback. |
| Original-submit guard runs | `14:24:43.705` | frame `9205`, both eyes | Relatch-frame CS-upscaled submit is bypassed and original submit is forwarded. |
| Log stops after next state change | `14:24:43.707` | frame `9206`, `settleRemaining=19`, `stretchRemaining=9` | Crash/hard stop likely occurs as the next post-relatch stretch frame begins. |

Interpretation:

- This is no longer the Log 022 crash class. The D3D post-teardown cooldown worked and D3D recreate completed.
- It is also not the old same-frame relatch-submit bug. The relatch-frame original-submit guard ran correctly.
- The remaining crash point is the first normal post-relatch stretch frame after the guarded relatch frame. Current code only bypassed CS submit-stage presentation on the relatch frame, then allowed submit-stage presentation texture creation during the remaining stretch window.
- The log suggests that creating/submitting CS stretch presentation textures is still unsafe immediately after D3D relatch, even though it was safe while relatch was pending before D3D recreate.

Code change made after this log:

- `src/Features/Upscaling.cpp`: `ShouldSkipVRRenderScaleRelatchSubmitStagePresentationThisFrame(...)` now also returns true while `IsVRRenderScaleRelatchStretchFallbackActive(...)` is true.
- Effect: during the existing post-D3D stretch window, the compositor hook forwards Skyrim's original submit instead of calling `SubmitVRUpscaledFrame(...)` and creating/submitting CS presentation textures.
- Scope remains narrow: this only applies after a render-scale relatch has armed post-relatch stretch recovery. It does not change vendor teardown timing, D3D recreate timing, non-render-scale transitions, or the later full-eye/foveated recovery phase.

Next validation:

- In the next log, after `Applied render-target relatch`, expect `original-submit-relatch-guard` not only on frame `9205` but also for the remaining stretch fallback frames.
- If this still crashes before stretch exits, the next mitigation is to extend original-submit forwarding through the full post-relatch recovery window, not just stretch.

## Log 026 - 2026-06-02 15:34 CS Menu DLSS Quality -> Balanced Crash

Source file:

- `D:\FireFox-downloads\crash-2026-06-02-15-34-25(1).log`, crash time `2026-06-02 15:34:25`.

Build/change context:

- User reports a crash on current head while switching in the CS menu from DLSS Quality to DLSS Balanced.
- No paired `CommunityShaders.log` was supplied for this run, so this entry is based on the crash log only.

Summary:

- Crash type is `EXCEPTION_BREAKPOINT` in `KERNELBASE.dll`.
- Probable stack enters `openvr_api.dll`, `dxgi.dll`, then the CS OpenVR submit hook at `src\Features\VR\InSceneOverlay.cpp:297`.
- Register strings identify OpenComposite/OpenOVR: `OpenOVR\Compositor\dx11compositor.cpp`.
- OpenComposite reports `DX11Compositor::CheckCreateSwapChain` and `Cannot create DX texture swap chain: err %d`.
- The stack decodes the concrete error as `err -2`.
- Repeated stack dimensions are `1494x1385`, matching a new submitted per-eye texture size during the DLSS quality-mode change.

Interpretation:

- This is not a cell-transition D3D recreate crash.
- The crash boundary is OpenComposite trying to create a DX submit swap chain for a newly sized texture submitted through our OpenVR hook.
- Existing protection covered the relatch frame and the post-D3D stretch recovery window, but not the pre-relatch pending/retry window.
- During a CS-menu DLSS Quality -> Balanced change, the render-scale relatch can be queued or retrying while submit-stage code still builds and submits CS-owned presentation/stretch textures at the target dimensions. OpenComposite appears able to abort during that texture-size handoff.

Code change made after this crash:

- `src/Features/Upscaling.cpp`: added one shared relatch-submit guard predicate that is active when a render-scale relatch is pending, in progress, or in the existing post-relatch recovery submit guard.
- `src/Features/Upscaling.cpp`: `SubmitVRUpscaledFrame(...)` now uses this shared predicate and returns false before creating/reusing CS submit-stage presentation textures.
- `src/Features/Upscaling.cpp`: `LogVRCompositorSubmitPath(...)` now reports `relatchGuard=true` for pending/in-progress relatches, not only post-D3D recovery.
- `src/Features/Upscaling.h` and `src/Features/VR/InSceneOverlay.cpp`: renamed the public hook helper from relatch-frame wording to relatch-guard wording and made the OpenVR submit hook forward the original Skyrim submit while the broader guard is active.

Scope:

- Still render-scale gated: it requires VR render-scale transition safety relevance.
- It should not protect `None`, `TAA`, or `DLAA` only changes when render-scale mode is off.
- It should not change unchanged render-scale operation.
- It does change the visual fallback during a queued/retrying render-scale relatch: instead of submitting CS stretch output, the hook forwards the original submitted texture until the relatch is no longer pending/in-progress and the post-relatch guarded stretch window is over.

Next validation:

- In a paired `CommunityShaders.log`, DLSS Quality -> Balanced should show `original-submit-relatch-guard` and `relatchGuard=true` while `pendingRelatch=yes` or `relatchInProgress=yes`.
- If OpenComposite still crashes with the same `Cannot create DX texture swap chain: err -2`, the next likely step is to keep original-submit forwarding through the full post-relatch full-eye recovery window, not only the pending/in-progress and stretch windows.

## Log 027 - 2026-06-02 01:06 CS Menu Disable Render Scale AMD FSR Runtime Crash

Source file:

- `D:\FireFox-downloads\crash-2026-06-02-01-06-37.log`, crash time `2026-06-02 01:06:37`.

Build/change context:

- User reports current head froze, then crashed after disabling render scale and exiting the CS menu.
- No paired `CommunityShaders.log` was supplied for this run, so this entry is based on the crash log only.

Summary:

- Crash type is `EXCEPTION_ACCESS_VIOLATION` in `amdxx64.dll+00D0C62`, reading address `0x80` with `RAX=0`.
- The crash occurs on the rendering thread on an AMD Navi 31 / RX 7900 XT/XTX system using Oculus/OpenComposite.
- Probable stack enters D3D11 and then CS at `src\Features\Upscaling\DX12SwapChain.cpp:220`, inside `WrappedResource::WrappedResource(...)`.
- The failing CS call is `a_d3d11Device->CreateTexture2D(&a_texDesc, nullptr, &resource11)`.
- The stack continues through `FidelityFX::EnsureRuntimeUpscalerSharedResources(...)`, `DispatchRuntimeUpscalerSingle(...)`, `FidelityFX::Upscale(...)`, and `Upscaling::Upscale()`.

Interpretation:

- This is not the OpenComposite submit swap-chain crash from Log 026.
- It is an AMD/FSR runtime shared-resource creation crash during the render-scale disable/recovery path.
- The likely gap was that the current settings can already report render scale off after the menu change, while FSR reset, render-target relatch, or post-relatch settling is still active.
- In that state, the old `ShouldDeferVRFSRRuntimeForRenderScaleReset(...)` could stop treating the frame as render-scale relevant and allow full-frame FSR runtime dispatch too early.

Code change made after this crash:

- `src/Features/Upscaling.cpp`: `ShouldDeferVRFSRRuntimeForRenderScaleReset(...)` now treats explicit unsafe FSR/relatch states as sufficient reason to defer FSR runtime dispatch before checking the current render-scale setting.
- The guarded states are `pendingFSRReset`, pending render-target relatch, in-progress render-target relatch, and post-relatch settling.
- Only after none of those states are active does it fall back to the normal render-scale relevance plus pending-transition check.

Scope:

- VR + FSR only.
- It should not affect DLSS.
- It should not affect settled render-scale-off gameplay.
- It should not add protection for `None`, `TAA`, or `DLAA` only changes when render-scale mode is off.

Next validation:

- In the next paired `CommunityShaders.log`, disabling render scale from FSR should show FSR runtime dispatch being skipped/deferred during reset/relatch/settling instead of entering `FidelityFX::EnsureRuntimeUpscalerSharedResources(...)` immediately.
- If the same AMD driver crash remains, the next suspect is FSR runtime shared-resource lifetime outside the relatch/reset flags, and logging should be added around the exact runtime resource dimensions and reset state at `EnsureRuntimeUpscalerSharedResources(...)`.

## Log 028 - 2026-06-02 21:13 CS Menu FSR -> DLSS/DLAA Same-Resolution Crash

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-02 21:13:06`.

User result:

- Crash/freeze after switching from FSR to DLSS/DLAA at the same apparent render resolution.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Relatch queued | `21:13:02.112` | frame `33595`, `req=kDLSS`, `runtime=kFSR`, `quality=3`, `renderScaleRelevant=yes`, `renderScaleRequested=yes`, `perfRequested=yes`, `perfActive=yes`, delay `20` | Method switch is still render-scale relevant even though the numeric resolution is unchanged. |
| Pending-relatch stretch submits | `21:13:02.124` to `21:13:03.239` | frames `33595` to `33674`, `pendingRelatch=yes`, `relatchGuard=no` | This build still submitted CS-owned stretch output during pending relatch. Current source should instead forward original submit while pending/in-progress relatch is active. |
| Relatch starts | `21:13:03.241` | frame `33675`, `relatchInProgress=yes`, `pendingReset(DLSS=yes,FSR=yes)` | Render-target relatch begins and owns vendor teardown plus D3D recreate. |
| FSR vendor teardown complete | `21:13:03.241` | same frame | FSR/common/periphery resources are torn down before D3D recreate. |
| Generic resource change races relatch | `21:13:04.357` | still frame `33675`, `relatchInProgress=yes` | `CheckResources` detects `method kFSR -> kDLSS` and destroys previous FSR resources with `waitForIdle=no` while the relatch D3D recreate is still active. |
| D3D recreate completes | `21:13:04.471` | same frame | Relatch completes D3D recreate and recreates vendor/common resources for `kDLSS`. |
| Post-relatch original submit | `21:13:04.479` left eye, `21:13:06.492` right eye | frame `33675`, `settling=yes`, `stretch=yes` | Same-frame eye submit has a large delay, matching the user-visible freeze before the crash/hard stop. |

Interpretation:

- Same render resolution is not enough to skip protection. The method switch still moves resource ownership from FSR to DLSS while render-scale/perf mode are active.
- There are two distinct issues in this log.
- First, the tested DLL did not include the newest broad pending-relatch submit guard: the log still says `relatchGuard=no` while `pendingRelatch=yes`, and the guard log string lacks the current `pendingRelatch` / `relatchInProgress` fields.
- Second, independent of the submit guard, generic `CheckResources` resource-change handling overlapped the relatch-owned teardown/recreate window and tried to handle the same FSR -> DLSS transition while D3D recreate was in progress.

Code change made after this log:

- `src/Features/Upscaling.cpp`: pending/in-progress render-target relatch now bypasses CS submit-stage presentation without depending on the current render-scale relevance predicate. The relatch flag itself is the safety source.
- `src/Features/Upscaling.cpp`: `CheckResources(...)` now defers generic resource-change handling while a render-target relatch is pending or in progress.
- `src/Features/Upscaling.cpp`: successful relatch records which method it recreated, and `CheckResources(...)` consumes only the bookkeeping for that already-handled change during post-relatch settling.
- `src/Features/Upscaling.cpp`: the relatch-recreated marker is cleared on relatch aborts, OpenComposite blocker exits, device-lost reset, and pending-transition cleanup.

Scope:

- This is scoped to actual pending/in-progress render-target relatches.
- It does not add protection for ordinary `None`, `TAA`, or `DLAA` only changes when no render-target relatch is pending.
- It does not add transition protection for unchanged, settled render-scale operation.

Next validation:

- For FSR -> DLSS/DLAA at the same apparent resolution, the next log should show `original-submit-relatch-guard` while `pendingRelatch=yes` or `relatchInProgress=yes`, not `pending-relatch-stretch-output` followed by `cs-upscaled-submit`.
- During D3D recreate, the next log should show `resource change deferred: render-target relatch owns resources`, followed after relatch success by `resource change consumed by render-target relatch`.

## Log 029 - 2026-06-02 21:28 CS Menu DLSS Hoshipa -> FSR Hoshipa Crash

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-02 21:28:46`.

User result:

- Crash while outside, switching from DLSS Hoshipa to FSR Hoshipa.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Relatch queued | `21:28:43.512` | frame `8681`, `req=kFSR`, `runtime=kDLSS`, `quality=1`, `renderScaleRelevant=yes`, `renderScaleRequested=yes`, `perfRequested=yes`, `perfActive=yes` | CS-menu method switch from DLSS render scale to FSR render scale is correctly treated as a render-target relatch. |
| First apply attempt | `21:28:43.831` | frame `8701`, method `kFSR` | Vendor resources were still in use, so teardown/recreate was deferred for the normal 60-frame retry. |
| Pending-relatch submit guard active | `21:28:44.374` to `21:28:44.950` | frames `8733` to `8766`, `pendingRelatch=yes`, `relatchGuard=yes` | The newer pending/in-progress relatch OpenVR guard worked: the hook forwarded the original submit instead of CS-owned submit-stage output. |
| Vendor teardown complete | `21:28:44.850` | frame `8760` | DLSS resources were torn down. Log reports pending DLSS teardown existed and FSR did not. |
| D3D cooldown begins | `21:28:44.850` | frame `8760`, cooldown `6` frames | D3D render-target recreate was delayed by the new 6-frame post-teardown cooldown. |
| D3D relatch resumes | `21:28:44.952` | frame `8767` | Relatch resumes after staged vendor teardown and calls D3D render-target recreate. |
| Generic resource change deferred | `21:28:46.070` | frame `8767`, `method kDLSS -> kFSR`, `quality 1 -> 1`, `renderScale 1 -> 1`, `perf 1 -> 1` | The newer generic `CheckResources` guard worked: it did not double-teardown/double-recreate while relatch owned resources. |
| FSR runtime defer guard entered | `21:28:46.070` | frame `8767`, `req=kFSR`, `runtime=kFSR`, `relatchAge=0/6` | FSR runtime dispatch was already being held during the relatch frame. |
| D3D recreate complete | `21:28:46.178` | frame `8767` | D3D render-target recreate completes successfully. |
| Relatch applied | `21:28:46.178` | frame `8767`, `settling=yes`, `settleRemaining=20`, `stretch=yes`, `stretchRemaining=10`, `pendingReset(DLSS=no,FSR=yes)`, `vendorPending=yes`, `fsrRuntimeDefer=yes` | Relatch itself succeeded, but FSR temporal/runtime resources were still queued for reset after the relatch. |
| Post-relatch original submit guard active | `21:28:46.185` to `21:28:46.186` | frame `8767`, both eyes, `settling=yes`, `stretch=yes`, `relatchGuard=yes` | OpenVR submission was still guarded on the first post-relatch frame. |
| Resource-change bookkeeping consumed | `21:28:46.188` | frame `8768`, `settling=yes`, `settleRemaining=19`, `pendingReset(FSR=yes)`, `vendorPending=yes` | Generic resource-change tracking was consumed after relatch, so the remaining live risk is the pending FSR runtime reset, not a second generic resource rebuild. |

Interpretation:

- This is not the same failure as Log 028.
- The pending-relatch OpenVR submit guard worked.
- The generic `CheckResources` relatch ownership fix worked.
- The D3D render-target relatch itself completed and applied.
- The crash boundary moved to the post-relatch recovery window while `pendingReset(FSR=yes)` was still active.
- For FSR relatch, the current relatch path recreates common/vendor resources with `recreateTemporal=no`, then leaves FSR temporal/runtime resources pending for the later vendor runtime reset. The log ends before any `Applying vendor runtime reset` or `Rebuilt FSR resources after VR reset`, so the likely unsafe point is letting that pending FSR runtime reset run immediately during post-relatch settling.

Code change made after this crash:

- `src/Features/Upscaling.cpp`: first changed `ApplyPendingVendorRuntimeReset(...)` to defer any pending vendor runtime reset through the full post-relatch settling window.
- Follow-up visual test showed this made the transition look worse: the fallback lasted longer, and the first stretch view could appear out of stereo with a brief HAM/FOV-like overlay when the black square appeared.
- The implementation was narrowed again: pending vendor runtime reset is now deferred only while the short render-target relatch stretch fallback is active, not for the full settling window.
- `ShouldDeferVRFSRRuntimeForRenderScaleReset(...)` was narrowed the same way so FSR runtime dispatch is not blocked for the full settling window after the pending reset has cleared.
- This still keeps the FSR temporal/runtime rebuild out of the immediate post-D3D frames, but should let the remaining recovery return to full-eye output sooner.

Scope:

- Only applies when a vendor runtime reset is already pending.
- Only applies during the short render-target relatch stretch fallback window.
- It should cover DLSS -> FSR and FSR -> DLSS/DLSS-DLAA relatches, because it is method-neutral.
- It should not affect settled, unchanged render-scale operation.
- It should not add protection for ordinary `None`, `TAA`, or `DLAA` only changes when no render-target relatch or vendor reset is pending.

Next validation:

- A successful retry should show `vendor runtime reset waiting: render-target relatch stretch fallback` while `stretch=yes`.
- After the short stretch fallback ends, it should then show `Applying vendor runtime reset` and `Rebuilt FSR resources after VR reset`, while the broader `settling=yes` full-eye recovery may still be active.
- If the crash still occurs before the waiting log appears, the next suspect is submit/SteamVR/OpenVR handoff immediately after D3D relatch.
- If the crash occurs when the reset applies after stretch fallback, the next step is to split or further cool down the FSR temporal/runtime rebuild itself.

## Log 030 - 2026-06-02 22:02 Older DLL DLSS Quality Post-Load Relatch Crash

Source file:

- `D:\FireFox-downloads\CommunityShaders(17).log`, last write `2026-06-02 22:02:36`.

User result:

- Crash with the version before latest.

Build identification:

- This log is not from current head/latest.
- It still logs `Relatch deferred: vendor resources are still in use; retrying in at least 60 frames`.
- It does not include the current submit-guard `teardownStarted` field.
- Current head instead logs the newer vendor-teardown-started state and uses the narrow pre-teardown runtime path before teardown actually begins.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Loading menu closed | `00:00:57.801` | frame `5510`, `req=kDLSS`, `runtime=kDLSS`, `quality=1`, `relatchPending=yes`, `vendorPending=yes`, `postLoadReset=yes` | Save/load boot path is already heading into a DLSS render-scale relatch. |
| First DLSS relatch starts | `00:00:59.756` | method `kDLSS` | Vendor teardown begins before D3D render-target recreate. |
| First vendor busy retry | `00:00:59.756` | old retry wording, `60` frames | DLSS resources were still in use. Older DLL holds the relatch pending across a long retry window. |
| First teardown complete | `00:01:00.420` | frame `5710`, D3D cooldown `6` frames | Vendor teardown finally completes, then waits the post-teardown D3D cooldown. |
| First D3D recreate complete | `00:01:01.674` | frame `5717`, `quality=1` | Initial post-load DLSS render-scale relatch succeeds. |
| Render-scale-off relatch queued | `00:01:01.970` | frame `5727`, `renderScaleRequested=no`, `perfRequested=no`, `perfActive=yes`, previous recovery still settling | A second relatch is queued while the previous recovery has not fully settled. This appears to disable render-scale output. |
| Render-scale-off D3D recreate complete | `00:01:03.948` | frame `5799` to `5800`, `quality=0`, `renderScaleRelevant=no`, `perfActive=no` | The off relatch also completes; this is not the final crash boundary. |
| Next loading/menu close | `00:01:16.562` | frame `6908`, `quality=0`, `renderScaleRelevant=yes`, `renderScaleRequested=yes`, `perfRequested=yes`, `perfActive=no` | API/loading transition asks to re-enter DLSS render scale. |
| Problem relatch queued | `00:01:17.373` | frame `6917`, `req=kDLSS`, `runtime=kDLSS`, `quality=1`, `relatchAge=0/20`, `pendingReset(DLSS=yes)` | DLSS render-scale relatch is queued after loading close. |
| Problem vendor busy retry | `00:01:17.605` | frame `6937`, old retry wording, `60` frames | Vendor resources are still in use. Older DLL enters a full pending-relatch submit guard during the long pre-teardown wait. |
| Vendor teardown complete | `00:01:18.375` | frame `6997`, D3D cooldown `6` frames | DLSS vendor teardown completes and D3D recreate is delayed briefly. |
| D3D recreate begins | `00:01:18.453` | frame `7003` | Relatch resumes after staged vendor teardown and calls D3D render-target recreate. |
| Resource change deferred | `00:01:19.475` | frame `7003`, `relatchInProgress=yes`, `pendingReset(DLSS=yes)` | Generic resource-change handling correctly does not overlap the relatch-owned D3D recreate. |
| D3D recreate complete | `00:01:19.584` | frame `7003` | D3D relatch completes and DLSS/common resources are recreated. |
| First post-relatch guarded submit | `00:01:19.917` | frame `7003`, `settling=yes`, `stretch=yes`, both eyes | Old build forwards original submit on the first recovery frame. |
| Final logged state | `00:01:19.920` | frame `7004`, `settling=yes`, `stretch=yes`, `vendorPending=no`, `pendingReset(DLSS=no)` | Log stops immediately after relatch success and first recovery state. |

Interpretation:

- This is a DLSS-only post-load/API render-scale relatch crash. It is not a DLSS -> FSR or FSR -> DLSS method-switch crash.
- The captured DLL is older than current head. It holds the submit-stage fallback during the whole pre-teardown pending window and waits the old `60` frame vendor-busy retry before D3D can proceed.
- Current head should behave differently before teardown starts: pending relatch alone no longer forces the submit-stage bypass; the normal vendor runtime path can continue until teardown has actually started.
- Current head should also log `teardownStarted=...` in the submit-guard line and should use the new shorter guarded retry wording if vendor resources are still busy.
- The crash boundary is still important: the log stops immediately after D3D recreate completed and the first stretch/recovery submit happened. If latest head reproduces with the new `teardownStarted` markers present, the remaining suspect is the immediate post-D3D recovery handoff, not the old pre-teardown long pending window.

Code status after this log:

- No source change made from this log alone.
- Existing current-head changes are the relevant mitigation: pre-teardown runtime path, teardown-started submit guard, relatch-owned resource-change deferral, and first recovery-frame submit guard.

Next validation:

- Retest with current head/latest.
- A latest-build log should no longer show the old `retrying in at least 60 frames` string for this path.
- A latest-build log should include `teardownStarted` in `Bypassing CS upscaled OpenVR submit...`.
- If the next crash still stops after `D3D render-target recreate complete`, then focus the fix on the first post-D3D recovery submit/presentation path rather than on longer pre-D3D waiting.

## Log 031 - 2026-06-02 22:08 Current Head First Interior -> Exterior DLSS Relatch Crash

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-02 22:08:16`.

User result:

- Crash in the first interior -> exterior transition.

Build identification:

- This log includes the current marker set: `teardownStarted=yes` appears in the OpenVR submit guard.
- It includes the current short vendor-busy retry wording: `retrying after 6 guarded frames`.
- Therefore this is not the older Log 030 build.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Save/load close | `22:07:48.116` | frame `4513`, `req=kDLSS`, `quality=1`, `postLoadReset=yes` | Initial load into DLSS render-scale active state. |
| Post-load reset waits | `22:07:48.397` to `22:07:49.398` | frames `4521` to `4631`, `178` occurrences | DLSS reset is held during load/transition context. |
| Post-load vendor reset completes | `22:07:49.414` to `22:07:49.415` | frame `4633` | DLSS vendor resources are torn down/recreated outside a render-target relatch. This does not crash. |
| Render-scale-off relatch queued | `22:07:53.119` | frame `4944`, `renderScaleRequested=no`, `perfRequested=no`, `perfActive=yes` | Earlier render-scale-off relatch. |
| Render-scale-off relatch completes | `22:07:54.882` | frame near `4962` | Earlier relatch reaches `D3D render-target recreate complete`. This does not crash. |
| Exterior transition close | `22:08:13.126` | frame `6517`, `quality=0`, `renderScaleRelevant=yes`, `renderScaleRequested=yes`, `perfRequested=yes`, `perfActive=no`, `pendingVR=yes`, `pendingRenderScale=yes` | Interior -> exterior transition asks to re-enter DLSS render scale. |
| Render-scale-on relatch queued | `22:08:13.198` | frame `6526`, `relatchAge=0/20`, `pendingReset(DLSS=yes)` | Current relatch starts after loading close. |
| First apply attempt | `22:08:14.483` | frame `6546`, closeAge `29` | DLSS vendor teardown begins. |
| Vendor resources busy | `22:08:14.483` | frame `6546`, retry `6` guarded frames | Current code defers because submit-stage reset is not ready. |
| Retry after vendor teardown | `22:08:14.666` | frame `6552`, closeAge `35` | Vendor teardown completes. |
| D3D post-teardown cooldown | `22:08:14.667` | frame `6552`, retry `6` frames | Current cooldown is active. |
| D3D relatch resumes | `22:08:14.827` | frame `6558`, closeAge `41` | Relatch resumes after staged vendor teardown. |
| D3D recreate complete | `22:08:16.048` | frame `6558` | Render-target recreate succeeds. |
| Vendor/common resources recreated | `22:08:16.049` | frame `6558`, `pendingReset(DLSS=yes)` before clear | DLSS/common resources are recreated. |
| Relatch applied | `22:08:16.049` | frame `6558`, `settling=yes`, `stretch=yes`, `stretchRemaining=10`, `pendingReset(DLSS=no)` | Relatch itself succeeds and enters short stretch recovery. |
| Handoff registered during recovery | `22:08:16.374` | frame `6558`, pass `ISCopyDynamicFetchDisabled`, `settling=yes`, `stretch=yes` | The dynamic-resolution handoff hook still produced a CS handoff texture on the relatch/recovery frame. |
| Guarded OpenVR submit forwards handoff | `22:08:16.374` | frame `6558`, both eyes, `original-submit-relatch-guard`, `inputHandoff=yes` | The submit guard forwards the input, but the input is still a CS handoff texture, not a clean vanilla texture. |
| Final logged state | `22:08:16.377` | frame `6559`, `settling=yes`, `stretch=yes`, `resource change consumed` | Log stops immediately after the first recovery-frame guarded submit and bookkeeping consume. |

Interpretation:

- Current pre-teardown gating is working: the log has `teardownStarted=yes` only after vendor teardown starts, and the old unqualified pending-relatch submit path is gone.
- The D3D post-teardown cooldown is working: vendor teardown completes at closeAge `35`, D3D relatch resumes at closeAge `41`.
- Generic `CheckResources` overlap is working: resource-change handling is deferred while relatch owns resources, then consumed after relatch success.
- The crash now points at a narrower path: the first recovery-frame OpenVR guard says it forwards original submit, but the submitted input is `inputHandoff=yes`. That means the guard is still forwarding a CS-produced submit-stage handoff texture from the just rebuilt frame.

Code change made after this log:

- `src/Features/Upscaling.cpp`: `TryReplaceVanillaDynamicResolutionUpsample(...)` now checks the same render-target relatch/recovery submit guard before creating a submit-stage handoff.
- When the guard is active, it resets submit-stage handoff bookkeeping, logs one info diagnostic per frame, and returns `false` so the vanilla dynamic-resolution pass runs.
- The intended result is that `original-submit-relatch-guard` receives a real vanilla OpenVR input during relatch/recovery, not a current-frame CS handoff texture.

Scope:

- This only applies while `ShouldBypassVRRenderScaleRelatchSubmitStagePresentation(...)` is true.
- It is therefore limited to active render-target relatch/recovery guard windows.
- It should not change normal settled render-scale operation.
- It should not add protection for unchanged in-game transitions without API/render-scale relatch.

Next validation:

- Retest the same first interior -> exterior transition.
- The next log should show `Bypassing submit-stage handoff during render-target relatch/recovery guard; letting vanilla pass run`.
- The matching `original-submit-relatch-guard` lines should show `inputHandoff=no`.
- If the crash persists with `inputHandoff=no`, the remaining suspect is SteamVR/OpenVR accepting any submit on the first post-D3D recovery frame, not the CS handoff texture.

## Log 032 - 2026-06-02 22:15 Before-Latest DLSS Relatch Crash

Source file:

- `D:\FireFox-downloads\CommunityShaders(19).log`, last write `2026-06-02 22:15:01`.

User result:

- Crash with the version before latest.

Build identification:

- This log has the current `teardownStarted=yes` submit-guard marker and the current `retrying after 6 guarded frames` behavior.
- It does not have `Bypassing submit-stage handoff during render-target relatch/recovery guard; letting vanilla pass run`, so it predates the Log 031 handoff-bypass fix.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Transition close | `00:09:35.022` | frame near `13141`, `quality=0`, `renderScaleRequested=yes`, `perfActive=no` | Transition asks to re-enter DLSS render scale. |
| Relatch queued | `00:09:35.117` | frame `13149`, `req=kDLSS`, `quality=1`, pending relatch | DLSS render-scale relatch is pending before teardown has started. |
| Pre-teardown submit path continues | `00:09:35.117` to `00:09:35.398` | frames `13149` to `13163`, `relatchGuard=no`, `cs-upscaled-submit`, `inputHandoff=yes` | This is the intended pre-teardown runtime path: submit-stage can still use the vendor path before teardown starts. |
| Vendor teardown starts / guard active | `00:09:35.48x` | frame around `13169`, `teardownStarted=yes` | Once teardown starts, the OpenVR guard becomes active. |
| Guard forwards handoff while teardown pending | `00:09:35.487` to `00:09:35.626` | frames `13169` to `13180`, `original-submit-relatch-guard`, `inputHandoff=yes` | Same unsafe marker as Log 031: the guard is forwarding a CS handoff texture, not a clean vanilla input. |
| D3D relatch resumes | `00:09:35.627` | frame `13181`, closeAge `41` | Relatch resumes after staged vendor teardown. |
| D3D recreate complete | `00:09:36.796` | frame `13181` | Render-target recreate succeeds. |
| Relatch applied | `00:09:36.796` | frame `13181`, `settling=yes`, `stretch=yes`, `stretchRemaining=10` | DLSS relatch succeeds and enters short stretch recovery. |
| Handoff registered during recovery | `00:09:37.126` | frame `13181`, pass `ISCopyDynamicFetchDisabled`, `settling=yes`, `stretch=yes` | Submit-stage handoff is still produced on the recovery frame. |
| Guard forwards recovery handoff | `00:09:37.126` | frame `13181`, both eyes, `original-submit-relatch-guard`, `inputHandoff=yes` | Same final crash signature as Log 031. |
| Final logged state | `00:09:37.129` | frame `13182`, `resource change consumed`, `settling=yes`, `stretch=yes` | Log stops immediately after first recovery-frame guarded submit and resource bookkeeping consume. |

Interpretation:

- This confirms Log 031 rather than introducing a new mechanism.
- The pre-teardown path is working as designed: before teardown starts, `relatchGuard=no` and submit-stage can still use the CS/vendor path.
- The unsafe point is the guard window after teardown starts and after relatch applies: `original-submit-relatch-guard` still receives `inputHandoff=yes`.
- The current uncommitted handoff-bypass fix should address this exact marker by making the handoff hook return `false` and clearing handoff bookkeeping while the guard is active.

Code status after this log:

- No additional source change made.
- The existing Log 031 code change remains the targeted fix.

Next validation:

- Retest with the latest local build that includes the handoff-bypass fix.
- During the guard window, the next log should show `Bypassing submit-stage handoff during render-target relatch/recovery guard; letting vanilla pass run`.
- The guarded submit lines should change from `inputHandoff=yes` to `inputHandoff=no`.
- If the crash still reproduces with `inputHandoff=no`, the next mitigation should target first-recovery-frame OpenVR submit timing itself.

## Log 033 - 2026-06-02 21:22/21:28 OpenComposite Render-Scale Enable Swapchain Crash

Source files:

- `D:\FireFox-downloads\crash-2026-06-02-21-22-48.log`, last write `2026-06-02 22:23:59`.
- `D:\FireFox-downloads\crash-2026-06-02-21-28-27.log`, last write `2026-06-02 22:30:09`.
- `D:\FireFox-downloads\CommunityShaders(21).log`, last write `2026-06-02 22:29:44`.
- `D:\FireFox-downloads\CommunityShaders(20).log`, duplicate/near-duplicate of the same OpenComposite run, last write `2026-06-02 22:29:15`.

User result:

- Crash happened while turning on render scale.
- First crash popup said `Cannot create DX texture swap chain: err -2`.
- Second crash reports OpenXR runtime failure in the same OpenComposite swapchain path.

Runtime/build identification:

- The CS log reports `Runtime: OpenComposite`.
- CrashLogger stack strings point to `OpenOVR\Compositor\dx11compositor.cpp` and `DX11Compositor::CheckCreateSwapChain`.
- Crash stack contains `virtualdesktop-openxr.dll`, `openvr_api.dll`, and CS submit-hook references in `src\Features\VR\InSceneOverlay.cpp:297`.
- The CS log does not contain `Bypassing submit-stage handoff during render-target relatch/recovery guard; letting vanilla pass run`, so it either predates the Log 031 handoff-bypass fix or the guard was not active for the stretch path.

Key sequence from `CommunityShaders(21).log`:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| OpenComposite detected | `23:27:08.661`, `23:27:10.898`, `23:28:13.676` | runtime `OpenComposite` | This is not the SteamVR crash path. |
| Render scale enabled | `23:27:30.196` | frame `1055`, `renderScale 0 -> 1`, `renderScaleRelevant=yes`, `pendingVR=yes`, `pendingRenderScale=yes` | The user/menu turns on render scale. |
| First CS scaled submits | `23:27:30.221` | frame `1055`, `cs-upscaled-submit`, input `4872x2884`, output `2436x2884`, `inputHandoff=yes` | OpenComposite starts receiving CS-sized per-eye output instead of only the vanilla combined texture. |
| Vendor reset after load tail | `23:28:12.024` to `23:28:12.046` | closeAge `120` to `121`, vendor pending then rebuild deferred | Runtime reset runs after the post-load tail. |
| Relatch pending | `23:28:12.193` onward | frame `4005+`, `pendingRelatch=yes`, `cs-upscaled-submit`, `inputHandoff=yes` | Before teardown starts, submit-stage still uses CS output. The log continues, so this is not immediately fatal by itself. |
| Relatch apply starts | `23:28:12.547` | frame `4025`, closeAge `147`, `relatchInProgress=yes` | D3D render-target relatch begins. |
| Vendor teardown deferred | `23:28:12.548` | frame `4025`, retry `6` guarded frames | Submit-stage DLSS resources are still busy. |
| Guard forwards handoff during retry | `23:28:12.565` to `23:28:12.786` | frames `4025` to `4036`, `original-submit-relatch-guard`, `inputHandoff=yes` | The submit guard forwards "original", but the input is still a CS handoff texture. |
| D3D relatch resumes | `23:28:12.787` | frame `4037` | Relatch resumes after the 6-frame cooldown. |
| D3D recreate complete | `23:28:13.778` | frame `4037`, screen becomes `3248x1922` | Render-target recreate succeeds. |
| Post-relatch recovery armed | `23:28:13.778` | `settling=yes`, `stretch=yes`, `stretchRemaining=10` | Short stretch/FOV recovery starts. |
| First post-relatch guarded submit | `23:28:14.018` | frame `4037`, left eye, `original-submit-relatch-guard`, `inputHandoff=yes` | The first recovery submit still forwards a handoff texture. |
| Right-eye submit after freeze | `23:28:19.272` | same frame `4037`, right eye, `inputHandoff=yes` | Roughly 5.25 seconds pass between eyes, then the log stops. |

CrashLogger interpretation:

- Both CrashLogger files stop inside OpenComposite/OpenXR swapchain creation, not inside CS D3D teardown code.
- `crash-2026-06-02-21-22-48.log` contains the direct popup text `Cannot create DX texture swap chain: err -2`.
- `crash-2026-06-02-21-28-27.log` contains `OpenXR Call failed... XR_ERROR_RUNTIME_FAILURE` while calling `CheckCreateSwapChain`.
- The CS hook is on the stack because the failure happens when the hook calls the original `IVRCompositor::Submit`.
- The most actionable CS-side marker remains `inputHandoff=yes` during `original-submit-relatch-guard`.

Interpretation:

- This is an OpenComposite submit-path crash during render-scale enable, not an ordinary interior/exterior transition crash.
- The pre-teardown CS upscaled submits are suspicious because they change the submitted texture shape, but they continue for many frames before the final failure.
- The final failure still matches the already identified handoff problem: during the guarded relatch/recovery path, the hook forwards an input texture that is still tagged as a CS handoff.
- OpenComposite appears stricter than SteamVR when the submitted texture/swapchain shape changes around relatch.

Code change made after this log:

- `src/Features/Upscaling.cpp`: `ShouldSkipVRRenderScaleRelatchSubmitStagePresentationThisFrame(...)` no longer disables the exact relatch-frame skip just because stretch fallback is active.
- `src/Features/Upscaling.cpp`: `ShouldBypassVRRenderScaleRelatchSubmitStagePresentation(...)` now returns true while `IsVRRenderScaleRelatchStretchFallbackActive(...)` is true.
- This makes the dynamic-resolution handoff bypass and the OpenVR submit guard agree during the post-relatch stretch window.

Expected next-log signature:

- During the relatch/recovery guard, the log should include `Bypassing submit-stage handoff during render-target relatch/recovery guard; letting vanilla pass run`.
- The matching `original-submit-relatch-guard` lines should change from `inputHandoff=yes` to `inputHandoff=no`.
- If OpenComposite still crashes with `inputHandoff=no`, the remaining likely fix is OpenComposite-specific handling of the first changed-size post-relatch submit, such as a bounded first-frame compositor submit suppression or longer post-D3D submit defer for OpenComposite only.

## Log 034 - 2026-06-02 22:45 SteamVR CS Menu Render-Scale Off Crash

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-02 22:45:28`, size `43,333,667` bytes.

User result:

- Crash when switching render scale off through the Community Shaders menu.
- User also noticed that before the crash, a Quality -> Balanced style preset change appeared to happen without a separate relatch.

Runtime/build identification:

- Runtime is `SteamVR`, not OpenComposite.
- The log contains the handoff-bypass marker from the current relatch guard work: `Bypassing submit-stage handoff during render-target relatch/recovery guard; letting vanilla pass run`.
- Guarded submits in the crash tail show `inputHandoff=no`, so this is not the older `inputHandoff=yes` failure from Logs 031-033.
- The log does not contain the new expected marker `Skipping VR vendor color upscale during render-scale exit recovery guard`; that marker was added after this analysis.

CS menu switch interpretation:

- While render scale was off, menu quality/preset changes correctly logged `renderScaleRelevant=no` and did not trigger render-scale relatch. Examples:
  - `22:44:06.673`: `quality 0 -> 1`, `renderScale 0 -> 0`, `perf 0 -> 0`, `renderScaleRelevant=no`.
  - `22:44:16.339`, `22:44:24.693`, `22:44:31.502`, `22:44:35.235`, `22:44:40.181`: further quality/preset changes with `renderScaleRelevant=no`.
- While render scale was active or being enabled, quality changes were folded into relatch ownership rather than causing a second independent relatch. Examples:
  - `22:41:56.833`: `quality 1 -> 2`, `renderScale 1 -> 1`, `perf 1 -> 1` while relatch owned resources.
  - `22:45:10.435`: `quality 0 -> 1`, `renderScale 1 -> 1`, `perf 0 -> 1` while render-scale relatch was being enabled.
- Conclusion: the apparent Quality -> Balanced "without relatch" is not itself a bug in this log when render scale is off. With render scale on, the change is either folded into an active relatch or triggers render-scale-relevant relatch handling.

Key crash sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Render scale off requested | `22:45:20.615` | resource change `renderScale 1 -> 0`, `perf 1 -> 1`, `renderScaleRelevant=yes` | CS menu requests exit from render-scale mode. |
| Relatch starts | `22:45:21.611` | frame `26625`, `renderScaleRequested=no`, `perfRequested=no`, `perfActive=yes` | Render-scale-off relatch starts while old perf mode is still active. |
| Vendor teardown deferred once | `22:45:21.611` | frame `26625`, pending reset `DLSS=yes` | Submit-stage DLSS resources are still busy. |
| Guard forwards vanilla input | `22:45:21.623` to `22:45:21.803` | frames `26625` to `26636`, `original-submit-relatch-guard`, `inputHandoff=no` | The handoff-bypass fix is active and working during pending retry. |
| Vendor teardown complete | `22:45:21.703` | frame `26631` | Staged vendor teardown completes after the guarded retry. |
| D3D relatch call | `22:45:21.805` | frame `26637` | Render-target recreate starts. |
| D3D relatch complete | `22:45:23.030` | frame `26637`, screen `4936x2740` | Render targets are recreated. |
| Vendor resources recreated | `22:45:23.030` | method `kDLSS` | DLSS/common resources are recreated for the new full-size state. |
| Render-scale exit recovery armed | `22:45:23.031` | `renderScaleRelevant=no`, `renderScaleRequested=no`, `perfActive=no`, `settling=yes`, `stretch=yes`, `stretchRemaining=10` | Render scale is now off, but the relatch recovery guard is active. |
| Normal VR DLSS intermediates created | `22:45:23.431` | per-eye in/out `2468x2740` | Normal VR DLSS path resumes during the recovery frame. |
| DLSS evaluate fails | `22:45:28.220` | eye 0 result `24`, eye 1 result `15`; eye 0 `colorOut=4936x2740`, eye 1 `colorOut=2468x2740` | Streamline evaluation fails during the first render-scale-off recovery frame. |
| Guarded submit after failure | `22:45:28.220` | frame `26637`, `original-submit-relatch-guard`, `inputHandoff=no` | OpenVR submit guard still forwards vanilla/original input; the failure already happened in normal DLSS evaluation. |

Interpretation:

- This is a new failure class relative to Logs 031-033.
- The handoff bypass fixed the old submit-guard problem: `inputHandoff=no` is present.
- The remaining crash path is the normal VR DLSS post-process path resuming too early after render-scale exit. Render scale is already off, so `IsPresentationUpscalingActive()` is false and `Streamline::Upscale(...)` runs before the recovery guard has expired.
- The crash is not evidence that same-resolution quality/preset changes need render-scale protection when render scale is off.

Code change made after this log:

- `src/Features/Upscaling.cpp`: added `ShouldBypassVRRenderScaleRelatchVendorEvaluation(...)`.
- `src/Features/Upscaling.cpp`: `Upscale()` now returns before DLSS/FSR color evaluation while VR render-scale exit recovery is active and render scale/perf mode are both off.
- The bypass leaves pending DLSS history reset untouched, so the reset is consumed when normal vendor evaluation resumes after the recovery window.

Expected next-log signature:

- During render-scale-off recovery, the log should show `Skipping VR vendor color upscale during render-scale exit recovery guard`.
- There should be no `slEvaluateFeature failed` on the first render-scale-off recovery frame.
- Same-resolution preset changes with `renderScaleRelevant=no` should remain unrestricted and should not queue render-scale protection.

## Log 035 - 2026-06-02 23:17 SteamVR First Interior -> Exterior Render-Scale Entry Crash

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-02 23:17:56`, size `2,590,246` bytes.

User result:

- Crash during the first interior -> exterior transition.

Runtime/build identification:

- Runtime is `SteamVR`.
- This log includes the newer handoff-bypass marker: `Bypassing submit-stage handoff during render-target relatch/recovery guard; letting vanilla pass run`.
- The crash is render-scale entry, not render-scale exit: transition requests `kDLSS`, `renderScaleRequested=yes`, `perfRequested=yes`.

Key crash sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Loading menu closed | `23:17:53.227` | frame `8961`, `req=kDLSS`, `renderScaleRequested=yes`, `perfActive=no`, `pendingRenderScale=yes`, grace `20` | Interior -> exterior transition queues render-scale entry. |
| Pre-teardown runtime path | `23:17:54.558` to `23:17:54.614` | frames `8987` to `8989`, `relatchGuard=no`, `cs-upscaled-submit`, `inputHandoff=yes` | Still before teardown, so submit-stage vendor path is allowed. |
| Relatch starts | `23:17:54.620` | frame `8990`, closeAge `29`, `relatchInProgress=yes` | Render-target relatch begins. |
| Vendor teardown deferred | `23:17:54.621` | frame `8990`, retry after `6` guarded frames | Submit-stage DLSS resources are still not idle. |
| Handoff bypass active | `23:17:54.636` onward | frames `8990` to `9001`, guard says vanilla pass should run | The handoff hook correctly refuses to produce a new handoff. |
| Guard still submits CS presentation output | `23:17:54.644` onward | frames `8990` to `9001`, `relatch-guard-presentation-output`, `cs-upscaled-submit`, `inputHandoff=no` | Regression: `SubmitVRUpscaledFrame()` re-enters CS presentation output despite `relatchGuard=yes`. |
| D3D relatch resumes | `23:17:54.947` | frame `9002`, closeAge `41` | Relatch resumes after staged teardown/cooldown. |
| D3D recreate complete | `23:17:56.177` | frame `9002`, screen `4194x2329` | Render targets and DLSS resources are recreated. |
| Recovery armed | `23:17:56.177` | `settling=yes`, `stretch=yes`, `stretchRemaining=10` | Short post-D3D stretch recovery begins. |
| Mask repair and CS output during recovery | `23:17:56.510` | frame `9002`, `hmdDefer=no`, `projectedDefer=no`, `relatch-guard-presentation-output`, `cs-upscaled-submit` | HAM/projected repair and CS presentation output both run on the first recovery frame. |
| Final logged state | `23:17:56.514` | frame `9003`, `settling=yes`, `stretch=yes` | Log stops immediately after relatch bookkeeping is consumed. |

Interpretation:

- This is not the old `inputHandoff=yes` problem. The handoff bypass worked.
- The failure is that the submit guard was not absolute. `SubmitVRUpscaledFrame()` treated the relatch guard as a special presentation-only mode and created/submitted CS-owned output while the guard was active.
- That contradicts the intended safety rule: during pending/in-progress/post-D3D relatch guard, CS must not create presentation textures or submit CS output; OpenVR should receive the original submitted texture.
- The first recovery-frame HAM/FOV repair is suspicious for the black-square artifact, but the immediate crash fix should first close the CS submit-output path during the guard.

Failure ledger update:

- Do not implement relatch safety as a CS presentation-only fallback. It still creates D3D textures and submits CS-owned output in the exact window we are trying to isolate.
- Log text saying "letting vanilla pass run" is not sufficient. The paired `VRSubmit` line must show `original-submit-relatch-guard`; if it shows `cs-upscaled-submit`, the guard is not actually protecting OpenVR.

Code change made after this log:

- `src/Features/Upscaling.cpp`: removed the relatch-guard presentation-only path from `SubmitVRUpscaledFrame()`.
- `SubmitVRUpscaledFrame()` now returns `false` whenever `ShouldBypassVRRenderScaleRelatchSubmitStagePresentation(...)` is active.
- Removed the `relatch-guard-presentation-output` diagnostics path and the special source-size handling that only existed for that mode.

Expected next-log signature:

- During pending/in-progress/post-D3D guard frames, there should be no `relatch-guard-presentation-output`.
- Guarded frames should show `Bypassing submit-stage handoff during render-target relatch/recovery guard` followed by `original-submit-relatch-guard`.
- `cs-upscaled-submit` should resume only after the relatch/recovery guard has ended.

## Code Change - 2026-06-02 Robust Strategy: Relatch Compositor Quarantine

Status:

- Superseded by Log 037. The no-submit quarantine lined up with a worse visual regression and crash, so current head no longer returns `VRCompositorError_None` on the relatch recovery frame. Keep this section as the failed hypothesis trail.

Scope:

- Source files changed after commit `2dfc292be`: `src/Features/Upscaling.cpp`, `src/Features/Upscaling.h`, `src/Features/VR/InSceneOverlay.cpp`.
- This is not a broad transition wait and does not change save/load handling.
- It applies only when a VR render-scale relatch has actually completed D3D/vendor recreate and armed the relatch recovery start frame.

Rationale:

- Logs already showed three separate unsafe outputs around relatch: CS handoff forwarded as "original", CS presentation output during the guard, and finally the risk that even the original submit may be unsafe on the exact just-relatched recovery frame.
- The robust rule is now: at the relatch recovery boundary, do not submit CS output and do not submit the original texture either for the quarantined frame. Let the compositor keep its previous frame for that frame, then forward original submits during the remaining guard, then resume CS submits after recovery ends.

Implementation:

- Added `Upscaling::ShouldSuppressVRCompositorSubmitForRenderScaleRelatchGuard()`.
- The IVRCompositor submit hook now checks suppression before `SubmitVRUpscaledFrame(...)`, overlay submission, or original-forward handling.
- On the quarantine frame it logs `suppressed-submit-relatch-guard` and returns `VRCompositorError_None`.
- Split suppression logging from normal bypass logging with a new `g_vrRenderScaleRelatchCompositorBypassLoggedFrame`, so `g_vrRenderScaleRelatchCompositorSuppressLoggedFrame` only means a real no-submit suppression happened.
- Normal guard frames still log/forward as `original-submit-relatch-guard`.

Expected next-log signature:

- Historical only; superseded by Log 037.
- On the relatch recovery start frame: `Suppressing OpenVR compositor submit during render-target relatch recovery quarantine` and `VRSubmit suppressed-submit-relatch-guard`.
- During the remaining stretch/recovery guard: `original-submit-relatch-guard`, not `cs-upscaled-submit`.
- No `relatch-guard-presentation-output` should appear anywhere; that path was removed by Log 035's fix.
- `cs-upscaled-submit` should resume only after the relatch/recovery guard has ended.

## Log 036 - 2026-06-02 23:46 SteamVR CS Menu Preset Relatch Crash With HAM Mask Visible

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-02 23:46:11`, size `33,674,768` bytes.

User result:

- Door transition felt much smoother with the relatch compositor quarantine.
- HAM mask became visibly apparent, including when changing out of the CS menu.
- Crash occurred while changing settings in the CS menu.

Runtime/build identification:

- Runtime is `SteamVR`.
- This log includes the robust-strategy quarantine markers from the current uncommitted patch.
- The failing action is a CS-menu render-scale preset change, not a door transition.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Normal foveated output before menu change | `23:46:09.673` to `23:46:09.682` | frames `18124` to `18125`, `foveated-vendor-output`, `hmdDefer=no`, `projectedDefer=no` | HAM/HMD clear and projected mask repair are running in the normal foveated path. User-visible HAM mask may correlate with this path if the mask is visually exposed. |
| CS menu preset relatch queued | `23:46:09.683` | frame `18126`, `quality=6`, `relatchAge=0/20`, reason `VR upscaling preset change` | CS menu change queues a render-scale relatch with the menu-stable 20-frame delay. |
| Pending relatch uses full-eye vendor | `23:46:09.707` onward | frames `18126+`, `foveatedRequested=no`, `hmdDefer=yes`, `projectedDefer=yes` | Pre-teardown guard correctly avoids foveated output and mask repair while relatch is pending. |
| Relatch retry resumes | `23:46:10.144` | frame `18158`, retry after `6` guarded frames | Vendor teardown had staged; relatch resumes. |
| D3D recreate starts | `23:46:10.145` | frame `18158`, boot-latched quality `2`, render `1898x2107` per eye | D3D render-target recreate begins for the new preset. |
| D3D recreate complete | `23:46:11.355` | frame `18158`, screen `3796x2107` | Render targets and DLSS/common resources are recreated. |
| Recovery armed | `23:46:11.356` | `settling=yes`, `stretch=yes`, `stretchRemaining=10` | Short post-relatch recovery starts. |
| Projected mask repair incorrectly runs | `23:46:11.653` | frame `18158`, `settling=yes`, `stretch=yes`, `hmdDefer=no`, `projectedDefer=no` | This is the new bug: mask repair runs on the quarantined recovery frame. |
| Compositor quarantine works | `23:46:11.653` to `23:46:11.654` | frame `18158`, both eyes `suppressed-submit-relatch-guard`, `inputHandoff=no` | The robust submit strategy is working: no CS output and no original submit on the recovery-start frame. |
| Mask guard exits too early | `23:46:11.657` | frame `18159`, `HAM/HMD mask clear guard exited`, `Projected HAM/FOV mask repair guard exited`, `settling=yes`, `stretch=yes` | Mask guards ended while post-relatch recovery was still active. |
| Final logged state | `23:46:11.657` | frame `18159`, `resource change consumed`, `settling=yes`, `stretch=yes` | Log stops immediately after the first recovery-frame mask repair/suppression and bookkeeping. |

Interpretation:

- The relatch compositor quarantine is validated for this crash tail: `suppressed-submit-relatch-guard` appears and no `relatch-guard-presentation-output` appears.
- The new crash-relevant gap is HAM/projected mask repair during post-relatch recovery. `ShouldDeferVRTransitionMaskRepair(...)` still returned false during `settling=yes/stretch=yes`, so projected mask repair ran on the same frame where OpenVR submit was quarantined.
- The user-visible HAM mask report matches this: mask clear/repair is no longer adequately blocked during the short recovery window.

Code change made after this log:

- `src/Features/Upscaling.cpp`: `ShouldDeferVRTransitionMaskRepair(...)` now includes `IsVRRenderScaleRelatchSettling(a_state)`.
- This keeps both HMD clear and projected HAM/FOV repair deferred during the short post-relatch recovery, not only while relatch is pending or in progress.
- The source comment on HMD clear deferral now explicitly includes active relatch recovery.

Expected next-log signature:

- Historical for the post-Log-036 test only; current head should follow Log 037's expected signature instead.
- On the recovery-start frame after `Applied render-target relatch`, diagnostics should keep `hmdDefer=yes projectedDefer=yes` while `settling=yes/stretch=yes`.
- There should be no `Projected/underwater mask repair dispatch` or `HMD clear dispatch` during the stretch/recovery guard.
- The compositor quarantine should still show `suppressed-submit-relatch-guard` on the relatch recovery-start frame.
- If CS-menu relatch still crashes after this, the next suspect is the no-submit quarantine itself or D3D resource pressure, not mask repair.

## Log 037 - 2026-06-02 23:54 SteamVR Door Relatch Regression After Compositor Quarantine

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-02 23:54:31`, size `2,190,505` bytes.

User result:

- Regression: black square now visible before the transition, after the transition, HAM/FOV artifact visible, then crash.

Runtime/build identification:

- Runtime is `SteamVR`.
- This log includes the robust-strategy no-submit quarantine and the post-relatch mask-deferral fix from Log 036.
- The failing action is a first door/API render-scale transition, not a CS-menu preset change.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Loading/menu transition starts | `23:54:28.477` | frame `5905`, `req=kDLSS`, `renderScaleRequested=yes`, `perfActive=no`, `pendingRenderScale=yes`, closeAge `0` | Door/API transition queues a DLSS render-scale change. |
| Relatch pending | `23:54:28.546` | frame `5914`, closeAge `9`, `quality=1`, `relatchAge=0/20`, `hmdDefer=yes`, `projectedDefer=yes` | Render-target relatch is queued, mask repair is deferred. |
| Pre-teardown CS output still active | `23:54:29.355` onward | frames `5914+`, `full-eye-vendor-output`, `cs-upscaled-submit`, `inputHandoff=yes`, `relatchPending=yes`, `relatchGuard=no` | Black square before transition likely comes from this window: the relatch is pending and masks are deferred, but CS still owns submit-stage output until teardown starts. |
| Guarded original forwarding starts | `23:54:29.963` to `23:54:30.245` | frames `5935` to `5945`, closeAge `30` to `40`, `original-submit-relatch-guard`, `teardownStarted=yes`, `hmdDefer=yes`, `projectedDefer=yes` | Once vendor teardown has started, CS output is correctly blocked and original submits are forwarded. |
| D3D recreate complete | `23:54:31.485` | frame `5946`, closeAge `41` | D3D relatch and vendor/common resource recreate complete. |
| Recovery armed | `23:54:31.486` | frame `5946`, `settling=yes`, `stretch=yes`, `settleRemaining=20`, `stretchRemaining=10`, `hmdDefer=yes`, `projectedDefer=yes` | The Log 036 mask-deferral fix works: recovery is visually guarded and mask repair remains deferred. |
| No-submit quarantine occurs | `23:54:31.802` | frame `5946`, both eyes `suppressed-submit-relatch-guard`, `inputHandoff=no`, `hmdDefer=yes`, `projectedDefer=yes` | The crash tail now lines up with returning `VRCompositorError_None` instead of submitting a texture. |
| Final logged state | `23:54:31.805` | frame `5947`, `resource change consumed`, still `settling=yes/stretch=yes`, `hmdDefer=yes/projectedDefer=yes` | No mask-repair dispatch appears in the recovery crash tail. |

Interpretation:

- Log 036's mask-deferral fix is validated: the recovery-start frame now keeps `hmdDefer=yes projectedDefer=yes` and there is no projected/HMD mask repair dispatch at the crash tail.
- The no-submit quarantine is not validated. The log stops immediately after `suppressed-submit-relatch-guard`, and the user reports a worse post-transition visual state plus crash.
- The pre-transition square remains a separate clue: before teardown, the log still allows `cs-upscaled-submit` with `inputHandoff=yes` while the relatch is pending. If the crash tail improves after removing no-submit suppression, the next visual target is this pre-teardown CS output window.

Code change made after this log:

- Removed `Upscaling::ShouldSuppressVRCompositorSubmitForRenderScaleRelatchGuard()`.
- Removed the IVRCompositor hook path that returned `VRCompositorError_None` and logged `suppressed-submit-relatch-guard`.
- Kept the stricter relatch guard that bypasses CS upscaled submit and forwards the original OpenVR submit as `original-submit-relatch-guard`.
- The one-frame boundary guard now treats an `original-submit-relatch-guard` bypass as handled, so it does not add an extra no-submit style frame.
- Kept the Log 036 mask-deferral fix: `ShouldDeferVRTransitionMaskRepair(...)` still includes `IsVRRenderScaleRelatchSettling(...)`.

Expected next-log signature:

- There should be no `suppressed-submit-relatch-guard`.
- At recovery start after `Applied render-target relatch`, the submit path should be `original-submit-relatch-guard`, with `inputHandoff=no`, `hmdDefer=yes`, and `projectedDefer=yes`.
- There should still be no `relatch-guard-presentation-output`, `cs-upscaled-submit`, HMD clear dispatch, or projected mask repair dispatch during active relatch recovery.
- If black square before transition remains, inspect the frames between `relatchPending=yes` and `teardownStarted=yes`, especially `full-eye-vendor-output` / `cs-upscaled-submit` with `inputHandoff=yes`.

## Code Change - 2026-06-03 Pending Relatch Presentation Guard Tightening

User correction:

- The phrase "mask-repair deferral is doing what we wanted" was too narrow.
- It only meant the log showed repair dispatch was no longer running in the forbidden recovery frame.
- It did not mean the visual problem was fixed. User saw a black square before transition, a black square after transition, and then HAM/FOV mask exposure, which means stale or unsafe presentation state is still being shown.

Rationale:

- Log 037 shows the first square likely starts before D3D teardown: `relatchPending=yes`, `hmdDefer=yes`, `projectedDefer=yes`, but submit-stage still produced `full-eye-vendor-output` and `cs-upscaled-submit` with `inputHandoff=yes`.
- Waiting until `teardownStarted=yes` before blocking CS submit-stage output is therefore too late.
- The old "vendor path can still be used before teardown" exception conflicts with the visual evidence. If a render-target relatch is pending, the output is not visually safe even if D3D teardown has not started yet.

Implementation:

- `ShouldBypassVRRenderScaleRelatchSubmitStagePresentation(...)` now returns true for any pending render-target relatch, not only after vendor teardown starts.
- Removed the `CanUseVRRuntimeVendorPathBeforeRenderScaleRelatchTeardown(...)` exception.
- FSR runtime reset deferral and pending-relatch stretch fallback no longer treat the pre-teardown pending window as a safe vendor path.
- `SubmitVRUpscaledFrame(...)` no longer allows pending vendor reset to continue just because teardown has not started.

Expected next-log signature:

- As soon as `relatchPending=yes`, the dynamic-resolution handoff hook should log `Bypassing submit-stage handoff during render-target relatch/recovery guard`.
- The paired `VRSubmit` lines should show `original-submit-relatch-guard`, `inputHandoff=no`, `relatchGuard=yes`.
- There should be no `cs-upscaled-submit` or `full-eye-vendor-output` while `relatchPending=yes`, even before `teardownStarted=yes`.
- There should still be no `suppressed-submit-relatch-guard`.
- If the pre-transition black square remains with this signature, it is no longer coming from CS submit-stage output; the next suspect becomes the vanilla/original source or OpenVR/OpenComposite transition frame itself.

## Log 038 - 2026-06-03 06:02 SteamVR Crash With Pending Relatch Guard Active

Source file:

- `D:\FireFox-downloads\CommunityShaders(22).log`, last write `2026-06-03 06:02:43`, size `1,547,897` bytes.

User result:

- Crash with current head.

Runtime/build identification:

- Runtime is `SteamVR`.
- This log includes the pending-relatch presentation guard from the 2026-06-03 code change.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Startup relatch queued | `03:29:10.913` | frame `0`, `relatchPending=yes`, `screen=0x0`, reason `Streamline DLSS availability resolved after deferred VR boot latch` | A boot/startup relatch is queued before normal world frames exist. |
| Transition close | `03:35:06.121` | frame `30927`, loading menu closed, `relatchPending=yes`, `saveLoad=yes` | Pending render-scale work carries through the transition. |
| Pending guard active before teardown | `03:35:06.386` onward | frames `30934+`, `original-submit-relatch-guard`, `inputHandoff=no`, `teardownStarted=no` | The pending-relatch guard works: CS handoff/output is blocked before teardown starts. |
| Relatch first tries teardown | `03:35:48.386` | frame `34395`, closeAge `29` | Vendor teardown begins and is deferred because submit-stage resources are still in use. |
| Vendor teardown completes | `03:35:48.453` | frame `34401`, closeAge `35` | Vendor teardown finishes, then D3D cooldown is armed. |
| D3D recreate starts | `03:35:48.520` | frame `34407`, closeAge `41` | D3D render-target recreate starts only 41 frames after loading/menu close. |
| D3D recreate complete | `03:35:49.666` | frame `34407`, closeAge `41` | D3D resources and vendor/common resources are recreated. |
| Recovery submit | `03:35:49.964` | frame `34407`, both eyes `original-submit-relatch-guard`, `inputHandoff=no`, `hmdDefer=yes`, `projectedDefer=yes` | No CS output, no handoff, no no-submit suppression, and mask repair is deferred. |
| Final logged state | `03:35:49.967` | frame `34408`, `resource change consumed`, `settling=yes`, `stretch=yes` | Log stops immediately after first guarded recovery submit and relatch bookkeeping. |

Interpretation:

- The pending-relatch presentation guard succeeded. The old pre-teardown `cs-upscaled-submit/inputHandoff=yes` signature is gone.
- The no-submit quarantine is also gone. The recovery frame uses `original-submit-relatch-guard`.
- The mask-repair dispatch is not the crash marker in this log. Recovery remains `hmdDefer=yes/projectedDefer=yes`.
- The remaining crash signature is timing: D3D/vendor recreate completes and the first guarded recovery submit happens at closeAge `41`, which is still inside the unstable post-transition period for this setup.
- This points back to the useful part of the old stable 120-frame model: delaying the actual render-engine rebuild, not changing what is submitted on the first recovery frame.

Code change made after this log:

- Added `kVRRenderScaleRelatchMinimumD3DPostCloseFrames = 120`.
- Added `GetVRRenderScaleRelatchMinimumD3DPostCloseDelayFrames(...)`, scoped to render-scale transition protection and disabled for active save/load context.
- `ApplyPendingPerfModeRenderTargetRecreate(...)` now defers before vendor teardown/D3D recreate until the latest loading/menu close is at least 120 frames old.
- This does not change generic save/load handling and does not apply to CS-menu-only relatches when there is no recent loading close.

Expected next-log signature:

- For door/API render-scale transitions, before D3D starts there should be a log line: `Relatch deferred: D3D render-target recreate waits until loading/menu close is at least 120 frames old`.
- `Relatch step: calling D3D render-target recreate` should occur at closeAge `>=120`, not around `41`.
- Pending frames should still show `original-submit-relatch-guard`, `inputHandoff=no`, and no `cs-upscaled-submit`.
- Recovery should still show `original-submit-relatch-guard`, not `suppressed-submit-relatch-guard`.

## Log 039 - 2026-06-03 17:55 SteamVR Interior-Save To Exterior Crash Despite 120-Frame D3D Gate

Source file:

- `D:\FireFox-downloads\CommunityShaders(23).log`, last write `2026-06-03 17:55:18`, size `1,873,376` bytes.

User result:

- Loading an interior save, then exiting to exterior, crashes.
- Loading an exterior save, then going inside, does not crash in the same way.
- User conclusion: crash depends on where the save was loaded.

Runtime/build identification:

- Runtime is `SteamVR`.
- This log includes the pending-relatch presentation guard and the 120-frame D3D post-close gate from `Log 038`.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Startup relatch queued | `14:51:59.936` | frame `0`, `screen=0x0`, `relatchPending=yes`, reason `Streamline DLSS availability resolved after deferred VR boot latch` | A boot/startup render-scale relatch survives into loaded game state. This is undesirable state hygiene even if not the direct crash tail. |
| First save/load safe mode visible | `14:56:43.612` | frame `8706`, `saveLoad=yes`, `postLoadReset=yes`, `relatchPending=yes` | The game is in the loaded interior save path while render-scale relatch/reset state is still pending. |
| Loading menu closes during save/load path | `14:56:48.058` | frame `9107`, `saveLoad=yes`, `postLoadReset=yes`, `relatchPending=yes` | Save/load and relatch state overlap. |
| Save/load clears | `14:56:49.757` | frame `9226`, closeAge `119`, `saveLoad=no`, `postLoadReset=yes` | Save/load safe mode has ended, but post-load/vendor reset and old relatch state remain. |
| Older pending relatch completes | `14:56:51.363` | frame `9259`, closeAge `152`, `screen=4194x2213` | A stale/startup-side relatch completes after load. This did not crash in this log but creates unnecessary post-load churn. |
| Render-scale transition to exterior begins | `14:57:03.661` to `14:57:10.044` | loading opened frame `10221`, resource change frame `10348`, close frame `10795` | The actual door/API transition that later crashes. |
| New relatch queued | `14:57:10.737` | frame `10804`, closeAge `9`, `quality=1`, `relatchAge=0/20` | Exterior render-scale relatch is queued after the door close. |
| 120-frame D3D gate works | `14:57:10.974` | frame `10824`, closeAge `29`, `relatchAge=0/91` | The new `Log 038` gate correctly defers D3D until closeAge `120`. |
| First relatch attempt after 120 | `14:57:12.177` | frame `10915`, closeAge `120` | Vendor teardown starts and defers once because submit-stage reset is not idle. |
| Vendor teardown completes | `14:57:12.254` | frame `10921`, closeAge `126` | Six-frame D3D cooldown starts. |
| D3D recreate starts | `14:57:12.333` | frame `10927`, closeAge `132` | D3D starts after the 120-frame gate, so this is not the old closeAge `41` failure. |
| Exterior terrain/typed UAV work overlaps D3D | `14:57:12.340` to `14:57:12.886` | many `TypedUAVLoad` and `*.Terrain.HeightMap.*.dds loaded` lines | D3D render-target recreate overlaps exterior terrain/resource initialization. |
| D3D recreate complete | `14:57:13.483` | frame `10927`, closeAge `132` | Render targets and DLSS/common resources finish recreating. |
| First guarded recovery submit | `14:57:13.815` | frame `10927`, `original-submit-relatch-guard`, `inputHandoff=no`, `hmdDefer=yes`, `projectedDefer=yes`, `screen=4194x2213` | CS output and mask repair are still blocked. The log stops immediately after this recovery start. |

Interpretation:

- The 120-frame D3D post-close gate is active and working, but it is not enough for the interior-save -> exterior path.
- The crash is not explained by DLSS/FSR method switching; the failing sequence remains `req=kDLSS runtime=kDLSS`.
- The crash is not explained by CS submit-stage handoff; the failing recovery submit is `original-submit-relatch-guard` with `inputHandoff=no`.
- The crash is not explained by HAM/FOV repair at the tail; diagnostics still show `hmdDefer=yes` and `projectedDefer=yes`.
- The new distinguishing feature is load origin and exterior resource pressure: the player loaded an interior save, then the first exterior render-scale relatch starts D3D while exterior terrain/resource initialization is still active.

Code change made after this log:

- Added save/load origin tracking in `Upscaling.cpp`.
- When save/load safe mode exits, record whether the loaded location ended in an interior or exterior.
- The normal D3D post-close gate remains `120` frames.
- If the last save/load ended in an interior and the current render-scale relatch is now in an exterior, the D3D post-close requirement becomes `300` frames.
- The scoped `300`-frame guard is consumed after a successful exterior relatch, so later exterior CS-menu changes are not permanently slowed by the old load origin.
- The log message now reports the actual required D3D close age (`120` or `300`) so the next log can validate which path ran.

Expected next-log signature:

- After an interior save/load finishes, the log should show `Save/load origin captured after safe-mode exit: location=interior`.
- On the first exterior render-scale relatch after that, the D3D wait should log `loading/menu close is at least 300 frames old`.
- `Relatch step: calling D3D render-target recreate` should occur at closeAge `>=300` for this specific path.
- If the relatch succeeds, the log should show `Consumed post-interior-load exterior relatch guard`.
- Loading an exterior save and then transitioning inside should keep the normal `120`-frame rule, not the scoped `300`-frame rule.

## Log 040 - 2026-06-03 18:55 SteamVR Interior Load Exterior Exit No Crash, Visual Square/HAM Remains

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-03 18:55:34`, size `12,413,542` bytes.

User result:

- No crash.
- A black square appears briefly when entering a door.
- On exit to exterior, a black square is visible and is directly followed by a HAM/FOV-like mask artifact.
- After exiting the door, the stretch/original guarded period before D3D relatch is visibly long.

Runtime/build identification:

- Runtime is `SteamVR`.
- This log includes the pending-relatch presentation guard and the scoped first-exterior-after-interior-load `300`-frame D3D gate from `Log 039`.
- It predates the `2026-06-03` visual recovery adjustment below.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Save/load origin captured | `18:54:13.858` | frame `6875`, `location=interior` | The new origin tracking correctly identifies that the loaded save ended in an interior. |
| Exterior transition resource change | `18:54:27.027` | frame `7643`, `renderScaleRelevant=yes`, `loadingMenu=yes` | The next exterior render-scale transition begins. |
| Loading/menu close | `18:54:45.805` | frame `9897` | Close-age timing source for the D3D safety gate. |
| Exterior relatch queued | `18:54:46.728` | frame `9906`, closeAge `9`, `quality=1`, `relatchAge=0/20` | Render-target relatch is queued after the exterior door close. |
| Scoped 300-frame gate active | `18:54:47.154` | closeAge `29`, remaining `271` | The log reports `D3D render-target recreate waits until loading/menu close is at least 300 frames old`, proving the interior-load exterior guard is active. |
| Guarded original forwarding during wait | `18:54:51.397` onward | frame `10145`, closeAge `248`, `original-submit-relatch-guard`, `inputHandoff=no`, `hmdDefer=yes`, `projectedDefer=yes` | CS submit-stage output remains blocked; any black square in this window is now from the original/vanilla/OpenVR transition source rather than CS vendor output. |
| D3D recreate starts | `18:54:52.622` | frame `10209`, closeAge `312` | D3D starts after the scoped 300-frame threshold, not at the older unsafe closeAge `132`. |
| D3D recreate completes and scoped guard consumed | `18:54:53.832` to `18:54:53.833` | frame `10209`, closeAge `312`, `Consumed post-interior-load exterior relatch guard` | The 300-frame path completes successfully and is consumed after one exterior relatch. |
| Recovery armed | `18:54:53.833` | frame `10209`, stretch `10`, foveated bypass up to `20` frames or `10` stable full-eye frames | This is the current short recovery model before the visual adjustment. |
| Stable full-eye threshold reached | `18:54:54.489` | frame `10228`, closeAge `331`, `10 stable full-eye vendor frames` | The short full-eye threshold completes. |
| HAM/FOV repair resumes immediately | `18:54:54.489` to `18:54:54.544` | frames `10228` to `10229`, `HMD clear dispatch`, `Projected/underwater mask repair dispatch`, then `foveated-vendor-output` | This aligns with the user report that the black square is followed by a HAM/FOV-like mask artifact. |
| Later exterior relatches use normal gate | `18:55:07.863` and `18:55:24.941` | both wait for `120` frames, D3D starts around closeAge `132` | The scoped 300-frame path does not stick after the first exterior relatch. |

Interpretation:

- Stability improved: the first interior-load -> exterior transition survived when D3D relatch was delayed to closeAge `312`.
- The long post-door wait is expected from the scoped `300`-frame gate. It is not a stuck relatch.
- The black square is still not proven to be caused by HAM/FOV repair. During the long pre-D3D guarded window, `hmdDefer=yes` and `projectedDefer=yes` while original submits are forwarded.
- The HAM/FOV-like artifact after the square does have a strong log marker: as soon as `10` stable full-eye frames are observed, HAM/HMD clear and projected repair resume immediately, followed by foveated output on the next frame.
- This suggests the recovery handoff to foveated/HAM is still too abrupt for this transition, even though it no longer crashes.

Code change made after this log:

- `src/Features/Upscaling.cpp`: increased render-scale entry post-relatch recovery from `20` frames to `30` frames.
- `src/Features/Upscaling.cpp`: increased the stable full-eye requirement before foveated/HAM resume from `10` frames to `20` frames.
- `src/Features/Upscaling.cpp`: added a bounded D3D-wait/teardown predicate to keep the foveated bypass active while an explicit D3D post-close wait, vendor teardown, or staged post-teardown cooldown is active.
- Scope: this stays inside render-scale relatch protection. It does not add generic protection for unchanged DLSS/FSR render-scale settings, non-render-scale `None`/`TAA`/`DLAA` transitions, or no-API in-game transitions.

Expected next-log signature:

- On render-scale entry recovery, the log should say `stretch fallback 10 frames, foveated bypass up to 30 frames or 20 stable full-eye frames`.
- HAM/HMD clear and projected repair should not resume immediately after only `10` stable full-eye frames.
- `FOV foveated vendor bypass exited` should happen later, after the longer stable full-eye observation.
- Superseded by `Log 041`: the first exterior relatch after an interior load should now use the scoped `600`-frame D3D post-close gate and consume it after success.
- Later ordinary door/API render-scale relatches should still use the normal `120`-frame D3D post-close gate.

## Log 041 - 2026-06-03 19:07 Tester Crash Despite Scoped 300-Frame Gate

Source file:

- `D:\FireFox-downloads\CommunityShaders(24).log`, last write `2026-06-03 19:07:37`, size `2,072,072` bytes.

User/tester result:

- Tester still crashes with the scoped `300`-frame first-exterior-after-interior-load gate.

Runtime/build identification:

- Runtime is `SteamVR`.
- This log includes save/load origin tracking and the scoped `300`-frame first-exterior-after-interior-load D3D gate.
- This tester build predates the local `30` frame / `20` stable full-eye recovery change. The log still says `foveated bypass up to 20 frames or 10 stable full-eye frames`.

Key sequence:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Boot/startup relatch queued | `20:58:52.303` | frame `0`, `relatchPending=yes`, `screen=0x0` | Boot render-scale relatch state exists from startup. |
| Boot latch created | `20:58:52.873` | `Boot-latched kDLSS quality 1` | DLSS render-scale boot latch exists before the loaded save path is stable. |
| Save/load origin captured | `21:04:44.082` | frame `12129`, `location=interior` | The new origin tracking correctly identifies the loaded save as interior. |
| Interior post-load relatch/reset churn | `21:04:44.097` through later | closeAge `119+`, `renderScaleRequested=no`, `perfActive=yes`, pending reset/relatch | The loaded interior first works through stale render-scale/vendor reset state before the exterior door transition. |
| Exterior transition relatch queued | `21:05:04.122` | frame `13726`, closeAge `9`, `renderScaleRequested=yes`, `perfActive=no`, `relatchAge=0/20` | First exterior render-scale relatch after interior load. |
| Scoped 300-frame gate active | `21:05:04.380` | frame `13746`, closeAge `29`, remaining `271` | The log explicitly reports `loading/menu close is at least 300 frames old`, proving the special gate engaged. |
| First relatch attempt after 300 | `21:05:07.817` | frame `14017`, closeAge `300` | Vendor teardown begins, but submit-stage reset is not idle. |
| Vendor teardown staged | `21:05:07.894` | frame `14023`, closeAge `306` | Vendor teardown completes and a 6-frame D3D cooldown starts. |
| D3D recreate starts | `21:05:07.970` | frame `14029`, closeAge `312` | D3D starts only after `300 + vendor busy retry + 6-frame cooldown`. |
| Exterior resource work overlaps D3D | `21:05:07.978` to `21:05:08.586` | many `TypedUAVLoad`, terrain heightmap loads, SSGI profile | The exterior resource pressure is still active around D3D recreate. |
| D3D recreate complete | `21:05:09.128` | frame `14029`, closeAge `312` | Render targets and vendor/common resources are recreated. |
| Scoped guard consumed | `21:05:09.128` | `Consumed post-interior-load exterior relatch guard` | The special guard is consumed after successful D3D relatch. |
| First guarded recovery submit | `21:05:09.455` | frame `14029`, `original-submit-relatch-guard`, `inputHandoff=no`, `settling=yes`, `stretch=yes`, `hmdDefer=yes`, `projectedDefer=yes` | CS output and mask repair are still blocked. |
| Final logged state | `21:05:09.457` | frame `14030`, `resource change consumed`, `settleRemaining=19`, `stretchRemaining=9` | Log stops immediately after first recovery submit/resource bookkeeping. |

Interpretation:

- The `300` gate did not fail to engage. It engaged correctly and D3D did not start until closeAge `312`.
- The crash is not explained by CS handoff forwarding; the guarded submit has `inputHandoff=no`.
- The crash is not explained by HAM/HMD or projected repair resuming; the final submit/state has `hmdDefer=yes` and `projectedDefer=yes`.
- The current local `30/20` recovery extension is not present in this tester build, but this crash happens before recovery would finish, so do not treat that extension as the primary stability fix for this log.
- The remaining crash marker is still the first exterior-after-interior-load D3D/recovery handoff under exterior resource pressure. For this tester, closeAge `312` is not enough.

Code change made after this log:

- `src/Features/Upscaling.cpp`: increased `kVRRenderScaleRelatchInteriorLoadExteriorD3DPostCloseFrames` from `300` to `600`.
- Scope: only the first render-scale relatch in an exterior after the last save/load ended in an interior.
- The normal door/API render-scale D3D post-close gate remains `120` frames.
- The special guard is still consumed after a successful exterior relatch, so later ordinary exterior transitions are not permanently slowed by the load origin.

Expected next-log signature:

- The same tester path should log `D3D render-target recreate waits until loading/menu close is at least 600 frames old`.
- D3D should start at closeAge `>=600`, plus any vendor busy retry and staged teardown cooldown.
- If the crash still happens after D3D at closeAge `600+`, the next target is not the pre-D3D wait; it is the first post-D3D recovery submit/release path.

## Code Change - 2026-06-03 Clean-Slate Safety-Latch Disable

Reason:

- User requested a clean slate because the current branch has accumulated too many overlapping safety latches and recent tests feel worse than the earlier stable/HAM-fixed state.
- The goal is not to remove logging or state rules. The goal is to disable optional protective timing/visual latches so the next logs show raw transition behavior again, then re-add only minimal proven fixes.

Implementation:

- `src/Features/Upscaling.cpp`: added `kVRRenderScaleTransitionSafetyLatchesEnabled = false`.
- With this switch off, these optional render-scale latches now no-op:
  - render-scale transition grace windows;
  - `120`/`600` D3D post-close gates;
  - post-relatch settling and stretch fallback;
  - submit-stage replacement/compositor upscaling bypass guards;
  - render-scale HAM/HMD and projected mask deferral;
  - render-scale foveated vendor bypass;
  - FSR runtime reset stretch fallback;
  - pending-relatch and allocation-cooldown stretch fallbacks;
  - staged post-teardown D3D cooldown.
- Kept state rules intact:
  - API/menu pending settings still stage and apply through the existing state machine;
  - render-scale relevance still requires actual current/target render-scale involvement;
  - `None`/`TAA`/`DLAA` and unchanged render-scale settings remain excluded from render-scale transition protection;
  - DLSS/FSR method-aware API behavior remains;
  - cross-vendor reset cleanup remains state-based, not latch-based, so DLSS can still retire pending FSR state and FSR can still retire pending DLSS state during render-scale transitions;
  - save/load safe-mode checks remain separate and were not removed.

Expected next-log signature:

- Diagnostics should still log transition state changes, relatch queue/apply steps, vendor teardown/recreate, submit paths, and HAM/FOV dispatches.
- `renderScaleRelevant` may still be `yes`, but latch-derived fields should mostly stay inactive: `graceRemaining=0`, `safetyRemaining=0`, `settling=no`, `stretch=no`, `hmdDefer=no`, `projectedDefer=no`, and `foveatedBypass=no` except during normal save/load safe-mode.
- D3D relatch should no longer wait for `120`, `300`, or `600` post-close frames. If a relatch waits now, it should be because pending settings/menu/load state or vendor resources are not ready, not because of the removed safety windows.

## Log 042 - 2026-06-03 22:24 Local No-Crash Visual Artifact Test

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-03 22:24:12`, size `69,255,975` bytes.

User result:

- No crashes.
- On transition entry, an immediate black square appears briefly in the top-left corner before the normal Skyrim black fade starts.
- At the end of the fade, the black square appears briefly again and then disappears.
- After the square disappears, a transparent HAM/FOV-like mask remains until the render-scale transition is concluded.

Important build/signature finding:

- This log does not match the expected clean-slate latch-disabled source signature.
- Current source has `kVRRenderScaleTransitionSafetyLatchesEnabled = false`, which should suppress render-scale safety latch fields outside normal save/load safe-mode.
- The log still shows old latch behavior:
  - `D3D render-target recreate waits until loading/menu close is at least 120 frames old` at `22:23:21.099`;
  - `settling=yes` and `stretch=yes` after relatch at `22:23:24.467`, `22:23:42.438`, and `22:23:51.070`;
  - `hmdDefer=yes`, `projectedDefer=yes`, and `foveatedBypass=yes` during render-scale relatches;
  - `relatchAge=0/20` and `safetyRemaining=20` for CS-menu preset relatches.
- Therefore this run is useful for visual timing, but it should not be treated as a valid clean-slate test. Either the DLL under test predates the latch-disable change, or a remaining path is still enabling latch behavior and must be found before clean-slate conclusions are drawn.
- User correction after analysis: the latest DLL and an older DLL were both active at the same time.
- Updated interpretation: this is a mixed-DLL/duplicate-plugin run. The old DLL can still install hooks and emit/drive old relatch safety behavior, so this log must not be used to judge the clean-slate source.

Key transition markers:

| Marker | Timestamp | Frame / state | Meaning |
| --- | --- | --- | --- |
| Initial boot latch | `22:18:20.065` | `Boot-latched kDLSS quality 3` | Started in DLSS render-scale mode. |
| Save/load close | `22:19:03.349` | frame `4578`, `saveLoad=yes`, `hmdDefer=yes`, `projectedDefer=yes`, `foveatedBypass=yes` | Normal save/load safe-mode protection is active. |
| Post-save/load render-scale-off relatch queued | `22:19:05.151` | frame `4704`, `relatchAge=0/6`, `hmdDefer=yes`, `projectedDefer=yes`, `foveatedBypass=yes` | Old short relatch safety is active. |
| Post-save/load D3D complete / relatch consumed | `22:19:06.693` | frame `4723`, `settling=yes`, `stretch=yes` | Old post-relatch stretch/settle path is active. |
| Door/API render-scale relatch queued | `22:23:20.970` | frame `26833`, closeAge `14`, `graceRemaining=6`, `relatchAge=0/6` | Relatch starts shortly after the door fade begins. This aligns with the first black-square report at transition entry. |
| D3D post-close wait | `22:23:21.099` | frame `26839`, closeAge `20`, remaining `100` | Old `120`-frame post-close gate is active. |
| D3D starts after wait | `22:23:22.931` | frame around `26945` | DLSS render-scale render targets are rebuilt after the post-close wait and vendor cooldown. |
| Relatch consumed | `22:23:24.467` | frame `26952`, closeAge `133`, `settling=yes`, `stretch=yes`, `hmdDefer=yes`, `projectedDefer=yes`, `foveatedBypass=yes` | World view has returned, but old post-relatch visual safety is still active. |
| HAM/FOV repair resumes | `22:23:24.911` | frame `26971`, after `143` frames, `hmdDefer=no`, `projectedDefer=no`, `foveatedBypass=no` | This is the strongest marker for the transparent HAM/FOV-like mask appearing after the square. |
| CS-menu relatch queued | `22:23:40.200` | frame `28404`, `relatchAge=0/20`, `safetyRemaining=20` | Old 20-frame relatch safety is still active for menu preset changes. |
| CS-menu HAM/FOV repair resumes | `22:23:42.726` | frame `28456`, after `52` frames | Mask repair resumes after relatch recovery. |
| CS-menu HAM/FOV repair resumes again | `22:23:51.345` | frame `29178`, after `52` frames | Same pattern repeats. |

Interpretation:

- Stability was good in this run: no crashes despite several render-scale relatches.
- The immediate top-left black square at transition entry is not proven to be HAM/FOV repair. At the relevant early door-relatch point, HAM/HMD and projected repair are still deferred.
- The transparent HAM/FOV-like mask after the square has a much stronger log match: it appears when the HAM/HMD clear guard, projected repair guard, and foveated vendor bypass exit after relatch recovery.
- Because this log still contains old latch behavior, it cannot tell us whether the clean-slate source removed or changed the square/mask timing. The next useful test must first confirm the clean-slate signature.

No code change from this log:

- Do not tune timings from this run. First remove the duplicate/older DLL and confirm the tested setup has only one active Community Shaders DLL.

Expected next-log signature for a valid clean-slate retest:

- No render-scale `120`/`300`/`600` D3D post-close wait lines.
- No post-relatch `settling=yes` or `stretch=yes`.
- No render-scale `relatchAge=0/20` or `safetyRemaining=20`.
- Outside normal save/load safe-mode, `hmdDefer=no`, `projectedDefer=no`, and `foveatedBypass=no`.

## Log 043 - 2026-06-03 22:38 Clean Head No-Crash No-Artifact Baseline

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-03 22:38:05`, size `68,838,502` bytes.

User result:

- Clean version/head after removing the duplicate older DLL.
- No crashes.
- No black square.
- No transparent HAM/FOV-like mask.

Clean-signature validation:

- The previous mixed-DLL artifacts are gone from the diagnostics.
- No render-scale `D3D render-target recreate waits until loading/menu close is at least 120/300/600 frames old` lines.
- No post-relatch `settling=yes`.
- No post-relatch `stretch=yes`.
- No render-scale `relatchAge=0/20`.
- No render-scale `safetyRemaining=20`.
- Outside normal save/load safe-mode, render-scale relatches show `hmdDefer=no`, `projectedDefer=no`, and `foveatedBypass=no`.
- Normal save/load safe-mode still works: guards entered at `22:33:53.976` and exited at `22:33:58.551` after `422` frames.

Relatch timing overview:

| Try | Scenario / requested state | Queued | Frame / closeAge | D3D complete | Applied | Queue -> applied | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 001 | DLSS render-scale on, quality `1` | `22:34:16.950` | frame `5916`, closeAge `1475` | `22:34:18.377` | `22:34:18.378` | `1428 ms` | Clean relatch; no visual guards. |
| 002 | DLSS render-scale off / full-size | `22:34:29.226` | frame `7087`, closeAge `2646` | `22:34:30.562` | `22:34:30.563` | `1337 ms` | Clean render-scale-off relatch. |
| 003 | DLSS render-scale on, quality `1` | `22:34:38.938` | frame `7930`, closeAge `3489` | `22:34:40.387` | `22:34:40.388` | `1450 ms` | Clean render-scale-on relatch. |
| 004 | DLSS render-scale relatch after menu/transition context | `22:35:05.660` | frame `10470`, closeAge `1195` | `22:35:06.992` | `22:35:06.993` | `1333 ms` | No post-close D3D wait. |
| 005 | DLSS quality/profile change to quality `5` | `22:35:15.302` | frame `11407`, closeAge `2132` | `22:35:16.665` | `22:35:16.665` | `1363 ms` | Clean quality relatch. |
| 006 | DLSS -> FSR, quality `5` | `22:35:22.166` | frame `12041`, closeAge `2766` | `22:35:23.488` | `22:35:23.488` | `1322 ms` | Cross-vendor relatch completed. |
| 007 | FSR quality/profile change to quality `1` | `22:35:33.664` | frame `13256`, closeAge `3981` | `22:35:35.257` | `22:35:35.258` | `1594 ms` | Several retry/apply attempts while teardown completed, but no hang. |
| 008 | FSR -> DLSS, quality `1` | `22:35:53.078` | frame `15109`, closeAge `5834` | `22:35:54.741` | `22:35:54.742` | `1664 ms` | Cross-vendor relatch completed. |
| 009 | DLSS render-scale off / full-size | `22:37:30.106` | frame `25232`, closeAge `47` | `22:37:31.505` | `22:37:31.505` | `1399 ms` | Clean menu-off relatch. |
| 010 | Door/transition DLSS render-scale on | `22:37:48.646` | frame `26266`, closeAge `14` | `22:37:50.099` | `22:37:50.100` | `1454 ms` | Important result: relatch starts close to door exit, no `120` wait, no reported black square/HAM, no crash. |

Other diagnostics:

- One warning occurred at `22:36:27.538`: `Submit-stage foveated kDLSS failed for eye 0; falling back to full-eye vendor dispatch for this frame.`
- This was a one-frame fallback and did not correlate with a crash or stuck transition.

Interpretation:

- This is the first valid clean-slate baseline after removing the duplicate DLL.
- The removal/disablement of optional render-scale safety latches correlates with the disappearance of both visible artifacts: the immediate top-left black square and the later transparent HAM/FOV-like mask.
- The base state machine still handles render-scale activation/deactivation and DLSS <-> FSR transitions without stuck pending relatches in this run.
- D3D recreation itself still costs about `1.2` to `1.27` seconds once called. The important improvement is that the old added post-close wait and visual recovery latches are no longer stretching or contaminating the visible transition.

Success ledger update:

- Success: clean head with only one active DLL completed ten render-target relatches, including DLSS -> FSR, FSR -> DLSS, render-scale off, render-scale on, and a door/transition relatch.
- Success: the door render-scale relatch at closeAge `14` completed without the old `120`-frame wait and without black-square/HAM artifacts.
- Success: disabling optional safety latches while keeping state rules/logging intact is a better current baseline than the accumulated guarded model.

Next validation:

- Repeat normal interior -> exterior and exterior -> interior API-driven transitions from both load origins.
- Repeat short rapid in/out stress with the clean single-DLL setup.
- Treat any future crash separately from the removed visual latches; this log shows the visual artifacts were not necessary for stability in the clean baseline.

## Code Change - 2026-06-03 API Fade Timing Guidance

Reason:

- Clean `Log 043` shows render-target relatches completing in roughly `1.3` to `1.7` seconds with no black square/HAM artifact once optional CS-side safety latches are disabled.
- The remaining best way to hide unavoidable D3D/vendor rebuild cost is to keep the external transition fade-to-black active long enough for the relatch to finish, rather than reintroducing CS-side visual/presentation latches.
- Community Shaders does not own the Papyrus `Game.FadeOutGame` call, so the implementation belongs in the API/controller contract rather than in render code.

Implementation:

- `include/VRAPI/CSinterface001.h`: added ABI-neutral advisory timing constants:
  - `CSVRRenderScaleTransitionFadeOutSeconds = 1.0f`;
  - `CSVRRenderScaleTransitionBlackHoldAfterProfileSeconds = 2.0f`;
  - `CSVRRenderScaleTransitionFadeInSeconds = 1.0f`.
- `API.md`: documented that `Game.FadeOutGame` does not pause CS or serialize D3D/vendor rebuilds.
- `API.md`: documented the recommended controller sequence:
  - fade to black;
  - once black and the destination profile is known, call `SetVRUpscalingTransitionProfileForMethod`;
  - perform the move/cell transition;
  - keep black for at least `2.0` seconds after the profile call or move, whichever is later;
  - fade back in.

Compatibility:

- No vtable changes.
- No build number change required.
- Existing v6/v7 compiled consumers are unaffected.
- New controller source can include the updated header and use the constants, but the values are advisory only.

## Log 044 - 2026-06-03 23:04 Clean-Latch Run, Black Square Remains, No HAM Mask

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-03 23:04:34`, size `52,475,915` bytes.

User result:

- No crash in the submitted run.
- Brief black square still visible at transition exit.
- Transparent HAM/FOV-like mask no longer visible.
- Later in the test, frametime became very high, especially CPU time, while using DLSS/FSR; it settled after switching to DLAA and waiting.
- User also reported a near-crash feeling when switching to DLAA outside in the middle/end of the test.

Clean-signature validation:

- No render-scale `120`/`300`/`600` D3D post-close wait lines.
- No post-relatch `settling=yes`.
- No post-relatch `stretch=yes`.
- No render-scale `safetyRemaining=20`.
- Outside normal save/load safe-mode, render-scale relatches show `hmdDefer=no`, `projectedDefer=no`, and `foveatedBypass=no`.
- Normal save/load safe-mode still appeared only during initial load: loading safe-mode opened at `22:58:58.911`, closed at `22:59:01.499`, and the guards were gone before later render-scale relatches.

Relatch timing overview:

| Try | Scenario / requested state | Queued | Frame / closeAge | D3D time | Applied | Queue -> applied | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 001 | DLSS render-scale off / full-size | `22:59:03.337` | frame `4614`, closeAge `129` | `1208 ms` | `22:59:04.646` | `1309 ms` | Post-load DLAA/full-size relatch. |
| 002 | Door/API DLSS render-scale on | `22:59:18.033` | frame `5910`, closeAge `6` | `1208 ms` | `22:59:20.235` | `2202 ms` | Important black-square candidate. Relatch begins late because it waits until after menu close; no HAM guards. |
| 003 | Door/API render-scale off / full-size | `22:59:54.026` | frame `8924`, closeAge `6` | `1214 ms` | `22:59:55.343` | `1317 ms` | Clean off relatch. |
| 004 | Door/API DLSS render-scale on | `23:00:09.680` | frame `10038`, closeAge `15` | `1205 ms` | `23:00:11.026` | `1346 ms` | Clean on relatch. |
| 005 | Door/API render-scale off / full-size | `23:00:28.008` | frame `11635`, closeAge `6` | `1221 ms` | `23:00:29.435` | `1427 ms` | Clean off relatch. |
| 006 | Door/API DLSS render-scale on | `23:00:36.184` | frame `12070`, closeAge `29` | `1214 ms` | `23:00:37.665` | `1481 ms` | Clean on relatch. |
| 007 | Door/API render-scale off / full-size | `23:00:46.620` | frame `12889`, closeAge `6` | `1205 ms` | `23:00:48.143` | `1523 ms` | Clean off relatch. |
| 008 | Door/API DLSS render-scale on | `23:01:07.711` | frame `14207`, closeAge `30` | `1203 ms` | `23:01:09.195` | `1484 ms` | Clean on relatch. |
| 009 | DLSS render-scale off / full-size | `23:01:55.754` | frame `18095`, closeAge `3918` | `1224 ms` | `23:01:59.094` | `3340 ms` | Slow start before D3D; still completes. |
| 010 | DLSS render-scale on | `23:02:52.273` | frame `21726`, closeAge `30` | `1230 ms` | `23:02:53.785` | `1512 ms` | Clean on relatch. |
| 011 | DLSS -> FSR at same render scale | `23:03:03.587` | frame `22456`, closeAge `760` | `1212 ms` | `23:03:04.984` | `1397 ms` | Cross-vendor relatch completed; FSR reset applied after. |
| 012 | FSR render-scale off / full-size | `23:03:18.660` | frame `23277`, closeAge `1581` | `1217 ms` | `23:03:20.420` | `1760 ms` | Settles to full-size; overlaps resource reload noise. |
| 013 | DLSS render-scale on | `23:04:05.951` | frame `26116`, closeAge `4420` | `1217 ms` | `23:04:07.375` | `1424 ms` | Clean on relatch. |
| 014 | Outside switch to DLAA/full-size | `23:04:31.996` | frame `27276`, closeAge `5580` | `1215 ms` | `23:04:33.376` | `1380 ms` | Near-crash candidate; overlaps resource reloads. |

Black-square interpretation:

- The remaining square is no longer explained by HAM/FOV mask repair in this log. During render-scale relatches, `hmdDefer=no`, `projectedDefer=no`, and `foveatedBypass=no`; the user also reports no persistent HAM mask.
- The strongest matching window is the pending-profile/original-submit handoff before relatch completion.
- First door/API render-scale entry:
  - API/pending render-scale state appears while the loading menu is still open at `22:59:11.576`.
  - Loading menu closes at `22:59:17.976`.
  - Relatch queues at `22:59:18.033`, only `6` frames after close.
  - D3D relatch applies at `22:59:20.235`, about `2.2` seconds after queue and about `2.26` seconds after menu close.
  - Around the close/fade boundary the submit hook logs repeated `original-submit-fallback` with `output=0`, `renderScaleRelevant=yes`, no settling/stretch guard, and no HAM deferral. This matches a brief wrong presentation during the period where the destination render-scale profile is known but the render target has not yet been rebuilt.
- The earlier `2.0` second external black-hold guidance may be borderline for this run if the profile call happens near or after menu close. Either the controller needs to call CS earlier while already black, or the black hold should be tested closer to `2.5` seconds after the profile call/move.

High-frametime / near-crash interpretation:

- The log does not contain direct GPU/CPU frame-time samples, so this is inferred from state changes and resource events.
- No relatch is stuck. Every queued render-target relatch reaches `Applied render-target relatch`.
- At `23:03:03.587` DLSS -> FSR at the same render scale completes in `1397 ms`, then FSR runtime reset applies. At `23:03:18.660` FSR render-scale off / DLAA-full-size completes in `1760 ms`.
- At the late outside DLAA switch:
  - Relatch queues at `23:04:31.996`.
  - D3D recreate starts at `23:04:32.160`.
  - `TruePBR` reloads `49` texture-set JSONs starting `23:04:32.182`.
  - `SSGI resource profile: AO-only resources` appears at `23:04:32.814`.
  - D3D completes at `23:04:33.375`, and DLAA/full-size applies at `23:04:33.376`.
- This near-crash/slow window is most likely a normal D3D/vendor relatch overlapping heavy CS/game resource reload work, not a pending render-scale state stuck forever.
- The log itself is `52 MB` for a short test and includes heavy per-eye submit diagnostics. That can materially distort CPU-ms measurements, so this build/logging level is valid for transition ordering but not clean performance judgement.

Other diagnostics:

- One one-frame submit-stage vendor failure occurred at `23:00:14.852`: `Submit-stage foveated kDLSS failed for eye 0`, then full-eye vendor dispatch and full-size stretch fallback for that frame. It did not correlate with a crash or a stuck transition.

Conclusions:

- Stability is still good in this clean-latch run.
- The transparent HAM/FOV-like mask appears fixed or at least absent in this run.
- The remaining black square is now most likely a presentation/fade-boundary artifact while the old/original submit path is visible before the destination render target relatch has completed.
- The next visual fix should avoid broad HAM/FOV safety latches and instead target the pending profile/original-submit/fade boundary.
- The next fade-controller test should either start the render-scale profile call earlier while fully black or hold black for more than the observed worst transition in this log, with `2.5` seconds as the first practical test value.

No code change from this log yet.

## Log 045 - 2026-06-04 21:14 Tester Save-Origin Profile Mismatch Crash

Source file:

- `D:\FireFox-downloads\CommunityShaders(25).log`, last write `2026-06-04 21:14:46`, size `1,553,923` bytes.

User/tester result:

- Tester has `0.85 DLSS K` / render-scale profile saved in CS settings.
- Loading an interior save immediately switches to DLAA because the VR FPS Stabilizer API wants the interior profile, and that initial switch works.
- First subsequent interior -> exterior transition crashes.
- Loading an exterior save with matching saved CS settings does not need the initial API correction and then repeated interior/exterior transitions reportedly work.
- Opposite mismatch is suspected: saved DLAA/interior settings plus loading an exterior save likely crashes on the first exterior-driven transition.

Important log markers:

- `14:13:22.003`: startup queues a render-target relatch as `req=kDLSS`, `runtime=kDLSS`, `quality=1`, `renderScaleRequested=yes`, `perfRequested=yes`.
- `14:13:22.974`: boot-latches `kDLSS quality 1` at display `2468x2604` per eye -> render `2097x2213` per eye. This confirms the saved CS profile starts as render-scale/Hoshipa.
- `14:18:40.625`: save/load context begins while still effectively in render-scale state: `quality=1`, `renderScaleRequested=yes`, `perfActive=yes`, `saveLoad=yes`.
- `14:18:45.895`: loading menu closes during save-load; vendor reset is queued while `req=kDLSS`, `quality=1`, render-scale still active.
- `14:18:47.579`: save/load safe-mode exits after `120` frames; the vendor reset is still pending.
- `14:18:50.608` to `14:18:51.804`: relatch finally applies to `quality=0`, `renderScaleRequested=no`, `perfActive=no`. This is the interior DLAA/full-size correction and it succeeds.
- `14:19:14.867`: next loading/menu transition begins from the corrected DLAA/full-size state.
- `14:19:18.907`: destination/API state wants render-scale again: `renderScaleRelevant=yes`, `renderScaleRequested=yes`, `pendingVR=yes`, `pendingRenderScale=yes`.
- `14:19:22.142`: exterior render-scale relatch queues as `quality=1`, closeAge `6`.
- `14:19:24.052`: relatch applies successfully back to render-scale.
- `14:19:28.226`: device removal is detected during submit-stage DLSS sharpening: `result=0x887A0006`, and submit-stage upscaling is disabled for the device. This is the crash/failure signature.

Interpretation:

- This is not a simple "DLSS unavailable" fallback case. DLSS loads and is available.
- The crash sequence is strongly tied to starting the game with saved CS upscaling settings that do not match the save's loaded cell profile, then correcting the mismatch via API during/after save-load, then performing the first opposite profile transition.
- The initial load correction itself completes, but it leaves the first real door/API transition after load as the first full render-scale rebuild after a saved-profile mismatch. That is the fragile point.
- The log supports a proactive CS-side alignment feature: when an opt-in sync is enabled, CS should read the same VR FPS Stabilizer unconditional Interior/Exterior upscaling profile and align CS's effective method/preset/profile/render-scale state to the loaded cell during save-load, before the user performs the first real transition.

Failure ledger update:

- Failure: relying only on external API calls after save-load can leave CS's saved upscaling profile mismatched to the loaded cell long enough that the first subsequent opposite transition becomes fragile.
- Failure: do not interpret `UpscalePreset` numeric values from VR FPS Stabilizer as CS internal `qualityMode`. The INI uses public API enum values: `5` means Hoshipa, not internal Performance.

Success ledger update:

- Success: the diagnostics are now good enough to distinguish the initial saved-profile boot latch (`quality=1`) from the later loaded-cell correction (`quality=0`) and the first subsequent exterior render-scale relatch (`quality=1`).

## Code Change - 2026-06-04 VR FPS Stabilizer Save-Load Sync

Reason:

- `Log 045` shows a reproducible crash pattern when the saved CS upscaling profile does not match the save's loaded cell profile.
- VR FPS Stabilizer already has the authoritative desired profiles in `VRFpsStabilizer.ini`.
- The goal is not to add another broad transition safety latch. The goal is to start from the correct loaded-cell upscaling profile so the first post-load door transition is not carrying a stale saved CS profile mismatch.

Implementation:

- Added an opt-in Upscaling setting: `VR FPS Stabilizer Sync`.
- The tooltip explains that on save-load it reads `VRFpsStabilizer.ini` `[Conditional]` unconditional `Interior|CS>` / `Exterior|CS>` upscaling rows.
- Parsed only these upscaling keys:
  - `UpscaleMethod`;
  - `UpscalePreset`;
  - legacy `DLSSMode`;
  - `DLSSProfile` / `DLSSPreset`;
  - `RenderScaleMode`;
  - legacy `RenderAtUpscaleRes` / `RenderAtUpscaleResEnabled`.
- Ignored weather/time-specific condition rows such as `Exterior,Raining|...` because CS cannot safely infer that conditional profile at generic save-load sync time.
- Translated public API `UpscalePreset` enum values to CS internal quality indices:
  - API `0` -> internal `0` Native AA/DLAA;
  - API `5` -> internal `1` Hoshipa;
  - API `6` -> internal `2` Ultra Quality;
  - API `1` -> internal `3` Quality;
  - API `2` -> internal `4` Balanced;
  - API `3` -> internal `5` Performance;
  - API `4` -> internal `6` Ultra Performance.
- Queued sync only when the loading menu closes while save/load safe-mode is active.
- Applied the sync once, after the game is in world and the loading menu is closed.
- Used the existing `ApplyCSMenuUpscalingTransition(...)` path so DLSS/FSR method changes, preset changes, DLSS profile changes, and Render Scale Mode staging remain centralized.
- Explicit `UpscaleMethod=2` selects FSR even when DLSS is available.
- Legacy profiles without `UpscaleMethod` use the same DLSS-preferred method selection as the legacy API, so old `DLSSMode`/`RenderAtUpscaleRes` rows can still switch a saved `None`/`TAA` profile into DLSS, or into the existing non-DLSS fallback if DLSS is unavailable.
- Normal door transitions and unchanged render-scale settings are not newly protected or delayed by this setting.

Expected effect:

- If CS saved settings are exterior render-scale but the user loads an interior save, CS can align to the stabilizer's interior DLAA/full-size profile during save-load.
- If CS saved settings are interior DLAA/full-size but the user loads an exterior save, CS can align to the stabilizer's exterior render-scale profile during save-load.
- If saved CS settings already match the loaded-cell profile, the sync logs an "already matched" no-op and does not stage a transition.

## Log 045 - 2026-06-04 21:20 OpenComposite + SexLabUtil Conflict Report

Source file:

- `D:\FireFox-downloads\CommunityShaders(26).log`, last write `2026-06-04 21:20:45`, size `30,051,873` bytes.

User result:

- Setup: CS branch + OpenComposite/OCU + SexLab VR patch `SexLabUtil.dll`.
- Pressing `End` does not visibly open the CS menu.
- In game, roughly half the HMD view is sky blue / torn.
- CS + SteamVR + SexLab VR patch works.
- CS + OpenComposite/OCU without `SexLabUtil.dll` works.

Important log findings:

- CS did load: `CommunityShaders v1-5-2-0` at `21:36:31.149`.
- CS installed its input and render hooks at `21:36:39.081`, including `BSInputDeviceManager::PollInputDevices`, `WndProcHandler`, render target creation, D3D init, and upscaling hooks.
- OpenComposite was detected at `21:37:54.205` and again at `21:38:03.757`: runtime `OpenComposite`, interfaces `overlay=yes`, `system=yes`, `compositor=yes`.
- CS menu VR overlays were created successfully at `21:37:54.584`:
  - `communityshaders.menu`
  - `communityshaders.menu.controller`
- There is no direct `SexLabUtil` text in the CS log. The game install path from the log was not present locally, so `SexLabUtil.dll` could not be inspected directly.
- No `ShowOverlay failed`, `SetOverlayTexture failed`, or `IVRCompositor::Submit` hook install lines appeared in this log. The menu may be toggling internally but not becoming visible through the VR presentation path, or the `End` key event may not be reaching CS; this log cannot distinguish those two.

Failure marker:

- CS switches to DLSS at `21:37:54.177`: `Resource change detected - Upscale: 1 (kTAA) -> 3 (kDLSS)`.
- Runtime VR intermediates are created at `21:41:28.971`: per-eye input `1795x1958`, output `2112x2304`.
- Starting at `21:42:16.422`, Streamline fails constantly for both eyes:
  - `[Streamline] Could not set constants for eye 0`
  - `[Streamline] Could not set constants for eye 1`
- Count: `5600` `Could not set constants` lines through the end of the log.
- At `21:42:22.691`, CS briefly recreates intermediates at full size `2112x2304 -> 2112x2304`, then immediately reuses the scaled `1795x1958 -> 2112x2304` intermediates at `21:42:22.731`, while Streamline constants continue failing.

Interpretation:

- This is not a "CS did not load" case. CS loaded, hooked, initialized OpenComposite interfaces, and created menu overlays.
- The visible half-sky-blue/torn output is strongly consistent with VR DLSS failing after CS has put the engine into dynamic/render-scale output size. When DLSS fails, the old path can leave the low-resolution per-eye scene visible in a full-size output/presentation slot, which exposes unrendered/cleared regions.
- The CS log alone does not prove where `SexLabUtil.dll` hooks. Public SexLab distribution evidence points to `SexLabUtil.dll` as a binary SKSE/Papyrus support plugin rather than a published render-source plugin, so the most defensible current conclusion is an indirect interaction: `SexLabUtil.dll` changes load/runtime state or timing enough that OpenComposite + CS DLSS enters a Streamline constant failure loop.
- The likely CS/OCU overlap points are:
  - CS input hook: `BSInputDeviceManager::PollInputDevices` for `End`.
  - CS menu overlay path: `IVROverlay` handles and/or in-scene menu submit path.
  - CS VR presentation/upscaling path: per-eye DLSS constants/evaluate and `IVRCompositor::Submit` when submit-stage is active.

Code change from this log:

- `src/Features/Upscaling/Streamline.cpp`: if VR DLSS/DLAA evaluation fails for either/both eyes after per-eye inputs were prepared, use the existing per-eye full-size stretch fallback and finalize both eyes into the output instead of leaving the dynamic-resolution scene texture as-is.
- `src/Features/Upscaling.cpp`: added forward declarations for helper functions used by the existing local VR FPS Stabilizer sync changes so the current worktree builds.

Verification:

- `cmake --build build\ALL --config Release --target CommunityShaders` passed and produced `build/ALL/Release/CommunityShaders.dll`.

Expected next-log signature:

- If the same OpenComposite + `SexLabUtil.dll` setup still causes Streamline `Could not set constants`, the log should now include one of:
  - `VR DLSS/DLAA evaluate did not complete for both eyes; using full-size stretch fallback for this frame.`
  - `VR DLSS/DLAA direct-eye evaluate failed; using full-size stretch fallback for this frame.`
- The HMD view should degrade to a stretched full-size frame rather than a half-blue/torn dynamic-resolution frame.
- If the menu still does not appear, add explicit input/menu visibility diagnostics next; this log proved overlay creation but did not prove whether `End` reached `Menu::ProcessInputEvents`.

## Code Change - 2026-06-04 VR FPS Stabilizer Sync Review Hardening

Reason:

- Review of the uncommitted sync implementation found the core scope correct: it is an opt-in save-load alignment tool, not a new general transition safety latch.
- The remaining robustness gap was parsing: legacy/current render-scale aliases should tolerate numeric and boolean-style INI values, while preset/profile/method values should remain numeric.
- Legacy API rows must stay DLSS-preferred for old NVIDIA configs, but explicit build-7 `UpscaleMethod` rows must be able to select FSR.

Implementation:

- Reused a shared ASCII boolean parser for OpenComposite and VR FPS Stabilizer INI handling.
- Accepted `true/false`, `yes/no`, `on/off`, `enabled/disabled`, and numeric `0/1+` for render-scale aliases.
- Rejected negative numeric strings so invalid values cannot wrap into large unsigned settings.
- Kept explicit `UpscaleMethod` authoritative over legacy DLSS markers.
- Kept legacy-only `DLSSMode` / `DLSSProfile` / `RenderAtUpscaleRes` profiles DLSS-preferred through `GetLegacyDLSSPreferredUpscaleMethodForAPI()`.

Verification:

- `git diff --check` is clean.
- No build run in this chat per instruction.

## Log 046 - 2026-06-04 22:03 OpenComposite CS Menu Render-Scale Enable Crash

Source files:

- `D:\FireFox-downloads\CommunityShaders\CommunityShaders.log`, last write `2026-06-04 22:03:21`, size `119,697,319` bytes.
- `D:\FireFox-downloads\crash-2026-06-04-21-03-21(1).log`, last write `2026-06-04 22:07:03`, size `72,207` bytes.

User result:

- Crash from a user while changing render-scale settings and exiting the CS menu.
- Runtime stack includes OpenComposite/OpenXR (`xrCreateSwapchain`), `openvr_api.dll`, `vrclient_x64.dll`, ENB, and `CommunityShaders.dll`.

Timeline:

- `23:02:11.667`: render-scale disable relatch starts from CS menu and successfully recreates full-size render targets (`6800x3428`).
- `23:02:13.377`: state is stable with `renderScaleRelevant=no`, `perfRequested=no`, `perfActive=no`.
- `23:03:12.991`: render-scale is enabled again from CS menu (`renderScaleRequested=yes`, `perfRequested=yes`, pending relatch).
- `23:03:15.464`: first relatch attempt starts DLSS vendor teardown but defers because submit-stage DLSS resources are still in use; retry is scheduled after `6` guarded frames.
- `23:03:15.564`: retry completes vendor teardown, boot-latches DLSS Quality, and calls D3D render-target recreate.
- `23:03:16.603`: D3D recreate completes at render-scale size (`4532x2285`) and relatch is applied.
- `23:03:16.931`: VR intermediates are recreated for per-eye input `2266x2285` and output `3400x3428`.
- `23:03:21.654`: submit-stage foveated/HMD clear runs, then device removal is detected during submit-stage DLSS sharpening: `0x887A0006`.
- Immediately after device removal, the hook logs `original-submit-fallback` with input texture `4532x2285`, output `0`, and render-scale still active. The crash stack then lands in OpenComposite/vrclient swapchain handling.

Interpretation:

- This is not a crash at D3D render-target recreate. The relatch and vendor/common resource recreation completed.
- The direct failure marker is submit-stage device removal during DLSS sharpening. After that, CS correctly disables submit-stage upscaling, but the submit hook then falls through to vanilla OpenVR submit while the source texture is still the low-resolution render-scale target.
- In this state the original texture is not a final HMD-sized presentation texture. Handing it to OpenComposite/ENB as a normal submit target can trigger swapchain/device failure.

Code change from this log:

- `src/Features/VR/InSceneOverlay.cpp`: if submit-stage device loss is already marked and render-scale/perf mode is still active, suppress the OpenVR submit and return `VRCompositorError_None` instead of falling through to `original-submit-fallback`.
- The suppression logs once as `device-lost-render-scale-submit-suppressed` to avoid per-frame spam after the device is already lost.

Expected effect:

- This does not make a removed D3D device recover. It prevents the secondary crash path where, after device removal, OpenComposite receives the low-resolution render-scale texture as if it were a valid final HMD submit target.
- If the underlying device removal still occurs, the expected result is a frozen/blanked submit path rather than an immediate OpenComposite swapchain crash.

Verification:

- `git diff --check` is clean.
- No build run in this chat per instruction.

## Log 047 - 2026-06-05 01:16 First Interior -> Exterior Black-Screen Crash, No Transition Flush

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`, last write `2026-06-05 01:16:20`, size `58,844` bytes.

User result:

- Crash after the first interior -> exterior transition.
- HMD image stayed black.

Important log findings:

- The log contains only `3` `[VRTransition]` lines.
- Last line is `01:16:20.601`, still during startup/loading state:
  - `loadingMenu=yes`
  - `cellContext=yes`
  - `req=kDLSS runtime=kDLSS quality=0`
  - `renderScaleRelevant=no`
  - `renderScaleRequested=no`
  - `perfRequested=no`
  - `perfActive=no`
  - `pendingVR=no`
  - `pendingRenderScale=no`
  - `relatchPending=no`
  - `relatchInProgress=no`
  - `pendingReset(DLSS=no,FSR=no,DLSSHistory=no)`
  - `submitDeviceLost=no`
- Earlier startup resource change at `01:16:17.598` was `kTAA -> kDLSS`, `quality 0 -> 0`, `renderScale 0 -> 0`, `foveatedDispatch no -> yes`, `peripheryTAA no -> yes`, with `renderScaleRelevant=no`.
- There are no logged door-transition markers after startup:
  - no loading-menu close tail
  - no VR FPS Stabilizer sync apply/no-op
  - no render-scale relatch queue
  - no vendor teardown
  - no D3D render-target recreate
  - no OpenVR submit suppression/fallback/device-lost marker

Interpretation:

- This log does not capture the reported interior -> exterior transition. Either the wrong/overwritten log was supplied, or the game/VR runtime hard-stopped before CS could flush the first transition diagnostics.
- The black-screen report fits a stop during Skyrim/VR loading fade or very early loading-menu state, not a proven CS render-scale relatch failure from this log alone.
- Because `renderScaleRelevant=no` and no relatch was pending in the final logged state, this sample must not be used as evidence for a D3D relatch crash, submit-stage device-loss crash, or HAM/FOV repair issue.

Code change from this log:

- None. The log is diagnostically insufficient for a targeted fix.

Next evidence needed:

- A CrashLogger crash file from the same run, if one exists.
- A CS log that continues past loading-menu close or contains the first actual `VRTransition` state change after the door transition.
- If this repeats with the log stopping at `loadingMenu=yes`, add a very early loading-menu-close / cell-attach heartbeat or flush point only for transition diagnostics.

## Log 048 - 2026-06-05 01:19 VR FPS Stabilizer Sync Queued But Not Applied

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`

User result:

- With `VR FPS Stabilizer Sync` active, in-game/CS-menu DLSS/FSR changes appeared to stop executing at some point.

Important log findings:

- `01:19:19.621`: `VR FPS Stabilizer Sync queued after save-load menu close at frame 5610`.
- No matching `VR FPS Stabilizer Sync applying...` or `profile already matched...` line appears anywhere in the log.
- `01:19:35.863`: a new loading menu opens before any sync apply/no-op line is logged.
- Later changes do execute, so sync is not acting as a continuous settings lock:
  - `01:19:36.903`: render scale request arrives.
  - `01:19:46.139`: render-target relatch applies.
  - `01:37:48.742`: DLSS -> FSR executes.
  - `01:37:56-01:38:15`: FSR quality changes execute.
  - `01:38:23.911`: FSR -> DLSS with render scale executes.
  - `01:38:37.921`: render scale off executes.
  - `01:38:45.060`: DLSS quality `0 -> 2` executes with render scale off.
- Some changes are deferred while a render-target relatch owns resources, and one DLSS vendor runtime reset is deferred because the DLSS rebuild is not idle. That can make UI/API changes appear delayed, but the log shows they eventually settle.

Interpretation:

- `VR FPS Stabilizer Sync` is not continuously overriding later in-game changes.
- The actual gap is lifecycle visibility and reliability: the queued save-load sync can be silently cleared before it applies, especially if another loading menu starts before `ConfigureUpscaling()` reaches a world-ready state.
- This can leave save-load state unsynchronized without leaving a clear diagnostic trail.

Code change from this log:

- Removed the silent pending-sync clear on loading-menu open.
- Added explicit lifecycle logging for pending save-load sync:
  - retained when a loading menu opens before it can apply
  - waiting for world-ready state, throttled to one line per `120` frames
  - cancelled when an explicit CS menu/API upscaling transition supersedes it
  - queued/applied/already-matched logs now include queued/applied frame information
- Added `pendingVRFpsStabilizerSyncLastWaitLogFrame` so waiting logs do not spam the file.

Expected effect:

- Sync should no longer vanish silently.
- If a manual/API upscaling change supersedes the queued sync, the log will state that clearly.
- If the sync stays pending because loading/world state is not ready, the log will show that at a controlled interval.
- This does not make sync a permanent lock; later CS menu/API changes still cancel or replace pending sync.

Verification:

- `git diff --check -- src\Features\Upscaling.cpp src\Features\Upscaling.h` is clean.
- No build run in this chat per instruction.

## Code Change - 2026-06-05 Conservative 6s External Fade Hold

Reason:

- Review of the accumulated timing data separates normal door relatches from first-load/profile-mismatch stabilization.
- Clean normal render-scale relatches usually apply in about `1.3` to `1.7` seconds, with the observed clean D3D recreate itself around `1.2` seconds.
- First-load saved-profile mismatch can take much longer: `Log 045` took about `5.9` seconds from save/load close until the loaded-cell upscaling profile had corrected and settled.

Implementation:

- `include/VRAPI/CSinterface001.h`: changed `CSVRRenderScaleTransitionBlackHoldAfterProfileSeconds` from `2.0f` to `6.0f`.
- `API.md`: updated the advisory transition-controller text to explain that the `6.0` second hold is conservative for first-load/profile-mismatch stabilization, not because normal D3D relatches always need that long.

Expected effect:

- API/controller authors using the advisory constants will hold black long enough to cover the worst recorded first-load correction case.
- Community Shaders render code is not delayed by this value; it remains external fade guidance only.

Verification:

- Pending after this note.

## Log 049 - 2026-06-14 22:53 Pre20 Report / Pre35 Workspace Menu RenderScale and VR Binding Restore

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`

Build / tag tracking:

- User-reported test context: `Pre20`.
- Workspace/source position at analysis time: `Pre35`.
- DLL log banner: `CommunityShaders v1-6-1-0`.
- Runtime: SteamVR.
- Log last write time: `2026-06-14 22:53:01`.
- Log size: `115701464` bytes.

User result:

- With render scale enabled, text in menus jitters and moves slightly with HMD movement.
- Pressing `Restore default values` for controller VR bindings in the VR Bindings UI makes the VR menu disappear from the HMD. After that, opening and closing CS menus only affects the desktop view, not the HMD view.
- Without using VRAPI, transitions between the same render-scale modes did not cause the black fade/square issue.

Important log findings:

- The log contains repeated `VRMenuDiag` risk verdicts while render scale and submit-stage presentation are active.
- Verdict counts from this log:
  - `risk:menu-ui-viewport-at-renderscale`: `9184`
  - `risk:menu-ui-rendering-at-renderscale`: `4948`
  - `ok:submit-stage-active`: `6747`
  - `ok:submit-menu-presentation`: `2912`
  - `ok:menu-ui-native-path`: `3232`
  - `ok:menu-ui-full-presentation-target`: `1607`
  - `check:menu-ui-nonpresentation-target`: `436`
- At `22:49:00.191` / frame `51845`, submit-stage DLSS is active with `owner=VRRenderScaleMode`, `target=SubmitStageIntermediate`, `screen=3290x1826`, `engine=3290x1826`, `final=4936x2740`, `renderScaleActive=yes`, `submitStageActive=yes`, and `presentationUpscaling=yes`.
- Immediately after that, `MenuManagerDrawInterface` reports `role=menu-ui`, `verdict=risk:menu-ui-rendering-at-renderscale`, and the same render-scale submit-stage ownership.
- At `22:49:02.869` / frame `52097`, submit-menu presentation is detected as `ok:submit-menu-presentation`, but `MenuManagerDrawInterface` in the same frame still reports `risk:menu-ui-viewport-at-renderscale` and `risk:menu-ui-rendering-at-renderscale`.
- This matches the menu text jitter report: menu presentation can be routed through submit-menu handling, but menu draw state is still tied to render-scale viewport/scissor/submit-stage state while HMD pose is moving.
- Later render-scale sections continue to emit the same menu-risk verdicts, including CS menu draws.
- Native/non-render-scale sections mostly return to `ok:menu-ui-native-path` or `ok:menu-ui-full-presentation-target`, which makes the render-scale-specific risk more credible.

VR binding restore finding:

- The log does not contain a direct, searchable `Restore default values` or VR binding reset event.
- Relevant overlay setup does appear at `22:49:00.172`: `VR: Installing IVRCompositor::Submit hook for in-scene overlay rendering` and `VR: In-scene overlay initialized`.
- After the reported binding restore, the tail contains repeated `VRSubmit original-submit-fallback` lines with `renderScaleRelevant=no`, `pendingRelatch=no`, `vendorPending=no`, and `output=0x0`.
- Menu draw diagnostics continue on the desktop/projected menu path, but the HMD route appears to have stopped receiving the CS menu presentation.
- Current conclusion: binding restore likely resets or invalidates OpenVR controller binding / overlay routing outside the current CS log visibility. The failure is real in user observation, but the log needs explicit before/after diagnostics around the restore action to prove the exact handle or routing loss.

Same-mode transition / VRAPI finding:

- User observation: without VRAPI, same render-scale mode transitions did not reproduce the black fade/square issue.
- This log does not show a clear same-mode black-square/fade failure marker.
- Current conclusion: the black fade/square issue should stay separated from normal internal same-mode render-scale relatches. The stronger suspect remains external VRAPI black-hold/fade timing or transitions that actually change render-scale/submit-stage ownership.

Transition timing sample from this log:

- First render-scale relevant resource change: `22:48:57.338`, frame `51575`.
- Render-target relatch pending entered: `22:48:59.751`, frame `51833`, `relatchDelay=6`.
- Vendor resource release initially deferred: `22:48:59.829`, frame `51839`.
- Vendor teardown completed and D3D recreate requested: `22:48:59.908`.
- D3D recreate completed: `22:48:59.926`.
- Resource change synced and first submit-stage active frames observed: `22:49:00.191`, frame `51845`.

Timing deltas:

- Resource change to relatch pending: about `2.413s`.
- Resource change to D3D recreate complete: about `2.588s`.
- Resource change to first synced submit-stage active frame: about `2.853s`.
- Relatch pending to D3D recreate complete: about `0.175s`.
- D3D recreate complete to first synced submit-stage active frame: about `0.265s`.

Timing interpretation:

- This is slower than the clean normal relatch samples recorded earlier, but still well under the conservative external `6.0s` fade hold used for first-load/profile-mismatch stabilization.
- The delay is mostly before relatch pending starts, not in the D3D recreate itself.
- Same-mode/no-VRAPI transitions should not be treated as equivalent to first-load/profile-mismatch timing.

Implementation implications:

- Menu/HUD text should be treated as unsafe whenever `renderScaleActive=yes` and `submitStageActive=yes` while `MenuManagerDrawInterface` reports a render-scale viewport or render-scale rendering verdict.
- A likely fix direction is to force known menu / CS menu 2D presentation back onto full-size native presentation target state, or to clamp menu viewport/scissor to the full presentation target even while scene render scale remains active.
- Add direct diagnostics around VR Bindings UI `Restore default values`: before/after OpenVR overlay handles, controller binding state, HMD overlay visibility, menu presentation mode, and whether CS switches to `original-submit-fallback`.
- Keep external VRAPI black-hold/fade behavior separated from internal render-scale relatch behavior in both logging and timing notes.

Code change from this log:

- Documentation only in this note.

Verification:

- No build or runtime validation performed for this documentation update.

## Log 051 - 2026-06-14 23:57 Pre36 CS Menu Flicker While Render Scale Is Active

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`

Build / tag tracking:

- User-reported test context: `Pre36`.
- DLL log banner: `CommunityShaders v1-6-1-0`.
- Runtime: SteamVR.
- VRAPI usage: none in this run.
- Log start: `23:51:15.587`.
- Log end / last write: `2026-06-14 23:57:38`.
- Log size: `61236267` bytes.
- Log line count: `255672`.

Standing process note:

- Every future user-provided log update should add a new section to this file.
- Each log section should track the tested build/tag, user-observed result, important log findings, transition timing deltas, implementation implications, code changes made from the log, and verification status.

User result:

- Letter jitter in menus is likely fixed or significantly improved.
- New dominant problem: while render scale is active and a menu is open, the world-space image flickers massively.

Important log findings:

- The log contains one error and `75` warnings.
- Menu/render-scale risk is still present:
  - `risk:menu-ui-rendering-at-renderscale`: `6651`
  - `risk:menu-ui-viewport-at-renderscale`: `1636`
  - `ok:submit-stage-active`: `3487`
  - `check:state-snapshot`: `3486`
  - `ok:menu-ui-native-path`: `204`
- The most important pattern is Community Shaders menu frames where the CS menu is open but VR menu presentation is not active:
  - Example window around `23:52:34.019`, frame `6500`.
  - Diagnostic state: `csMenu=yes`, `vrMenuPresentation=no`, `submitStageActive=yes`, `presentationUpscaling=yes`.
  - Runtime target state: `owner=VRRenderScaleMode`, `target=SubmitStageIntermediate`, `screen=3290x1826`, `engine=3290x1826`, `final=4936x2740`.
- This directly explains the user's world-space flicker report: CS menu frames were not routed through the existing VR menu-presentation protection, so submit-stage render-scale presentation could keep running while the CS menu was open.
- Known Skyrim menu frames were improved but still produced risk diagnostics:
  - Example around `23:53:02.428`, frame `8554`.
  - Diagnostic state: `knownMenu=yes`, `gameMenu=yes`, `vrMenuPresentation=yes`, `submitStageActive=no`, but owner/target still showed `VRRenderScaleMode` / `SubmitStageIntermediate`.
- The known-menu risk may be partly diagnostic lag or stale runtime-plan state, but it still shows the plan and target metadata are not fully synchronized with the menu-protected path.
- Save/load and vendor-runtime instability are still visible:
  - `23:52:27.159`: Streamline deferred DLSS free and VR post-load runtime reset because vendor resources were still in use.
  - `23:52:27.205`: submit-stage DLSS rebuild deferred after VR reset because Streamline resources were still in use.
  - `23:52:27.420`: desktop mirror writeback skipped because the submit texture was not a compatible full stereo target.
  - `23:52:48.111`: Streamline could not set constants for eye `0`.
  - `23:52:48.111`: submit-stage `kDLSS` failed for eye `0`; full-size stretch fallback used for that frame.
  - `23:55:58.160`: dynamic-resolution upsample replacement copy failed with source `fmt=28` and main `fmt=26`.
- Repeated render-target relatch deferrals occurred because vendor resources were still in use:
  - `23:54:41.806`
  - `23:54:53.452`
  - `23:55:08.447`
  - `23:56:12.122`
  - `23:56:24.574`
  - `23:56:43.192`
  - `23:57:02.794`
  - `23:57:12.358`
  - `23:57:23.908`

Transition timing update:

- Initial DLSS render-scale resource change:
  - Resource change: `23:51:31.500`, `kTAA -> kDLSS`.
  - Initial loading menu state observed with render scale active: `23:51:35.671`.
  - Loading menu closed: `23:52:10.268`, frame `4102`.
  - Save/load menu opened: `23:52:15.386`, frame `4664`.
  - Save/load menu closed and vendor runtime reset entered/queued: `23:52:24.853`, frame `5801`.
  - Vendor runtime reset waiting on load/transition context: `23:52:25.728`.
  - First notable submit-stage presentation frame during menu/loading context: `23:52:25.728`.
  - Submit-stage DLSS failure / fallback: `23:52:48.111`.
- Timing deltas:
  - Resource change to initial loading menu render-scale state: about `4.171s`.
  - Resource change to loading menu close: about `38.768s`.
  - Resource change to save/load close and queued vendor reset: about `53.353s`.
  - Resource change to reset waiting / menu-loading presentation frame: about `54.228s`.
  - Resource change to submit-stage DLSS failure: about `76.611s`.
  - Save/load close to reset waiting / presentation frame: about `0.875s`.
  - Save/load close to submit-stage DLSS failure: about `23.258s`.
- This run does not provide a clean normal relatch completion sample before the visible menu flicker. The transition is dominated by loading/menu context plus vendor-resource deferrals.
- The repeated deferrals from `23:54:41.806` through `23:57:23.908` span about `162.102s`, which should be tracked separately from normal clean relatch timing.

Interpretation:

- Pre36 likely fixed the main perceived menu-letter jitter by keeping known projected menu text away from submit-stage presentation.
- Pre36 did not fully protect CS menu presentation because `IsVRMenuPresentationContextActive()` did not include `IsCommunityShadersMenuOpen()`.
- The world flicker with menus open is therefore a separate bug from the original text jitter: submit-stage render-scale presentation remained active for CS menu frames.
- Runtime-plan metadata also needs to use the central VR menu-presentation predicate so menu frames do not continue to look like normal render-scale scene frames to later submit/fallback logic.

Implementation implications:

- CS menu must be treated as a VR menu-presentation context everywhere the submit-stage/render-scale path is blocked.
- Runtime resolution planning should use the central VR menu-presentation predicate for VR instead of plain `IsGameMenuContextActive()`.
- A short CS-menu presentation tail should be extended before `PostDisplay()`, so the current frame is protected early enough.
- Submit fallback should use the central predicate and the runtime plan, not partial duplicated checks that can miss CS menu frames.

Code change from this log:

- Working-tree code updated in `src\Features\Upscaling.cpp`:
  - `IsVRMenuPresentationContextActive()` now includes `IsCommunityShadersMenuOpen()`.
  - VR runtime resolution planning now sets `menuContextActive` from `IsVRMenuPresentationContextActive()`.
  - Submit presentation checks now include both the runtime plan menu context and current central VR menu-presentation context.
  - CS-menu presentation tail extension now happens before `PostDisplay()`.
  - AAVRS menu pause UI state now uses the central VR menu-presentation predicate in VR.

Verification:

- Build validation passed with `cmake --build build\ALL --target CommunityShaders --config Release`.
- No runtime validation performed after this fix.

## Log 050 - 2026-06-14 23:19 Pre35 Menu Jitter, Load Flicker, and FSR/DLSS Reset Churn

Source file:

- `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`

Build / tag tracking:

- User-reported test context: `Pre35`.
- DLL log banner: `CommunityShaders v1-6-1-0`.
- Runtime: SteamVR.
- VRAPI usage: none in this run.
- Log start: `23:06:07.299`.
- Log end / last write: `2026-06-14 23:19:56`.
- Log size: `148150179` bytes.
- Log line count: `570016`.

User result:

- Text in menus still jitters when render scale is on.
- Initially, outside while the save/load menu was open, the world-space view flickered, as if upscaling did not work correctly.
- After going inside Dragonsreach and changing FSR settings, CPU/GPU frame times did not settle. They only settled later after going back outside and making another render-scale change.
- No VRAPI was used.

Important log findings:

- Menu render-scale risk is still present in Pre35:
  - `risk:menu-ui-rendering-at-renderscale`: `5228`
  - `risk:menu-ui-viewport-at-renderscale`: `1428`
  - `ok:submit-stage-active`: `3532`
  - `ok:submit-menu-presentation`: `860`
  - `ok:menu-ui-native-path`: `15710`
  - `ok:menu-ui-full-presentation-target`: `7855`
- Example risk window starts after DLSS render scale is enabled at `23:15:30.603`.
- At `23:15:35.241` and following frames, `MenuManagerDrawInterface` reports `role=menu-ui`, `owner=VRRenderScaleMode`, `target=SubmitStageIntermediate`, `screen=3290x1826`, `engine=3290x1826`, `final=4936x2740`, `renderScaleActive=yes`, `renderScaleRequested=yes`, `perfModeActive=yes`, `submitStageActive=yes`, and `presentationUpscaling=yes`.
- This confirms the previous diagnosis: menu text is still being drawn while the active render target / viewport state belongs to the render-scale submit-stage path.
- Native/non-submit-stage sections before the render-scale switch report `ok:menu-ui-native-path` or `ok:menu-ui-full-presentation-target`, so the jitter is still specifically tied to render-scale submit-stage presentation.

Initial outside / save-load flicker finding:

- Save-load menu opened at `23:13:49.753`.
- Save-load menu closed at `23:13:59.155`.
- At `23:14:01.074`, shortly after the save-load close tail, the log reports:
  - `[Upscaling] Dynamic-resolution upsample replacement could not copy source to main: input=4936x2740 source=4936x2740 fmt=28 samples=1 main=4936x2740 fmt=26 samples=1`
- This is a concrete render-copy failure near the user's outside/load-menu flicker observation.
- The failure is a source/main format mismatch while dimensions match. That fits a temporary frame where dynamic-resolution replacement cannot copy into the main path and the scene can appear as if upscaling is not working.
- At `23:15:18.213`, Streamline reports `Could not set constants for eye 0` and `Could not set constants for eye 1`.
- At `23:15:18.218`, Streamline falls back: `VR DLSS/DLAA direct-eye evaluate failed; using full-size stretch fallback for this frame.`
- This is another short-frame upscaling failure before the first render-scale relatch fully settles.

FSR / Dragonsreach stability finding:

- The log does not emit the actual CPU/GPU frame-time values shown to the user, so the CPU/GPU non-settling result is user-observed rather than directly measurable from this log.
- The log does show a serious resource-state churn sequence that matches the observation:
  - `23:15:52.609`: DLSS submit-stage changes `1 -> 0`.
  - `23:15:55.162`: relatch starts with `req=kFSR`, `runtime=kDLSS`.
  - `23:15:55.404`: DLSS submit-stage resources are not idle and render-target relatch is deferred.
  - `23:16:00.849`: FSR quality `2 -> 4`.
  - `23:16:10.193`: FSR quality `4 -> 3`.
  - `23:16:18.083`: FSR quality `3 -> 1`.
  - `23:16:18.249`: FSR quality `1 -> 2`.
  - `23:16:19.072`: FSR -> none.
  - `23:16:21.178`: none -> FSR.
  - `23:16:22.826`: FSR -> DLSS.
  - `23:16:30.224` to `23:16:32.752`: repeated DLSS quality changes.
  - `23:17:18.840`: DLSS render-scale submit-stage is requested again.
- The worst section lasts from the `23:17:18.840` render-scale request until `23:18:34.440` when resource-change tracking finally syncs after relatch.
- During that window the log shows repeated vendor-resource deferrals, including:
  - `23:17:19.705`: render-target relatch deferred because vendor resources are still in use.
  - `23:17:39.431`: render-target relatch deferred again.
  - `23:17:51.778`: FSR resource teardown deferred because the D3D11 queue did not become idle.
  - `23:18:33.476`: render-target relatch deferred again.
  - `23:18:34.399`: D3D render-target recreate completes.
  - `23:18:34.440`: resource change synced.
- This is about `75.600s` from render-scale request to final synced state, far outside the normal clean relatch timings.
- The long section includes a method/runtime mismatch and both vendor reset paths being involved: requested DLSS while runtime still tracks FSR, with pending DLSS/FSR reset state.
- User observation says stability returned only later after going back outside and making another render-scale change. The log supports that as a plausible state-reset escape, not as a normal expected transition.

Later exterior / second render-scale change:

- `23:19:23.682`: DLSS -> FSR.
- `23:19:32.855`: FSR quality `0 -> 2`.
- `23:19:40.479`: FSR submit-stage changes `0 -> 1`, `renderScaleRelevant=yes`.
- `23:19:41.218`: render-target relatch is again deferred because vendor resources are still in use.
- The log ends at `23:19:56.173`, so long-term settling after this exterior change is mainly user-observed. The log does confirm that a new FSR render-scale transition was requested near the end of the run.

Transition timing update:

- Clean-ish DLSS render-scale enable in this log:
  - Resource change: `23:15:30.603`.
  - Relatch pending entered: `23:15:31.553`.
  - D3D recreate complete: `23:15:31.833`.
  - Resource change synced: `23:15:32.121`.
  - Resource change to relatch pending: about `0.950s`.
  - Resource change to D3D recreate complete: about `1.230s`.
  - Resource change to synced submit-stage state: about `1.518s`.
- Pathological FSR/DLSS reset churn:
  - Resource change / render-scale request: `23:17:18.840`.
  - First vendor-resource relatch defer: `23:17:19.705`.
  - D3D recreate complete: `23:18:34.399`.
  - Resource change synced: `23:18:34.440`.
  - Resource change to synced state: about `75.600s`.

Interpretation:

- Pre35 did not fix the render-scale menu jitter.
- The load/save flicker has a concrete dynamic-resolution copy failure near the user's observation.
- The Dragonsreach FSR instability is probably not a menu-only problem. It looks like vendor resource teardown/reset churn between FSR and DLSS with render-scale relatches entering while vendor resources are still busy.
- Since VRAPI was not used, the menu jitter, load flicker, and FSR/DLSS churn in this log are internal CS/upscaling state issues, not external VRAPI fade/black-hold issues.

Implementation implications:

- Menu rendering still needs a hard full-presentation/native target path while render scale submit-stage is active.
- Dynamic-resolution upsample replacement should either handle the `fmt=28` source to `fmt=26` main mismatch safely or bypass that replacement during the save-load close tail.
- The vendor reset state machine needs a stronger guard for FSR/DLSS method changes while render-scale relatch is pending or while either vendor runtime is not idle.
- Logging should add compact CPU/GPU timing snapshots if the CS overlay timing values are available, because the current log can only infer non-settling from reset churn.
- The long `75.600s` churn case should be tracked separately from normal relatch timings and from VRAPI fade timings.

Code change from this log:

- Documentation only in this note.

Verification:

- No build or runtime validation performed for this documentation update.
