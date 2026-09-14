# Shared Neural Rendering colour processing

Source baseline: `face-of-gogh` at
`1a5bda199f0bd8e710c720a77b8c6931b65e8708`.
This is a source-level experiment. Windows compilation, WARP shader execution,
NR runtime compatibility and in-game image quality still require validation.
It is not a confirmed repair for the reported colour/lighting discrepancy.
No NVIDIA binaries or runtime-admission changes accompany this work.

## Baseline audit and integration

The baseline already contains single-rectangle character compute, experimental
multi-ROI, per-region histories, a deterministic CSX selection mask, and scalar
raw-depth extraction. These remain intact. The existing final-LDR composite
rejects nonfinite model values, clamps the model to UNORM range before mask and
feather blending, and preserves destination alpha. Those guards remain intact.

All existing renderer entry points converge on `Renderer::State::ApplyBatchLocked`.
Logical eye requests expand into up to four physical evaluations (eight retained
slots across main/submit routes). The new colour work executes after expansion,
using the mapped physical region, before the existing logical output commit.
It is independent of character eligibility or mask coverage. Normal NR and
character NR therefore use the same processing; multi-ROI does not sample gaps
between the initialized rectangles.

The audited renderer takes matching colour/output formats and already maps
output rectangles to colour and guide rectangles. Its colour transfer was a
copy, not an exposure or gamma transform. Normal SR enables internal automatic
exposure; this does not provide an accessible engine exposure texture to NR.
The current API also does not carry a proven transfer function or pre-exposure
contract. Consequently this implementation does **not** infer colour domain from
DXGI format or from an insertion-point name.

| Route | Retained behaviour | Colour interpretation |
| --- | --- | --- |
| Main / Upscaled Centre | Process after the caller's DLSS centre; existing later composition remains | Unknown/native unless explicitly asserted |
| Submit / Upscaled Centre | Preserve existing private float NR conversion and final presentation path | Unknown/native unless explicitly asserted |
| Main / Final LDR pre-UI | Preserve late capture, menu separation and final destination guards | Post-processing placement is known; transfer function is not inferred |
| Submit / Final LDR pre-UI | Preserve late submit baseline and stereo commit | Same late profile, with frame/route provenance |

Colour-enabled processing requires equal colour/output dimensions, but guides
may be lower resolution. This is checked explicitly. Legacy Raw retains the
old validation behaviour. Encoded/proxy experiments require a supported float
resource format; the code does not change engine/OpenVR formats or claim
support for a new NVIDIA input format.

## Controls and deployment

A core **Neural Rendering Colour** feature appears in the Display category,
next to the existing Upscaling feature. These are general NR controls, not
character-only settings. Install the new `NeuralColor.ini` and colour shaders
along with the rebuilt plugin. The normal CMake feature/version/source discovery
includes the new folder and C++ modules; no generated files are checked in.

Persisted settings live under `Neural Rendering Colour`:

- `legacy_raw`: compatibility default. No new colour dispatches or allocations
  when diagnostics and bypass are off. Existing shared output gains SRV access
  so the optional processing can read it.
- `managed`: explicit preparation and matching output reconstruction, with
  nonfinite/invalid-inverse fallback to the baseline. Identity is the default
  transform; this is not automatic colour correction.
- `preserve_source`: managed reconstruction plus bounded local detail transfer.
  `detailStrength` is 0..2, `appearanceMix` 0..1, and `maximumDetailStops` 0..2.

`appearanceMix=0` suppresses broad neural appearance changes while allowing
bounded detail; `appearanceMix=1` accepts the reconstructed model appearance.
This includes tone/luminance changes, not only chroma. Zero detail and zero
appearance return the baseline at this stage. The detail filter removes a local
low-frequency log-luminance residual and applies the remaining bounded scalar
gain to baseline RGB. Its edge-aware samples stay within each physical ROI and
fade near the ROI edge. It is an artistic mitigation, not a physical guarantee
that every shadow/reflection remains correct. With an unknown domain, the
luminance statistic is only a weighted native-value proxy, not measured light.

### Transient domain and encoding experiments

Early-centre and late-pre-UI profiles are separate. They are never persisted.
Each allows `unknown`, `linear` or `srgb` as an explicit source assertion.
An assertion is reported as `manual_experiment`, not detected metadata.
There is no per-face/per-eye automatic re-exposure.

Transforms:

1. `identity`: preserve source numbers; multiplier must be 1.
2. `linear_to_srgb`: multiply by a manual diagnostic exposure and encode sRGB;
   reconstruction decodes and divides by the same multiplier.
3. `reversible_proxy`: multiply, divide RGB by `1 + max(RGB)`, then encode sRGB.
   Reconstruction decodes, divides by `1 - max(decoded RGB)`, then divides by
   the same exposure multiplier.

Nonidentity transforms require the explicit `linear` assertion. The multiplier
is 1/256..256, not measured SR/engine exposure. The exposure-scaled nonnegative
source must have maximum channel <=32 for these experiments. Proxy inverse
requires a denominator >=1/64. Invalid inputs/inverses retain original pixels
on reconstruction; invalid prepared inputs are finite black rather than NaNs.
This bounded support is not lossless HDR coverage. Do not apply a scene-HDR
proxy to already tone-mapped LDR merely because a texture is floating point.

Reconstruction transfers the model residual relative to the **actual quantized
prepared input**:

`result = baseline + inverse(neural) - inverse(prepared)`

Thus a copied/identity model result returns the original baseline rather than
accumulating a lossy packed-float forward/inverse round trip. Source alpha is
preserved where the source format carries it. Genuine nonfinite baselines are
sanitized, not passed into further arithmetic.

## Ownership, history and fallback

A per-route transaction key (presentation frame, immutable source world frame,
generation, insertion point) latches configuration for both eyes, even when they
arrive through separate public calls. Each physical slot snapshots its colour
baseline in a local ROI-sized texture. Prepared inputs and raw NR outputs remain
private. Every physical reconstruction is queued before any caller output is
committed. Existing caller-level stereo/menu fallback remains authoritative.

Colour-enabled sequential stereo requests use the atomic batch path. Direct
output requests still receive reconstructed colour through private staging;
the UI/status states this explicitly rather than silently bypassing correction.
Colour input interpretation changes increment per-insertion history epochs.
Post-composite strength changes do not reset neural history. Bypass invalidates
history so inference resumes with a reset. Existing cluster-specific history
logic and provider rectangles are preserved.

ROI colour allocations round capacity up to 64 pixels and are retained/reused,
not allocated per pixel or per frame. The colour pipeline saves/restores touched
compute bindings and predication. GPU ordering uses existing shared-fence waits.
New resources participate in reset and unsafe-detach paths. No new runtime
`Flush` or synchronous readback is introduced by colour diagnostics.

## Transport bypass and measurements

Transient `transportBypass` retains preparation, D3D11-to-D3D12 synchronization,
a D3D12 subrect copy in place of inference, reconstruction and output commit.
It still initializes the existing backend, so it requires a compatible local
runtime; it is not a runtime-free interop test. No NGX evaluation success or NR
GPU inference sample is invented for this copy-only submission. The dedicated
copy scope supplies the submission metadata required by the existing interop.

Transient `diagnostics` uses a maximum of 4096 spatial samples per physical ROI,
three readback buffers and nonblocking event/Map polling. Results retain the
sample's source frame, slot, rectangle and configuration revision. Unavailable
samples are dropped rather than blocking the render thread. Diagnostics are
optional, not runtime-admission gates. Old samples can remain visible in status;
always compare their revision/frame, not their position in the array.

The sixteen `values` entries are four float4 rows:

| Indices | Meaning |
| --- | --- |
| 0..3 | Baseline mean RGB; valid baseline/result comparison count |
| 4..7 | Reconstructed mean RGB; mean absolute RGB error |
| 8..11 | Maximum absolute RGB error; result near-black count; result at/above-one count; raw NR outside-zero-to-one count |
| 12..15 | Nonfinite baseline, raw NR and result sample counts; total sample count |

Above-one is not necessarily clipping for HDR. These are source-value summaries,
not calibrated display luminance. Preparation/reconstruction microseconds are
**CPU enqueue times**. Existing profiler scopes mark the added passes; NR's
existing D3D12 timestamps continue to measure real inference only. Retained
colour texture bytes exclude provider backing storage and optional readback
objects.

## DevBench

The complementary `communityshaders.nr_color` tool avoids changing the existing
render-scale API. It supports atomic `configure`, `status`, `reset_experiments`.
Use `expectedRevision` for compare-and-set updates. The handler only changes a
mutex-protected registry, never engine/GPU state from its worker thread.

Baseline-preserving appearance experiment (works for standard and character NR):

```json
{"action":"configure","settings":{"mode":"preserve_source","detailStrength":1.0,"appearanceMix":0.0,"maximumDetailStops":1.0}}
```

Transport test with identity processing and bounded measurements:

```json
{"action":"configure","settings":{"mode":"managed"},"experiments":{"transportBypass":true,"diagnostics":true}}
```

Explicit early linear-source proxy experiment; choose only after auditing the
actual source colour domain:

```json
{"action":"configure","settings":{"mode":"managed"},"experiments":{"transportBypass":false,"upscaled_center":{"domain":"linear","transform":"reversible_proxy","exposureMultiplier":1.0}}}
```

Reset diagnostic overrides before comparing insertion points:

```json
{"action":"reset_experiments"}
```

Configuration requests validate names, types, finite ranges and profile
compatibility before mutation. Persisted settings include only the mode/detail/
appearance controls. Use the game's existing save-settings mechanism to save
those controls. A successful configuration response does not establish that a
subsequent shader/runtime evaluation succeeded; inspect processed/failure and
source attribution in status.

## Validation

Standalone tests, independent of the Windows plugin build:

```powershell
cmake -S tests/neural_color -B build/nr-color-tests
cmake --build build/nr-color-tests --config Release
ctest --test-dir build/nr-color-tests -C Release --output-on-failure
```

The portable policy executable covers enums, finite/range checks, encoding/proxy
round trips, residual identity, near-black/invalid cases and input-history
identity. Python source-contract tests guard integration and ownership order;
they do not prove C++/HLSL compilation. The Windows-only WARP test compiles all
three compute shaders and exercises prepare/reconstruct, identity, alpha,
nonzero ROI offsets, sentinels and appearance endpoints with mock model output.
It does not execute NVIDIA NR or D3D12 interop.

Source-level checks and portable tests were run during authoring. The Windows
plugin build, WARP execution and real NR tests are explicitly deferred to the
user. Run the existing controller/depth/mask tests as well, then test matched
NR-off, transport, raw, managed and preserve-source frames with character mode
off/on, single/multi-ROI, early/late insertion and main/submit routes. Warm up
histories consistently. Include bright highlights, dark interiors, moving
characters, ROI edges, pauses and UI transitions. Differences in smaller-ROI
NR candidates can arise from changed model context, not only colour conversion.

## Evidence boundaries

Streamline 2.14.1 publishes feature/tag identifiers, not a complete NR colour
contract. SR exposure fields must not be mapped to invented NR keys. The
September 13 OptiScaler HUD-less capture change (`731f3b7`) aligns its UNORM
interop resource format; it is not evidence that arbitrary CSX HDR values must
be sRGB-decoded. No blanket view-format or gamma change is made here.

Future automatic exposure capture must prove the engine producer, units,
pre-exposure relationship and same-source-frame lifetime before replacing the
explicit Unknown/manual status. This experiment supplies real transport and
comparison controls without claiming those missing facts have been established.
