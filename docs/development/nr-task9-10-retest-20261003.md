# Task 9/10 retest and narrow-context admission

Tasks 9 and 10 remain open. The corrected implicit-plan and compact-retention
paths passed their targeted checks, but a subsequent GPU device removal
invalidates session health. This change excludes narrow current-context
experiments; it does not establish the watchdog's root cause or qualify a
production performance/quality change. Task 11 remains at the ownership and
packing audit documented in [the preceding record](nr-task9-11-live-20261003.md).

## Producer, scene and preserved evidence

Skyrim PID 24436 started at `2026-10-03T10:26:24.6950737Z`. The universal
SE/AE/VR DevBench producer was
`8b1973afbc4fc4df38527b3bed5325299f947bbefa485caf13309bdf0075697e`, compiled
from `b50c94f7c321b7b1cb883821b627e26dd864fbac` with dirty digest
`3889a45ac0f0cfa40e15fbafa60440c7e20f331a2a150ee2de9e6963f4c1916c`.
The subsequent source commit `46fd070fabb565f4827510e475e589f6470f4547`
does not replace this compiled identity. Physical enabled AIO DLL, adjacent
manifest, archive receipt and runtime producer agreed: 31,678,976 bytes,
SHA-256 `d7cad6625249eade390a103468c2f2f44de4ab97c6f9183bc51eeaf30e947dbe`.
There was one enabled loose provider; Overwrite and unmanaged Data had none.

The native runtime was 310.8.0, SHA-256
`8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
The exact profile was
`Codex Task - 20260919t055629z-tracy-guardian-main-vr-1bf5803a`, using
physical SteamVR. The Bannered Mare C input was 1008x1120 per eye, raw
colour, current-context experiment enabled, multi-ROI disabled. Five
eligible actors were initially reported. AI was disabled and an owned free
camera held the initial position; animations and source pixels were not frozen.

The initial camera move was obstructed by shelving. This limits the sparse
measurements to that view, not a representative multi-character quality
comparison. The user subsequently repositioned it. The post-fault HMD
preview shows several clear faces/hair and a large right-side pillar, but
cannot qualify NR output after quarantine. Its first viewer request failed;
all PNG chunk CRCs and decoded sizes verified and a later view succeeded.
The files are not corrupt screenshot evidence.

Receipts, native HMD images, profiler histories, driver-event XML, the frozen
lifetime journal, source logs, identity and restoration are retained under
`build/validation/nr-task9-11-retest-20261003/`. No full native-input export
was armed in this session.

## Targeted results and rejected performance comparison

The measured planner accepted the implicit single-region representation,
published one valid candidate with actual physical slots 2/3 and returned
`unknown_cost_fallback` without an experimental profile. Invalid profile
input returned `measured_profile_rejected`, `mutationApplied=false`; no
profile was loaded. The planner was disabled before timing.

All three bounded captures resolved 300 frames. The following values are
`Upscaling::DLSSNeuralRenderingStereo` self time, not whole-frame cost or
an independent sum of clocks.

| Treatment               | GPU mean ms | GPU p99 ms | CPU mean ms |
| ----------------------- | ----------: | ---------: | ----------: |
| Full coordinates before |    6.422135 |   7.310614 |    1.619364 |
| Compact retained        |    9.862357 |  11.428066 |    1.621036 |
| Full coordinates after  |    7.680829 |  14.407933 |    2.084051 |

During the wider compact status interval, frames 37221 through 38971,
3,500 native evaluations returned successfully and resource rebuilds stayed
at six. Both full-coordinate status intervals also had zero rebuilds.
This verifies retention in the tested interval; it does not prove general
lifetime or visual correctness. The full-coordinate GPU means drift 19.60%,
the scene/ROI changes, and compact is slower in these observations. No
performance improvement, cost-profile calibration or quality promotion is
accepted. The post-treatment capture completed at `10:45:21.769Z`, before
the driver events, but the complete session still fails its health gate.

## Failure localization and limits

The frozen 64-record journal contains the following final accepted CPU
submissions. No resources were rebuilt in these entries; slots 2/3 retained
resource serials 7/8 at 1008x1120 capacity and reset-per-evaluation.

| Source frame | Both eye contexts (x,y,width,height) | Issued / observed completed fence |
| -----------: | ------------------------------------ | --------------------------------- |
|        39778 | (0,512,256,256)                      | 16666 / 16664                     |
|        39779 | (0,384,192,736)                      | 16668 / 16667                     |
|        39780 | (0,384,64,736)                       | 16670 / 16669                     |

The last submission's native calls returned success; fence 16670 was not
proven complete by its CPU observation. At the next NR batch, source frame
39876, the **entry** fence snapshot already reports device removal. That
batch attempted no native evaluation. Its later colour-input-copy check
reported `DXGI_ERROR_DEVICE_HUNG` (`0x887A0006`), quarantined NR and retained
unsafe native ownership. The copy stage is therefore an observation point,
not evidence that this copy caused the hang.

Windows logged five `nvlddmkm` event-153 records at
`10:45:26.9006126Z` through `10:45:26.9347017Z`, reporting GPUID 6400.
NR logged removal at `10:45:47.861Z`. A Lighting/Hair shader compile
completed at `10:45:33.567Z`; no causal claim follows from that proximity.
Windows Error Reporting identifies the current LiveKernelEvent 141 dump
`WATCHDOG-20261003-1245.dmp` and a `vrcompositor.exe` failure at
`10:45:30.3320148Z` (`ucrtbase.dll`, exception `0xc0000409`). Other queued
reports submitted at the same time reference older dumps; they are not
additional failures in this run. The two current 141 reports share one
report identity. No Skyrim process crash was reported in the bounded query.
The protected WATCHDOG directory remained inaccessible, including outside
the workspace sandbox. No GPU instruction trace identifies the failing
dispatch. The counters retain one device removal, one failure and one
quarantine after 16,658 native API evaluation successes; subsequent
quarantined bypasses/stereo failures are not additional removal events.

## Conservative correction and adversarial review

The current-context experiment could submit a 64-pixel-wide clipped region.
The existing 128-pixel experimental geometry floor only covered plans with
more than two regions, so the implicit single-region case bypassed it.
`ApplyCurrentContextExperiment` now retains the original ROI unchanged when
either padded dimension is below 128. It reuses one geometry predicate with
the existing higher-count admission check. The registered DevBench action
and parameter description state the fallback and its qualification limits.

This is not a new provider minimum-size claim. The earlier eight-by-64x64
completion failure and this single-strip event justify excluding an
unqualified experimental envelope, not asserting an internal driver cause.
The normal retained-envelope algorithm is unchanged, including its own
unqualified edge cases. There is no claim that every baseline ROI is now
safe or that 128 pixels guarantees completion.

Review covers horizontal/vertical clipping, odd dimensions, exact 127/128
boundaries, the observed 64x736 shape, retained ownership/support/capacity,
disabled and unsupported modes, multi-ROI exclusion and immutable history.
The change adds no production render-loop check, resource, wait, copy or
telemetry: the current-context implementation is compiled only with the
DevBench bridge, and the existing higher-count comparison is equivalent.
Saved selections, default-off experiments and production defaults remain.
Preset revision 8 and rendering values are retained; only source-contract
fingerprints are regenerated after the guarded code/schema change.

## Shutdown and next qualification

No `nr_reset` or retry was dispatched after removal. Disabling NR was
accepted with failed retirement reported separately; it did not recover
the unsafe backend. The original settings fingerprint, colour controls,
AI-on state and profiler-off state were restored, and free-camera ownership
was released without another camera drive to the original pose. Screenshot
queues/jobs/sequences and native replay were verified inactive.

`qqq` was accepted and Skyrim exited. The exact MO2 process closed through
File/Exit; its recovery session and access lease were released. This task
did not stop any SteamVR process; the compositor had crashed during the
GPU incident and the existing vrserver remained running. Installation and
restart remain user-owned.

The replacement DLL needs a fresh current-context edge-entry/exit run,
followed by a stable unobstructed full/compact/full comparison with exact
frame attribution and quality checks. The watchdog's cause remains open.
Task 9 additionally retains candidate-search/reanchor and held-out model,
transition, CPU and residency gates. Task 10 retains whole-pipeline and
quality qualification. Task 11 must update verified current equipment
ownership, the shared versioned CPU/HLSL codebook and every setting/mask/
cache consumer together; no equipment implementation is included here.

## Offline validation and replacement producer

`pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/nr-narrow-context-20261003`
passed the universal Release DevBench build, all 227 controller/shader tests
with none failed or skipped, preset generator tests/check, whitespace,
source stability and DLL provenance in 1136.35 seconds.
Main compilation took 225.18 seconds, test builds 640.31 seconds and CTest
199.44 seconds; concurrent compilation used a separate output directory.
An earlier sandboxed targeted build was denied access to the vcpkg cache;
`pwsh ./tools/dev-doctor.ps1 -Network` passed outside that sandbox and the
complete run above succeeded. The source contract check initially rejected
its stale fingerprint before the documented metadata-only regeneration.

Replacement Build ID:
`f0acc3926842a84b507d0657b0fa4936cf45286d9627e8524ac807cca7a8f3b6`.
Compiled base: `46fd070fabb565f4827510e475e589f6470f4547`;
dirty digest: `3834acbea5a4868fc35a904c7c6f6a6b0e642ed61e4e5675afeb7b52d1913210`.
DLL: 31,678,976 bytes, SHA-256
`96bdf41dcfb47902ce10e16bed107b5635e78b94ea431e8a9536c23a166684d7`.
Exact compiled changed files, patch and manifest are retained under
`build/validation/nr-narrow-context-aio-20261003/`. Final evidence text was
updated after compilation; the eventual commit is not a new producer.

No new live game, SE/AE gameplay or complete bridge-off DLL was tested.
No narrow native GPU probe was retried; the semantic regression tests check
admission/fallback without submitting the unqualified shape to the driver.
The source and bridge-off controller coverage do not substitute for a live
watchdog or quality qualification.

The verified archive is
`dist/CSX_AIO-main-vr-nr-NR-NarrowContext-DevBench-20261003-f0acc3926842.7z`
(199,352,686 bytes), SHA-256
`4ae7af56b57811bc08a0325a161b0be769750bc26473974f3614fb1c65f2fe5f`.
7-Zip integrity and full extraction passed; all 386 extracted files match
staging by size and SHA-256, and the packaged DLL/PDB/manifest match the
validated producer. DevBench is enabled, Tracy disabled, and all three
runtimes are compiled. No personal settings or compiled shader cache is
included. The archive was not installed and the game was not relaunched.
