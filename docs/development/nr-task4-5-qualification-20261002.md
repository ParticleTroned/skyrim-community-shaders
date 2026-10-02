# Task 4 live qualification and Task 5 candidate — 2026-10-02

## Subsequent conclusion

The [final Task 4/5 record](nr-task4-5-conclusion-20261002.md) concludes
the implemented scope after the `7aaec1186f23` live pass. It records
empty-guide elimination, 91,496 fault-free native evaluations, the held
reference/candidate comparisons, rejection of automatic sparse adoption,
the standalone DevBench-toggle correction and remaining validation limits.
The producer identities, results and open statements below describe the
earlier checkpoint and are retained as historical evidence.

## Acceptance status at this checkpoint

Task 4 progressed through the previously missing C and asymmetric-eye
checks without a new GPU fault. The run found avoidable full-resolution
guide work on CPU-proven-empty frames; this change removes that work.
**Task 4 remains open until that correction is exercised in game.**

Task 5 has a tested typed-depth copy path, ready-bound mask-work reduction,
and an opt-in GPU support dispatcher. The sparse candidate is deliberately
**DevBench-only and default off**: the offline benchmark found a dense-case
regression. **Task 5 is not concluded or a qualified production speedup.**
The next user-installed AIO is required for live before/after measurement.

Task 2 stays concluded. Native context, output ownership, allocation policy,
history policy and the late A/B insertion points are not redesigned here.

## Live producer and session

-   Branch implementation: `5ed4e57dd998664392b844bac24f581b622132b9`.
-   Producer Build ID:
    `60ed4a06ee15c8b1a0ef8373d13042e39c30c1d3c23eacc1a2568d6f8bef43b5`.
-   Compiled source: `694c64716792db45cfe35538d7ff8caab00571e2` plus
    ownership correction, dirty digest
    `ff5440ffa6b393f61f08b162c8c1791bfa08911c670ed85d43826be7e099ef95`.
-   Enabled physical AIO DLL: 31,217,152 bytes, SHA-256
    `854ab6cea3aa69815702aae75802efdeca0452e2da2f2b0a407f5713ef9583d4`.
    Manifest, producer and archive receipt matched; loose Overwrite and
    unmanaged DLL overrides were checked.
-   Skyrim PID 63908, started `2026-10-02T15:54:08.0989047Z`, Bannered Mare,
    existing null HMD, C input 1008×1120 and output 1512×1680 per eye.
-   Evidence: `build/validation/nr-task4-5-live-20261002/`. Raw evidence
    remains local. Free camera was held, but NPC motion was not frozen;
    these are bounded scene measurements, not immutable-input replay.

## Task 4 results

| Case                              | Observed result                                                                                     |
| --------------------------------- | --------------------------------------------------------------------------------------------------- |
| Cold C, evidence disabled/enabled | Healthy native evaluation                                                                           |
| C→B→C and B→A→C                   | Accepted transitions and sustained C evaluation healthy; Console-assisted admission                 |
| Unpaused B→C                      | Eight explicit `renderer_busy`, `mutationApplied=false` rejections; not an accepted-transition pass |
| A/B/C distance exclusion          | Current CPU empty proof; native counters unchanged across advancing frames                          |
| C diagnostic forced zero          | No native work; proof labeled separately from authored CPU empty                                    |
| C full-scene control              | Native evaluation continued with character isolation disabled                                       |
| C ceiling/offscreen and return    | Empty bypass followed by re-entry without resource recreation                                       |
| Asymmetric FOV/distance           | Left empty, right evaluated; coherent transaction frame 66683, subsequent runtime snapshot 66684    |
| Console/Inventory source freeze   | Source frames continued advancing; not a frozen-source runtime pass                                 |
| Completed GPU-empty proof         | Not observed live; compiled current/stale/pending fixtures remain the evidence                      |

Final counters: 49,826 feature evaluations, zero renderer failures, no
quarantine, 23 resource rebuilds and 13 runtime initializations across the
intentional mode/FOV changes. Empty/re-entry windows did not rebuild the
backend. No Display/nvlddmkm warning or error was found from process start
through shutdown. This supports the corrected control path but does not
identify the original watchdog's exact GPU dispatch.

The one-empty-eye screenshot retains ordinary scene delivery; its receipt
establishes the eye-specific native bypass. It is not a matched NR image
quality comparison. Controlled occlusion, genuinely frozen source, live
completed GPU-empty and SE/AE gameplay remain unqualified.

NR/character settings, FOV controls, profiler and evidence settings were
restored, and the owned free camera was disabled. The FOV planning snapshot
is dynamic; it is not a persisted setting mismatch. `qqq` was queued and
the exact Skyrim process exited. The recovered exact MO2 session
`20261002T163210Z-nr-task4-5-live-20261002-7bd3ff4f` completed normal close
and access release. No deployment or relaunch was performed.

## Application-stage baseline

Two C visible windows each contain 300 resolved profiler frames. Values
below are GPU self-time means in milliseconds; they are not additive to
the inclusive native pass or to a different GPU queue's clock.

| Stage                   |  First C | Repeated C |
| ----------------------- | -------: | ---------: |
| Character mask          | 0.210364 |   0.210569 |
| Native depth guide      | 0.092497 |   0.090111 |
| Character composite     | 0.111605 |   0.108402 |
| Category capture        | 0.078261 |   0.078737 |
| Early category bounds   | 0.049329 |   0.049338 |
| Inclusive native stereo | 23.40031 |   23.43556 |

Sixteen separate tiny-atlas receipts per visible window joined exact native
transactions: two calls, 2,257,920 evaluated pixels, pending CPU bounds;
native queue means were 22.90113 and 23.03719 ms. The screenshot sequences
were separate from the quiet profiler windows. Missing per-stage capture
timers are preserved as unavailable in `live-stage-report.json`.

The C empty 300-frame window retained category capture (0.066389 ms) and
bounds discovery (0.040307 ms), without native, mask, depth-guide or
character-composite timers. A empty retained 0.033838 ms of full-resolution
guide preparation. Its 16 joined captures had no native execution. The
early-guide fix addresses that measured unnecessary work; no saving for
the new DLL is claimed before testing it.

## Implementation and adversarial review

The early full-resolution probe requires the exact current source,
capture serial, logical view, selected category policy and evaluation crop.
Uncertain projection retains preparation. Its projected plan is consumed
by late mask preparation instead of being recomputed, and observation or
projection invalidation expires the cache. Each empty eye skips both
NR-only guides. A source/generation-bound record withholds the unproduced
depth view; unexpected later nonempty selection fails closed. Retained
source frames can reuse only their original prepared mask contract.

Ready current GPU bounds now narrow `maskWorkSubrect` as well as native
planning. The optional sparse dispatcher reuses the existing 32×32 source
category-superset buffer on the immediate context, even while its CPU
readback is pending. There is no added discovery pass, bounds wait,
conditional native call or per-ROI interop transaction. Metadata is bound
to capture serial, source frame, eye layout and policy. The source ring
retains the buffer and D3D command ordering protects its consumers.

Each 8×8 output group tests a conservative clamped bilinear/feather
footprint. Exact category, depth, distance and strength calculations stay
in the original shader. Positive quantized output marks a dirty tile;
departed tiles are cleared once. Initialization, reference-path use,
uniform masks and resource changes invalidate that dirty metadata.
Small rectangles and completed dense summaries retain the reference path;
pending summaries cannot reliably select the dense crossover yet.

The candidate still launches a whole output grid: it reduces expensive
per-pixel work and writes, not launched thread count. Capture evidence
reports actual launched pixels/threads and the selected dispatcher.
`clearedPixels` counts explicit CPU-issued clears; GPU tile-clear and
support-test counts are not read back. Dirty metadata is four logical
bytes per ceil(width/8)×ceil(height/8) tile and allocated only on opt-in.

The sparse shader is a separate compile permutation. Default reference
DXBC matches HEAD byte-for-byte for CSHADER, VR, HDR_OUTPUT and VR+HDR_OUTPUT.
The session-only `experimentalGpuMaskSupport` switch is excluded from
saved configuration and exposed with input/output schema and capture
identity. Non-DevBench builds cannot enable it. Default-off avoids adopting
the observed dense regression; no universal performance-neutral claim is
made for an untested enabled candidate.

Typed depth copying reuses `CopyTextureSubrect` only after existing resource,
view, source-layout and ROI validation, with both resource and view
`R32_FLOAT`. The replaced shader performs an identity texel `Load`, so the
same source, mip, crop, encoding and sample positions are preserved.
Typeless and other format sources retain the shader path. Mixed batches
preserve bindings through the existing compute-state guard. Shader
compilation is unnecessary for an entirely compatible batch.

Review covered scope, source/lifetime correctness, mono/stereo behavior,
overflow, dirty initialization/re-entry, no unproduced reads, fallback,
resource naming, state restoration and reuse of existing copy/planning
utilities. No context-only padding becomes output ownership. Reconstruction
and final exact selection keep their immutable inputs and zero-weight
guards. No new history stabilizer, actor scan or automatic cost model was
introduced.

## Offline evidence and limitation

The WARP suite compares reference/sparse R8 output exactly, including 54
additional sparse/dense, mono/stereo, crop, thin/alpha-hole, excluded category,
jitter phase and feather cases. Each checks poisoned initialization,
17-texel movement/return, departure, repeat empty and re-entry. Existing depth/distance, final
composition and finite/NaN unused-storage tests remain active. Typed depth
copy matches the identity shader bitwise across valid rectangles in two
different backing capacities. The compiled production empty-probe fixture
checks both runtimes' eye counts, stale/frozen sources, invalid dimensions,
policy mismatch, projection uncertainty, diagnostic masks and failure.

Offline hardware comparison, Skyrim closed, 1008×1120 per eye, 12 paired
alternating-order samples with 16 repetitions each. The measured scope is
**existing bounds discovery plus mask**, not native NR or complete gameplay.

| Synthetic support / feather radius | Reference mean ms | Candidate mean ms | Median paired change |
| ---------------------------------- | ----------------: | ----------------: | -------------------: |
| Sparse / 0                         |          0.212704 |          0.139159 |               −31.8% |
| Sparse / 4                         |          2.855688 |          0.353158 |               −87.5% |
| Dense / 0                          |          0.411566 |          0.459951 |               +12.7% |
| Dense / 4                          |          6.602785 |          6.841748 |                +3.8% |

The first sparse result saves about 0.074 ms, 0.66% of an 11.11 ms budget;
the dense radius-zero case costs about 0.048 ms, 0.44% of that budget.
GPU clock/thermal drift is visible, especially radius four. All raw pairs
and ranges are retained in `nr-task5-offline-20261002`; this is a candidate
screening result, not a portable break-even model or game-frame speedup.

Local automation feedback was recorded as
`AUTO-20261002-171554279-DDFF8018` (safe transition admission starvation)
and `AUTO-20261002-171555106-2E2A730A` (read-only capability receipt rejected
by the capture adapter). Nothing was published externally.

## Next installed-DLL pass

1. Verify the new AIO's physical DLL, manifest and runtime Build ID.
2. Repeat A empty/entry and the asymmetric-eye case; require no early
   full-resolution guide work for CPU-proven-empty eyes and no stale reads.
3. Repeat accepted B→C and C steady-state health, preserving any safe busy
   rejection separately from an applied transition.
4. Compare typed-copy correctness and application-stage timing against the
   retained baseline. Exercise real frozen source/occlusion where available;
   do not substitute an advancing menu frame for a frozen-source pass.
5. Run matched `experimentalGpuMaskSupport=false/true/false` sparse, dense,
   motion, edge and disappearance windows with exact receipt attribution.
   Dense/pending crossover and total cost must satisfy improvement-or-neutral
   before production enablement. Otherwise retain the reference path and
   record the rejected candidate explicitly.

Installation and a fresh game launch remain with the user. Neither task
may be reported as concluded solely from successful compilation or this
archive's existence.

## Final validation and test archive

The universal SE/AE/VR Release DLL and all **220/220** controller/shader
CTest entries passed. `validate-local.ps1` first retained a 219/220 run
with an outdated dispatch-expression contract; that contract was corrected.
Its next run passed all 220 entries, then stopped at the preset fingerprint
check. Review confirmed the added NR control is session-only, with no new
serialized key, default or migration. Contract revision 8 was retained;
preset regeneration changed only source-fingerprint metadata. The preset
generator regression and `-Check` then passed, as did manifest verification
and scoped pre-commit. The two original failed runner results remain intact.

The completed follow-up is
`build/validation/nr-task4-5-aio-20261002/validation-followup.json`;
it combines the successful DLL/test stages with the repaired preset checks.
It does not relabel the interrupted full runner as a pass. The compiled
non-documentation Task 4/5 file hashes still match. Concurrent preset values
were preserved and are excluded from the NR commit and AIO inputs; only
this change's compatibility metadata is committed with NR.

Producer Build ID:
`7aaec1186f239dbe2917c2f09adba1327a3886ec7defeef53d31f1140ceee0e0`.
Compiled base: `5ed4e57dd998664392b844bac24f581b622132b9`, dirty digest
`895aa65a125700cc7b94e981630778251a25e98d8a4cf8e4c9d3a069913099a6`.
This exact digest includes the contemporaneous workspace metadata; later
reporting and preset-fingerprint updates do not change the compiled code.
DLL: 31,229,952 bytes, SHA-256
`5f1d7032a7fda5313022294411c73bf635f32ac215e69f973a1aeb027bde645a`.
DevBench is ON, Tracy and automatic deployment OFF, with runtime 310.8.

Test archive:
`dist/CSX_AIO-main-vr-nr-NR-Task4-5-DevBench-20261002-7aaec1186f23.7z`.
Its adjacent receipt preserves producer identity, validation limitations,
archive integrity and the complete extracted-payload hash comparison.
It contains no user settings or shader cache and has not been installed.

Archive integrity and **385/385** extracted file sizes/hashes passed.
Archive size: 199,071,805 bytes; SHA-256
`f3908a61c14ddbc741c48c13619105882c7b68c5641c0af8b7f1933ec622f90a`.
The extracted DLL/PDB/manifest matched the producer and the neural runtime
hash matched the pinned 310.8 payload. The default experiment is disabled.
