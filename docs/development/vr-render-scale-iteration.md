# VR render-scale iteration records

The VR render-scale controller can capture a bounded CSX-menu stress session and write a versioned JSON record for an MCP/Ghidra optimization loop. The capture observes user-driven changes; it never changes render-scale settings itself.

## Capture workflow

1. Enable CSX developer mode.
2. Open **Upscaling > Render Pipeline > Render Scale Stress Capture**.
3. Select **Start Capture**.
4. Exercise the same fixed scenario for every candidate build. At minimum, perform two render-scale changes. Include repeated preset changes and a fast-travel cycle when evaluating memory recovery.
5. Wait for the final change to reach a stable in-world presentation, then select **Stop Capture**.
6. Read the new record from `Data/SKSE/Plugins/CommunityShaders/Diagnostics/VRRenderScale/`.

Use identical save, location, CSX profile, change order, dwell frames, HMD resolution, backend, and graphics settings when comparing iterations. Prioritize the production workload in this order: repeated same-backend resolution changes (for example Hoshipa/Quality), DLSS/DLAA and FSR/Native-AA activation changes, then a fixed-profile DLSS/FSR alternating series as a lower-priority backend-handoff stress oracle. Run each matrix as a separate capture so exact-profile memory grouping remains attributable.

## DevBench automation

Step 17 exposes the capture contract through the external devbench host used by
Open Shaders. The bridge is built by default through `DEVBENCH_BRIDGE=ON`, is
inert when the devbench SKSE plugin is absent, and can be omitted completely
with `DEVBENCH_BRIDGE=OFF`.

The registered tool is `communityshaders.renderscale`:

-   `status` returns a compact live snapshot of the controller profiles, VRAM
    pressure, retirement queue, post-load recovery, backend generations,
    current metrics, both-eye fidelity, and compositor-accepted per-eye
    presentation paths;
-   `record` returns the complete schema-v9 record without changing capture
    state;
-   `start` begins a new fixed-memory stress capture;
-   `apply` uses the same latest-wins transition entrypoint as a CSX-menu change.
    It requires `method` (`dlss` or `fsr`), `enabled`, `qualityMode`, and an
    optional `dlssPreset`;
-   `stop` stops the capture, writes the disk artifact, and returns the complete
    record in the tool response;
-   `reset` clears a stopped capture.
-   `probe_start` begins a bounded load-presentation probe at the final OpenVR
    submission boundary;
-   `probe_stop` stops accepting new probe samples;
-   `probe_record` returns the retained per-eye submission and luminance
    timeline;
-   `probe_reset` clears a stopped probe.

Mutating actions fail closed outside Skyrim VR. `start` and `apply` require
developer mode, and `apply` also requires an active capture so an automation
client cannot make unrecorded benchmark changes. Quality modes are `0` for
native AA/DLAA, `1` Hoshipa, `2` Ultra Quality, `3` Quality, `4` Balanced, `5`
Performance, and `6` Ultra Performance; enabled render scale requires `1..6`.

For one candidate cycle, start capture, apply the fixed scenario profiles, and
poll `status` until each requested epoch reaches `Active` with the stable
profile, exact backend generation, and both eyes valid. Drive the fixed
fast-travel leg through devbench's own console tool when that scenario requires
it. Stop capture only after the final recovery settles, then reject the run if
any returned acceptance gate fails. The record includes the build's git
description so artifacts remain attributable to the exact candidate source.

For a memory-qualification capture, apply each of the two exact profiles at
least three times (six alternating transitions total). A shorter diagnostic
capture may satisfy the generic acceptance object while
`memoryTrend.evaluated` remains false; automation must not treat that as memory
acceptance.

Performance builds keep `kEnableVRMenuPresentationTraceDiagnostics` false.
Changing it to true creates a dedicated forensic build with high-frequency D3D
menu detours and must not be compared against normal optimization captures.

The load-presentation probe is compiled only with `DEVBENCH_BRIDGE=ON`, requires
developer mode to start, and remains disabled until `probe_start`. While active,
it copies a 5x5 grid from each final DirectX eye texture into an eight-slot
staging ring and uses D3D11 event queries plus non-blocking maps. It never reads
back a full-resolution frame or waits synchronously for the GPU. It does not
emit per-frame info or debug log messages; all probe output is returned through
the DevBench tool. `probe_start` installs the existing idempotent OpenVR submit
interception immediately and fails closed if the compositor interface is not
available. The early interception remains observer-only and forwards Skyrim's
original submissions unchanged until the ordinary production render path
enables submit processing, so main-menu and loading presentation is captured
without changing it. Each record
correlates the sampled luminance grid with QPC/frame time, OpenVR submit path
and result, texture identity/format/bounds, loading and destination-world
frames, Stabilizer synchronization state, render-scale presentation path, and
the CSX HMD hidden-area-mask clear decision. `predominantlyWhite` marks a broad
white submitted texture. `hamWhitePattern` also recognizes a spatially uniform
bright or lavender perimeter around a substantially darker center, so the
engine's approximately 0.82-luminance startup clear cannot evade the strict
white threshold. The record exposes strict-white and broader bright sample
counts separately. This expanded luminance record is load-presentation probe
schema version 2.

Start the probe at the main menu or immediately before invoking the in-game
load command. Stop it only after the destination has visibly settled, then poll
`status.loadPresentationProbe.pendingReadbacks` until it reaches zero before
requesting `probe_record`. The 4,096-record ring retains about 22 seconds at
90 Hz with both eyes submitted every frame; older records are overwritten and
reported explicitly. If the HMD flashes white but the retained submitted
textures do not, correlate the QPC interval with a SteamVR mirror recording:
that result places the artifact after the application texture boundary, in an
OpenVR/compositor layer rather than the CSX presentation texture.

Step 18 calibrates the acceptance contract against the first live MCP rapid-
switch baseline. A request that reuses the already-active physical contract can
complete on its apply frame without entering vendor stabilization. That path
emits both `Applied` and `Stable`, matching its completed transition metric, so
the stable-latency gate accepts bounded synchronous reuse while retaining the
same event evidence required from a rebuilt DLSS or FSR contract. Session start
and stop events carry zero transition counters rather than inheriting metrics
from work outside the capture boundary.

Step 19 closes the logical-versus-physical convergence gap found by the first
RC97 MCP preflight. An unchanged active CSX-menu profile is synchronous only when
the boot latch, quality, exact backend generation, backend resources, and common
vendor textures are all present. If that contract is incomplete and no recovery
is already in flight, the request enters the normal latest-wins controller and
queues an epoch-owned relatch. Zero-generation or resource-free lifecycle state
is not reported as backend-ready, preventing a missing contract from producing
a false `Applied`/`Stable` result.

Step 20 preserves the DLSS teardown result across Streamline, submit-stage, and
vendor-reset boundaries. A D3D11 idle fence that is still pending now records
`WaitingForDrain` and a bounded backend retry instead of a backend failure. Query
or Streamline resource-free errors remain `Failed`, so the acceptance contract
continues to reject genuine teardown faults while allowing expected asynchronous
GPU drain polling during DLSS/FSR handoffs and post-load recovery.

Step 21 bounds repeated backend-switch allocation churn. During an ordinary
CSX-menu relatch at the same dimensions and `Normal` memory pressure, compatible
inactive host-FSR and DLSS runtime resources remain resident and are reused when
that backend becomes active again. Recovery, post-load, resize, pending-reset,
device-loss, non-`Normal` pressure, and runtime-FSR paths retain the existing
teardown behavior. The relatch plan records warm retention and target reuse so
automation can distinguish deliberate residency from missed teardown.

Vendor dimension compatibility is independent of full D3D target readiness. A
same-dimension CSX-menu handoff may reuse the physical target layout when its
always-resident anchors and every currently resident optional target still have
the expected dimensions, and the previous contract has complete, exact both-eye
fidelity evidence. This stable-contract fallback avoids treating an absent lazy
target or optional/method-specific view as a resize signal while still requiring
the strict resource probe when stable evidence is unavailable. A true render or
display dimension change still disables warm retention.

The same guarded path preserves shared submit-stage intermediates, foveated and
menu resources, common vendor textures, and periphery-TAA allocations. Their
frame/history state is invalidated and the compatible intermediates are rebound
to the new logical contract generation without reallocating them. The stable
probe requires the always-resident main, main-copy, motion-vector, and depth
textures, while optional engine targets are dimension-checked when resident and
remain governed by the render-target creation hook when created later. Records
expose `reuseRenderTargets`, `reuseStableRenderTargets`,
`renderTargetDimensionsMatch`, `stableContractEvidenceMatches`,
`vendorDimensionsUnchanged`, and `reuseSharedSubmitResources` so automation can
distinguish strict reuse, stable-layout reuse, and shared-resource retention.
The record also reports named render-target missing/mismatch masks, named stable
contract evidence blockers, and the observed `stateScreenWidth/Height`. These
are diagnostic facts only: they do not relax dimension-changing, recovery,
pressure, retirement, or device-loss behavior.

Schema v3 introduced grouping of completed transition peaks by exact backend profile
(method, backend, quality, preset, and dimensions). Once one profile has three
samples, the memory trend can distinguish cold, warm, and repeated residency.
Exact-profile grouping prevents quality or resolution changes from being
classified as leaks. Step 25 refines the acceptance rule for schema v4 below.

Live Skyrim VR decompilation for Step 21 shows that the common
`BSShaderRenderTargets::Create` path repopulates the full engine target table;
only one special target is explicitly released by that top-level routine before
the table is rebuilt. Runtime captures then showed the same approximately
1.55-GiB repeated-profile growth for DLSS and FSR, with CSX retirement fully
drained and the allocation returning naturally after a long idle. This is
treated as deferred D3D/DXGI residency rather than backend-owned leakage.

After the second distinct rapid CSX-menu relatch, and after every further
distinct relatch within the 1,800-frame window, the controller now retires
transient CSX resources and arms a common-target memory trim. Pressure,
post-load, and low-peak native restores arm the same recovery independently of
the rapid-switch count. The trim is placed behind a D3D11 event query and is
polled without blocking; `IDXGIDevice3::Trim` runs only after the GPU crosses
that fence. Post-load admission waits for this bounded attempt to complete, but
continues safely if DXGI trim is unavailable. This keeps ordinary isolated menu
changes on the fast path while bounding deferred residency during the workloads
that can otherwise approach OOM.

The live MCP status and iteration record expose `controller.memoryTrim`,
post-load trim state, and per-transition trim counts/failures. The
`memory_trim_drained` gate rejects a capture stopped while cleanup is still
pending. A candidate still has to pass `steady_state_memory_growth`; a reported
successful trim is evidence of the attempted recovery, not a substitute for the
measured VRAM plateau.

Step 22 corrects the ordering exposed by the first Step 21 live qualification.
Six-switch DLSS and FSR Hoshipa/Quality captures preserved exact both-eye
fidelity, bounded latency, and drained retirement, but the last two peaks still
grew by approximately 1.45--1.89 GiB. DLAA/Hoshipa grew by approximately
2.20 GiB. `IDXGIDevice3::Trim` completed on every protected transition, proving
that a post-allocation trim alone cannot prevent the released and replacement
engine target tables from overlapping in WDDM residency.

Before a protected recreate, the controller now unbinds all texture and UAV
stages, invalidates Skyrim's matching renderer-resource caches, flushes queued
D3D11 work, and offers the size-dependent engine, depth, deferred, underwater,
and display targets to DXGI at low priority. `IDXGIDevice4::OfferResources1`
uses `DXGI_OFFER_RESOURCE_FLAG_ALLOW_DECOMMIT` when available; older DXGI
devices fall back to `IDXGIDevice2::OfferResources`. The engine immediately
releases those offered resources while rebuilding the table, and the existing
GPU-fenced post-recreate trim remains the final destruction barrier. Offered
resources are never reclaimed or reused.

This pre-drain runs only for rapid relatches, native restores, pressure, and
post-load recovery. An isolated ordinary CSX-menu change keeps the original fast
path. Status and schema-v3 records expose pre-recreate drain counts, failures,
the last offered resource count, and whether decommit was used. The
`common_target_predrain` gate rejects an unavailable offer during a captured
protected transition; `steady_state_memory_growth` remains the authoritative
plateau check.

To evaluate this gate, complete at least three transitions to each of two exact
profiles in alternating order. Prefer two resolutions on one backend or native
AA versus an enabled profile; use DLSS versus FSR only for the backend-handoff
stress series. A one-time rise while the profiles become warm is expected; the
final same-profile delta must plateau within the bound.

Step 23 isolates the remaining Step 22 plateau failure to host FSR context
recreation. On the live NVIDIA qualification system, six-switch DLSS
Hoshipa/Quality and DLAA/Hoshipa captures passed every gate with zero measured
steady-state growth. FSR Hoshipa/Quality still grew by approximately 1.44 GiB
and 1.52 GiB for its two exact profiles, while native-AA/Hoshipa grew by
approximately 2.20 GiB. Every FSR correctness, fidelity, latency, retirement,
pre-drain, trim, OOM, and device-loss gate passed. The final relatch plans showed
that each active FSR resize destroyed and recreated two host contexts even
though those contexts were already allocated to the full per-eye display
extent.

An ordinary CSX-menu transition may now preserve those host contexts when the
previous boot contract still identifies FSR, the SDK compatibility check covers the target
render and display extents, memory pressure is `Normal`, and no runtime
upscaler, pending reset, post-load recovery, device loss, or recovery-owned
contract is active. This covers same-backend quality changes. It does not
reuse shared submit textures or engine render targets; those are still rebuilt
for the new dimensions, and FSR history is explicitly reset.
The short rapid-relatch cleanup window does not block compatible context reuse
by itself; an actual non-`Normal` DXGI pressure sample does.

The historical AMD forced-recreate path remains unchanged because the live
Step 22 evidence came from an NVIDIA DLSS-capable system and therefore cannot
qualify AMD behavior. Runtime FSR/FSR4 and incompatible host contexts also keep
their existing teardown path. The controller exposes
`reuseCompatibleHostFSRResources` in `resourcePlan`; a Step 23 FSR
qualification must observe it together with `preserveFSRResources=true`,
`destroyFSRResources=false`, and a passing steady-state growth gate before this
replacement for the generic non-AMD resize rebuild is accepted.

Step 24 follows the RC107 live qualification. Six rapid FSR Hoshipa/Quality
relatches again had zero failures, OOMs, device loss, retries, or both-eye
fidelity mismatches, and the compatible host context was preserved. However,
the final two samples of each exact profile still grew by approximately
1.37--1.40 GiB. Usage remained at approximately 10.79 GiB (11.58 GB) for a full minute
with no pending CSX retirement and with the fenced trim completed. The remaining
churn was therefore below the context layer: rapid cleanup and vendor reset
retired the per-eye textures submitted to the preserved context, while
`EnsureVRIntermediateTextures` recreated them for each contract generation.

All FSR submit inputs are now allocated to stable full-display per-eye bounds.
The host and runtime APIs continue receiving the exact active render and
upscale rectangles for every dispatch, so Hoshipa, Quality, and other profiles
do not change their effective fidelity. Compatible CSX-menu relatches preserve
the full-eye and foveated-center FSR input identities while still invalidating
frame, foveated-layout, periphery-TAA, menu-composite, and temporal history.
Engine render targets and size-dependent common textures continue to rebuild.
Actual pressure, post-load recovery, pending reset, device loss, incompatible
display bounds, and recovery-owned contracts retain the destructive path.

The compatibility policy now applies equally to SDK fallback, native/runtime
FSR3, FSR4, NVIDIA, and AMD. Every VR FSR path already creates its context and
runtime shared resources against the same full per-eye display bounds; no live
evidence supports retaining the old RC28 vendor/path-specific resize rebuild.
Path or provider changes remain independently protected by the existing reset
tracking. Native-AA reactivation can use the controller's last applied or stable
FSR method when the inactive boot snapshot no longer carries method evidence.

The resource plan exposes `reuseCompatibleFSRResources` and
`preserveCompatibleFSRIntermediates`. The legacy
`reuseCompatibleHostFSRResources` JSON member remains as a host-only alias for
existing Step 23 automation. Step 24 qualification must observe the two generic
flags together with `preserveFSRResources=true`, `destroyFSRResources=false`,
both-eye fidelity, and a passing steady-state memory-growth gate.

Step 25 follows the RC108 live qualification. Six 2.5-second DLSS
Hoshipa/Quality relatches reached 18.42 GB and `Elevated` pressure, with the two
profiles growing by approximately 1.46--1.49 GiB. FSR showed the same pattern
despite preserving its contexts and submit inputs. No capture reported an OOM,
device loss, fidelity mismatch, failed pre-drain, or undrained retirement. A
passive sample then released approximately 4.7 GB between 30 and 45 seconds and
returned to `Normal`, identifying delayed WDDM residency rather than a
backend-owned leak.

Matched 15-second-dwell controls made the distinction explicit: DLSS ended at
9.09 GB and FSR at 7.76 GB under `Normal` pressure, but the schema-v3 gate still
failed isolated upward peak oscillations of 1.49 GiB and 603 MiB respectively.
Schema v4 therefore requires growth above 256 MiB across both consecutive
same-profile intervals before classifying the trend as sustained. The record
retains all three peaks plus both individual deltas, so a decrease followed by
one WDDM rebound remains visible without being mislabeled as monotonic leakage.
Rapid captures that add a target table on every repeat still fail because both
consecutive intervals grow.

Runtime admission now protects the same transient peak. The first isolated
switch remains on the existing fast path. Once rapid-relatch memory relief is
active, or current pressure is already `Elevated` or higher, a size-changing recreate
projects its additional residency with a 50-percent estimator uncertainty
margin. If projected usage would enter the existing `Elevated` boundary, the
epoch performs its bounded cleanup and trim attempt, then waits at a 120-frame
retry cadence for WDDM headroom instead of allocating another overlapping
target table. Latest-wins request handling remains active while admission is
deferred. The rapid-relatch guard remains owned while the controller is waiting,
so clean rendering by the old contract cannot accidentally clear admission
protection. Pressure-protected transitions have a separate 3,600-frame latency
bound; ordinary transitions retain the 120-frame limit.

`resourcePlan` exposes `projectedAdditionalBytes`, `projectedUsageBytes`,
`admissionUsageLimitBytes`, `projectedResidencyGuardActive`,
`projectedResidencyDeferred`, and the existing cleanup/deferred flags. These
fields distinguish preventive backpressure from a backend stall or an actual
allocation failure.

Step 26 follows the RC110 machine-commit reproduction. Thirty uninterrupted
2.5-second DLSS Hoshipa/Quality relatches advanced cleanly through epoch 31 with
zero controller failures and exact both-eye fidelity, while DXGI local usage
rose from 5.45 GB to 15.75 GB and still reported `Normal`. Over the same run,
Skyrim private committed memory rose from approximately 21.8 GB to 72.23 GB
and Windows commit reached 108.32/109.14 GB (99.25 percent). The resulting
`STATUS_COMMITMENT_LIMIT` (`0xc000012d`) could prevent both Skyrim and unrelated
processes such as the Codex command runner from allocating before the Step 25
local-video admission boundary was reached.

The shared memory sample now records Windows commit usage, limit, headroom,
ratio, and Skyrim private usage alongside DXGI local-video residency. Ordinary
active-contract allocation overlap, including the isolated-switch path,
projects system commit at four times the resource estimate, retaining margin
above the approximately 2.8-times growth measured in RC110. Full-resolution
inactive/native restoration uses an eight-times projection because that path
showed a larger host-commit multiplier. Ordinary admission waits before the
projection reaches the lower of 75 percent of the current Windows commit limit
or an 8-GiB system reserve. This calculation is constant-time and leaves the
ordinary isolated switch unchanged while sufficient headroom exists.

A recognized VR FPS Stabilizer `PostLoadSync` door handoff instead preserves
the RC94 physical cadence. It bypasses projected local-residency ratios,
post-trim relaxation, common-target offering/decommit, engine-target
reclamation, warm-resource policy, and steady-state analysis during the
handoff. RC94 rapid-relatch cleanup remains part of the physical relatch:
memory-relief transitions retire submit intermediates, and a render-scale-off
restore tears down the active vendor contract, waits for its retired
intermediates, releases deferred targets, and only then allocates the
full-resolution replacement. That deactivation is admitted immediately so its
own reclamation cannot be blocked by pre-cleanup memory evidence. Exterior
activation fails closed when current memory evidence is missing, the device is
lost, an OOM is recent, local pressure is `High`/`Critical`, the estimated
allocation exceeds actual local headroom, or the four-times system-commit
projection would consume the hard 8-GiB reserve. Newer cleanup and recovery run
only after the destination contract becomes stable and visible; final memory
acceptance still requires recovery below the ordinary 75-percent boundary.

The first live RC94-cadence qualification exposed a presentation-release
deadlock rather than an admission or backend failure. The exterior DLSS
contract applied with matching dimensions and a ready generation, but the
combined loading/menu presentation signal and deferred transition cleanup kept
both eyes in `PresentationStretch` indefinitely, so no vendor evaluation could
promote the contract. A resolved door handoff now derives authority from the
matching LoadingMenu close frame, completed destination world frame, resolved
Stabilizer sync, applied epoch/method/generation, ready vendor runtime, and
exact submit dimensions. Once those facts hold, aggregate menu/tail and cleanup
flags cannot retain presentation-only mode. A physically open LoadingMenu, a
new load (which clears the close frame), any real non-loading menu, a pending
vendor reset, missing motion/depth inputs, or mismatched dimensions still
blocks release. This override remains valid after the controller becomes
`Active`, so a stale aggregate flag cannot re-enter stretch on the next frame.

Previous-vendor ownership resolves from the physical boot contract while that
contract is active. When it is inactive, the authoritative applied profile is
used first and the stable profile second, and only an inactive vendor-labelled
contract is accepted. Requested or applying profiles never identify previous
ownership. The resolved method is exposed as `previousVendorMethod`, so DLSS
and FSR teardown decisions remain attributable across DLAA/Native-AA handoffs.

The first guarded attempt owns one GPU-fenced trim for its epoch. Once that
trim completes, later retries only resample and wait at the existing 120-frame
pressure cadence; they do not repeatedly force cleanup. Latest-wins requests
remain available, and the previously stable physical contract continues
rendering until both local-video and system-commit admission succeed. Post-load
recovery also requires system commit below the same boundary before it admits
the fast-travel relatch.

Schema v5 exposes `systemCommit*` and `processPrivateUsage*` values in live
status, events, controller memory, transition peaks, post-load recovery, and
exact-profile memory trends. `resourcePlan` adds
`projectedSystemCommitAdditionalBytes`, `projectedSystemCommitBytes`,
`systemCommitAdmissionLimitBytes`, `systemCommitGuardActive`,
`doorHandoffHardReserveOnly`, `previousVendorMethod`, and
`systemCommitDeferred`. Acceptance
independently rejects unsafe final commit,
sustained system-commit growth, and sustained Skyrim-private growth, so a run
cannot pass merely because DXGI reports reclaimable local-video residency.

Step 27 follows the RC111 guarded qualification. Nine rapid DLSS
Hoshipa/Quality changes completed with exact both-eye fidelity before epoch 11
projected 18.62 GB of local-video use against the 18.38-GB normal admission
limit. Its successful one-shot trim left the system-commit projection safely
below the 81.85-GB hard limit, but the unchanged local guard waited 4,585 frames
and accumulated 39 pressure retries before WDDM released enough residency.
While the old Quality contract continued rendering correctly, fidelity also
reported an epoch mismatch solely because the pending Hoshipa request owned a
newer desired epoch.

After a successful pressure trim for the same epoch, local-video admission may
now use the existing `High` boundary (the lower of 87.5 percent of budget or
512 MiB of headroom). The relaxation applies only when the normal projected
limit would defer, the post-trim projection remains below that `High` boundary,
and projected Windows commit remains below the Step 26 admission limit. The
system-commit limit is never relaxed. A failed or missing trim therefore keeps
the normal local boundary, and each epoch still receives at most one forced
trim.

Fidelity observations are now owned exclusively by the physical `applied`
contract. A newer desired epoch waiting for admission no longer invalidates the
old contract that is still presented; method, generation, dimensions,
evaluation, and eye symmetry continue to be checked against that applied
contract. Schema v6 exposes `postTrimAdmissionUsageLimitBytes` and
`projectedResidencyPostTrimRelaxed` in both live status and the complete record,
so automation can distinguish normal admission, bounded post-trim admission,
and a genuine pressure deferral.

Step 28 closes the presentation-fidelity gap exposed by the RC111 manual
door-transition test. The prior fidelity record proved that a vendor evaluation
had succeeded for each eye, but it did not prove which texture OpenVR actually
accepted afterward. An intentional loading/menu stretch could therefore remain
on screen, or a source-bounds mismatch could fall back to the original submit,
while a stale successful fidelity observation still allowed the capture to
pass.

Each accepted compositor submission is now classified per eye as
`VendorEvaluated`, `PresentationStretch`, `VendorFailureStretch`, or
`BoundsMismatchOriginalFallback`. Candidate observations are discarded when
OpenVR rejects the submission, and repeated queries do not create stress-ring
events or high-frequency logs. The controller retains the latest dimensions,
method, generation, epoch, context flags, consecutive-frame count, and
monotonic path counters; capture baselines make the complete record report only
paths observed inside that session. Starting a capture also resets only the
consecutive-path measurements, so its maximum presentation-stretch duration is
attributable to that scenario without discarding the monotonic totals.

`PresentationStretch` remains valid while a loading/menu presentation or
transition cooldown deliberately protects the compositor. It must recover by
capture stop to a fresh same-frame `VendorEvaluated` submission for both eyes
whose method, epoch, generation, input extent, and output extent exactly match
the stable physical contract. `VendorFailureStretch` and
`BoundsMismatchOriginalFallback` are capture failures. Schema v7 exposes the
live paths under `controller.presentation`, session deltas under
`presentationPath`, and the `presentation_fallbacks` and
`presentation_recovered` gates.

## MCP contract

Records use schema `community-shaders.vr-render-scale.iteration` and `schemaVersion: 9`. An automation client should:

1. Reject unknown schema versions.
2. Check `acceptance.accepted` before comparing performance.
3. Require `memoryTrend.evaluated` for a memory comparison; a short diagnostic pass is not memory acceptance.
4. Use `acceptance.gates` to classify a failed run instead of inferring failure from log text.
5. Compare transition records by `transitionEpoch`, never by array position alone.
6. Prefer lower stable latency and fewer retries only after correctness, fidelity, OOM, device-loss, retirement, memory-recovery, and backend-readiness gates pass.
7. Retain the complete JSON artifact with the candidate commit and scenario identifier.

The event ring retains 302 entries and the transition metrics ring retains 50 transitions. Consecutive retries with the same request, epoch, profile, controller state, pressure, vendor-work-gate state, and normalized retry/failure kind share one event: `frame` is the first observation, `lastFrame` is the latest, `occurrences` is the aggregate count, and the memory and transition counters reflect the latest observation. The packed vendor-work-gate state combines its raw source mask with an ownership epoch, so a newly reacquired source remains distinct from an older owner with the same mask. A capture-overflow gate still fails if distinct events overwrite the ring, and a metrics-coverage gate fails if any captured request epoch rotates out of the metrics ring. Keep each iteration within both bounds. Retry and failure kinds remain classifiable as pressure, retirement, backend, OOM, or device loss.

## Acceptance gates

The runtime currently requires:

-   a stopped capture containing at least two accepted requests;
-   an `Active` or `Idle` terminal controller with no transition still in flight;
-   no overwritten capture events and complete per-request metric coverage;
-   no backend failures, OOM, or device loss in either metrics or classified events;
-   no more than 32 retries for one transition;
-   at least one stable transition, no more than 120 frames to stability on the ordinary fast path, and no more than 3,600 frames when pressure backpressure is recorded;
-   zero fidelity invariant mismatches across method, applied generation, dimensions, evaluation, and eye symmetry, with finalized vendor evaluation proven for both eyes;
-   no compositor-accepted vendor-failure stretch or bounds-mismatch original fallback during the capture, and a terminal vendor profile recovered to a fresh, exact `VendorEvaluated` presentation for both eyes;
-   a fully drained retirement queue with no deferred cleanup frame, outstanding fence, or capacity block;
-   no failed or pending GPU-fenced common-target memory trim;
-   valid DXGI, Windows-commit, and Skyrim-private samples, with local pressure recovered below `High`, post-load recovery complete, and final system commit below the lower of 75 percent or an 8-GiB reserve;
-   no more than 256 MiB of local-video, system-commit, or Skyrim-private growth in both consecutive same-profile peak intervals once an exact backend profile has at least three completed samples;
-   the active DLSS or FSR backend ready with exact requested, runtime, and stable contract generations.
-   no effective lifecycle-mutation deferral remaining at capture stop, including relevant source gates, post-load reset ownership, or queued/in-progress relatch ownership; raw source owners that are irrelevant to the active render-scale lifecycle do not fail acceptance.

Schema version 9 adds source-resolved vendor-work-gate status to the live snapshot, complete record, and retained stress events. These thresholds are part of schema version 9. Change the schema version if their meaning or units change.

## Ghidra correlation

The record lists the principal native symbols under `analysis.symbols`. In Ghidra 12.1.2, correlate regressions with these paths first:

-   `Upscaling::ApplyPendingPerfModeRenderTargetRecreate` for admission, teardown, allocation, and retry behavior;
-   `Upscaling::ApplyPendingPostLoadRuntimeReset` for fast-travel recovery ownership;
-   `Upscaling::ResetVRVendorRuntimeResources` for DLSS/FSR lifetime differences;
-   `Upscaling::ServiceVRRenderScaleMemoryTrim` for fenced common-target residency recovery;
-   `QueryVRRenderScaleSystemCommit` for Windows commit and Skyrim-private sampling;
-   `Upscaling::TryPromoteVRRenderScaleSubmitStageContract` for stable-presentation latency;
-   `Upscaling::RecordVRRenderScaleFidelityObservation` for both-eye contract failures;
-   `Upscaling::RecordVRRenderScalePresentationObservation` for the actual compositor-accepted path and terminal presentation recovery;
-   `Upscaling::EnsureVRIntermediateTextures` and
    `Upscaling::AreVRIntermediateTexturesCompatibleForFSR` for Step 24 stable
    external-resource identity validation;
-   `FidelityFX::AreFSRResourcesCompatible`, `FidelityFX::CreateFSRResources`,
    and `FidelityFX::DestroyFSRResources` for FSR context lifetime validation.

Use Ghidra to validate control flow and ownership against the shipped binary, while using the JSON record as runtime evidence. A candidate should be promoted only when repeated scenario records pass and improve the target metric without regressing another accepted backend or pressure scenario.
