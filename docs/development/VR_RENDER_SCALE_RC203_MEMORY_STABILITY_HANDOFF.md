# VR Render Scale RC203 memory-stability handoff

## Purpose and status

This branch integrates the local VR memory-stability work and the Render Scale
fallback fixes on current upstream `cs-1.7-PL-VR` head `64d837d3` (the merged
RC203 recovery lineage plus subsequent upstream review fixes). It was developed in a
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

-   Render Scale state remains transactional. Replacement presentation textures
    are allocated and validated before publication; allocation failure leaves the
    prior pair intact.
-   A reduced candidate that cannot be presented at final eye dimensions is
    rejected with `VRCompositorError_RequestFailed`, allowing OpenVR to retain its
    last accepted full-size stereo state.
-   The startup state is defined as Upscaling None until a completed world frame
    exists when no stable contract is available. Configured gameplay settings are
    not overwritten.
-   Render state is not logically invalidated during loading transitions. The
    stable profile remains authoritative until the target is physically proven
    and accepted for both eyes.
-   Loading fade interval and Scaleform time remain frozen until a coherent
    destination stereo pair is accepted. The validated soft windows remain 6 s
    for first/main-menu load, 3 s for in-game load, and 1 s for render
    transitions; the shared 500 ms grace makes the hard caps 6.5 s, 3.5 s, and
    1.5 s. A post-mutation generation remains black until coherent stereo
    acceptance; the time cap must not expose a partially mutated target.
-   If a recoverable pre- or post-mutation owner remains black-covered beyond
    6.5 s, the protected keepalive changes from opaque black to a very dim,
    slow blue pulse. The cue is generated only by clearing the already-validated
    keepalive candidate; it never samples the incoherent game target, changes its
    eligibility, allocates a notification surface, or weakens stereo proof. It
    returns to black if terminal failure is claimed and disappears immediately
    when the coherent handoff/reset completes. The 6.5 s delay, 1.8 s period, and
    0.2-0.8% raw blue intensity are provisional usability values for HMD review,
    not a substitute for a future securely composed OpenVR notification.
-   LoadingMenu event state, serial identity, and its vendor-work-gate bit are
    published as one gate-owned transaction. Each open edge also owns a
    non-renewable missed-close candidate. If a close callback is lost, the exact
    unchanged generation may synthesize it only after either that menu was
    physically observed open or the exact game-load completion arrived, both the
    State and UI mirrors report closed, and two distinct completed world frames
    confirm convergence. A newer edge cancels the candidate. The steady-state
    path is one atomic-zero check; there is no timed mirror bypass.
-   Non-door post-load recovery has an epoch-owned monotonic admission clock and
    an absolute 120-frame deadline which cleanup or fence stalls cannot restart.
    Two consecutive safe samples remain the normal path. The deadline permits
    one exact evaluation only; it does not waive fresh memory, pressure, GPU
    headroom, commit projection, retirement, device-loss, OOM, loading-serial,
    or transition-epoch checks. Once the recreate checkpoint publishes the
    broader post-mutation serialization owner immediately before native creator
    entry, a separate non-renewable wall clock may request one recovery-creator
    service turn after 2 s. That one-shot path relaxes normal predictive memory
    admission, but it does not eliminate it: the current suggested starting
    policy projects `2x estimatedAdditionalBytes` and preserves a final 2 GiB of
    system commit. Exact ownership, cleanup, retirement, target validity, device
    health, and coherent stereo release stay mandatory. These emergency memory
    values are deliberately provisional tuning suggestions, not determined
    safety constants. The immutable serialization clock selects a provisional
    15 s terminal deadline while recovery has not demonstrated constructive
    progress, a 60 s ceiling once recovery resources are admitted and progress
    advances through creator/target/publication phases, or 120 s while a debugger
    is attached. A just-completed creator or covered presentation handoff may
    retain its current frame and at most one following presentation turn, capped
    at an additional 500 ms. This grace cannot renew the selected absolute clock,
    and phase transitions are monotonic so retry churn cannot manufacture extra
    time. The narrower physical-mutation
    marker can clear after coherent target publication while serialization
    remains owned through presentation retirement. A Stabilizer door still
    retains its stable contract immediately on hard-safety rejection before
    physical mutation.
-   A physically started recovery remains bound to its immutable serialization
    source while a successor is being admitted. Memory/deadline evaluation may
    recognize that source-owned successor, but only the atomic mutation-owner
    transfer changes both serialization and recovery to the new transition.
-   Unsafe deadline evaluation ends before mutation and retains a resource-proven
    stable contract. The startup None fallback is used only when reduced physical
    targets are not active. If reduced targets remain active but their stable
    resource identity can no longer be proven, the exact deadline owner promotes
    once to an internal provider-neutral native relatch. The user's fidelity
    profile is kept as a fresh latest-wins replay request rather than being
    rewritten to None. Ordinary requests remain deferred behind that worker.
    Creator entry transfers it to the same post-mutation chain; failure to enter
    the creator within the same hold/loading generation is terminal only after a
    provisional 15 s recoverable fallback bound, or 120 s with a debugger
    attached, instead of renewing the black hold. A failed attempt to publish the
    promoted native fallback retains and requeues the exact covered fallback
    under that same bound; it is not treated as an immediate integrity failure.
    Its immutable clock begins when the exact hard-deadline fallback token is
    first published. If legacy/corrupt state presents that token without a clock,
    the complete owner lock set initializes it once rather than granting an
    infinite waiver. Terminal selection atomically closes further physical
    mutation before locks are released, so a successor cannot race a decided CTD.
    A newer loading generation removes the 2 s emergency-service waiver and
    returns the same worker to normal safe-point
    admission. While that exact LoadingMenu serial is open, Skyrim owns renderer
    mutation and its time is not charged. A successfully published worker starts
    its own pre-creator clock once; adoption by a newer loading serial transfers
    charging to that serial's authoritative close timestamp. Duplicate callbacks
    for the same serial cannot renew either clock, and no second native successor
    is created.
-   System commit reserve remains:

    ```text
    reserve = clamp(commit_limit / 8, 8 GiB, 16 GiB)
    ```

-   Estimated system-commit growth remains deliberately conservative: 4x normal
    overlap allocation and 8x full-resolution native restore.
-   The one-shot emergency path uses a separately visible, relaxed guard:

    ```text
    emergency_projected_commit = current_commit + 2 * estimatedAdditionalBytes
    emergency_admission_limit = commit_limit - 2 GiB
    require emergency_projected_commit < emergency_admission_limit
    ```

    The `2x` multiplier and 2 GiB final reserve are suggested initial values for
    upstream review and AMD/NVIDIA measurement. They are not claimed to be
    determined optima or safety constants. Emergency recovery is a legitimate
    use of part of the normal reserve, but not a free pass for a leaking process
    to consume the system's final commit headroom. The emergency decision takes
    a fresh `GlobalMemoryStatusEx` sample at the exact evaluation; relaxed
    admission cannot reuse a controller sample retained across retry frames.
-   PR10's provider-independent DXGI pre-recreate offer remains in place for
    protected memory-relief transitions. Its scope is Skyrim's eligible common
    render, depth, deferred, underwater, and display targets; it does not offer
    vendor-runtime resources or shared compositor textures. The exact offered
    identities and matching DXGI device interface are retained through Skyrim's
    creator and reclaimed under the same unique target-table lock before global
    or CSX setup can use a surviving identity. Shared, immutable, CPU-backed, or
    previously poisoned resources are never offered. `OK` makes a surviving
    identity safe to use. A failed reclaim call, `DISCARDED`/`NOT_COMMITTED`
    result, or failure to retain every unsafe identity as poison terminates
    synchronously at the checkpoint; the renderer is never resumed to wait for
    another recreate. Poison tracking is a defensive identity invariant, not a
    deferred recovery route. An Offer call which never succeeded has created no
    offered identities and remains drain-failure telemetry rather than this
    unsafe-reclaim terminal case.
-   Replaced engine-target references use their existing correctness barrier:
    ownership is retained until a GPU event fence proves they can be released.
    The later `IDXGIDevice3::Trim` is a separate, best-effort residency hint
    behind its own fence. It is neither the reclaim checkpoint nor the lifetime
    barrier for displaced COM identities, and Trim availability does not make an
    otherwise unsafe reclaim result usable.
-   A stable active FSR/DLSS contract now makes destructive low-peak native
    restore ineligible. Shared presentation resources are retained through
    physical native-target recreation. Low-peak teardown remains available only
    when there is no stable contract to preserve.
-   Recovery orchestration and terminal ownership are provider-independent.
    The explicit provider-neutral successor is a native Upscaling None/TAA
    physical relatch used when a vendor path fails after target mutation, when
    vendor operation is unavailable, or for the one exact pre-mutation deadline
    promotion above. This does not relabel every ordinary watchdog retry as a
    provider-neutral recreate. The internal pre-mutation successor changes only
    local physical target inputs; selected settings remain intact. FSR-specific
    request-key latching remains FSR-specific, while the bounded chain and
    terminal policy apply to FSR and DLSS.
-   Confirmed D3D11 device removal, reset, hang, or driver-internal failure is
    treated differently from allocation refusal and terminates immediately while
    a post-mutation serialization owner exists, even if its narrower physical
    marker has already cleared. Unsafe DXGI reclaim results and a creator
    exception after the physical boundary also terminate synchronously because
    no truthful live target table can be exposed. Other healthy-device provider,
    retirement, publication, or ownership stalls receive bounded recovery with
    the provisional progress-sensitive 15 s/60 s deadline (120 s under a
    debugger) plus next-frame/500 ms maximum grace above. The code logs and
    flushes full transition context, then raises application-defined
    noncontinuable exception `0xE0525343` for Crash Logger. Ordinary pre-mutation
    OOM, commit rejection, and provider failure retain the truthful stable
    contract and do not terminate. The distinct terminal pre-mutation case is an
    already-promoted native liveness fallback which still cannot enter its
    creator or publish a coherent fallback within its exact recoverable deadline
    generation.

## Live observations on 2026-08-08

The available RC194 log supports a pre-creator memory-admission/liveness stall;
the screenshot by itself does not identify a root cause. That log never records
the physical creator checkpoint, so it is not evidence that Offer/Reclaim
produced the screenshot. `OfferResources1(ALLOW_DECOMMIT)` itself was introduced
by `05266def9`, is an ancestor of both RC184 and RC194, and has no matching code
change between those tags. A pressure-dependent Offer issue is therefore not
disproved in general, but neither the log nor screenshot establishes it here.
The Offer/Reclaim work in this branch is preventive hardening for PR10's new
recovery paths, not a diagnosis of, or claimed fix for, the RC194 screenshot.

Environment:

-   Skyrim VR test modlist: `D:\Games\Skyrim\MadGod2`
-   GPU: AMD Radeon RX 7900 XT
-   DevBench: 1.12.0, VR port 8921
-   Stabby profiles: Hoshipa interior, Ultra Quality exterior; Render Scale off
    interior and on exterior
-   TestLimit: Sysinternals 5.24 at
    `K:\Utils\SysInternals\Testlimit64.exe`
-   Tested DLL before the newest planner correction: integration commit
    `dfe824e4`

Unpressured baseline:

-   Breezehome to Whiterun to Breezehome completed without OOM, device loss,
    fidelity mismatch, retirement backlog, or dimension-mismatch fallback.
-   The cold transition held presentation for 172 frames / approximately 3.234 s,
    above the 120-frame fast-path latency target. GPU pressure briefly reached
    High and process-private/system commit rose substantially.
-   The next warm exit completed its handoff in approximately 1.516 s and did not
    have a comparable latency spike.
-   The user initially reported a possible brief PiP on exit, then withdrew
    certainty after discovering the player/camera was clipped inside the door.
    The screenshot shows near-plane door geometry crossing the view and is not
    reliable evidence of PiP. Treat the visual result as inconclusive.
-   Subsequent repeated Breezehome in/out cycles repeatedly left the player
    trapped in the door, including after changing position before activating it.
    Similar trapping had occurred once or twice previously. Track this as a
    separate low-priority transition/placement issue; no causal link to Render
    Scale has been established. It is also a test confounder because near-plane
    door geometry can be mistaken for a presentation artifact.
-   In a later series of Breezehome door transitions, most fade-ins appeared
    correct but the reduced-live-region/stale-remainder PiP morphology was
    briefly visible on several transitions. Treat this repeated observation as
    a credible reproduction on the deployed `dfe824e4` DLL, distinct from the
    earlier door-clipping ambiguity. It is also distinct from the known flash at
    the beginning of some transitions, currently assumed to be a single stale
    frame associated with OCU ASW; that attribution remains unproven.
-   DevBench recorded zero bounds-mismatch original fallback observations and
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

-   `UsesSystemCommitProjectionGuard` depends only on overlap allocation plus a
    valid commit sample/limit. Door direction no longer disables it.
-   RC94 hard-safety deferral applies to activation and deactivation.
-   A stable vendor contract must be valid, active, vendor-backed, and match the
    physical boot method/generation before it can be preserved.
-   With such a contract, native restore skips pre-recreation vendor/shared
    teardown and keeps stable presentation resources alive through target
    recreation.
-   Low-peak teardown is selected only when no stable contract satisfies that
    proof.

Policy tests cover stable-contract preservation, no-stable low-peak fallback,
activation exclusion, and commit-guard prerequisites.

## PR-head verification and newly found latch

The next live session used integration build `23ad3de4`, which includes the
planner correction above.

Unpressured results:

-   Repeated Breezehome/Whiterun round trips converged to native interior and
    FSR4 exterior contracts in both eyes. No bounds-mismatch unsafe fallback,
    vendor-failure stretch, OOM, device loss, fidelity mismatch, or retained
    unproven retirement remained after settling.
-   Fade/compositor protection generally released in approximately 1.5-1.6 s.
    One transition had an indistinct end flash; another exterior transition was
    visually smooth. No clear PiP was reported in this PR-head series.
-   One return trapped the player in the doorway. A later interior fade appeared
    to begin bright/correct and rapidly dim to normal illumination. Both remain
    observations, not established Render Scale causes.
-   Presentation probing materially retains diagnostic capacity. A round trip
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

-   RC94/Stabilizer door relatches intentionally bypass
    `CanAdmitVRRenderScalePostLoadRecoveryRelatch` so cleanup cannot destroy the
    current stable presentation.
-   That bypass also omitted the only code which binds `transitionEpoch`, starts
    the admission clock, and publishes `settleDeadlineExpired`.
-   The hard-safety branch nevertheless waited for that deadline before its
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

## Terminal-fallback live verification (`a20fa4e9`)

The corrected DLL identified itself through DevBench as
`RC203-6-ga20fa4e9`. The critical AMD/FSR4 test used the same exterior FSR4 and
interior native Stabby profiles as the failed run above. Before charging
commit, the exterior contract was stable and current in both eyes.

```text
TestLimit command: Testlimit64.exe -accepteula -m 256 -c 43
TestLimit PID:     33064
TestLimit private: 11,566,792,704 bytes

live system commit after charge:       93,251,461,120 bytes
actual commit headroom:                16,459,247,616 bytes
8x projected native addition:           3,220,512,768 bytes
projected commit:                       96,471,973,888 bytes
bounded admission limit:                95,996,870,144 bytes
controlled projected overshoot:            475,103,744 bytes
```

The pressured Whiterun-to-Breezehome request was rejected before physical
mutation and the door completed. The controller returned to Active with the
previous exterior FSR4 contract (1928x1928 to 2508x2508, generation 6) still
authoritative and coherent in both eyes. The rejected request was cleared,
settings reported `Restart required`, engine-target retirement did not begin,
and the compositor cover completed its 1500 ms owned timeout handoff with both
eyes accounted for. There were no bounds mismatches, vendor-failure stretches,
OOMs, device losses, or pending retirement sets.

The exact TestLimit PID and executable path were reverified before termination.
After release, system commit fell to 81,638,047,744 bytes with
28,072,660,992 bytes of real headroom. The retained FSR4 presentation remained
stable and the controller remained responsive.

Post-pressure convergence was then exercised without restarting Skyrim:

-   Breezehome-to-Whiterun completed on coherent FSR4 in both eyes with zero
    mismatch or lifecycle failure. The relatch produced a new stable generation;
    six unproven pointers were conservatively retained at the first sample, with
    no fence or retirement set pending.
-   The following Whiterun-to-Breezehome transition admitted the previously
    rejected native restore. It converged to the configured 2508x2508
    `NativeOriginal` path in both eyes in 14 transition frames. Retirement drained
    completely, including the six retained pointers, with zero mismatch,
    vendor-failure stretch, OOM, device loss, or lifecycle failure.
-   The focused native-return record is intentionally below the generic
    acceptance helper's two-request minimum and ends on `NativeOriginal`, while
    that helper currently requires a fresh `VendorEvaluated` terminal frame.
    Its overall false verdict is therefore not a transition failure; every
    applicable safety, latency, memory, cleanup, and retirement gate passed.
-   The longer combined record also reports a false terminal-state gate after
    the rejected epoch: the live controller was Active with no requested or
    applying contract, but the rejected epoch's still-valid current metrics made
    the helper reject its own `Active or Idle` condition. The underlying record
    contains zero transition failures and a recovered coherent FSR4 presentation.

This is positive AMD/FSR4 evidence for the unsafe-fallback correction and for
later convergence once commit becomes available. Equivalent NVIDIA/DLSS review
and live testing remain required.

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
    - the rejected controller request is cleared while the configured native
      profile remains restart-required;
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

-   NVIDIA review/test is required for every provider-neutral and mirrored
    change. In particular, verify that retaining the stable DLSS presentation
    resources through native target recreation is safe and that a later DLSS
    activation correctly reuses or retires them.
-   AMD/FSR4 should repeat the pressured rejection case and the post-pressure
    convergence case with the corrected DLL.
-   Review the lifetime of retained shared presentation resources. The current
    design keeps a bounded last-stable set rather than destroying it before the
    replacement is accepted. It must not become an accumulating set across door
    cycles.
-   Consider extending the presentation probe with regional temporal hashes or a
    live-region bounding box. Current dimension/bounds telemetry cannot prove or
    disprove a full-size surface containing a live reduced region plus stale
    remainder.
-   The acceptance helper currently expects a fresh vendor presentation even when
    a test intentionally finishes in native mode; test tooling should make its
    terminal presentation expectation profile-aware.
-   Explicitly review the final catastrophic policy choice. Confirmed D3D11
    device loss while the post-mutation serialization chain is owned terminates
    immediately, including after the narrower physical marker has cleared.
    Unsafe reclaim identities and a creator exception after the physical
    boundary are likewise synchronous integrity failures. Other healthy-device
    unresolved chains receive one emergency service turn after 2 s. The proposed
    progress policy uses a non-renewable 15 s absolute bound before constructive
    recovery is demonstrated, a 60 s absolute ceiling after resources are
    admitted and recovery is actively progressing, and 120 s while a debugger is
    attached; only a just-completed creator or covered presentation handoff may
    add the next presentation frame, capped at 500 ms. Failure then produces a
    crash-logger-visible CTD instead of an indefinite black apparent hang. These
    durations are suggested starting values for review, not empirically
    determined optima. In particular, review whether the phase threshold used to
    earn the longer ceiling is strict enough to prevent churn from masquerading
    as recovery while still tolerating real driver/debugger latency.
    Confirm that exception `0xE0525343` is compatible with the crash loggers
    upstream supports. A modal desktop message is intentionally avoided because
    it may be invisible in the HMD and resemble the hang it replaces.
-   Review the provisional extended-black liveness cue on both AMD and NVIDIA,
    with Gamma and Linear compositor color spaces where available. Confirm that
    it is visible enough to distinguish recovery from a hang without resembling
    a transition flash, causing discomfort, or changing keepalive identity and
    stereo ownership. The cue is intentionally basic and remains in the current
    recovery scope; a secure OpenVR notification layer is deferred.
-   Explicitly review the conservative commit constants rather than weakening
    them incidentally while reviewing the fallback:
    -   `4x estimatedAdditionalBytes` covers ordinary overlap with substantial
        uncertainty in driver/runtime allocation and resource accounting.
    -   `8x estimatedAdditionalBytes` is used for full-resolution native restore,
        whose replacement targets and retained stable vendor presentation can
        overlap at the highest-cost point.
    -   `clamp(commit_limit / 8, 8 GiB, 16 GiB)` reserves meaningful breathing
        room for Windows, the compositor, drivers, and unrelated applications
        across small and large commit limits. The lower bound prevents a weak
        reserve on modest systems; the upper bound avoids making very large page
        files unusably conservative.
        These deliberately reject well before actual exhaustion. The current
        recommendation is to preserve them unless NVIDIA/AMD measurements establish
        a tighter safe bound; the memory leak under investigation makes reducing
        the margin especially unattractive.
    -   The one-shot emergency recovery path is intentionally less conservative,
        currently `2x estimatedAdditionalBytes` with a final 2 GiB system-commit
        reserve. Those two values are suggested starting points, not determined
        constants. Review whether they provide enough room for Windows, the
        compositor, and the driver on both AMD and NVIDIA while allowing recovery
        to use reserve that normal operation must leave untouched.

## Build and fatal-path verification

-   Configure and compile with CommonLibSSE-NG tag `v6.1.1`; record the resolved
    CommonLib commit in the PR verification.
-   Unit policy coverage must prove that immediate device-loss termination requires
    both confirmed loss and a non-zero current post-mutation serialization owner.
    Cover both an active physical marker and the presentation phase after that
    narrower marker has cleared.
-   Unit policy coverage must also prove the 2 s one-shot attempt, provisional
    15 s stalled / 60 s progressing / 120 s debugger terminal selection, and
    at-most-500-ms/next-presentation-frame grace cannot be renewed by a retry,
    successor epoch, phase regression, or loading serial. It must prove that only
    admission of recovery resources earns the progressing ceiling.
-   Unit policy coverage must prove the emergency commit guard rejects invalid or
    overflowing samples, equality at the final-reserve boundary, and projections
    above that boundary, while accepting a strictly lower `2x` projection. These
    values remain reviewable tuning inputs rather than test-asserted optima.
-   Live/log review must confirm each emergency evaluation records a fresh system
    commit sample; staleness is not one of the relaxed constraints.
-   Unit policy coverage must prove the liveness cue stays black before 6.5 s,
    without an exact hold/clock, and after terminal ownership is claimed; the cue
    becomes eligible exactly at the threshold without publishing game content.
-   Verify the pre-mutation native deadline promotion preserves selected settings,
    gives a synthesized fidelity replay a fresh request/transition identity, keeps
    a genuinely newer deferred request, and cannot renew itself within one loading
    generation. OpenComposite and a newer LoadingMenu generation may remove only
    its fast-path waiver; they must not erase a live provider-neutral physical
    worker. Prove an open newer serial cannot consume the terminal budget, its
    recoverable bound begins at the matching close tick, duplicate same-serial
    adoption cannot rebase that tick, and failed publication stays covered and
    retryable until that same bound rather than terminating immediately.
-   Inject `DISCARDED`, `NOT_COMMITTED`, reclaim-call failure, and poison-retention
    failure at the creator checkpoint. Each must select terminal handling
    synchronously before global/CSX setup or rendering can consume the identity;
    none may enter the healthy-device recoverable retry path.
-   An ordinary preflight rejection, TestLimit commit rejection, or OOM before
    mutation must not terminate. A provider failure with a healthy device must
    retain the stable contract when pre-mutation; after reconciled target mutation,
    it must queue the provider-neutral native successor and receive the bounded
    healthy-device recovery before terminal selection.
-   A controlled device-removal test after mutation should produce plugin log
    marker `[VRRenderScale][FATAL]`, exception code `0xE0525343`, and a Crash Logger
    report whose stack enters `SignalVRRenderScaleTerminalFailure`.
-   Repeat the live door protocol on AMD/FSR and NVIDIA/DLSS. The fatal test is a
    separate destructive test and must not be conflated with TestLimit pressure,
    which should exercise stable-contract retention rather than a CTD.

## Evidence limits

The first live run proves that RC203's native-door commit projection could be
calculated but not enforced. The `23ad3de4` run proves the guard is reached and
exposes the former infinite-requeue/fade latch under rejection. The
`a20fa4e9` run proves on AMD/FSR4 that rejection can terminate without physical
mutation, preserve coherent last-stable presentation, complete the door, and
later converge to native after commit recovery. It does not prove that the
uncertain clipped-door visual was PiP, nor does it substitute for the requested
NVIDIA/DLSS review and live test.
