# CSX shader and feature interaction survey

Status: first-pass static survey

Snapshot: `debug/vr-followup-20260816` at `4f7f0db58eb37493d37e23602a4b4ab3c667d95b`

Survey date: 2026-08-16

## Purpose

This document is an initial map of CSX's graphics features, shader families,
data producers, consumers, and obvious interaction boundaries. It is intended
to answer accessible questions first and to identify where deeper investigation
is likely to pay off.

It is **not** a claim that every feature is physically correct, that every
consumer has been found, or that the current menu categories describe the
rendering architecture. In particular, a feature's shader define being offered
to a shader family proves compile-time exposure, not necessarily that every
permutation executes feature code.

No runtime behaviour was changed while producing this survey.

## Evidence notation

- **D — direct:** explicit feature registration, shader define, resource bind,
  dispatch, hook, or shader call was found.
- **S — stated:** taken from the feature's in-code summary or repository
  documentation; implementation was not exhaustively verified.
- **I — inferred:** a conservative inference from names, surrounding code, or
  pass placement.
- **? — open:** not established in this pass.

The tables intentionally prefer `?` to false precision.

## The layers currently presented as “features”

The registered list contains 38 flat-screen features and appends VR as a 39th
feature at runtime. The 40 directories under `features/` also include the
separately packaged Terrain Shadows heightmap assets, which are not a distinct
registered `Feature`. These entries are not architectural peers:

1. **Engine shader-family replacements:** `Lighting`, `Effect`, `Particle`,
   `Water`, `Grass`, `Sky`, `DistantTree`, `Utility`, `BloodSplatter`, and
   `ImageSpace` are Skyrim render domains replaced or intercepted by the shader
   cache.
2. **Material models:** True PBR, Extended Materials, Extended Translucency,
   Hair Specular, Subsurface Scattering, wetness, and terrain material changes.
3. **Lighting and visibility producers:** clustered lights, screen-space GI,
   skylight probes, cubemaps, and several different shadow representations.
4. **Lighting consumers/modifiers:** inverse-square falloff, linear-lighting
   adjustments, IBL, and direct-light shadow application.
5. **Independent image-space pipelines:** volumetric lighting and vanilla/CSX
   image-space operations.
6. **Display transforms:** upscaling, sharpening, foveation, render-scale
   handling, and VR submission/mirror composition.
7. **Policy and tools:** adaptive brightness, weather controls, editors,
   screenshots, profiling, RenderDoc, and the performance overlay.

Consequently, menu adjacency does not imply execution adjacency, and a single
feature can occupy more than one layer.

## Base shader-family map

The descriptions below are the most accessible domain boundaries. They are
coarse: one shader family can contain many technique permutations.

| Shader family | Accessible role | Important ambiguity |
| --- | --- | --- |
| `Lighting` | Dynamic/static object, landscape, character, and material lighting passes. | Very broad. A define scoped to `Lighting` is not automatically scoped to one material or one kind of light. |
| `Effect` | Special effects and effect geometry; also participates in shared lighting/shadow helpers. | Overlaps conceptually with particles and transparent materials. |
| `Particle` | Particle systems such as smoke and sparks. | The family mixes participating-media-like particles with emissive or decorative particles. |
| `Water` | Water surfaces, refraction, underwater and water-light interaction. | Contains both surface material work and view-ray/refraction work. |
| `Grass` / `RunGrass.hlsl` | Grass geometry, deformation and lighting. | Grass is both material and vegetation-specialized geometry. |
| `Sky` | Sky dome, clouds, and related effects. | Also acts as an input producer for cloud shadows, cubemaps, weather and ambient light. |
| `DistantTree` | Distant vegetation/LOD rendering. | Shares environmental lighting while having specialized LOD assumptions. |
| `Utility` | Utility passes such as shadow masks or G-buffer fills. | “Utility” describes pipeline purpose, not a visual phenomenon. |
| `ImageSpace` / `IS*.hlsl` | Full-screen and post-processing operations. | Operations occur at different points and resolutions; order is essential. |
| `BloodSplatter` | Blood-splatter effects. | Separate family but can inherit defines declared for “all shader types.” |
| `DeferredCompositeCS` | Combines CSX G-buffer data and full-screen lighting outputs into the main scene. | A major convergence point for SSGI, IBL, skylighting, cubemaps, terrain blending, motion vectors and VR. |

## Readily visible frame/data flow

This is an execution skeleton, not a complete frame graph.

| Stage | Directly visible behaviour | Architectural significance |
| --- | --- | --- |
| Skyrim shadow rendering completes | `Deferred::EarlyPrepasses()` updates shared data and directional-shadow light data, then invokes each loaded feature's `EarlyPrepass()` in registration order. | Shadow-derived producers can run here. Ordering is centralized but dependencies are not declared. |
| Reflections prepass | Loaded features receive `ReflectionsPrepass()`. Cloud Shadows copies/binds cloud-occlusion cubemap data here. | Reflection-domain state can differ from the main view. |
| General prepass | Loaded features receive `Prepass()` in registration order. | Many unrelated producers bind resources here; register and stale-state contracts matter. |
| Deferred scene | CSX redirects engine render targets to G-buffer-like targets. | Base shader families become producers of common scene data as well as final shading. |
| Deferred feature passes | SSGI, SSS and cubemap updates execute before deferred composite. | These features are independent producers whose outputs converge later. |
| Deferred composite | Binds cubemap, skylight, SSGI, IBL, terrain and G-buffer resources and dispatches `DeferredCompositeCS`. | Primary cross-feature convergence point. |
| VR stereo blend | Runs immediately after deferred composite in VR. | Per-eye resource shape and history must already be correct. |
| Image-space and submission | Volumetric lighting, vanilla image-space passes, upscaling/foveation and HMD/mirror submission operate later. | Resolution domain, temporal history, ordering and transition state become first-class contracts. |

All loaded-feature callbacks use the feature registration order in
`src/Feature.cpp`. No feature-specific overrides of the available
`GetActiveConstraints()` mechanism were found in this pass.

## Feature inventory

### Lighting, visibility, and atmosphere

| Feature | Role and accessible producer/input | Declared or observed consumers | Obvious relationships / first-pass question |
| --- | --- | --- | --- |
| Adaptive Brightness | **S/D:** policy/calibration layer balancing scene lighting, atmosphere and bloom by location/time profile. Depends on CS Utility being loaded. | Shared settings and bloom controls rather than a dedicated shader define. | Overlaps Linear Lighting, HDR/CS Utility, volumetric lighting, weather and Bloom. Is this exposure control, artistic grading, or both? |
| Cloud Shadows | **D:** renders cloud occlusion into a cubemap during sky/reflection work; binds it in reflection and early prepasses. | Define declared for all base families. Explicit package references in Lighting, Effect, Particle, Grass, Sky, DistantTree, volumetric-light generation and shared shadow sampling. | One shadow producer with unusually broad reach. Clarify which domains should receive terrain/object cloud attenuation and whether sky consumption is production, application, or both. |
| Screen Space Shadows | **D/S:** screen-space raymarch/reprojection producer with optional VR stereo synchronization. | Define declared for all families; explicit package references found in Lighting, Grass and DistantTree. | Overlaps engine directional shadows, Terrain Shadows and Volumetric Shadows. Establish whether it supplies contact detail, replaces a mask, or multiplies another shadow term. |
| Terrain Shadows | **D/S:** terrain-heightmap shadow producer with a compute update and shared sampling helper. | Define declared for all families; explicit references in Effect, Particle, Grass, DistantTree, volumetric-light generation and shared shadow sampling. | Overlaps cloud/world-shadow terms and other directional shadows. Document scale, update cadence and intended receiving geometry. |
| Volumetric Shadows | **D/S:** downsampled and blurred directional VSM producer. Replaces the shared shadow-resource layout when compiled. | Define declared for all families. Direct application in Lighting, Particle and Water; Effect calls the shared 3D-filtered helper and is an indirect consumer. | Current summary says “transparent and volumetric shadow consumers,” while ordinary Lighting and generic Particle application broaden that scope. Resource-layout and application scope are currently coupled. High-priority deep dive. |
| Interior Sun | **S/D:** enables/coordinates sunlight-like directional lighting in interiors and changes raster-state handling for applicable passes. | Engine/light state and other features rather than a dedicated package define. | Interacts with volumetric lighting and every directional-shadow system. Establish the authoritative meaning of “directional light active” in interiors. |
| Light Limit Fix | **D/S:** clustered-light producer and replacement/extension for Skyrim's local-light limit, including particle-light handling. | Define declared for all families; explicit references in Lighting, Effect, Particle, Water and Grass. | Major owner of local-light enumeration and attenuation. Directly coordinates with Inverse Square Lighting and Upscaling. |
| Inverse Square Lighting | **S/D:** changes local-light attenuation toward inverse-square behaviour. | Define declared for all families; explicit references in Lighting, Effect, Particle, Water and Grass. | Directly overlaps Light Limit Fix and Linear Lighting. Determine which code owns physical units, source radius and legacy compatibility. |
| Linear Lighting | **S/D:** converts or calibrates selected lighting calculations into a linear-lighting workflow and supplies shared adjustment data. | No dedicated package define was found; setup/prepass and shared colour helpers are used. | Overlaps Adaptive Brightness, Inverse Square Lighting, Light Limit Fix, HDR and material BRDFs. A units/colour-space contract would be valuable. |
| Skylighting | **D/S:** probe/grid producer for sky occlusion and directional ambient illumination. | Define declared for all families; explicit references in Lighting, Effect, Water, Grass, shared shadow sampling and deferred composite. | Overlaps IBL, SSGI, ambient terms and cubemap-derived lighting. Determine which component owns diffuse ambient visibility versus radiance. |
| Image Based Lighting | **D/S:** produces diffuse IBL textures from cubemap spherical harmonics and states that it replaces game ambient lighting. | Define declared for all families; explicit references in Lighting, Effect, Water, Grass, DistantTree, SSAO composite, shared shadow sampling and deferred composite. | Overlaps Skylighting, Dynamic Cubemaps and SSGI. “Replaces ambient” needs a precise hand-off rule to avoid double-counting. |
| Screen Space GI | **D/S:** multi-pass screen-space AO/GI producer with temporal denoising, resolution modes and VR stereo history handling. | Outputs AO, diffuse GI and optional specular GI to deferred composite. | Overlaps IBL and Skylighting; depends on scene depth/normals, Upscaling resolution policy and temporal resets. Establish additive/replacement energy policy. |
| Volumetric Lighting | **D/S:** custom image-space/compute generation, raymarch and blur pipeline for atmospheric light scattering. Replaces or controls Skyrim volumetric-lighting objects/settings. | Applied through dedicated image-space shaders; also reads shadow/environment inputs. | Interacts with Cloud Shadows, Terrain Shadows, Interior Sun, Sky Sync, weather, Upscaling and transitions. Separate the media model from the final image-space application. |

### Materials and surface response

| Feature | Role and accessible producer/input | Declared or observed consumers | Obvious relationships / first-pass question |
| --- | --- | --- | --- |
| True PBR | **S/D:** replaces Skyrim's legacy material interpretation with physically based material data and Lighting-shader integration. | Lighting techniques/material setup. No general feature define was found in the standard `Feature` header because implementation lives in `src/TruePBR.*`. | Foundational material owner. Compare roughness/metalness/normal conventions with Extended Materials, wetness, cubemaps, SSS and translucency. |
| Extended Materials | **S/D:** parallax occlusion and complex material blending. | Define restricted to Lighting; explicit Lighting reference. | Overlaps True PBR, Terrain Variation/Blending and wetness. Determine precedence when multiple material encodings apply. |
| Extended Translucency | **S/D:** additional translucent/subsurface-like response for Lighting materials, with geometry setup hooks. | Define restricted to Lighting; explicit Lighting reference. | Overlaps SSS and material alpha/transmission conventions. Clarify whether it represents thin transmission, diffusion, or an artistic wrap term. |
| Hair Specular | **S/D:** tangent-based hair highlight model. | Define restricted to Lighting; shared lighting-evaluation helpers and Lighting reference it. | Specialized BRDF layered on the general lighting path. Check interaction with Linear Lighting, True PBR and wetness. |
| Subsurface Scattering | **D/S:** material classification plus separable/Burley SSS compute work before deferred composite. | Define declared for all families but explicit package use is primarily Lighting/common math; dedicated compute passes consume G-buffer data. | Overlaps Extended Translucency, skin wetness and deferred composite. Distinguish material marking from screen-space diffusion. |
| Dynamic Cubemaps | **D/S:** captures/infers environment cubemaps and produces specular irradiance/reflection textures. | Define declared for all families; explicit references in Lighting, Effect, Water, Grass, DistantTree, shared lighting evaluation and deferred composite. | Overlaps IBL and every reflective material. Clarify which texture supplies ambient radiance versus reflection and when histories refresh. |
| Wetness Effects | **S/D:** weather/shore/puddle/raindrop material effects. Currently hidden and force-disabled in AIO. | Define declared for all shader families; explicit package references in Lighting, Water and shared lighting evaluation. | Appears to overlap heavily with Wetterness and directly references it. Establish whether this is legacy, migration scaffold, or separate implementation before any cleanup. |
| Wetterness | **S/D:** active wet material, puddle, shore and raindrop system with weather state and resources. | Define restricted to Lighting and Water; explicit package references in those families and shared lighting evaluation. | Overlaps Wetness Effects, Dynamic Cubemaps, True PBR, Grass Lighting and water/shore state. High-value ownership clarification. |

### Grass, terrain, and LOD

| Feature | Role and accessible producer/input | Declared or observed consumers | Obvious relationships / first-pass question |
| --- | --- | --- | --- |
| Grass Lighting | **S/D:** grass-specific diffuse/specular/subsurface lighting adjustments. | Define restricted to Grass; explicit `RunGrass.hlsl` reference. | Should share light/colour contracts with general Lighting without inheriting inappropriate material assumptions. Wetterness reads Grass Lighting state. |
| Grass Collision | **D/S:** actor collision collection and compute-updated grass deformation data. | Define restricted to Grass; explicit `RunGrass.hlsl` reference. | Mostly geometric rather than lighting. Confirm update and world-space conventions under VR and cell transitions. |
| Terrain Blending | **D/S:** depth/material blending for landscape transitions; also intercepts render techniques and dirty-state changes. | Define declared for all families; explicit Lighting and deferred-composite references plus a feature depth-blend shader. | Coupled to screen-space depth, VR and Upscaling. It also interacts with Screen Space Shadows. Clarify producer/consumer ordering around deferred targets. |
| Terrain Variation | **S/D:** reduces landscape texture tiling through sampling variation. | Define restricted to Lighting; explicit Lighting reference. | Material-only intent but coexists with Extended Materials and Terrain Blending. Likely a tractable local contract. |
| Terrain Helper | **I/D:** runtime/helper classification and terrain hooks; no package shader reference found. Non-core. | Engine hooks and other terrain features. | Determine whether it is user-visible rendering, shared metadata, or compatibility infrastructure. |
| LOD Blending | **S/D:** blends LOD transitions in the Lighting family. | Define declared for all families but explicit package use found only in Lighting. | Broad declaration appears larger than observed scope. Interacts with deferred LOD shadow assumptions and terrain features. |

### Water and shoreline

| Feature | Role and accessible producer/input | Declared or observed consumers | Obvious relationships / first-pass question |
| --- | --- | --- | --- |
| Water Effects | **D/S:** loads caustic resources and binds them during prepass; adds water caustics/parallax/underwater lighting. | Define declared for all families; explicit references found in Water and Lighting. | Water-caustic illumination on ordinary geometry may be intentional, but the all-family declaration is broader than observed use. |
| Unified Water | **S/D:** reorganizes or replaces water shading options through the Water shader. Non-core. | Define declared for all families; explicit package reference found only in Water. | Candidate for tighter compile scope. Relationship to Water Effects, Wetterness, Horizon Fix and volumetric shadows needs a single water-pipeline diagram. |
| Horizon Fix | **D/S:** allows Water rendering beyond the far clip only when an external HorizonFix plugin is installed. | Define restricted to Water; explicit Water reference. | A well-bounded compatibility feature and a useful example of narrow scope and explicit runtime dependency. |

### Sky and environment policy

| Feature | Role and accessible producer/input | Declared or observed consumers | Obvious relationships / first-pass question |
| --- | --- | --- | --- |
| Sky Sync | **S/D:** synchronizes or edits sky/weather state; installs lifecycle hooks and is referenced by Volumetric Lighting. | Engine/weather state rather than a package shader define. | Overlaps Weather Picker, Cloud Shadows, cubemap production, IBL, Adaptive Brightness and volumetric lighting. Identify authoritative weather-transition timing. |

### Display, VR, and temporal/resolution infrastructure

| Feature | Role and accessible producer/input | Declared or observed consumers | Obvious relationships / first-pass question |
| --- | --- | --- | --- |
| Upscaling | **D/S:** FSR/Streamline/RCAS, motion-vector/depth preparation, foveation, render-scale relatching, temporal history, VR layer composition and submission-stage work. | Operates across scene targets and final submission; explicitly coordinates with SSGI, SSS, screen-space shadows, Dynamic Cubemaps, Volumetric Lighting, LLF, VR and RenderDoc. | This is infrastructure rather than one visual phenomenon. Resolution domain and resource lifecycle should be documented as contracts consumed by other features. |
| VR | **D/S:** stereo data, per-eye transforms, depth-culling policy, overlays, stereo blend and VR compatibility. Appended only for VR. | Cross-cuts deferred composite, upscaling, screenshots and feature resource setup. | Every screen-space/temporal feature is implicitly a consumer. VR support booleans establish availability, not correctness. |

### Utilities, editing, and diagnostics

| Feature | Role and accessible producer/input | Declared or observed consumers | Obvious relationships / first-pass question |
| --- | --- | --- | --- |
| CS Utility | **D/S:** controls vanilla depth of field, underwater fog blur, shared colour/HDR helpers and selected vanilla point-light data. | Define offered to Lighting, Water and ImageSpace; explicit package references in common colour and HDR code. | Despite its name, this is part rendering feature and part infrastructure. Adaptive Brightness requires it. Split conceptual responsibilities in documentation before considering code changes. |
| CS Editor | **S/D:** in-game inspection/editing of weather and renderer-facing data. | Tool and engine-state hooks; define has no package shader reference in this pass. | Can mutate inputs used by many environment features but is not itself a light-transport stage. |
| Weather Picker | **S/D:** weather browsing and switching. | Engine state; coordinates with CS Editor and wetness systems. | Test tool and user feature. Weather transition timing is shared state. |
| Screenshot | **D/S:** captures final or intermediate views and integrates with the VR in-scene overlay/present path. | Present/VR display path. | Diagnostic output must state which eye, resolution domain and pipeline stage it captures. |
| Performance Overlay | **D/S:** instrumentation, per-shader-family draw statistics and feature A/B measurement UI. | Observes broad pipeline state; coordinates with Upscaling and VR. | Useful survey-validation tool but measurements can include settle/history costs. |
| RenderDoc | **D/S:** graphics capture integration. | Diagnostic only; Upscaling coordinates with it. | Establish capture boundary and injected-runtime limitations rather than treating it as a rendering feature. |

## Cross-cutting compile exposure

The following features return `true` from `HasShaderDefine()` for every base
shader type in this snapshot:

- Cloud Shadows
- Dynamic Cubemaps
- Image Based Lighting
- Inverse Square Lighting
- Light Limit Fix
- LOD Blending
- Screen Space Shadows
- Skylighting
- Subsurface Scattering
- Terrain Blending
- Terrain Shadows
- Unified Water
- Volumetric Shadows
- Water Effects
- Wetness Effects

This is not proof that all shader families apply each effect. It is, however,
important because it:

1. increases permutation and cache coupling;
2. allows common includes to change resource declarations for apparently
   unrelated families;
3. makes “loaded” versus “runtime enabled” semantics significant;
4. makes a feature-scope correction unsafe if it changes only the define filter
   while shared resource bindings remain feature-wide.

Examples already visible in the easy pass:

- `LOD_BLENDING` is explicitly referenced only by `Lighting.hlsl` despite the
  all-family declaration.
- `UNIFIED_WATER` is explicitly referenced only by `Water.hlsl` despite the
  all-family declaration.
- `WATER_EFFECTS` is explicitly referenced only by Lighting and Water despite
  the all-family declaration.
- `VOLUMETRIC_SHADOWS` changes the shared shadow-resource interface and is also
  applied by multiple consumer families; compile scope and application scope
  cannot safely be treated as the same switch.

These are review candidates, not automatic bugs.

## Interaction clusters worth a second pass

### 1. Directional shadow ownership — highest immediate value

Participants: native engine shadow masks, Cloud Shadows, Terrain Shadows,
Screen Space Shadows, Volumetric Shadows, Interior Sun, Skylighting/shared
shadow sampling, Water, Particle, Effect, Lighting and volumetric lighting.

Questions:

- Which producer represents geometric visibility, terrain-only visibility,
  contact detail, cloud attenuation, and participating-media visibility?
- Which terms replace one another and which may legitimately multiply?
- Which consumers require a surface sample, a ray integral, or both?
- Which resource layout remains bound when an effect is loaded but disabled?

### 2. Ambient and indirect-light ownership

Participants: vanilla ambient terms, Skylighting, IBL, SSGI, Dynamic Cubemaps,
Adaptive Brightness, Linear Lighting and deferred composite.

Questions:

- Who owns diffuse ambient radiance, ambient visibility/AO and specular
  environment response?
- Are IBL, skylight and SSGI additive, replacement or confidence-weighted?
- At what stage does Adaptive Brightness alter scene energy versus display
  presentation?

### 3. Material-model composition

Participants: True PBR, Extended Materials, Extended Translucency, Hair
Specular, SSS, Dynamic Cubemaps, Wetterness/Wetness Effects, Terrain Variation,
Terrain Blending and Grass Lighting.

Questions:

- Which feature owns base colour, roughness, metalness, transmission,
  subsurface response and environment reflection when several are active?
- Are material flags mutually exclusive, layered, or ordered overrides?
- Do all features use the same colour-space and normal/roughness conventions?

### 4. Water pipeline

Participants: Water shader, Unified Water, Water Effects, Wetterness, Horizon
Fix, Dynamic Cubemaps, IBL, Skylighting and Volumetric Shadows.

Questions:

- Separate surface BRDF, refraction/underwater ray, caustic projection, shore
  state and far-horizon responsibilities.
- Identify which shadows apply to the water surface versus refracted scene.

### 5. Resolution and temporal-history contract

Participants: Upscaling, VR, SSGI, SSS, Screen Space Shadows, Terrain Blending,
Dynamic Cubemaps, volumetric lighting, screenshots and deferred composite.

Questions:

- For each resource: source resolution, eye layout, lifetime, history owner,
  reset triggers, and expected state during save/load transitions.
- Which consumers follow scene render scale and which operate at submission
  resolution?

### 6. Wetness implementation ownership

Participants: hidden/force-disabled Wetness Effects and active Wetterness.

Questions:

- Is one a legacy implementation, a staged migration, or a distinct layer?
- Which feature owns weather analysis, puddles, shore wetness, raindrops and
  material response?

## Structural observations from the first pass

1. **The repository has a modular feature registry but not yet an explicit
   frame graph or producer/consumer registry.** Most dependencies are expressed
   through global feature access, shared resources, callback order and shader
   macros.
2. **UI categories are useful for users but insufficient for architecture.** A
   “Lighting” feature can be a producer, consumer, policy layer, replacement
   pipeline or material modifier.
3. **Shared shader helpers are hidden convergence points.** `SharedData`,
   `ShadowSampling`, common colour/lighting evaluation and deferred composite
   can extend a feature beyond the files that mention its name directly.
4. **Feature registration order is also execution order for generic prepasses.**
   No declaration explains whether that order is required or accidental.
5. **Compile scope is sometimes broader than observed application scope.** This
   may be harmless convenience, cache debt, resource-interface necessity, or a
   real scope error; each case needs classification.
6. **“Supports VR” means eligible to load in VR, not that every resource,
   transition, history reset and per-eye calculation is independently proven.**
7. **The codebase already contains useful seams for gradual improvement:**
   feature-owned resources, per-family define filters, shared constant data,
   named prepass stages and a centralized deferred composite.

## Suggested second-pass record format

For each selected interaction cluster, extend this document or add a linked
deep-dive with the following fields:

| Field | Meaning |
| --- | --- |
| Intended phenomenon | Plain-language physical or artistic purpose. |
| Producer | Pass/function that creates the data. |
| Representation | Texture/buffer format, register, coordinate space, units and eye layout. |
| Consumers | Exact shader families/functions that read or apply it. |
| Composition rule | Replace, add, multiply, blend, min/max, or conditional fallback. |
| Lifecycle | Creation, bind, unbind, reset, resize and disable semantics. |
| Ordering | Required predecessor/successor passes. |
| Resolution/history | Render domain, temporal owner and reset triggers. |
| Compatibility | SE/AE/VR branches and external dependencies. |
| Validation | Compile test, static assertion, RenderDoc evidence or in-game observation. |

The Directional Shadow cluster is the best next candidate because the current
Volumetric Shadows question has already exposed a real distinction between
resource-interface scope and visual-application scope.

## Primary evidence locations

- Feature registry and VR filtering: `src/Feature.cpp`
- Feature interface and lifecycle callbacks: `src/Feature.h`
- Shader-family compilation and define collection: `src/ShaderCache.cpp`
- Shared state and base-family resource binding: `src/State.cpp`
- Prepass ordering and deferred convergence: `src/Deferred.cpp`
- Base shader families: `package/Shaders/`
- Shared shader contracts: `package/Shaders/Common/`
- Feature-owned shader assets: `features/*/Shaders/`
- Individual feature metadata and resources: `src/Features/` and
  `src/TruePBR.*`
