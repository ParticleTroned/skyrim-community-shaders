# CSX Reverse Z and Hybrid Hi-Z culling: Codex handoff

Prepared **2026-10-03** for continuation on another machine.

This document combines the design analysis, implemented contracts, review
findings, source identities and remaining validation work. It is a portable
handoff, not a runtime acceptance report.

**Current outcome:** an optional **Hybrid Hi-Z** producer is implemented and
pushed on **codex/vr-hybrid-hiz-culling**. It preserves Skyrim's object
collection and result delivery while replacing depth reduction and the GPU
bounding-box test. It uses **conventional depth**, not Reverse Z.
**Advanced remains the default.** Neutral-or-better performance and acceptable
stereo appearance have **not** been demonstrated in Skyrim.

## 1. Instructions for the next Codex session

The user's objective is the most performant CSX VR depth culling with the fewest
visual issues. Performance must be neutral or better than the previous
implementation. A larger rework is acceptable if evidence justifies it.

Start by reading this handoff and the branch's AGENTS.md, then inspect the
pinned implementation and its two Hybrid design documents. Preserve Advanced,
Legacy, the Info-visible Hybrid selector, and the shared DevBench controls.

Do not confuse the tested pre-rebase source with the current pushed source.
Do not claim that conservative testing against a captured depth image proves
safe reuse after head motion or movement of occluders. Do not assume a native
result index identifies the same object on a later frame.

The immediate technical gap is validation of the rebased tip and actual
in-game integration, fidelity and performance. Building, deploying or running
an assay should follow the user's next instruction and the destination
machine's repository/automation policies. This document itself does not
authorize deployment, stopping a running game, or changing its configuration.
The user's last rebase instruction explicitly said **no compile tests**.

## 2. Repository and exact source identities

Repository: [ParticleTroned/skyrim-community-shaders](https://github.com/ParticleTroned/skyrim-community-shaders)

Authenticated origin URL: git@github.com:ParticleTroned/skyrim-community-shaders.git

| Role                                          | Identity                                    |
| --------------------------------------------- | ------------------------------------------- |
| Feature branch                                | codex/vr-hybrid-hiz-culling                 |
| Current pushed tip                            | f17b833b56b5078527bb9a5b413e033455ad855d    |
| Current parent / September 29 rebase base     | dab1874a76fd39175dcefdc52110ba69d7284e12    |
| Tested clean pre-rebase tip                   | be8891ad0486909effb78cc4c47fbe8fcfa3b35f    |
| Original implementation base on main-VR       | df9f377a53d237518c1b671b7be1085c9a65b69d    |
| Local pre-rebase backup branch                | backup/vr-hybrid-hiz-before-rebase-20260929 |
| Earlier intermediate review source            | 7d04ad7268fd59e21f283a7b363d3c2b05f8ba7d    |
| Earlier source before Info-selector amendment | 851b52c3d6de8e41f65bd142151948a451129ddd    |

[Current branch snapshot](https://github.com/ParticleTroned/skyrim-community-shaders/tree/f17b833b56b5078527bb9a5b413e033455ad855d)

The branch was pushed to origin on October 3. Local and origin-tracking
feature refs were verified at the current tip during this handoff. No PR
creation or merge is recorded. The listed base is the base used for the
September 29 rebase; it is not a claim about today's latest main-VR.

The rebase resolved three conflict blocks in src/MenuDevBenchBridge.cpp.
The resolution retained Hybrid actions, telemetry and captured method state
alongside the newer FOV blend-curve, parallax-strength and Adaptive Balance
atmosphere/color contracts. CMake's focused test additions survived its
automatic merge. Static review and Git checks were performed; **no compilation
or tests were run on the rebased tip**.

The current worktree on the originating machine is:

```text
C:/src/skyrim-community-shaders/build/worktrees/vr-hybrid-hiz-culling
```

That path is historical/local convenience, not a required destination layout.

## 3. User decisions that must survive continuation

1. Keep Skyrim's object collection and bounding boxes.
2. Replace or improve depth downsampling with a conservative, stereo-aware
   depth hierarchy.
3. Adapt or replace the native bounding-box visibility test to consume it.
4. Preserve deferred/asynchronous result delivery after verifying
   object/result correspondence.
5. Improve temporal validity so head movement does not turn stale occlusion
   into missing geometry.
6. Expose Hybrid next to Advanced for direct comparison, available at normal
   **Info** logging rather than only Developer Mode.
7. Inherit existing DevBench enable/reset, logging and profiling facilities.
8. Review scope, correctness, robustness, performance and DRY; fold related
   fixes/evidence into the feature change when authorized.
9. Require neutral-or-better performance and a separate visual correctness
   pass. Do not present an unmeasured optimization as an established gain.

## 4. Reverse Z, Hi-Z and temporal culling are different concerns

### Reverse Z changes depth representation

A normal D3D perspective depth buffer maps the near plane toward 0 and the
far plane toward 1. Reverse Z maps near toward 1 and far toward 0. Combined
with floating-point depth, its distribution can substantially improve useful
depth precision. Simply reversing an integer-depth representation does not
provide the same benefit. This addresses precision and z-fighting, rather
than establishing whether an object remains hidden after the camera moves.
[NVIDIA: Visualizing Depth Precision](https://developer.nvidia.com/blog/visualizing-depth-precision/)

In particular, computing 1 - depth after writing a conventional D24 scene
buffer cannot restore information that was already quantized away. Neither
can copying that existing depth into an R32_FLOAT hierarchy. Such a conversion
can change comparison convention, but cannot deliver the precision of a
renderer that produced floating-point reversed depth from the outset.

### Hi-Z summarizes spatial coverage

An explicit Hi-Z texture hierarchy lets a visibility test inspect a few
coarse cells covering an object's projected bounds. It is separate from the
GPU's internal hardware hierarchical depth/early-Z implementation.

For CSX's conventional depth, each cell stores the **maximum** depth of every
covered source sample: the farthest sample. A captured-view occlusion test is:

```text
objectNearestDepth > farthestCoveredSceneDepth + depthBias
```

This intentionally requires the nearest point of the bounding volume to be
behind the farthest sampled scene depth across its entire screen rectangle.
Averaging samples or considering only a few source pixels can hide holes and
cannot support the same conservative argument.

With a genuinely reversed renderer, the corresponding convention would be:

```text
pyramid cell = minimum of all covered reversed depths
objectNearestReversedDepth = maximum reversed depth of the projected bounds
occluded = objectNearestReversedDepth < minimumCoveredReversedDepth - bias
unknown / uncovered / far background = 0
```

Changing only max to min in the current CSX implementation would be wrong.
Production, clear values, projection, reduction, reconstruction and
comparisons must all use the same convention.

### Temporal validity is a separate proof obligation

A perfectly conservative hierarchy proves something about its captured depth
and camera. It does not prove visibility after a camera translation, head
rotation, object movement, or movement of an occluder. Reverse Z does not fix
object/result mismatches, stale camera snapshots, disocclusion or delayed
result consumption.

The promising native integration is therefore the implemented hybrid:
retain the native collection/delivery system, improve spatial testing, and
explicitly police temporal reuse. A full Reverse Z renderer is a separate,
much broader potential project.

## 5. What Bottled-Shaders actually implements

The original discussion linked
[InTheBottle/Bottled-Shaders](https://github.com/InTheBottle/Bottled-Shaders).
The available September notes did not preserve an exact Bottled revision.
The following is a **fresh public-source recheck on October 3**, not a claim
about the exact code originally considered.

| Reference                               | Pinned identity and significance                                                                      |
| --------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| Bottle-Compendium                       | 68356b57f624eacf98033632500724c0d0cf66a1; evolved implementation; commit time 2026-10-03 17:28:40 UTC |
| Evolved Reverse Z introduction          | 6db6512c962d9cb6d7a166f206b1f10d9f2789b1; September 22                                                |
| Interior perk-menu correction           | 1e88b274719be6fc29eee792827ef76cae76a510; September 23                                                |
| Older reversez branch                   | 98bb36b38b995194bde27854079a4b8de274d0aa; September 7, 2025 prototype                                 |
| Separate codex/hiz-culling-clean branch | Observed during discovery; not audited for this handoff                                               |

The evolved implementation:

-   Transforms clip depth using z' = w - z, adjusts projection/view-projection
    and inverse matrices, handles orientation, and avoids double reversal.
-   Widens selected main/copy/decal/post-prepass/post-water depth targets from
    R24G8_TYPELESS to R32G8X24_TYPELESS with D32_FLOAT_S8X24_UINT views and
    corresponding SRVs. This changes storage and bandwidth requirements.
-   Reverses applicable comparisons, clear depth, depth-bias signs and viewport
    intervals; tracks depth conventions across passes, with separate cubemap
    handling.
-   Latches enablement at boot and explicitly requires
    **!REL::Module::IsVR()**. The older prototype's SupportsVR declaration
    does not establish working VR support for the current implementation.

These mechanics are in the pinned
[ReverseZ.cpp](https://github.com/InTheBottle/Bottled-Shaders/blob/68356b57f624eacf98033632500724c0d0cf66a1/src/Features/ReverseZ.cpp).
They make Reverse Z a renderer-wide migration, not a culling toggle.

The feature also changes depth shader types/reconstruction and numerous
consumers, including upscaler integration and conventional-depth conversion
for Effects11. See the
[introducing patch](https://github.com/InTheBottle/Bottled-Shaders/commit/6db6512c962d9cb6d7a166f206b1f10d9f2789b1)
and pinned
[SharedData.hlsli](https://github.com/InTheBottle/Bottled-Shaders/blob/68356b57f624eacf98033632500724c0d0cf66a1/package/Shaders/Common/SharedData.hlsli#L663).

Its grass Hi-Z code is a separate consumer adapted to both conventions:
normal depth uses max reduction, far padding 1 and greater-than rejection;
Reverse Z uses min, padding 0 and less-than rejection. See
[GrassHiZCS.hlsl](https://github.com/InTheBottle/Bottled-Shaders/blob/68356b57f624eacf98033632500724c0d0cf66a1/features/Grass%20Optimizations/Shaders/GrassOptimizations/GrassHiZCS.hlsl#L24)
and
[GrassCullingCS.hlsl](https://github.com/InTheBottle/Bottled-Shaders/blob/68356b57f624eacf98033632500724c0d0cf66a1/features/Grass%20Optimizations/Shaders/GrassOptimizations/GrassCullingCS.hlsl#L210).

**Assessment for CSX:** the useful lesson is consistent depth semantics and
conservative coverage. A direct transplant of this Reverse Z feature is not
a demonstrated VR culling solution. A full port would need coordinated
resource, projection, render-state, shader, shadow, effects, VR and upscaler
compatibility work, plus measurement of its resource cost. No Bottled build,
runtime validation or performance comparison was performed here, and this
audit does not establish that CSX copied its implementation.

## 6. Implemented Hybrid pipeline

```text
Skyrim collects objects and assigns submission-local result pointers
    |
Skyrim uploads the affine OBB buffer
    |
Native outer downscale runs and publishes camera/depth-ready state
    |
Hybrid Prepare succeeds -> suppress only the inner native depth-copy draw
    |
Hybrid producer validates the native batch and prepared frame
    +-- success:
    |     build conservative depth array, one layer per eye
    |     build max-reduction mip levels
    |     test native OBBs in compute, writing every active result
    |     copy results to the existing native staging buffer
    |     retain submission metadata and exact bounds
    |
    +-- failure after suppression:
          replay native downscale under bypass
          cancel Hybrid preparation/history
          run native producer
          retain Advanced temporal recovery
    |
Native readback maps staging into the native CPU result array
    |
Hybrid validates correspondence and temporal reuse before caller resets batch
    +-- valid: retain results
    +-- invalid with writable current array: make every active result visible
    +-- unreadable batch: report failure and retire history; cannot safely write
```

There is no additional GPU query or extra readback path. Delivery remains
deferred through native staging, but **the native READ map can block** if the
GPU is late. Do not call the entire mechanism guaranteed nonblocking.

The ordinary comparison does not deliberately run both producers. Native
downsampling/testing is retained as fallback, and the outer native routine
continues to run because its non-draw side effects are essential.

## 7. Source admission and depth hierarchy

The source is the effective depth SRV of kPOST_ZPREPASS_COPY, native depth
target index 7. Do not reconstruct a different source simply because its
name sounds equivalent.

Current host admission requires:

-   One texture layer and one sample; a Texture2D SRV at most-detailed mip 0.
-   Even full texture width interpreted as double-wide stereo.
-   R24_UNORM_X8_TYPELESS or R32_FLOAT SRV format.
-   Exactly two cached views and two camera adjustments.
-   Expected full-eye viewport values {0, 1, 1, 0} and depth range [0, 1].
-   Valid graphics/camera state, outside main/loading menus and save/load safe
    mode.
-   If dynamic resolution is unlocked, both dynamic-resolution ratios equal
    1. Unsupported packed subrect layouts fall back.

Physically resized targets can satisfy these rules. This is implementation
support, not qualification of every render-scale/upscaler/mask combination.

Terrain Blending's existing ShouldUseBlendedDepthSRV guard disables its
blended-depth alias while effective VR depth culling is enabled. A previous
suspicion that this alias necessarily invalidated Hybrid was withdrawn after
checking that guard. Do not carry it forward as an established defect.

### Layout and resource contract

| Item                             | Implemented value                                          |
| -------------------------------- | ---------------------------------------------------------- |
| Eyes                             | 2, separate Texture2DArray layers                          |
| Pyramid format                   | R32_FLOAT                                                  |
| Default source reduction         | 4 by 4; policy accepts powers of two 1, 2, 4, 8            |
| Source dimension limit           | 16384                                                      |
| Pyramid dimension limit          | 4096                                                       |
| Native object limit              | 4096                                                       |
| Base dimensions                  | ceil(eye dimension / reduction), then power-of-two padding |
| Mip termination                  | Both dimensions at most 2; a 1 by 1 base is valid          |
| Build/reduce thread groups       | 8 by 8 by 1; dispatch Z = 2                                |
| Bounds thread group              | 64 threads, one native result index per thread             |
| Build/reduce/test constant sizes | 48 / 16 / 224 bytes                                        |
| Host pixel guard                 | 2 source pixels                                            |
| Policy minimum/default guard     | 1 source pixel                                             |
| Default depth bias               | 8 / 16777216, approximately 4.768e-7                       |

Both array layers have the same allocated dimensions. The layout takes the
largest required width/height across the eye rectangles and pads each axis.
The current host supplies equal left/right halves.

BuildDepthCS covers **all** samples in each source region. Invalid/nonfinite
depth, out-of-range depth, zero-depth VR mask values, incomplete edge regions
and padding become far depth 1. ReduceDepthCS takes the maximum over complete
2 by 2 coverage and handles one-dimensional tails. Far values reduce culling
efficiency rather than introducing false occluders.

For power-of-two dimensions, mip count is
max(1, bit_width(max(width, height)) - 1). The otherwise-unused final 1 by 1
dispatch was removed because a terminal level no larger than 2 by 2 already
covers every possible query with at most four cells.

The host uses one DEFAULT 224-byte constant buffer with zero-padded updates.
Build/reduce/test agree on layouts. Shader bindings use b0 for constants,
t0 for the pass input, t1 for the all-mips hierarchy in the bounds pass, and
u0 for the pass output. The native result UAV is a structured buffer, not
a typed R32_UINT texture/buffer.

## 8. Bounding-box visibility test

Each native OBB is a 64-byte affine matrix: four row-major float4 rows.

For each eye:

1. Subtract that eye's camera adjustment from the OBB translation entries
   before multiplying corners. This preserves small extents at large world
   coordinates; the shader uses precise arithmetic where required.
2. Transform all eight signed unit-cube corners.
3. Project with the eye's actual view-projection matrix.
4. Reject the occlusion proof if the data are nonfinite/non-affine, w is too
   small, a corner crosses the eye/near/far limits, or the guarded projected
   rectangle leaves the eye viewport.
5. Add the host's 2-pixel guard and calculate the full covered rectangle.
6. Select the first mip where the rectangle spans at most two cells per
   axis. Read **every** overlapping cell, up to four.
7. Hide only if the nearest box depth exceeds the farthest covered depth
   plus bias.

Every active result is initialized to visible, so uncertain paths retain
visibility. An object is hidden only if **both eyes** prove occlusion. The
explicit branch after the first eye avoids evaluating the second when the
first already keeps the object; SM5 logical AND alone did not produce that
early return.

This is conservative for the admitted captured source/view, including its
coverage and padding rules. Temporal reuse and live source lifecycle are
additional conditions, not consequences of this spatial proof.

CPU ViewData.viewProjMat is transposed **once** for HLSL row-major matrix
times column-vector multiplication. Native cached GPU matrices already have
that transpose. Do not transpose those a second time.

## 9. Verified native integration contract

The detailed tracked record is
[vr-hybrid-culling-native-contract.md](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/docs/development/vr-hybrid-culling-native-contract.md).

### Evidence scope

The ABI investigation used bounded, read-only process inspection of
**Skyrim VR 1.4.15.0**, not execution of the replacement culler.

```text
SkyrimVR.exe SHA-256:
6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971

Historical capture:
2026-09-25; PID 7284; process start 22:46:08 local
Module base: 0x7ff6e9c60000
```

These process details are evidence provenance, not addresses/PIDs to reuse.
All offsets below are **RVAs**. Existing CSX detours were already present;
their destinations are not pristine executable signatures. The investigation
used Capstone and D3D shader disassembly and did not write process memory.

### Native entry points and data

| Native item                    | RVA / contract        |
| ------------------------------ | --------------------- |
| Culler singleton               | 0x36F1870             |
| Culler vtable / constructor    | 0x1908500 / 0x1355770 |
| Object registration            | 0x13560A0             |
| Bounds upload                  | 0x13562D0             |
| Producer entry / callsite      | 0x13561F0 / 0x1323601 |
| Readback entry / callsite      | 0x1356270 / 0x132208B |
| Post-readback caller reset     | 0x1322090             |
| Outer downscale / callsite     | 0x1322D80 / 0x13235CF |
| Inner depth-copy draw callsite | 0x1322EC2             |
| Imagespace dispatcher          | 0x12D23D0             |
| Depth-ready byte               | 0x1ED4180             |
| GPU-to-staging copy helper     | 0xDC0F30              |
| Map/copy/unmap helper          | 0xDC11B0              |
| GPU result reset helper        | 0xDC0F80              |

The pointer spelled RE::BSImagespaceShader\* at the producer hook is actually
the native BSOBBOcclusionTestingShader culler, extending the 0x90-byte
BSShader. The spelling is not a reliable layout declaration.

| Culler offset   | Meaning                                        |
| --------------- | ---------------------------------------------- |
| +0xB0           | Current count, at most 4096                    |
| +0xB8           | CPU array of 64-byte affine OBBs               |
| +0xC0           | CPU result selector, 0 or 1                    |
| +0xC9           | Bounds upload completed flag                   |
| +0xD0 and +0xD8 | Two CPU uint32 result arrays                   |
| +0xF8           | Native structured OBB-buffer wrapper           |
| +0x100          | Native result-buffer wrapper including staging |
| +0x108          | Native GPU zero-source-buffer wrapper          |

Registration assigns index i, writes transforms[i], clears the selected
CPU result and returns its address. That correspondence lasts for this
submission only. Upload copies count \* 64 bytes and sets +0xC9 before the
producer; the producer itself does not upload the bounds.

After readback, the caller changes selector, clears counts and clears the
upload flag. CSX must inspect/correct the consumed batch **before returning
to that caller**.

The native buffer wrapper is 0x30 bytes:

| Offset | Field                     |
| ------ | ------------------------- |
| +0x00  | ID3D11Buffer GPU resource |
| +0x08  | SRV                       |
| +0x10  | UAV                       |
| +0x18  | Staging buffer            |
| +0x28  | Element capacity          |

OBBs use structured stride 64, results stride 4, capacity 4096. Structured
view formats are DXGI_FORMAT_UNKNOWN. Hybrid checks strides, capacities,
byte widths, view ranges/dimensions/formats, backing-resource identity,
bind flags, staging usage/CPU access, and upload completion.

It writes all active results, unbinds its UAV and copies into native
staging. Native readback maps D3D11_MAP_READ with flags zero, copies
count \* 4 bytes, unmaps, and resets GPU results from the zero source.
Engine resources remain engine-owned.

### Hook boundaries and fallback ordering

The outer downscale snapshots camera data and publishes depth-ready state.
**Never skip the entire outer routine.** Suppression applies only to the
inner imagespace operation, effect 100 / ISCopyDepthBufferTargetSize,
source depth target 7, destination argument -1, source-is-depth true.
The native downscaled depth target is index 14, kMAIN_DOWNSAMPLE.

If preparation suppresses the inner draw but producer validation later
fails, or the method changes, native downscale must be replayed under
bypass **before** native OBB production. Otherwise the fallback can consume
old depth.

VRHybridCullingLifecycle::RunProducer centralizes this ordering. It retires
suppression exactly once, returns after successful replacement, and orders
replay, cancellation, native production and Advanced pose capture on
fallback. Its exception tests prevent a fallback draw after failed replay.

Hooks install only on SKSE's VR 1.4.15 runtime. Installation checks expected
call opcodes, not a full executable fingerprint, and chains the current
call target so existing frame-annotation hooks survive. Optional downscale
hook failure leaves the native producer and Advanced recovery available.
No corresponding native hooks install on SE or AE.

### Stereo camera cache

The checked lookup is RVA 0xDD1AD0, with effective signature:

```cpp
void* FindCameraData(BSGraphics::State*, NiCamera*, bool useJitter);
```

It searches without creating an entry and can return null. Use the world
root camera and useJitter=false. The convenience getter at 0xDCF970
dereferences a missing result and is unsuitable for safe probing.

The VR cache entry is 0x70 bytes; ViewData is 0x250 bytes. Do not iterate it
using an incompatible cross-runtime CommonLib CameraStateData stride.

| Cache / ViewData field                | Offset              |
| ------------------------------------- | ------------------- |
| Cache views array                     | +0x08               |
| Cache camera-adjustment array         | +0x20               |
| Cache current / previous positions    | +0x38 / +0x50       |
| Cache jitter selection / camera count | +0x68 / +0x6C       |
| BSTArray data pointer / size          | Array +0x00 / +0x10 |
| ViewData view matrix                  | +0x30               |
| ViewData projection / view-projection | +0x70 / +0xB0       |
| ViewData unjittered projection        | +0x1B0              |
| ViewData viewport / depth range       | +0x230 / +0x240     |

The per-eye adjustment array matched the native culler's inputs. A renderer
shadow-state position was the midpoint, so arbitrary shadow-state positions
are not interchangeable with eye adjustments. Native raster projection
packs clip X into double-wide stereo; Hybrid tests each eye independently
before any such packing.

The implementation also checks the live world-camera transform, but
freshness of cached eye data at readback still needs live confirmation.

## 10. Temporal policy: Advanced versus Hybrid

### Existing Advanced behavior

Advanced retains native GPU visibility and applies CPU recovery when the
producer/consumer view coherence window is exceeded. Its shared policy uses
conservative OBB-derived spheres, including shear-aware corner extents, and
motion envelopes accounting for translation and rotation chord distance.

Recovery prioritizes candidates intersecting the current frustum directly,
then expanded motion-envelope candidates. A fixed-capacity selection keeps
up to **64** promotions, favoring direct intersection and angular coverage.
This bounds additional promoted draws, not necessarily the entire scan cost.
It is a pragmatic recovery policy, not a proof that every newly exposed
object becomes visible.

Legacy uses the native behavior without that Advanced recovery policy.

### Hybrid submission and history checks

Hybrid retains the submitted bounds and metadata, then accepts a readback
only with:

-   Matching nonzero culler, CPU transform pointer and CPU result pointer.
-   Matching object count, selector and rendering epoch.
-   Exactly one frame of age, using unsigned frame arithmetic.
-   Exact submitted transform bytes; maximum snapshot is 4096 \* 64 bytes.
-   Matching effective depth SRV identity, dimensions and eye rectangles.
-   Coherent world-camera transform and both eye poses.
-   Exact matching finite unjittered projection matrices.
-   Finite positive, unchanged world-camera scale.

Mode and effective-enable changes invalidate the culling epoch. Policy
publication also uses an odd/even epoch around mode changes.

The shared small-motion limits are at most **0.05 degrees** rotation and
**0.1 world unit** translation. Eye bases must be orthonormal; reflected
bases with determinant -1 are valid, so a determinant +1-only test would
incorrectly reject legitimate native views.

If history is invalid and the current native result array is structurally
available, Hybrid promotes **every active result** to visible. It does not
apply the 64-object Advanced quota. If the batch cannot be safely read, it
records unreadable_batch and retires history; it cannot promise to write
all-visible results through an invalid pointer.

Advanced recovery remains available when Hybrid falls back to native
production. A handled Hybrid readback does not need a second redundant
Advanced pass.

### Could Hybrid benefit from more of Advanced's temporal approach?

Yes, but the useful direction is a more selective, demonstrably conservative
validity/recovery policy, not blindly stacking the existing promotion quota
onto Hybrid.

Current full-batch invalidation is simple and cautious, but frequent head
motion may cause many all-visible batches and erase the GPU savings.
A future per-object motion/coverage analysis could retain more trustworthy
results, provided it accounts for stereo disocclusion, occluder motion,
coverage holes and the exact producer/consumer interval.

Such selective reuse is **not implemented**. Wider pose thresholds alone
would trade safety for an unproven performance gain. Persistent multi-frame
hysteresis would require a real stable object identity: native array indices
cannot supply it.

The present small-motion thresholds and pixel guard are heuristics. They
do not prove safety for every accepted sub-threshold movement, nor for
independently moving occluders. These are explicit acceptance gaps.

## 11. Graphics ownership and failure behavior

The host acquires Util::RendererOwnership and swaps to isolated D3D11.1
context state for compute work. Scope-exit restores the original state and
unbinds compute resources. New resources use RAII and Util::SetResourceName.
Resource creation, constants, dimensions and dispatch limits are checked.

Pipeline failure is latched to avoid repeated compilation/validation work;
shader-cache clearing requests recreation on the render thread. Immutable
test constants are validated during preparation rather than fully
revalidated every dispatch. A dispatch still validates the current native
batch and object count.

The host uses the repository's DX::ThrowIfFailed path. An earlier
winrt::check_hresult choice introduced an unsupported
RoOriginateLanguageException linkage dependency; do not casually restore it.

These are implemented protections. They do not replace live proof that
hook chaining, state restoration, fallback replay and resource lifetime
work correctly in the running game.

## 12. UI, serialization and DevBench

### User-facing selector

The VR Depth Culling UI exposes Advanced, Legacy and Hybrid Hi-Z at Info
logging and in Developer Mode. Changing logging level does not select a
different policy.

| Display name      | Machine method | Saved DepthCullingMethod |
| ----------------- | -------------- | ------------------------ |
| Advanced, default | balanced       | 0                        |
| Legacy            | legacy         | 2                        |
| Hybrid Hi-Z       | hybrid         | 3                        |

Retired numeric value 1 does not select Hybrid. A present invalid method
normalizes to Advanced; a missing method can migrate from the old
DepthCullingLegacyMode boolean. Saving keeps that compatibility boolean
synchronized. Performance-tuning snapshot/restore includes the method.

Exterior/interior enable switches and minimum object sizes remain
independent of the method. Defaults are enabled for both locations with
minimum extent 10; extents are bounded to [0,1000]. Legacy shared extents
and old configurations follow the existing migration rules.

Method changes pass through the central setter and emit an Info-level log.
DevBench setters execute on the main thread, require VR for the relevant
settings, mark settings dirty and return persisted=false. Normal settings
save performs persistence.

### DevBench actions

Tool: **communityshaders.menu**. The following are example request bodies,
not commands already executed in this handoff:

```json
{ "action": "status" }
```

```json
{ "action": "set_depth_culling_method", "method": "hybrid" }
```

```json
{ "action": "set_depth_culling_method", "method": "balanced" }
```

```json
{ "action": "set_depth_culling_method", "method": "legacy" }
```

```json
{
    "action": "set_depth_culling_settings",
    "depthCulling": {
        "exteriorEnabled": true,
        "interiorEnabled": true,
        "exteriorMinExtent": 10,
        "interiorMinExtent": 10
    }
}
```

```json
{ "action": "set_depth_culling_telemetry_enabled", "enabled": true }
```

```json
{ "action": "reset_depth_culling_telemetry" }
```

The compatibility action set_depth_culling_legacy_mode selects Legacy for
enabled=true and Advanced for enabled=false. It does not select Hybrid.

The settings object supports a nonempty partial update; unknown fields or
invalid values reject the update. Where supported, include expectedBuildId
with the exact 64-character ID of the intended loaded DLL. It fails closed
on a producer mismatch. Do not use the historical build's ID for a newly
compiled artifact.

Always inspect status.depthCullingTemporal, especially installed,
hybridInstalled, cullingEnabled, policy, telemetryEnabled and cullingEpoch.
A selected method is not proof that Hybrid actually ran.

### Hybrid telemetry contract

Under status.depthCullingTemporal.hybrid:

-   state
-   effectiveBackend: disabled, native, pending or hybrid
-   fallbackReason and historyRejectionReason
-   submittedBatches, acceptedBatches, invalidatedBatches, fallbackBatches
-   unreadableBatches, promotedObjects, lastObjectCount
-   cpuTimings.prepare, cpuTimings.dispatch, cpuTimings.readback, each with
    samples, totalNanoseconds and maximumNanoseconds

CPU timings include attempted/failed stages and can include startup pipeline
work. They are not GPU duration measurements. Warm up and reset at deliberate
capture boundaries.

GPU profiler labels are VRHybridCulling::Visibility and
VRHybridCulling::BuildHierarchy. The outer visibility scope includes
hierarchy work; do not blindly sum nested inclusive scopes. GPU profiling
uses the usual profiler controls independently of the telemetry switch.

Hybrid shares Advanced's writer-admission/reset gate. Disabling telemetry
rejects new measurements; already admitted writers may finish. Reset
clears both methods in one transaction, or reports busy without clearing
either. Measurement code is compiled out when DevBench is disabled.
Operational backend/failure diagnostics remain available with runtime
measurement disabled. Reset does not change culling policy or history.

Backend publication is attributed to the producer epoch, preventing an old
readback from masquerading as a new-method result after a mode round trip.
Inactive/old-epoch last-count data are filtered.

Status reads are thread-safe but not a globally atomic multi-counter
snapshot. Exact accounting should use a quiescent sampling boundary.

Useful reasons include hooks_unavailable, pipeline_unavailable,
unsupported_frame, resource_setup_failed, not_prepared,
empty_or_invalid_batch, preparation_expired, native_buffers_invalid,
renderer_unavailable and replacement_not_prepared. History reasons include
unreadable_batch, batch_mismatch, bounds_changed, frame_unavailable,
depth_changed and view_changed. Specific preparation failures survive
fallback replay instead of being replaced with a generic reason.

## 13. Adversarial review fixes already incorporated

| Finding                                                       | Final implementation                                                  |
| ------------------------------------------------------------- | --------------------------------------------------------------------- |
| SM5 logical AND evaluated both eyes                           | Explicit branch returns before second-eye projection when possible    |
| Unused final 1 by 1 pyramid dispatch                          | Stop once both dimensions are at most 2                               |
| Hybrid ignored shared telemetry admission/reset               | Shared RAII writer gate and combined reset; conditional compilation   |
| Specific fallback reason lost during replay                   | Preserve operational reason separately                                |
| Old readback could report a new mode's backend                | Attribute backend to producer epoch                                   |
| Unreadable readback not counted                               | Explicit unreadable metric/reason                                     |
| Failed preflight appeared as GPU pass work                    | Start GPU scope after admission; separate CPU attempted-stage timings |
| Immutable constants revalidated during dispatch               | Validate once during preparation; recheck current batch/count         |
| Latched pipeline failure still repeated expensive preparation | Avoid repeated frame capture until recreation is requested            |
| Stale inactive diagnostics invited per-frame clearing         | Epoch/active filtering without unnecessary per-frame writes           |

The initial implementation also addressed large-world arithmetic,
sheared-box coverage, asymmetric stereo matrices, reflected native eye
bases, null camera-cache lookup and the HRESULT linkage issue.

Reusable policy, history and lifecycle helpers allow focused tests to
exercise production routing. The implementation reuses native buffers,
existing temporal constants, telemetry gating, renderer ownership and
resource naming. No wholesale unrelated cleanup is part of the feature.

Static review did not establish additional concrete native ABI defects.
That is not equivalent to runtime acceptance. The unresolved issues below
are evidence gaps and design risks, not a claim that all game paths work.

## 14. Validation record and exact artifact provenance

### What has and has not been validated

| Surface                                                    | Evidence/status                                                 |
| ---------------------------------------------------------- | --------------------------------------------------------------- |
| Native ABI                                                 | Read-only inspection of the identified VR executable            |
| Pre-rebase universal Release DLL                           | Linked successfully; preserved clean-source artifact verified   |
| Focused CPU/controller and production-shader WARP tests    | 8/8 historical pass; preserved test log verified                |
| Production and developer FXC compilation                   | Historical documented pass; raw worktree artifacts absent today |
| DXBC stereo early return                                   | Historical documented inspection; raw artifact absent today     |
| Non-DevBench host compilation                              | Two syntax-only passes, not a complete disabled DLL link        |
| Scoped formatting/whitespace                               | Historical documented checks                                    |
| Current f17b833b5 tip compilation/tests                    | **Not run**, per rebase instruction                             |
| Live replacement hooks and replay                          | Not established                                                 |
| Live D3D state restoration and fresh eye cache at readback | Not established                                                 |
| Stereo fidelity / moving-occluder correctness              | Not established                                                 |
| Neutral-or-better performance                              | Not established                                                 |
| Full unrelated controller/shader suite                     | Not run for this focused change                                 |

### Preserved final pre-rebase build

```text
Source commit:
be8891ad0486909effb78cc4c47fbe8fcfa3b35f

Source state:
clean

Build ID:
c5395e9160496b9593adede7f69be2c6332f597eff37273604b1bdf9de0e2294

CommunityShaders.dll SHA-256:
42b3d9b0d12b6eb65ef50a0760f19476c2a4c50619be4e78092d27084e314118

DLL size:
29,359,616 bytes

Focused test-log SHA-256:
f9bb8fa7eecac2fad2483f6385c443f91c58c53891611a2899e02fd103327214
```

The final receipt, adjacent manifest, DLL hash/size and test-log hash were
read/rechecked during this handoff. This verifies the preserved artifact's
identity; it does not rerun its tests or establish runtime behavior.

Historical producer configuration:

-   Universal Release, SE/AE/VR enabled.
-   DevBench enabled; Tracy disabled; MultiThreadedDLL.
-   CMake 4.4.1.
-   MSVC 19.51.36252.0.
-   Visual Studio 18 2026 x64; Windows SDK 10.0.28000.0.
-   x64-windows-static-md triplet.

These are provenance, not a promise of byte-identical output from a
different machine/toolchain.

### Focused tests

The preserved log records 8/8 passed, zero failures, total 2.26 seconds:

1. VRDepthCullingTelemetryPolicy
2. VRDepthCullingSettingsUI
3. VRDepthCullingSettings
4. MenuDepthCullingSettingsPolicy
5. VRDepthCullingTemporalPolicy
6. VRHybridCullingPolicy
7. VRHybridCullingHistory
8. VRHybridCullingShader

Coverage includes dimensions and dispatch limits; exhaustive mip rectangle
coverage for power-of-two sizes 1 through 256; odd edges, VR masks and NaNs;
every hierarchy level; both-eye visibility; viewport/near/far rejection;
asymmetric projection; small bounds at large coordinates; sheared bounds;
one-dimensional mip tails; mixed stereo waves; and 1 by 1 / 2 by 1 bases.

History/routing cases include pointer/count/selector/epoch/age mismatches,
frame wrap, pose/projection changes, replay ordering, exactly-once
suppression retirement and exceptions. Settings tests include Info/Debug
visibility, persistence/migration and invalid inputs. Telemetry tests cover
combined disable/reset, busy resets and concurrent writer admission.

WARP runs production shaders on a software D3D device. It does not validate
the game's hook installation, camera-cache lifecycle, native readback timing,
graphics-state restoration or headset performance.

### Additional historical checks

All three compute shaders were reported to pass:

```text
fxc /T cs_5_0 /E main /WX /Ges /O3
fxc /T cs_5_0 /E main /WX /Zi /Gfa /Gpp
```

DXBC inspection was reported to confirm return before second-eye projection.
Scoped repository formatting passed; unrelated legacy CMake formatting
changes were removed rather than added to the feature.

VRHybridCulling.cpp and VRDepthCullingTemporal.cpp passed MSVC syntax checks
using the Release includes/definitions with /Y- /Zs and
/UDEVBENCH_BRIDGE_ENABLED, plus a forced header asserting the macro was
undefined. Both exited zero without diagnostics. This is **syntax coverage,
not a full DevBench-disabled DLL link**.

Recorded syntax-check source hashes:

```text
VRHybridCulling.cpp:
373bb26c504dd0c5fcaf2631f60108ef735d00cabcdeb8e5002228cf97d9df2a

VRDepthCullingTemporal.cpp:
df13d08c7e5d8df183bab66642d90518796a4db98ea9b8e9166dc644bb50f913
```

### Earlier evidence is not the final candidate

The initial September 25 dirty build predates later UI/review amendments:

```text
Base source: df9f377a53d237518c1b671b7be1085c9a65b69d
Build ID: 0b66202ce6e2b57d1581b75b1791f35ef84d9df34b0289a51ce0a8180fbbef8c
Dirty digest: de156ac99597acbdf06b3e2c14c4cba808dbe169f143da9c5a662ff44807edd4
DLL SHA-256: 5e5b83f891b7c47eee45e39acc38862b323a1266453b5a106b0dfefa69b33f93
DLL size: 29,343,744 bytes
```

The intermediate review DLL likewise predates the final inactive-status
cleanup:

```text
Source: 7d04ad7268fd59e21f283a7b363d3c2b05f8ba7d, dirty
Build ID: ab87bc6bacc331b366a7974ad37d3d44608c43be585b9558971bb626df637cad
Dirty digest: d8efa96e3d53557e93bf7b44479bab88647451c4bbfba70dbd6a14fe1f5dad2c
```

Do not use either as proof that the final rebased source was built.

## 15. Local evidence availability and portability

The following paths are relative to the originating machine's root checkout,
C:/src/skyrim-community-shaders:

| Local path                                                        | Availability at handoff                                          |
| ----------------------------------------------------------------- | ---------------------------------------------------------------- |
| build/hiz/evidence/review-final-validation/receipt.json           | Present; inspected                                               |
| build/hiz/evidence/review-final-validation/CSX.BuildManifest.json | Present; inspected                                               |
| build/hiz/evidence/review-final-validation/CommunityShaders.dll   | Present; independently hashed                                    |
| build/hiz/review-tests.log                                        | Present; inspected and hashed                                    |
| build/hiz/review-no-devbench/receipt.json                         | Present; inspected                                               |
| build/hiz/evidence/dirty-validation/                              | Earlier evidence location recorded by tracked documentation      |
| build/hiz/evidence/review-initial-validation/                     | Intermediate evidence location recorded by tracked documentation |
| Worktree build/hiz-review-validation/                             | Absent at the recorded path today                                |
| Worktree build/native-contract/                                   | Absent at the recorded path today                                |

The old worktree was removed and later recreated for the rebase. Its ignored
shader/native raw evidence did not survive there. Do not repeat the tracked
documents' older “preserved under worktree” wording as a current existence
claim. The tracked native-contract document still preserves decisive
findings and evidence fingerprints.

Build outputs, raw captures and these evidence directories are ignored and
do **not** travel with a clone. This handoff embeds the critical source and
artifact identities, but does not embed binaries or recreate missing raw
dumps. Transfer present evidence separately if a future task needs it.

## 16. Portable checkout and future validation recipe

These are **future commands**, not checks executed when producing this
handoff. Choose paths appropriate to the destination machine and respect
its current instructions. Do not automatically rebase again.

For a fresh checkout, ordinary Git can clone before the repository wrapper
exists:

```powershell
git clone --branch codex/vr-hybrid-hiz-culling git@github.com:ParticleTroned/skyrim-community-shaders.git csx-hybrid-hiz
Set-Location csx-hybrid-hiz
pwsh ./tools/git.ps1 status --short
pwsh ./tools/git.ps1 rev-parse HEAD
pwsh ./tools/git.ps1 log -1 --format=fuller
```

Confirm whether HEAD is the pinned f17b833b56b5078527bb9a5b413e033455ad855d.
If origin has moved, inspect the differences and record the new source
identity instead of silently attributing this handoff's evidence to it.
Use an isolated worktree if the machine already has unrelated work.

Once compilation is requested, initialize dependencies and use the
maintained wrappers. Read current setup/prerequisite instructions first:

```powershell
pwsh ./tools/git.ps1 submodule update --init --recursive
pwsh ./tools/setup-dev.ps1
pwsh ./tools/cmake.ps1 --preset ALL -B build/hiz-portable -DDEVBENCH_BRIDGE=ON -DBUILD_CONTROLLER_TESTS=ON -DBUILD_SHADER_TESTS=OFF -DAUTO_PLUGIN_DEPLOYMENT=OFF -DZIP_TO_DIST=OFF -DAIO_ZIP_TO_DIST=OFF
pwsh ./tools/cmake.ps1 --build build/hiz-portable --config Release --target CommunityShaders vr_depth_culling_telemetry_policy_test vr_depth_culling_settings_ui_test vr_depth_culling_settings_test menu_depth_culling_settings_policy_test vr_depth_culling_temporal_policy_test vr_hybrid_culling_policy_test vr_hybrid_culling_history_test vr_hybrid_culling_shader_test --parallel 4
ctest --test-dir build/hiz-portable -C Release -R '^(VRDepthCullingTelemetryPolicy|VRDepthCullingSettingsUI|VRDepthCullingSettings|MenuDepthCullingSettingsPolicy|VRDepthCullingTemporalPolicy|VRHybridCullingPolicy|VRHybridCullingHistory|VRHybridCullingShader)$' --output-on-failure --no-tests=error
python ./tools/build_provenance.py verify --manifest build/hiz-portable/Release/CSX.BuildManifest.json --artifact build/hiz-portable/Release/CommunityShaders.dll
```

The focused WARP test belongs to the controller group; disabling the broad
BUILD_SHADER_TESTS group does not omit that focused test. Use CTest from the
selected CMake installation.

Preset ALL selects Visual Studio 2026; ALL-VS2022 is an available alternative
where appropriate. The repository minimum is CMake 4.2; maintained
validation rejects 4.3.0/4.3.1 and recommends a fixed newer version.
Preserve the actual toolchain in the new manifest.

Do not copy the old machine's VCPKG_INSTALLED_DIR, disabled
VCPKG_MANIFEST_INSTALL, or FETCHCONTENT_SOURCE_DIR_HDE64 overrides. They
referred to an existing root build/ALL dependency cache. A fresh machine
should resolve its dependencies normally.

Use tools/validate-local.ps1 for the broader DLL/controller/shader/preset
validation record when requested. It is broader than the focused recipe.
Do not claim that a syntax check replaces a full build or that all unrelated
suites passed. Do not delete existing build outputs or shader caches to
simplify setup.

## 17. Runtime acceptance and performance experiment

### First establish that the intended code runs

1. Verify the newly built source commit, Build ID, manifest, DLL SHA-256
   and size.
2. Resolve the exact enabled AIO mod on the destination machine. Compare
   its physical CommunityShaders.dll and adjacent manifest with the AIO
   receipt; inspect enabled loose providers, Overwrite and unmanaged Data.
3. Check DevBench's exact producer identity, hook-installation flags,
   effective culling enablement and Hybrid effectiveBackend.
4. Exercise method switches, disabled culling, unsupported-source fallback,
   cache reload and normal scene transitions. Check that native replay and
   epoch attribution behave as designed.
5. Confirm the consumer sees genuinely current world/eye data and the
   native result array still corresponds to the retained submission.

Discover and follow the destination machine's installed automation skills,
particularly devbench-control, mo2-control, shader-cache-control,
profiler-control and capture-interaction-control. Skill installation paths,
transport/session IDs and mod roots are machine-specific.

### Controlled comparisons

Compare Advanced and Hybrid on the same source base, hardware, scene,
settings, resolution and camera-motion sequence. Legacy and culling-off can
serve as additional diagnostic references if the test scope includes them.

Warm shader/pipeline creation before steady-state captures. Keep telemetry
and profiling settings identical across alternatives. Repeat baselines to
establish variability before classifying a small difference as neutral.

Cover:

-   Stationary dense exterior and interior views.
-   Slow and fast head rotation, translation, and combined motion.
-   Thin occluders, doorway edges, near-camera geometry and distant detail.
-   Moving occluders and objects whose bounds change.
-   Cell transitions, load/recovery paths and rapid method/enable changes.
-   Native resolution and admitted scaled/upscaled configurations.
-   Both eye images over time, not just a desktop mirror or one still image.

Record CPU and GPU frame time, relevant stage costs, P95/P99, spikes,
memory/resource behavior, submitted/accepted/invalidated/fallback/unreadable
batches, promoted objects and fallback/rejection reasons. Distinguish CPU
dispatch timing from GPU execution. Preserve denominators and warmup windows.

Use the actual headset refresh budget: 1000 / refreshHz milliseconds
(11.11 ms at 90 Hz). Report deltas relative to that budget and baseline
variability. Compare whole-frame behavior as well as culling-pass cost;
saving compute while drawing many more objects can still regress the frame.

### Acceptance

Correctness, run health/completion and performance are separate outcomes.

-   No accepted result should introduce missing geometry in either eye.
-   Unreadable/fallback behavior must be observed and explained.
-   Neutral-or-better performance requires comparable measurements.
-   An average gain cannot conceal repeatable scene/motion regressions.
-   Incorrectly culled geometry is not a legitimate performance improvement.

Keep Advanced as the default and fallback until the required evidence
exists. Public CSX releases use the complete CSX_AIO package; internal split
artifacts are not the public distribution contract.

If future work actually changes/evaluates VR render-scale behavior, follow
the repository's current render-scale qualification and durable-ledger
rules. This handoff and the focused WARP tests do not satisfy those runtime
protocols.

## 18. Open risks and sensible next improvements

| Risk / uncertainty                       | Why it matters                                                   | Next useful evidence                                 |
| ---------------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------- |
| Rebased tip not built                    | Merge integration can fail despite earlier passes                | Build and focused tests when requested               |
| Live fallback/state restoration untested | Failure could corrupt subsequent rendering or use stale depth    | In-game controlled admission/fallback exercise       |
| Eye cache freshness at readback unproven | Matching stale snapshots can overstate validity                  | Correlated producer/consumer camera evidence         |
| Small-motion thresholds are heuristic    | Translation/rotation can expose previously hidden surfaces       | Stereo motion sequences near occluder edges          |
| Moving occluders                         | Unchanged camera/target bounds do not prove scene-depth validity | Controlled moving-occluder scenes                    |
| Whole-batch invalidation                 | Can make many objects visible every moving frame                 | Rejection/promoted counts correlated with frame time |
| Full source reduction and mip work       | Bandwidth, padding and dispatch overhead may outweigh savings    | GPU hierarchy cost and resolution sweep              |
| Bounds snapshot/compare and preparation  | CPU overhead can matter under a VR budget                        | CPU stage timing and whole-frame comparison          |
| Native staging READ can block            | Deferred delivery is not a guarantee against stalls              | Native readback timing correlated with GPU load      |
| Unreadable native batch                  | Cannot guarantee all-visible writes through unavailable state    | Runtime reason counts and lifecycle diagnosis        |

Potential changes after measurement, **not current implementation**:

-   More selective per-object temporal validity with a defensible coverage and
    disocclusion argument.
-   Stable-identity temporal history if a trustworthy engine identity/lifetime
    contract is established.
-   Clear/empty-tile early-outs and tighter hierarchy work where profiling
    justifies them.
-   Constant-upload/dispatch cost reduction without weakening state isolation.
-   Reconsidered bias or coverage policies supported by failure cases and tests.

Do not begin by loosening motion thresholds, using averaged depth, sampling
only rectangle corners at base resolution, merging the two eyes, adding
synchronous GPU waits, or dropping fallback ordering. Each can undermine
the reason for this design.

A renderer-wide Reverse Z experiment remains possible as a separate
precision project, but should not displace measurement of the existing
hybrid without evidence that depth precision is the actual failure source.

## 19. Source map for continuation

All links below are pinned to the pushed feature tip so they remain useful
even if the branch advances.

| Responsibility                           | Source                                                                                                                                                                                                     |
| ---------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Design, review and historical validation | [vr-hybrid-culling.md](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/docs/development/vr-hybrid-culling.md)                                     |
| Native ABI and evidence fingerprints     | [vr-hybrid-culling-native-contract.md](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/docs/development/vr-hybrid-culling-native-contract.md)     |
| Advanced temporal policy                 | [vr-depth-culling-temporal-policy.md](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/docs/development/vr-depth-culling-temporal-policy.md)       |
| Telemetry semantics                      | [vr-depth-culling-recovery-telemetry.md](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/docs/development/vr-depth-culling-recovery-telemetry.md) |
| Hybrid host/resources/status             | [VRHybridCulling.cpp](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRHybridCulling.cpp)                                           |
| Layout/constants/admission policy        | [VRHybridCullingPolicy.h](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRHybridCullingPolicy.h)                                   |
| Batch and stereo history                 | [VRHybridCullingHistory.h](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRHybridCullingHistory.h)                                 |
| Replacement/fallback routing             | [VRHybridCullingLifecycle.h](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRHybridCullingLifecycle.h)                             |
| Hook installation and native recovery    | [VRDepthCullingTemporal.cpp](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRDepthCullingTemporal.cpp)                             |
| Shared temporal math                     | [VRDepthCullingTemporalPolicy.h](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRDepthCullingTemporalPolicy.h)                     |
| Shared telemetry gate                    | [VRDepthCullingTelemetryPolicy.h](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRDepthCullingTelemetryPolicy.h)                   |
| VR settings/UI/central setter            | [VR.cpp](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VR.cpp)                                                                     |
| Serialization and migration              | [VRDepthCullingSettings.h](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRDepthCullingSettings.h)                                 |
| DevBench method/settings parsing         | [MenuDepthCullingSettingsPolicy.h](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/MenuDepthCullingSettingsPolicy.h)                          |
| DevBench schema/actions/status           | [MenuDevBenchBridge.cpp](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/MenuDevBenchBridge.cpp)                                              |
| Performance-tuning snapshot/restore      | [PerformanceTuningRenderer.cpp](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Menu/PerformanceTuningRenderer.cpp)                           |
| Full-coverage source reduction           | [BuildDepthCS.hlsl](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/package/Shaders/VRHybridCulling/BuildDepthCS.hlsl)                            |
| Conservative mip reduction               | [ReduceDepthCS.hlsl](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/package/Shaders/VRHybridCulling/ReduceDepthCS.hlsl)                          |
| Stereo OBB test                          | [TestBoundsCS.hlsl](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/package/Shaders/VRHybridCulling/TestBoundsCS.hlsl)                            |

Focused test sources under tests:

```text
vr_depth_culling_telemetry_policy_test.cpp
vr_depth_culling_settings_ui_test.cpp
vr_depth_culling_settings_test.cpp
menu_depth_culling_settings_policy_test.cpp
vr_depth_culling_temporal_policy_test.cpp
vr_hybrid_culling_policy_test.cpp
vr_hybrid_culling_history_test.cpp
vr_hybrid_culling_shader_test.cpp
extract_vr_depth_culling_settings.cmake
```

## 20. Repository practices to retain on another machine

Read the checked-out AGENTS.md rather than relying solely on this summary.
Material requirements include:

-   Git through tools/git.ps1 on Windows; CMake through tools/cmake.ps1;
    scoped pre-commit through tools/pre-commit.ps1.
-   Preserve unrelated user changes, build products and shader caches.
-   Keep runtime-specific divergence localized; consider universal SE/AE/VR
    compilation even though these native hooks are VR-only.
-   Use existing graphics naming, ownership and RAII; expose new settings
    through DevBench with current description/schema.
-   Conventional commits need explicit Rationale and Implementation sections,
    accurate authorship and focused staged files.
-   Target main-VR for a future PR. Follow its title/number policy; do not
    rename an existing open PR's head just to retrofit a number.
-   Do not force-push or rewrite shared branches. An owned feature-branch
    rewrite requires authorization and appropriate lease protection.
-   Performance claims require comparable measured evidence; preserve exact
    source, Build ID, configuration and limitations.

This handoff was prepared through source/evidence inspection and document
creation only. No new compile, test, game launch, deployment or performance
measurement was performed for it.
