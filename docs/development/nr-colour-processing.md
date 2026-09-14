# Shared NR colour processing: internal experiment

## Baseline and revised assessment

This work starts at `face-of-gogh` commit
`1a5bda199f0bd8e710c720a77b8c6931b65e8708`, not the old
`f7f2679ef616167a499f00dbf563a20cf18133f5` review. Their histories diverge;
this is an incremental change to the newer tree, not a merge of the old one.

The newer branch already implements dynamic per-eye single-subrect NR,
optional multi-region character submission, matched cluster history,
raw-depth extraction/visibility fixes and late-LDR output safety. In
particular, `PrepareFinalLdrModelColor` already rejects nonfinite model
colour, clamps selected LDR routes and retains baseline alpha. Those changes
remain intact. Character masks alone should no longer be confused with the
new branch's actual rectangular dispatch machinery.

The common `Renderer::State::ApplyBatchLocked` validates logical eyes,
expands physical regions, prepares private resources, evaluates Feature 18,
and commits evaluated rectangles. Single, batched and sequential public
entry points reach this implementation. That is the colour integration point;
character classification, control masks, depth/MV mapping, cluster IDs and
runtime admission are not changed.

## Source-domain evidence and limits

| Route | Existing input/commit boundary | What is not established |
| --- | --- | --- |
| Upscaled centre | Normal DLSS centre enters the shared renderer; its output is later feathered/composited. | A floating-point resource is not proof of scene-linear or pre-exposed values. |
| Render Scale submit | Existing private floating-point NR resources are converted to the established presentation resource. | This storage conversion does not identify the transfer function or undo exposure. |
| Final LDR pre-UI | The completed pre-UI colour is used; existing final-LDR guards apply on composition. | The insertion-point name alone does not prove the numeric encoding is sRGB. |
| Single/multi-region character | The same colour resources are addressed by validated active rectangles and selected with the existing mask. | Changing a rectangle can change neural context; it is not a colour-space conversion. |

No new NR exposure parameter is invented. The normal SR path's auto-exposure
setting does not make its internally calculated exposure available to this
code. Engine exposure has not been recovered or validated here. Explicit
configuration is therefore labelled manual; `native` means an unknown
transfer/code-value metric, **not** linear luminance. No automatic HDR/LDR
classification is added from DXGI format alone.

The reported green/tone/shadow problem remains to be localized by runtime
comparisons. This commit provides colour reconstruction, a source-appearance
mitigation and a controlled transport test. It does not establish the runtime's
intended colour contract or certify that the reported problem is fixed.

## OptiScaler evidence reviewed on 14 September 2026

The strict last-day window checked was 13 September 07:25:30 UTC through
14 September 07:25:30 UTC.

- Upstream [731f3b7](https://github.com/optiscaler/OptiScaler/commit/731f3b79c762bc92971e5fe33dade87c6f83067b),
  13 September 20:49:41 UTC, makes the D3D11 HUD-less capture use non-sRGB
  interop formats to match its non-sRGB swapchain. It maps RGBA8/BGRA8 sRGB
  resource formats to their UNORM counterparts. This is an instructive
  storage/view consistency fix, **not an NR exposure fix** and not permission
  to strip sRGB from every CSX view.
- The checked `Dagherbou/OptiScaler_DLSSNR` `dlss-neural-rendering` branch had
  no commits in that window. Its v0.2.0-dlssnr release and same-frame exposure
  work are earlier September 3 changes, not new last-day discoveries.
- Upstream [PR 1158](https://github.com/optiscaler/OptiScaler/pull/1158) was open,
  not merged, when inspected. Its recorded update at September 13 03:00:02 UTC
  is outside this strict window. Its contributor branch nevertheless contains
  relevant [colour codec/precision work](https://github.com/wilsjo2/OptiScaler_DLSSNR/commit/4af597ebfe8cf73f9526377413150c4624796d16):
  full-float calculations, explicit colour transforms, residual transfer and
  golden-vector/GPU probes. Its author still reports unresolved visual
  artifacts. These observations motivate matched-reference reconstruction and
  tests here; no third-party source or runtime hooks were transplanted.

Streamline feature 1004 / uplift tags 70-72 and direct NGX feature 18 are
separate APIs. Public SR exposure fields, generic extents and the four-channel
uplift-mask name do not specify the NR colour, mask-channel or sparse-dispatch
ABI. The existing direct NGX invocation is retained.

## Operation

`ColorPipeline` is owned and locked by the shared NR renderer. Profiles are
read once from `Data/SKSE/Plugins/CommunityShaders/NRColor.ini`, then remain
immutable until the existing NR Runtime Reset or restart. The reset waits for
existing GPU ownership before releasing resources and reloads the profiles
before the next evaluation. No file polling, blocking GPU readback or
per-frame exposure lookup is introduced. Missing configuration means the old
raw path; malformed configuration rejects NR with a diagnostic until reset.

The file is deliberately **not installed/enabled automatically**. This is an
internal comparison surface, not new controls falsely advertised as part of
the existing in-game settings UI. Copy the adjacent `nr-colour.example.ini`
to that path and reset NR to enable both profiles. Profile state and manual
exposure limitations are included in the renderer's existing snapshot `detail`
field, available through current status/DevBench reporting. The existing
frame/slot/route/rectangle telemetry still identifies the evaluated resources.

Each active physical slot retains a private baseline with the same format and
coordinates as its input, plus a private reconstructed result. Colour data is
not sampled outside the evaluated rectangle. Preparation/reconstruction runs
for standard NR and character NR alike; character selection happens once in
the existing downstream composite. Every region is resolved privately before
any caller destination is copied within a batch. Existing sequential stereo
caller-level rollback remains in force. Read/write resource lifetimes,
D3D11/D3D12 fences, depth and motion-vector mappings are unchanged.

### Profiles and keys

Sections: `[UpscaledCentre]`, `[FinalLdrPreUI]`. Unspecified sections are raw.
Keys are case-sensitive; duplicate/unknown keys and sections are rejected.
There are no inline comments after values. File size is bounded to 16 KiB.

| Key | Values/default | Meaning |
| --- | --- | --- |
| Mode | `raw` (default), `managed`, `preserve_lighting` | Raw leaves the previous path intact. Managed reconstructs the model delta. Preserve additionally suppresses broad appearance drift. |
| SourceTransfer | `native` (default), `linear`, `srgb` | Manual declaration of the actual source values. Native makes no transfer/primaries assertion. The working brightness weights are Rec.709 coefficients; wider-gamut transforms are not implemented. |
| ModelCodec | `identity` (default), `srgb`, `reinhard_srgb` | Explicit experimental encoding before inference; non-identity requires SourceTransfer=linear. Never choose it just from the input texture format. |
| WhitePoint | 1; range 0.0001-10000 | Manual normalization for the Reinhard proxy. NOT measured game/SR exposure. |
| DetailStrength | 0.65; range 0-1 | Strength of the bounded high-frequency log-brightness residual in preserve mode. |
| AppearanceMix | 0; range 0-1 | Blend from the source-preserving detail result toward reconstructed NR appearance, including tone and chroma. |
| MaxDetailStops | 1; range 0-2 | Bound on the detail gain in stops. |
| Radius | 2; integer 1-4 | Edge-weighted low-frequency rejection radius in evaluated pixels. |
| RoundTrip | 0; 0 or 1 | Diagnostic described below; invalid with raw mode. |

The identity codec does not add a preparation dispatch. A baseline copy and
resolve are still necessary for managed/preserve operation. Raw requires no
new colour textures or GPU passes. New colour work does not run with NR off.
Private textures add memory per active slot, and the optional neighborhood
filter adds GPU cost. Existing profiler scopes report `DLSSNRColorPrepare`
and `DLSSNRColorResolve`; caller enqueue timings remain CPU timings. These
costs have not been benchmarked in Skyrim here.

### Matched-reference reconstruction

Let B be the baseline, P the actual finite-precision colour resource submitted
to NR, M the model output and D the configured inverse/working-domain mapping.
The candidate is `B_work + (D(M) - D(P))`, not simply `D(M)`. When M=P,
return the source directly. This prevents quantization of the proxy itself
from creating a colour delta. Computation uses float, not half/min16float.
Original alpha is retained, including exact zero-contribution endpoints.

`sRGB` uses an extended positive transfer without clipping HDR values to 1.
The Reinhard proxy scales linear RGB by `1/(WhitePoint + max(R,G,B))`, then
encodes it as sRGB. Its inverse is explicit, but finite precision and model
output outside its domain can make inversion unstable. Non-invertible peaks
at or above 0.9999, nonfinite values, unsupported negatives and destination
float overflows retain the source pixel. This is not a lossless tone mapper.
No arbitrary engine tone curve is inverted. Existing final-LDR clamping stays
at its original boundary.

Preserve mode applies an edge-weighted high-pass log-brightness residual as a
bounded scalar gain to baseline RGB, optionally mixed with the NR candidate.
Near-black amplification is suppressed. Uniform changes are not treated as
detail. This is an image-space mitigation, not a physical decomposition of
lights/materials and not a guarantee that all highlights/shadow edges are
unchanged. With native transfer it operates on code values; use a confirmed
source transfer for linear-light interpretation.

### Transport round-trip diagnostic

Set Mode=managed, ModelCodec to the codec being tested and RoundTrip=1.
Inference **still executes**. After the Feature 18 timestamp scope, D3D12
copies the exact prepared input rectangle over the private model output, then
the normal fence/resolve/commit path completes. Thus NGX attempts/successes
remain truthful, and no unsupported evaluation-outcome ABI is invented.

This is a transport/codec comparison, not a way to measure NR-off performance.
The extra D3D12 copy is outside the Feature 18 timer. Feature initialization
and valid guides are still required. Use equal baseline images and compare
against raw NR-off output at the same composition boundary. The route should
return the baseline (subject to its existing later presentation conversions).
If it does not, investigate views/copies/order before tuning neural appearance.
Turn RoundTrip back off and reset before visual or performance comparisons.

## Validation

Portable tests:

```sh
cmake -S tests/neural_color -B build/nr-colour-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/nr-colour-tests --config Release
ctest --test-dir build/nr-colour-tests -C Release --output-on-failure
```

The same standalone project adds a Windows WARP test on Windows. It compiles
the actual production HLSL and covers quantized R11/R16/FP32 prepared inputs,
identity residuals, ROI exteriors, source alpha, broad colour drift rejection,
zero-contribution settings and invalid model pixels. It does **not** exercise
NGX, the D3D12 bridge or Skyrim's stereo composition.

In the implementation environment, Release portable policy/reference tests
and supplementary source-wiring tests were run. A Windows SDK, D3D runtime,
NVIDIA GPU and Skyrim are unavailable, so neither the plugin build nor WARP
nor in-game validation was run. Do not interpret the portable results as a
GPU build/visual sign-off. Run the existing branch controller/shader suites
and normal Windows build before adopting this internal experiment.

For controlled game tests, compare NR off, raw NR, managed RoundTrip=1,
managed RoundTrip=0 and preserve mode. Hold scene, camera, resolution, NR
tuning and history warm-up constant. Repeat with character selection off/on,
both insertion points, main/submit, sequential/batched and staged/direct.
Also exercise both current single-region and multi-region paths, motion,
menus, retained frames, bright highlights beside skin and dark interiors.
All-one mask equivalence is meaningful only at matched evaluation extents;
a tighter ROI can change the neural candidate itself.

Remaining work needs runtime evidence: identify the model's actual colour
contract and any accessible engine exposure; measure added GPU/VRAM cost;
verify stereo/mask/foveation boundaries and determine whether tone drift or
transport is responsible for the reported lighting difference. Wider-gamut
transforms, automatic exposure recovery, a native colour-control GUI and
asynchronous GPU colour histograms are not claimed by this commit.
