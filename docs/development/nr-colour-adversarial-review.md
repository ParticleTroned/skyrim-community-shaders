# NR colour adversarial review and hardening

Reviewed base: `a6697404a2b3045f4d3d49d8468ebf3bcd6fc5d0`, on
`work/face-of-gogh-colour-managed-20260914`. This change also incorporates the
previously uncommitted `nr-colour-dry-validation.patch` against that base.
Do not apply that older patch again after integrating this commit.

## Scope and disposition

This is a correctness, robustness, DRY and offline-test pass, not another NR
colour algorithm or a claim that the model's colour contract has been identified.
The shared standard/character/multi-ROI renderer, early/late placement, NVIDIA
runtime admission/loading, normal DLSS and character masks remain unchanged.
All runtime colour controls still use the in-game feature and DevBench registry.
No editable colour INI is introduced. The feature/version manifest is **1-2-0**.

### Confirmed defects addressed

| Area | Defect | Correction |
| --- | --- | --- |
| Shader output | FP32-finite RGB can overflow R11G11B10/FP16 or silently clamp in UNORM. | Carry destination-storage bits in the existing 48-byte constant buffer; reject unrepresentable edits before storing them. Preserve the baseline rather than guess a new exposure. |
| Reconstruction diagnostics | A bad inverse of the actual prepared input could fall back to baseline while the measurement reported no inverse error. | Share `ReconstructCandidate` between rendering and measurement; read prepared input at CS t4 and validate both inverses and destination range. |
| CPU reference | CPU reconstruction did not validate the baseline's forward transform, unlike the GPU. | Check the same admission rule and sanitize a nonfinite baseline rather than return it. |
| Exposure validity | Two negative adaptation components could yield a positive, accepted ratio. | Reject negative components in CPU/GPU capture; retain zero-input fallback as a distinct, unmeasured state. |
| D3D ownership | Exposure binding could reuse resources after a device/context change. | Reject before update/copy and test the shared transaction-admission policy. |
| HDR effect indexing | Capacity is not the count of initialized effects. | Bound effect access by size rather than allocated capacity. |
| State restoration | Exposure and colour passes independently implemented overlapping CS state guards. | One `ComputeStateGuard<N>` owns touched shader/class-instance, SRV, UAV, constant-buffer and predication restoration. |
| Work/readback provenance | Reconstruction depended on a supplied configuration; delayed older readbacks could overwrite newer slot evidence. | Retain the preparation configuration and reject revision mismatch; order measurements by an internal monotonic preparation serial that survives pipeline reset. |
| Assessment evidence | Missing mode/profile/flags, inconsistent sample counts or exposure data could be accepted. | Require typed effective state, full editable profile, valid extents/formats, expected sampled-pixel counts and coherent exposure arithmetic. |
| Assessment workflow | A late result could exceed the sample deadline, or one unavailable candidate could stop every later candidate. | Propagate the remaining deadline; continue only for a healthy unchanged session with no qualifying samples. Transport/identity/ownership failures still stop. |
| A/B and recovery | A settings acknowledgement was insufficient to prove hidden-edit output; uncertain mutations could invite unsafe cleanup. | Require fresh hidden-edit baseline evidence; never retry an indeterminate mutation or blindly restore/stop an unproven owner. Preserve primary and cleanup errors. |
| Deployment | A local include not mapped into the deployment package could pass a source-presence check. | Validate include closure against the deployment mapping and return structured unreadable/missing-file failures. |

## Representation and diagnostic contract

The storage classes are CSX-owned flags, not NVIDIA ABI fields. R11G11B10 is
unsigned: maximum finite values are 65024 for R/G and 64512 for B. FP16's finite
maximum magnitude is 65504. UNORM accepts 0..1. FP32-finite edits outside the
applicable limits are rejected before the UAV store. Legacy Raw remains the
comparison path; it is not silently converted into a different artistic mode.

The existing control buffer remains 48 bytes. Low flag bits retain their prior
meaning; bits 0x300 select the destination representation. The shader result is
computed with an explicit precise residual expression. The bounded detail
filter uses differences of logarithms instead of dividing large luminances
before taking a logarithm. It retains source alpha and fades detail at ROI edges.

The measurement buffer remains 24 floats / 96 bytes (API v2). Index 17, exposed
by the existing `invalidInverseSamples` name, now counts a rejected reconstruction:
this includes invalid prepared-input inverse, neural-output inverse and
unrepresentable reconstructed RGB. A zero colour error with this counter nonzero
is **not** a passing transport test. Measurement binds prepared input at t4;
colour state restoration therefore preserves t0..t4. The five existing shader
assets are sufficient; no unlisted shader include was added.

`processed` means a private reconstruction was queued. It does not prove the
outer renderer or HMD presentation subsequently committed the result. Assessment
reports explicitly retain `outputCommitVerified: false` and describe this scope.
Source-drift ranking is not proof of physically correct shadows or reflections.
`domainVerified` stays false; no production conversion is chosen automatically.

API v2 also lacks an immutable per-transaction region manifest. For a fixed
multi-ROI test, supply `--expected-physical-slots`; otherwise the runner checks
all regions visible in retained observations without claiming exhaustive hidden
region coverage. It never counts two routes as two different source frames and
rejects a generation transition within the selected sampling window.

## DRY and lifecycle boundaries

`ComputeStateGuard` is deliberately local to NR colour/exposure. It does not
replace unrelated engine state management. Rendering and diagnostic rejection
use the same HLSL candidate helper; the Python workflow tests reuse a controller
process double rather than create another live transport. The existing
skyrim-vr-automation controller remains the live owner of identity, semantic and
performance-neutral checks. This commit does not change that repository.

Configuration is snapshotted once for each colour Work. Resource/context changes
cannot reuse an exposure latch, while a capture toggle cannot change a latch
halfway through the same stereo/ROI transaction. Capture timing and the real HDR
hook remain runtime qualification requirements. No forced GPU flush or blocking
runtime readback was introduced. Standalone WARP tests deliberately use blocking
readback for assertions; that does not alter the runtime path.

## Offline evidence

A controlled comparison against the base assessment reproduced three additional
false acceptances: missing effective mode; a rectangle inconsistent with the
reported sample count; and negative captured average with a ratio-valid flag.
Each is rejected after the changes. The earlier dry-patch regressions cover late
deadlines, incomplete known secondary regions and missing transport flags.

Tests run in a blob-verified **partial source snapshot**, not a full Windows
checkout:

- NRColorPolicy: 452 checks; NRExposurePolicy: 513 checks.
- NRExposureLifecycle: 88 policy checks; NRColorAdversarialPolicy: 98 checks.
- NRColorAdversarialAssessment: 10 tests; NRColorRunnerWorkflow: 30 tests.
- NRColorAssetVerifierFixtures: 9 tests; NRColorAssetInventory: passed.
- Nine standalone existing assessment tests and two existing source contracts.
- Python syntax checks and strict-warning C++ adversarial-policy compilation.

Eight selected portable CTest targets passed. Full source-contract execution
requires the complete Renderer/Feature sources; the full existing assessment
suite also requires NeuralColor.cpp. Those full-checkout suites were not claimed
as executed in the partial sandbox and remain enabled in CMake.

`exposure_shader_gpu_test.cpp` now includes corrupt-prepared-input diagnostics,
negative exposure, packed-output overflow fallback and actual R11G11B10 UAV
readback. FP16 range policy is exercised separately; it is not a claimed physical
FP16-texture test. Existing WARP tests remain registered.

**Not run here:** Windows plugin compilation, either WARP executable, live
PowerShell/DevBench orchestration, real engine hooks, NVIDIA inference, deployed
MO2/VFS resolution or in-game visual/performance tests. A source verifier and CPU
reference cannot prove any of these.

## Next local validation gate

Build the plugin using the repository's normal Windows configuration. Install
the rebuilt DLL and the 1-2-0 feature/shader package together, then run:

```powershell
python tools/nr-color/verify_assets.py
cmake -S tests/neural_color -B build/nr-color-tests
cmake --build build/nr-color-tests --config Release
ctest --test-dir build/nr-color-tests -C Release --output-on-failure
```

Only after those gates should the existing documented live assessment be used.
Verify HDR bindings/timing, exposure recovery/history behaviour, outer stereo
commit, standard/ROI equality for matched rectangles, A/B appearance and added
GPU cost. A missing exposure cannot be repaired by borrowing a later/older frame
or by calling an arbitrary multiplier measured engine exposure.
