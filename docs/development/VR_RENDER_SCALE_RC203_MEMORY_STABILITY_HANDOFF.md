# VR Render Scale RC203 memory-stability handoff

## Purpose and status

This branch integrates the local VR memory-stability work and the Render Scale
fallback fixes on the exact RC203 base (`5542e798`). It was developed in a
separate worktree so the RC190 memory-investigation tree and its DLL were not
modified.

The current change set addresses two related failure classes:

1. A failed or deferred Render Scale transition must not expose a reduced raw
   eye texture as a full-size compositor submission.
2. Post-load admission must be bounded and must preserve the last truthful
   physical contract when conservative memory checks reject a transition.

The latest local test exposed and fixes an additional RC203 planner exception:
post-load exterior-to-interior native restores calculated the conservative 8x
commit projection but explicitly disabled the projection guard and then applied
the transition. The new policy applies the guard in both door directions and
does not select destructive low-peak restore while a truthful stable vendor
contract is available.

## Integrated behavior

- Render Scale state remains transactional. Replacement presentation textures
  are allocated and validated before publication; allocation failure leaves the
  prior pair intact.
- A reduced candidate that cannot be presented at final eye dimensions is
  rejected with `VRCompositorError_RequestFailed`, allowing OpenVR to retain its
  last accepted full-size stereo state.
- The startup state is defined as Upscaling None until a completed world frame
  exists when no stable contract is available. Configured gameplay settings are
  not overwritten.
- Render state is not logically invalidated during loading transitions. The
  stable profile remains authoritative until the target is physically proven
  and accepted for both eyes.
- Loading fade interval and Scaleform time remain frozen until a coherent
  destination stereo pair is accepted. The transition cover uses a 1000 ms soft
  deadline, 1500 ms hard/keepalive budget in the current upstream path, and
  stereo acceptance rather than settings publication.
- Non-door post-load recovery has an epoch-owned monotonic admission clock and
  a 120-frame deadline. Two consecutive safe samples remain the normal path.
  The deadline permits one exact evaluation only; it does not waive fresh
  memory, pressure, GPU headroom, commit projection, retirement, device-loss,
  OOM, loading-serial, or transition-epoch checks. A presentation-critical
  Stabilizer door handoff instead terminates immediately on hard-safety
  rejection, retaining the current contract without mutation rather than
  consuming the fade while waiting.
- Unsafe deadline evaluation ends before mutation and retains the stable
  contract. With no truthful stable contract, the boot latch is reset so the
  startup None fallback applies.
- System commit reserve remains:

  ```text
  reserve = clamp(commit_limit / 8, 8 GiB, 16 GiB)
  ```

- Estimated system-commit growth remains deliberately conservative: 4x normal
  overlap allocation and 8x full-resolution native restore.
- A stable active FSR/DLSS contract now makes destructive low-peak native
  restore ineligible. Shared presentation resources are retained through
  physical native-target recreation. Low-peak teardown remains available only
  when there is no stable contract to preserve.
- Terminal vendor recovery is provider-neutral. FSR-specific request-key
  latching remains FSR-specific; controller recovery outcomes apply to FSR and
  DLSS.

## Live observations on 2026-08-08

Environment:

- Skyrim VR test modlist: `D:\Games\Skyrim\MadGod2`
- GPU: AMD Radeon RX 7900 XT
- DevBench: 1.12.0, VR port 8921
- Stabby profiles: Hoshipa interior, Ultra Quality exterior; Render Scale off
  interior and on exterior
- TestLimit: Sysinternals 5.24 at
  `K:\Utils\SysInternals\Testlimit64.exe`
- Tested DLL before the newest planner correction: integration commit
  `dfe824e4`

Unpressured baseline:

- Breezehome to Whiterun to Breezehome completed without OOM, device loss,
  fidelity mismatch, retirement backlog, or dimension-mismatch fallback.
- The cold transition held presentation for 172 frames / approximately 3.234 s,
  above the 120-frame fast-path latency target. GPU pressure briefly reached
  High and process-private/system commit rose substantially.
- The next warm exit completed its handoff in approximately 1.516 s and did not
  have a comparable latency spike.
- The user initially reported a possible brief PiP on exit, then withdrew
  certainty after discovering the player/camera was clipped inside the door.
  The screenshot shows near-plane door geometry crossing the view and is not
  reliable evidence of PiP. Treat the visual result as inconclusive.
- Subsequent repeated Breezehome in/out cycles repeatedly left the player
  trapped in the door, including after changing position before activating it.
  Similar trapping had occurred once or twice previously. Track this as a
  separate low-priority transition/placement issue; no causal link to Render
  Scale has been established. It is also a test confounder because near-plane
  door geometry can be mistaken for a presentation artifact.
- In a later series of Breezehome door transitions, most fade-ins appeared
  correct but the reduced-live-region/stale-remainder PiP morphology was
  briefly visible on several transitions. Treat this repeated observation as
  a credible reproduction on the deployed `dfe824e4` DLL, distinct from the
  earlier door-clipping ambiguity. It is also distinct from the known flash at
  the beginning of some transitions, currently assumed to be a single stale
  frame associated with OCU ASW; that attribution remains unproven.
- DevBench recorded zero bounds-mismatch original fallback observations and
  zero vendor-failure stretch observations. This does not exclude a full-size
  texture whose contents are only partially updated; dimensions alone cannot
  detect that morphology.

Pressured exterior-to-interior run:

```text
TestLimit command: Testlimit64.exe -accepteula -m 256 -c 51
TestLimit PID:     37300
TestLimit private: 13,718,466,560 bytes (about 12.78 GiB)
```

The charge was calculated from live commit rather than hard-coded. It left
approximately 15 GiB of actual system commit headroom while crossing the
conservative native-restore admission boundary.

At request 6 / transition epoch 6:

```text
commit at request:                    93,248,290,816 bytes
commit at apply admission:            93,353,521,152 bytes
estimated additional allocation:         402,564,096 bytes
8x projected commit addition:          3,220,512,768 bytes
projected commit:                      96,455,266,304 bytes
bounded admission limit:              95,996,870,144 bytes
actual post-allocation peak:           97,144,901,632 bytes
```

Despite projected commit exceeding the admission limit by 458,396,160 bytes,
the pre-fix plan reported `systemCommitGuardActive=false` and
`systemCommitDeferred=false`. The transition mutated and completed. Source
inspection identified the exception:

```cpp
(!rc94PostLoadDoorRelatch || relatchTargetRenderScaleActive)
```

This disabled the guard for the larger exterior-to-interior native restore.
The same RC94 branch also allowed only activation to propagate hard-safety
deferral, based on the assumption that native restore could tear down the
active vendor contract first to reduce peak usage.

The transition nevertheless completed coherently in this run: both eyes
reached 2508x2508 `NativeOriginal`, no unsafe fallback was recorded, and all
four engine-target retirements drained. This proves the planner exception, not
a visual failure.

TestLimit PID 37300 was then verified by executable path, terminated, and its
commit charge released. System commit fell from approximately 95.3 GB to
81.5 GB without restarting Skyrim.

## Correction added after the live run

The current source, not yet represented by the DLL used for the observations
above, changes native-restore admission as follows:

- `UsesSystemCommitProjectionGuard` depends only on overlap allocation plus a
  valid commit sample/limit. Door direction no longer disables it.
- RC94 hard-safety deferral applies to activation and deactivation.
- A stable vendor contract must be valid, active, vendor-backed, and match the
  physical boot method/generation before it can be preserved.
- With such a contract, native restore skips pre-recreation vendor/shared
  teardown and keeps stable presentation resources alive through target
  recreation.
- Low-peak teardown is selected only when no stable contract satisfies that
  proof.

Policy tests cover stable-contract preservation, no-stable low-peak fallback,
activation exclusion, and commit-guard prerequisites.

## PR-head verification and newly found latch

The next live session used integration build `23ad3de4`, which includes the
planner correction above.

Unpressured results:

- Repeated Breezehome/Whiterun round trips converged to native interior and
  FSR4 exterior contracts in both eyes. No bounds-mismatch unsafe fallback,
  vendor-failure stretch, OOM, device loss, fidelity mismatch, or retained
  unproven retirement remained after settling.
- Fade/compositor protection generally released in approximately 1.5-1.6 s.
  One transition had an indistinct end flash; another exterior transition was
  visually smooth. No clear PiP was reported in this PR-head series.
- One return trapped the player in the doorway. A later interior fade appeared
  to begin bright/correct and rapidly dim to normal illumination. Both remain
  observations, not established Render Scale causes.
- Presentation probing materially retains diagnostic capacity. A round trip
  performed without the heavy probe changed process private bytes by only
  16,457,728 bytes (approximately 16 MiB), rather than the 0.3-0.5 GiB steps
  seen during probe-heavy cycles. This does not prove absence of a longer-term
  leak, but it rules out treating those probe-heavy increments as direct code
  leakage.

The corrected guard was then forced to reject a native restore while leaving
substantial real commit headroom:

```text
TestLimit command: Testlimit64.exe -accepteula -m 256 -c 38
TestLimit PID:     35968
TestLimit private: 10,221,875,200 bytes

live system commit after charge:       93,226,274,816 bytes
actual commit headroom:                16,484,433,920 bytes
8x projected native addition:           3,220,512,768 bytes
projected commit:                       96,446,787,584 bytes
bounded admission limit:                95,996,870,144 bytes
controlled projected overshoot:            449,917,440 bytes
```

The transition retained animated content but never completed its fade. Stopping
the exact TestLimit PID released the pressure but did not recover the
transition. DevBench's off-thread health continued to advance render frames,
while `lastTaskFrame` remained 45153 and queued main-thread tasks accumulated.

Source inspection established the cause:

- RC94/Stabilizer door relatches intentionally bypass
  `CanAdmitVRRenderScalePostLoadRecoveryRelatch` so cleanup cannot destroy the
  current stable presentation.
- That bypass also omitted the only code which binds `transitionEpoch`, starts
  the admission clock, and publishes `settleDeadlineExpired`.
- The hard-safety branch nevertheless waited for that deadline before its
  retain-stable decision. It therefore requeued forever under a rejected door
  allocation, leaving the compositor/FaderMenu transition cover owned.

A diagnostic minidump was captured before restart:

```text
b/diagnostics/SkyrimVR_1282412824_pressured-transition-stuck_2026-08-08_0547.dmp
size:   135,675,616 bytes
SHA256: 7A41F0DF171875F0BB99027B9E39DC684CB762A120E26E755805AA675599C5B8
```

The dump is intentionally untracked. The follow-up source correction keeps the
door cleanup bypass, but makes hard-safety rejection terminal before any
physical mutation. It restores the stable controller contract (or startup
None), completes the exact recovery owner even when its transition epoch has
not yet been bound, and releases only the compositor/fade cover owned by that
loading serial. It does not reduce the 8x projection or 8-16 GiB reserve.

## Repeatable DevBench/TestLimit protocol

1. Enable Community Shaders developer mode and Debug logging. Confirm DevBench
   answers `communityshaders.renderscale` and reports the intended DLL fields.
2. Configure Stabby to change profiles at every door. The primary combined
   case is Render Scale enabled outside and disabled inside.
3. Start a RenderScale event session (`action=start`) and presentation probe
   (`action=probe_start`). Reset the probe between individual transitions so
   its 4096-record retention cannot scroll past the handoff.
4. Run at least two unpressured exterior/interior cycles. Record cold and warm
   latency separately.
5. Settle outside and verify all of the following before pressure:
   - controller state Active;
   - stable/requested profiles match the exterior target;
   - both eyes are current `VendorEvaluated` frames;
   - actual dispatch backend converges to the authoritative backend;
   - shader compilation is inactive;
   - no retirement or trim is pending;
   - memory pressure is Normal or Elevated.
6. Calculate TestLimit charge from the current DevBench sample:

   ```text
   admission_limit = commit_limit - clamp(commit_limit / 8, 8 GiB, 16 GiB)
   desired_charge ~= admission_limit
                     - current_commit
                     - projected_system_commit_additional
                     + 256..512 MiB controlled overshoot
   N = ceil(desired_charge / 256 MiB)
   ```

7. Start exactly one hidden TestLimit process:

   ```powershell
   K:\Utils\SysInternals\Testlimit64.exe -accepteula -m 256 -c N
   ```

   `-m` charges commit. Do not use `-r`, which reserves address space without
   charging commit, or `-d`, which adds unnecessary physical-memory pressure.
   `-c` must be last.
8. Verify the exact PID, executable path, private bytes, live system commit, and
   real remaining headroom before opening the door.
9. Reset/start the presentation probe, enter the interior, remain still for
   approximately five seconds, and immediately stop the probe to preserve the
   transition window.
10. Under the corrected build, expected rejection behavior is:
    - the rejected door request terminates for this transition and remains
      restart-required in settings;
    - stable exterior vendor contract remains authoritative;
    - no physical mutation occurs while projected commit is unsafe; the
      controller may transiently enter Applying while it evaluates the plan,
      then returns to the stable Active contract;
    - `systemCommitGuardActive=true`;
    - `systemCommitDeferred=true` and `pressureDeferred=true`;
    - hard-safety rejection immediately produces one terminal retain-stable
      decision and releases the owned fade/compositor cover;
    - the door transition completes rather than remaining black/latched;
    - neither eye submits a reduced raw target or mixed provider;
    - `sessionBoundsMismatchOriginalFallbackEyeObservations` remains zero;
    - no OOM or device loss occurs.
11. Terminate only the verified TestLimit PID. Confirm commit recovery, then
    explicitly retry or cross the door again and verify convergence to the
    configured target.

## Review and GPU test requests

- NVIDIA review/test is required for every provider-neutral and mirrored
  change. In particular, verify that retaining the stable DLSS presentation
  resources through native target recreation is safe and that a later DLSS
  activation correctly reuses or retires them.
- AMD/FSR4 should repeat the pressured rejection case and the post-pressure
  convergence case with the corrected DLL.
- Review the lifetime of retained shared presentation resources. The current
  design keeps a bounded last-stable set rather than destroying it before the
  replacement is accepted. It must not become an accumulating set across door
  cycles.
- Consider extending the presentation probe with regional temporal hashes or a
  live-region bounding box. Current dimension/bounds telemetry cannot prove or
  disprove a full-size surface containing a live reduced region plus stale
  remainder.
- The acceptance helper currently expects a fresh vendor presentation even when
  a test intentionally finishes in native mode; test tooling should make its
  terminal presentation expectation profile-aware.

## Evidence limits

The first live run proves that RC203's native-door commit projection could be
calculated but not enforced. The PR-head run proves the guard is now reached,
and exposes the pre-follow-up infinite-requeue/fade latch under rejection. It
does not prove that the uncertain clipped-door visual was PiP, nor does it yet
prove the newest terminal fallback on AMD or NVIDIA. Those claims require the
new DLL and the protocol above.
