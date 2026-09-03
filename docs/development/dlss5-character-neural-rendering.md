# DLSS 5 character Neural Rendering experiment

This document defines the capability and safety boundary for the experimental
`face-of-gogh` character-selective Neural Rendering path. It supplements the
[general Feature 18 experiment notes](dlss-neural-rendering-experiments.md);
the same internal-only runtime and distribution restrictions apply.

The feature attempts to apply Neural Rendering to NPC faces, exposed skin, and
optionally hair. It does not establish that Neural Rendering inference itself
becomes cheaper. The current provider call still evaluates the normal Feature
18 image extent.

## Architecture

The feature is split along the runtime contracts that must remain independently
testable:

| Concern                       | Owner                                                   | Contract                                                                                                                         |
| ----------------------------- | ------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| Capability and initialization | `NeuralRendering::Runtime` and `Renderer`               | Admit 310.8 Feature 18 runtimes independently of logging/developer mode, report identity, and fail closed on invalid resources.  |
| Character classification      | the existing subsurface/lighting setup hook             | Classify actor-owned face, RGB-tint skin, and hair materials without treating generic skinned geometry as skin.                  |
| Semantic mask                 | `CharacterRendering` and `DLSS5CharacterMaskCS.hlsl`    | Resolve synchronized category and depth provenance into a dedicated per-eye `R8_UNORM` CSX selection texture.                    |
| Per-eye resources             | `CharacterRendering` and `Renderer` feature slots       | Keep mask, provider resources, history, and statistics separate for each eye and insertion route.                                |
| Feature 18 evaluation         | `Renderer` and `Runtime`                                | Bind color, depth, motion vectors, and output through the known-working automatic-mask invocation in one stereo transaction.     |
| Compute ROI policy            | `CharacterRendering`                                    | Build measured, stabilized per-eye character rectangles, but do not claim or dispatch sparse inference without an evidenced API. |
| Composite and output          | Gogh route integration and `FoveatedCenterBlendCS.hlsl` | Select untouched DLSS outside the mask and Neural Rendering inside it before the existing feathered center composite.            |
| UI and settings               | Upscaling settings UI                                   | Centralize category, strength, eligibility, mask-calibration, and debug controls.                                                |
| Diagnostics and profiling     | DevBench bridge and CSX GPU profiler                    | Report per-eye coverage/regions/evaluation dimensions and separate capture, mask, evaluation, and composite costs.               |

The architectural boundary is intentional: the unpublished provider
`ControlMask` contract is not used. Deterministic CSX output compositing does
not imply compute ROI support or change the dimensions evaluated by Feature 18.

## Evidence and confidence

NVIDIA describes DLSS 5 as a final rendering stage conditioned on the rendered
frame, engine motion vectors, temporal state, and artistic controls. Its public
launch article now explicitly describes a per-pixel uplift control mask and
engine-level masking for selected props or asset groups. This is public product
evidence for visual masking, but it still does not publish the mask resource
format, value contract, binding symbols, or a compute-ROI interface:

-   [NVIDIA DLSS 5 research overview](https://research.nvidia.com/labs/adlr/DLSS5/)
-   [NVIDIA DLSS 5 feature overview](https://www.nvidia.com/en-eu/geforce/news/dlss-5-3d-guided-neural-rendering/)
-   [NVIDIA DLSS technology overview](https://www.nvidia.com/en-us/geforce/technologies/dlss/)

At the time of this experiment, the public NVIDIA NGX header names feature ID
18 only as `NVSDK_NGX_Feature_Reserved18`. It does not define a typed Neural
Rendering creation structure, evaluation structure, semantic-mask structure,
control-mask format, ROI list, tile list, or compute-region contract:

-   [Public `nvsdk_ngx_defs.h`](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_defs.h)

The public Streamline repository likewise has no Neural Rendering feature
header equivalent to `sl_dlss.h` and no public NR/ROI programming guide. Its
general programming guide says that a Streamline feature's data and API belong
in a corresponding feature header:

-   [NVIDIA Streamline repository](https://github.com/NVIDIA-RTX/Streamline)
-   [Streamline programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md)

Consequently, this branch treats every `DLSSNR.*` parameter below as a private,
version-specific, reverse-engineered binding contract. A successful call is
runtime evidence for one pinned binary, not an NVIDIA-supported integration
guarantee.

### Checked-in NVIDIA SDK inventory

The repository evidence below comes from `extern/Streamline-DX12` at commit
`e8aaa6eaac968711fb62473d4ae8256dde20919b` (`v2.12.0`). Searches covered its
public `include` tree and bundled NGX `external/ngx-sdk/include` tree.

| Header                                                                                          | Exact symbols found                                                                                                                                                                                               | What they establish                                                                                                                                                   |
| ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `extern/Streamline-DX12/include/sl_version.h`                                                   | `SL_VERSION_MAJOR=2`, `SL_VERSION_MINOR=12`, `SL_VERSION_PATCH=0`                                                                                                                                                 | This checkout is Streamline 2.12.0, not an unreleased Neural Rendering SDK.                                                                                           |
| `extern/Streamline-DX12/external/ngx-sdk/include/nvsdk_ngx_defs.h`                              | `NVSDK_NGX_Feature_Reserved18 = 18`                                                                                                                                                                               | The numeric feature exists, but the checked-in NGX API still gives it no typed Neural Rendering name or contract.                                                     |
| `extern/Streamline-DX12/include/sl_core_types.h`                                                | `kBufferTypeReserved70=70`, `kBufferTypeReserved71=71`, `kBufferTypeReserved72=72`                                                                                                                                | The tags observed in the private plugin remain reserved in source. None is publicly named uplift input, output, or control mask.                                      |
| `extern/Streamline-DX12/include/sl_core_types.h`, `extern/Streamline-DX12/include/sl_helpers.h` | `kFeatureDLSS_RR=1001`; helper name `"dlss_d"`                                                                                                                                                                    | Streamline's published DLSS-D surface is DLSS Ray Reconstruction, not Feature 18 Neural Rendering.                                                                    |
| `extern/Streamline-DX12/include/sl_dlss_d.h`                                                    | `DLSSDPreset`, `DLSSDNormalRoughnessMode`, `DLSSDOptions`, `DLSSDOptimalSettings`, `DLSSDState`, `slDLSSDGetOptimalSettings`, `slDLSSDGetState`, `slDLSSDSetOptions`                                              | These typed calls import against `kFeatureDLSS_RR`; they cannot be substituted for an NR options, mask, or ROI API.                                                   |
| `extern/Streamline-DX12/external/ngx-sdk/include/nvsdk_ngx_helpers.h`                           | `NVSDK_NGX_D3D12_DLSS_Eval_Params`, `NGX_D3D12_CREATE_DLSS_EXT`, `NGX_D3D12_EVALUATE_DLSS_EXT`, `InRenderSubrectDimensions`, `InColorSubrectBase`, `InDepthSubrectBase`, `InMVSubrectBase`, `InOutputSubrectBase` | Normal DLSS exposes one render subrect and per-resource bases. That is not evidence of Feature 18 character ROI.                                                      |
| `extern/Streamline-DX12/external/ngx-sdk/include/nvsdk_ngx_helpers_dlssd.h`                     | `NVSDK_NGX_D3D12_DLSSD_Eval_Params`, `NGX_D3D12_CREATE_DLSSD_EXT`, `NGX_D3D12_EVALUATE_DLSSD_EXT`, `pInResponsivityMask`, and the corresponding `In*SubrectBase` members                                          | DLSS-D exposes a rich Ray Reconstruction input contract. Those symbols do not describe Neural Rendering or a sparse character-region dispatch.                        |
| `extern/Streamline-DX12/include/sl_consts.h`, `extern/Streamline-DX12/include/sl_core_types.h`  | `Extent`, `ResourceTag`                                                                                                                                                                                           | Streamline can tag a rectangular extent for a resource generally. It does not provide an NR ROI array, tile list, or promise that inference cost follows that extent. |

No checked-in `sl_dlss_nr.h`, `DLSSNR` structure, Neural Rendering semantic-mask
type, engine-mask type, structure/tone-control structure, ROI list, or sparse
tile-dispatch interface was found. The only NR names used by this branch come
from the pinned private binaries described below.

The normal-DLSS parameter macros behind the typed subrect members are
`NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X`,
`NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y`,
`NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X`,
`NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y`,
`NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X`,
`NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y`,
`NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X`,
`NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y`,
`NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width`, and
`NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height`. They are declared
for DLSS and reused by DLSS-D helpers; none is declared for Reserved18.

## Visual masking is not compute ROI

The implementation keeps two concepts deliberately separate.

### A. Visual masking

The engine renders a per-pixel character-selection image. Feature 18 receives
the normal color, depth, motion-vector, and output resources with
`DLSSNR.UseAutoMask` enabled and no `DLSSNR.ControlMask` resource. CSX then
uses its own per-eye mask to blend the successful Feature 18 output over the
untouched normal-DLSS baseline before Gogh's existing foveated feather.
The mask's actual `0..1` value is the composite weight, so face, skin, and hair
strengths are applied exactly once.

Disabling visual isolation is the working full-center Feature 18 control. It
also skips character classification, category/depth capture, mask generation,
and mask compositing.

This guarantees isolation to the mask texture that CSX actually produced.
Synchronized authored and current depth reject later depth-writing occluders;
the remaining final-visibility limits are documented below.

### B. Compute ROI

Compute ROI would mean that the neural model evaluates only selected character
rectangles or tiles. No such public contract was found. The private binary does
consume one active subrect for each resource, derives active network dimensions
from the output subrect and scaling ratio, and distinguishes active dimensions
from allocated capacity. That makes one compact rectangular evaluation a
credible future private-ABI experiment, but it does not establish a performance
gain.

No ROI collection, rectangle array, tile list, or sparse character dispatch was
found. Multiple character regions would have to be collapsed into one bounding
rectangle or evaluated in multiple calls; neither choice is proven cheaper.

The branch therefore reports multi/sparse character compute ROI as unsupported
and does not enable the private single-subrect experiment. There is no mutable
ROI mode: the UI reports masked full-center evaluation as the only implemented
path. A functional `Enabled` state must not be added until a bounded
implementation is timed and validated against the pinned ABI.

The per-eye actor rectangles in this experiment bound and stabilize visual-mask
eligibility. They may reduce mask coverage and provide measurements for a
future ROI implementation, but they do not reduce Feature 18 evaluation
dimensions. The implementation does not dispatch Feature 18 once per NPC.

## Reverse-engineered Feature 18 surface

The inspected local 310.8 runtime has SHA-256
`8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206`.
Its external NGX entry points used by CSX are:

-   `NVSDK_NGX_D3D12_Init_Ext`
-   `NVSDK_NGX_D3D12_AllocateParameters`
-   `NVSDK_NGX_D3D12_CreateFeature`
-   `NVSDK_NGX_D3D12_EvaluateFeature`
-   `NVSDK_NGX_D3D12_ReleaseFeature`
-   `NVSDK_NGX_D3D12_DestroyParameters`
-   `NVSDK_NGX_D3D12_Shutdown1`

CSX passes numeric feature ID 18. The parser reads `DLSSNR.ControlMask` and its
subrect fields near binary virtual addresses `0x18001A3ED` through
`0x18001A4CA`. A present control mask forces `UseAutoMask` to zero near
`0x18001AA4B`. Evaluation obtains the resource dimensions and normalizes its
subrect near `0x180022333` through `0x1800224A9`. These addresses are anchors
for this exact binary, not symbols or a stable ABI.

The branch currently sets these creation and dimension parameters:

-   `DLSSNR.Width`, `DLSSNR.Height`
-   `DLSSNR.InputWidth`, `DLSSNR.InputHeight`
-   `DLSSNR.OutputWidth`, `DLSSNR.OutputHeight`
-   `DLSSNR.Output.Width`, `DLSSNR.Output.Height`
-   `DLSSNR.Scale`, `DLSSNR.ScalingRatio`, `DLSSNR.Upscaling`
-   `DLSSNR.Hint.Render.Preset`

Only `DLSSNR.Width`, `DLSSNR.Height`, `DLSSNR.ScalingRatio`, and the render
preset were found in the inspected 310.8 binary. The additional input/output,
scale, and upscaling aliases above are inherited experimental compatibility
writes; they are not part of the evidenced provider contract.

It sets these resources and resource rectangles for evaluation:

-   `DLSSNR.Color` and `DLSSNR.ColorSubrectBaseX`,
    `DLSSNR.ColorSubrectBaseY`, `DLSSNR.ColorSubrectWidth`,
    `DLSSNR.ColorSubrectHeight`
-   `DLSSNR.Depth` and the corresponding `DepthSubrect*` keys
-   `DLSSNR.MVec` and the corresponding `MVecSubrect*` keys
-   `DLSSNR.Output` and the corresponding `OutputSubrect*` keys

The runtime wrapper can bind `DLSSNR.ControlMask` and the corresponding
`ControlMaskSubrect*` keys for isolated ABI research, but the
character-rendering integration intentionally leaves them absent and sets
`DLSSNR.UseAutoMask=1`. Binary parser evidence alone did not validate the
mask's format or semantics well enough for the reliable path.

It also sets `DLSSNR.MVecScaleX`, `DLSSNR.MVecScaleY`,
`DLSSNR.DepthInverted`, `DLSSNR.Enabled`, `DLSSNR.Reset`,
`DLSSNR.Intensity`, `DLSSNR.LocalToneStrength`,
`DLSSNR.LocalStructureStrength`, `DLSSNR.SkinStructureStrength`,
`DLSSNR.UseAutoMask`, `DLSSNR.Style`, and `DLSSNR.UICorrection`.

Strings for UI, UI alpha, backbuffer, and distortion resources also occur in
the binary. They are not part of the character-mask path and their presence
alone does not define a safe binding contract.

No parser field or typed interface was found for an ROI array, tile list, or
per-object rectangle collection. The singular subrect path is documented above
as a private experimental candidate and must be revisited when NVIDIA publishes
an SDK.

### Unpublished Streamline plugin finding

The local `sl.dlss_nr.dll` with SHA-256
`9F6672E5E0170DC118A3188D21BDA187E1FC1AA3502895B21AB846D23165C11D`
embeds plugin ID `1004` and the name `sl.dlss_nr`. Reverse engineering this
specific binary found that it resolves `slDLSSNRSetOptions` and maps Streamline
buffer tags 70, 71, and 72 to uplift input, uplift output, and control mask,
respectively. Its required resource-tag set is 70, 71, 1, and 0; tag 72
(`ControlMask`) is optional.

This is unpublished, version-specific reverse-engineering evidence. The
checked-in Streamline 2.12 headers expose those identifiers only as
`Reserved70`, `Reserved71`, and `Reserved72`; they provide no public typed
Neural Rendering option or resource contract. Inspection also found no ROI
list or sparse-compute interface. These tags therefore corroborate a visual
control-mask path, but do not authorize treating the private plugin ABI as a
supported API and do not establish compute ROI.

## Existing CS/CSX mask and stencil survey

Repository searches covered `src`, `package/Shaders`, and the feature shader
trees. CSX has several things called masks, but none is already a persistent,
per-eye NPC face/skin/hair identity surface:

| Concern inspected     | Existing mechanism and evidence                                                                                                                                                                                                                                                                                                                                    | Reuse decision                                                                                                             |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------- |
| Deferred masks        | `src/Deferred.cpp` binds `MASKS` and `MASKS2` as lighting MRTs. `package/Shaders/Lighting.hlsl` writes SSS amount/human class/ambient data to `MASKS` and inverse vertex AO plus category provenance to `MASKS2`.                                                                                                                                                  | Reuse the existing draw-time MRT path. Store category provenance in VR `MASKS2.y`; do not add a second geometry traversal. |
| Wetness               | `package/Shaders/Lighting.hlsl` includes either `features/Wetterness/Shaders/Wetterness/WetternessLighting.hlsli` or `features/Wetness Effects/Shaders/WetnessEffects/WetnessEffects.hlsli` and derives rain, shore, puddle, and character-wetness weights in the material lighting pass. Its precipitation-occlusion input describes shelter, not actor identity. | No reusable character mask exists. Keep wetness data independent.                                                          |
| Contact shadows       | `features/Light Limit Fix/Shaders/LightLimitFix/ClusterCullingCS.hlsl` builds light-index lists and `features/Light Limit Fix/Shaders/LightLimitFix/LightLimitFix.hlsli::ContactShadows` traces the scene depth texture.                                                                                                                                           | Light eligibility and screen-depth rays cannot identify face, skin, or hair pixels.                                        |
| Subsurface scattering | `package/Shaders/Lighting.hlsl` writes SSS strength and human class to `MASKS.xy`; `src/Features/SubsurfaceScattering.cpp::DrawSSS` consumes that G-buffer. `SubsurfaceScattering::Hooks::BSLightingShader_SetupGeometry` already sees the lighting property, material, geometry, and actor.                                                                       | Reuse the setup hook for classification. Do not reinterpret the coarser SSS mask as face/skin/hair identity.               |
| Complex grass         | `package/Shaders/RunGrass.hlsl` identifies complex grass from authored texture data and shades it in the grass pass. It writes category zero to the extended `MASKS2` tuple.                                                                                                                                                                                       | This is a material heuristic, not an actor mask; it is explicitly kept out of character selection.                         |
| Skylighting           | `src/Features/Skylighting.cpp` and `src/Features/Skylighting.h` own a precipitation-style depth occlusion texture plus 3D probe and accumulation textures (`texOcclusion`, `texProbeArray`, `texAccumFramesArray`).                                                                                                                                                | The light-probe occlusion domain is not a stereo screen-space material-ID domain.                                          |
| Particle lights       | `features/Light Limit Fix/Shaders/LightLimitFix/Common.hlsli` defines `LightFlags::Particle`, and `features/Light Limit Fix/Shaders/LightLimitFix/ClusterCullingCS.hlsl` consumes it while building light/contact-shadow lists.                                                                                                                                    | These are light records, not geometry ownership or screen-space character coverage.                                        |
| Foveation             | `package/Shaders/Common/FoveatedMask.hlsli` computes an analytic per-eye center distance and feather weight; `features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl` consumes that weight.                                                                                                                                                               | Reuse its final center-composite behavior, but not as semantic selection. It contains no scene material information.       |
| Motion vectors        | `kMOTION_VECTOR` is a dedicated deferred target (`package/Shaders/Lighting.hlsl` target 1 and `package/Shaders/DeferredCompositeCS.hlsl` UAV 2).                                                                                                                                                                                                                   | Preserve it as Feature 18 temporal input. It does not encode material or actor identity.                                   |
| Material IDs          | CommonLib exposes `BSShaderProperty::flags`, `GetBaseMaterial()`, and `BSShaderMaterial::GetFeature()` at draw setup. No general screen-space material-ID render target was found; `MASKS.y` is only SSS human class.                                                                                                                                              | Classify at draw setup and pass only the three bounded categories through the existing permutation/MRT path.               |
| Stencil               | `src/Features/TerrainBlending.cpp` and `src/Features/Upscaling.cpp` inspect, replace, and restore engine depth-stencil state; Upscaling also creates states with full `0xFF` stencil read/write masks. No reserved character bit or lifetime contract was found.                                                                                                   | Do not claim an engine-owned stencil bit. A color attachment with explicit lifetime is safer and inspectable.              |

This survey is why the implementation extends the established deferred
lighting setup and G-buffer route. The dedicated provider-facing `R8_UNORM`
texture is a later compute resolve, not a second character draw pass.

## Control-mask contract

The dedicated provider-facing texture is `R8_UNORM` at the Feature 18 output
extent. This branch experimentally interprets its normalized values as:

| Value | Experimental meaning                   |
| ----- | -------------------------------------- |
| `0.0` | no character-specific Neural Rendering |
| `1.0` | full configured character uplift       |

Intermediate values carry category strength and feathered coverage. Neither
the `R8_UNORM` format nor the `0..1` meaning is specified by a public NVIDIA
header. Both are hypotheses to validate against the pinned runtime. They must
not be presented as an official NVIDIA contract.

Character category IDs are authored while the existing deferred geometry is
drawn. In VR, `MASKS2` remains a two-byte attachment by changing from
`R16_UNORM` to `R8G8_UNORM`:

| Channel | Meaning                                                          |
| ------- | ---------------------------------------------------------------- |
| `x`     | inverse vertex AO at 8-bit UNORM precision                       |
| `y`     | exact category code: none=`0`, face=`85`, skin=`170`, hair=`255` |

The category lane accepts only those four R8 codes. An ordinary normalized
blend therefore decodes as category zero unless it lands on an exact code.
The pixel shader continues to output opacity in its fourth component, so the
inherited MRT source-alpha blend factor remains available without introducing
stored destination alpha that the former single-channel target did not have.

After deferred terrain replay and before blended decals, CSX freezes the active
packed stereo extent of `MASKS2` together with main depth from the same render
point. Capturing before blended decals prevents ordinary alpha blending from
diluting exact semantic IDs. The mask resolve later samples that synchronized
depth and the current per-eye depth guide. A category pixel is rejected only
when a materially closer later surface has taken ownership; a farther or
cleared later sample does not erase valid authored geometry. Category
enablement and strength are then resolved into the dedicated per-eye
`R8_UNORM` texture. The path avoids a second character draw traversal and
avoids copying unused native-allocation padding, but its storage and bandwidth
costs are not free.
The frozen depth texture preserves the source texture format and SRV
interpretation. The implemented depth-view allowlist is
`DXGI_FORMAT_R24_UNORM_X8_TYPELESS`,
`DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS`, `DXGI_FORMAT_R32_FLOAT`, and
`DXGI_FORMAT_R16_UNORM`; any other view format fails closed.

Core frozen/current visibility rejection is controlled independently from edge
feathering. The default keeps rejection enabled. The diagnostic
`Authored (Ignore Visibility Depth)` mode bypasses only that core comparison;
it preserves authored categories, strengths, eligibility regions, and any
separately enabled edge-feather policy. Per-eye telemetry reports the frozen
stereo base/crop and current local depth dimensions used by the shader.

Keeping category identity independent of its current strength prevents
optional feathering from growing enabled skin into a disabled face or hair
material. Optional edge feathering compares both authored visibility and
linearized current view depth with bounded relative tolerances, so it does not
intentionally spread control values across a depth discontinuity. It only
crosses from an enabled category into category-zero background, never between
different character categories.
Alpha-tested hair inherits the material's surviving pixel coverage rather than
its uncut mesh silhouette. Alpha-blended character materials are excluded,
including lighting properties whose scalar alpha implies blending without an
explicit alpha-blend property, because normalized alpha blending cannot
preserve the exact category endpoints. Hair materials that combine alpha
testing and blending are therefore intentionally absent from this experimental
path; supporting them safely requires a separately authored attachment or pass.

## Material classification

Classification uses Skyrim lighting shader/material information, with this
precedence:

1. Face
2. Exposed-skin candidate
3. Hair

The primary property flags are `kFace`, `kFaceGenRGBTint`, and `kHairTint`.
The corresponding lighting material features are `kFaceGen`,
`kFaceGenRGBTint`, and `kHairTint` where that material abstraction provides a
meaningful feature value. `kFace`/`kFaceGen` is a face. RGB-tint geometry below
the actor's `BSFaceGenNiNode` is also a face; actor-owned RGB-tint geometry
outside that face node is the exposed-skin candidate. This spatial distinction
is necessary because one RGB-tint technique can occur in both roles. Debug-mask
inspection remains required for modded materials that replace either signal.

The checked-in CommonLib headers provide the exact engine abstractions used by
that policy:

| Header                                                                                                     | Relevant declaration                                                                                                                                   |
| ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `extern/CommonLibSSE-NG/include/RE/B/BSShaderMaterial.h`                                                   | `BSShaderMaterial::Feature::{kFaceGen=4, kFaceGenRGBTint=5, kHairTint=6}` and virtual `GetFeature()`                                                   |
| `extern/CommonLibSSE-NG/include/RE/B/BSShaderProperty.h`                                                   | `BSShaderProperty::EShaderPropertyFlag::{kSkinned=BIT64<<1, kFace=BIT64<<10, kHairTint=BIT64<<18, kFaceGenRGBTint=BIT64<<21}` plus `GetBaseMaterial()` |
| `extern/CommonLibSSE-NG/include/RE/B/BSLightingShaderProperty.h`                                           | `BSLightingShaderProperty` derives from `BSShaderProperty`; no separate character-category enum exists there.                                          |
| `extern/CommonLibSSE-NG/include/RE/B/BSLightingShaderMaterialFacegen.h`                                    | `BSLightingShaderMaterialFacegen::FEATURE = Feature::kFaceGen`                                                                                         |
| `extern/CommonLibSSE-NG/include/RE/B/BSLightingShaderMaterialFacegenTint.h`                                | `BSLightingShaderMaterialFacegenTint::FEATURE = Feature::kFaceGenRGBTint`                                                                              |
| `extern/CommonLibSSE-NG/include/RE/B/BSLightingShaderMaterialHairTint.h`                                   | `BSLightingShaderMaterialHairTint::FEATURE = Feature::kHairTint`                                                                                       |
| `extern/CommonLibSSE-NG/include/RE/C/Character.h`, `extern/CommonLibSSE-NG/include/RE/B/BSFaceGenNiNode.h` | `Character::GetFaceNodeSkinned()` returns the `BSFaceGenNiNode` used for RGB-tint face-versus-body ancestry.                                           |

At runtime, `BSRenderPass::shaderProperty` supplies the property/material,
`BSRenderPass::geometry->GetUserData()->As<Actor>()` establishes actor
ownership, and geometry ancestry supplies the RGB-tint distinction. These are
draw-time facts; they are not reconstructed from a generic skinned bit after
the frame has been rendered.

Generic `kSkinned` geometry is deliberately not classified as skin. Doing so
would include armour, clothes, weapons, and animated accessories. A geometry
observation is accepted only when its render user data resolves to an actor,
and the player actor is excluded from the initial experiment. The semantic
descriptor is emitted only after that observation is accepted, so an empty
observation set also proves that no selected category ID was authored. Thus
player hands, arms, weapons, and body are not intentionally admitted as NPC
skin.

TruePBR and other material replacements may preserve property flags while not
reporting a useful vanilla material feature. Property flags therefore remain
the primary classification contract, with material feature as a constrained
fallback. Unsupported or ambiguous materials produce zero character control.

## Stereo and temporal ownership

Skyrim VR presents scene resources as a side-by-side stereo surface. The mask
source follows that surface, but CSX extracts one output-resolution mask per
eye using the exact eye viewport and crop used by the selected Gogh Neural
Rendering route.

Actor bounds are projected independently with each eye's unjittered matrix and
cached as one stereo plan for the frame. The left-eye projection is never
reused for the right eye. Bounds are expanded, clamped to that eye's viewport,
quantized for stability, and retained briefly only while the actor still has a
current valid projection. Behind-camera, culled, too-distant,
outside-viewport, and too-small candidates are omitted without reusing a stale
screen rectangle. Both distance and size admission use the observed face
bounds; a large body or hair bound cannot admit a distant face or satisfy
`Minimum Face Size`. Admission uses the
maximum face projection from the stereo pair, preventing one eye from
independently crossing the threshold while the other remains below it;
clipping still uses each eye's actual rectangle.

The authored G-buffer is temporally jittered. Mask extraction subtracts the
same low-resolution pixel jitter that the normal DLSS constants report, while
the eligibility rectangles remain unjittered. This keeps character boundaries
registered to the reconstructed output rather than allowing them to crawl with
the sample sequence.

Feature 18 resources and temporal histories remain private per eye and route:

-   slots 0 and 1: main/upscaled-centre left and right
-   slots 2 and 3: submit/final-LDR left and right

The control-mask resource uses the same slot ownership. No mask or history may
cross from left to right or between insertion points. Both eyes still publish
as one stereo transaction: if either eye cannot prepare or evaluate its mask,
neither eye's Neural Rendering result is committed for that pair.

In normal `Authored` mode, a stereo pair with no eligible character region
clears its masks and bypasses Feature 18. If either eye has an eligible region,
both eyes still evaluate as one transaction; the empty eye receives a zero
mask. Calibration overrides intentionally continue evaluating even without an
observed character. A later re-entry is treated as a temporal discontinuity by
the existing per-slot history policy.

Menus, pause state, stale world input, camera cuts, resource changes, and
settings transitions retain Gogh's temporal admission and reset rules. A held
rectangle never overrides those gates.

## Settings and defaults

The character feature is disabled by default and lives under the existing
Neural Rendering controls. Its central defaults are:

| Setting                      | Default      |
| ---------------------------- | ------------ |
| Deterministic mask composite | On           |
| Faces                        | On           |
| Skin                         | On           |
| Hair                         | Off          |
| Face strength                | `1.0`        |
| Skin strength                | `1.0`        |
| Hair strength                | `0.65`       |
| Maximum character distance   | `10.0 m`     |
| Minimum projected size       | `64 px`      |
| Rectangle margin             | `25%`        |
| Maximum regions per eye      | `4`          |
| Rectangle hold               | `3 frames`   |
| Compute ROI                  | Unavailable  |
| Visibility depth test        | On           |
| Depth-aware feather          | Off          |
| Feather radius               | `1 input px` |
| Relative depth threshold     | `0.002`      |
| Debug view                   | Off          |

Skyrim world units are converted with the repository's established game-unit
conversion before applying the distance limit. Category strengths and all
allocation/dispatch-affecting controls are validated at the settings boundary.
Invalid automation input is rejected rather than silently changing the
requested experiment.

Overlapping or nearby actor bounds are merged deterministically using the
least-area-inflating eligible pair. If separated regions still exceed the cap,
the lowest-priority regions are dropped instead of creating a large union that
could admit unrelated actors between them. These regions stabilize the visual
mask and support measurement; they do not imply compute ROI.

## Diagnostics and profiling

The in-game diagnostics and DevBench status should expose, per eye:

-   evaluation dimensions and Feature 18 slot
-   eligible face-actor and character-region counts
-   merged rectangle count and rectangle coordinates
-   rectangle pixel coverage and asynchronous nonzero-mask coverage, including
    the sampled frame, feature slot, and dimensions
-   authored face, skin, and hair pixel counts before visibility rejection,
    visible category counts, and the total rejected by frozen/current depth
-   frozen stereo base/crop and current eye-local depth coordinates
-   whether mask preparation succeeded and whether evaluation was required
-   visual-mask mechanism and the exact compute-ROI unsupported reason
-   synchronized category/depth-capture readiness, frame, attempts, successes,
    same-frame reuses, empty bypasses, and failures
-   preparation attempts, successes, failures, and dropped readbacks

The debug views are `Off`, `Character Mask`, `ROI Rectangles`, and
`DLSS 5 Output`. “ROI Rectangles” means eligibility/measurement rectangles, not
provider compute dispatches.

Diagnostics also expose six CSX-selection-mask modes: `Authored`, `Force Zero`,
`Force One`, `Force Half`, `Invert Authored`, and
`Authored (Ignore Visibility Depth)`. The forced modes isolate CSX composite
behavior at known weights; the last mode isolates category authoring from core
depth rejection. They do not modify Feature 18's automatic-mask invocation and
are not image-quality presets. Normal use remains `Authored`.

Profiling distinguishes synchronized category/depth capture, mask generation,
CPU rectangle setup, Feature 18 GPU evaluation, and
`Upscaling::DLSS5CharacterComposite`. Mask coverage and Feature 18 time must be
compared experimentally: lower coverage is not proof of lower inference cost.
The character snapshot attributes its latest coverage sample to a frame, eye,
slot, and dimensions, while Renderer telemetry attributes Feature 18 time to
the main/submit route and feature-slot mask. The auxiliary mask, copy, and
composite profiler labels remain aggregate; their independent asynchronous
timers are not summed into a misleading per-frame total. Character isolation
forces the main Upscaled-Center route to its existing staged neural output, so
the normal-DLSS center remains the baseline without an extra copy. The submit
float route already keeps its neural output separate. Frames with no
eligible material observation are logical empty captures: they clear the
per-eye masks and bypass both active-stereo provenance copies and Feature 18.
Repeated capture callbacks with the same frame, active extent, category policy,
and jitter reuse the first snapshot instead of copying it again.
Authored and forced-mask coverage is sampled on stable policy/layout changes
and at a 30-frame cadence per feature slot. Head motion and changing projected
rectangles do not trigger extra counter/readback work. Readbacks are polled once
per frame and ordered by a monotonic request serial. Authored-category counters
scan every low-resolution input pixel in the active eye crop directly,
independent of output resampling, render jitter, depth rejection, and ROI
motion. A sample with zero pixels in all enabled categories may request a
Feature 18 bypass for a deliberately bounded, stale-tolerant four frames while
policy, layout, and the eligible actor set remain unchanged. Mask generation
continues on those frames; the next scheduled readback revalidates coverage
without a synchronous CPU/GPU wait. Same-frame empty observations or a
deterministically empty eligibility plan request a bypass immediately. A
one-eye projection disagreement is not treated as proof: that eye falls back
to a full-eye eligibility rectangle while the authored category texture still
selects the actual pixels. The
stereo execution boundary records each slot separately as successfully
evaluated, evaluation-failed, empty-bypassed, or aborted, so a zero request
from only one eye is not reported as a skip when the paired Feature 18 call
still evaluates both eyes.

## Known selection limitations

The semantic channel is written by deferred opaque/alpha-tested lighting and is
frozen after Terrain Blending but before ordinary blended decals, together with
main depth from that exact render point. The mask resolve compares that authored
depth against the current per-eye guide directionally. A closer later depth
sample rejects an occluded category; a farther or cleared sample preserves it.

That reconciliation is limited to surfaces represented in the selected current
depth guide. Translucent, blended-decal, particle, and other post-deferred
surfaces that do not update it can still modify color over an underlying
character after provenance was frozen. The mask cannot infer that coverage, so
strict final-image visibility remains unproven for those cases. Solving it
requires a later visibility/composition signal with defined ownership, not a
looser depth tolerance.

The category channel has no actor identity. Eligibility rectangles prevent the
normal case of distant or tiny actors entering the mask, and separated regions
are dropped rather than force-unioned at the cap. A different character already
inside an admitted or legitimately nearby merged rectangle still cannot be
distinguished per pixel. A strict per-actor distance/size guarantee would need
an actor-ID buffer or a selected-geometry pass.

The VR `MASKS2` attachment remains two bytes per combined-stereo pixel even
when character NR is disabled: `R8G8_UNORM` replaces the previous
`R16_UNORM`. This avoids an always-on attachment-size and MRT-bandwidth
increase, at the cost of reducing inverse vertex AO from 16-bit to 8-bit UNORM
precision. Flat SE/AE permutations retain their original `R16_UNORM`
representation.

When character mode observes an enabled material, it additionally allocates an
active packed-stereo RG8 category snapshot and a matching native-format depth
snapshot, then copies only `2 * logical eye width` by logical render height each
nonempty frame. Native allocation padding is excluded. Logical empty frames
skip both copies, although previously allocated snapshot resources remain
resident until reset or resource recreation. The two copies share the
`Upscaling::DLSS5CharacterCategoryCapture` profiling scope. Dedicated per-eye
`R8_UNORM` CSX selection masks and their small diagnostic counter buffers are
separate slot resources. The current ROI rectangles do not reduce provider
inference dimensions.

## Failure behavior and validation boundary

Character mode fails closed. It does not bind the unvalidated private provider
mask, reuse a stale peer-eye mask, or publish a one-eye result. If material
classification, mask extraction, dimension/format checks, D3D11/D3D12 sharing,
or Feature 18 evaluation fails, the pair retains Gogh's completed normal-DLSS
result. Disabling character mode restores the ordinary automatic-mask route.

The branch must report rather than conceal an unavailable compute-ROI path.
Only a future, documented NVIDIA contract plus runtime evidence may change that
capability to supported.

A successful build validates type and shader integration only. A controlled VR
run must still establish all of the following before the experiment can be
called visually correct:

1. face/skin/hair classification on vanilla and representative modded actors;
2. alpha-tested hair boundaries and optional depth-aware feathering;
3. independent left/right alignment through motion, menus, and camera cuts;
4. no history or mask leakage between eyes or Gogh insertion points;
5. CSX composite response to the `R8_UNORM` `0..1` selection mask;
6. GPU cost versus evaluation dimensions, mask coverage, and actor count.
