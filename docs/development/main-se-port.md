# main-SE synchronization from main-VR

The next incremental synchronization, based on the published SE follow-ups,
is recorded in [main-se-sync-2026-11-02.md](main-se-sync-2026-11-02.md).
It advances the source pin from `f140c878` to `da73f53d` and includes a
complete 39-commit disposition inventory. The record below describes the
initial port and its review.

## Scope and source identity

This worktree implements the shared functionality from the pinned `main-VR`
source on the flat Skyrim SE/AE branch. Rendering decisions use native SE
camera, render-target, depth, presentation and frame-generation ownership.

| Item                     | Identity                                   |
| ------------------------ | ------------------------------------------ |
| Worktree branch          | `main-SE`                                  |
| Parent branch            | `cs-1.7-PL-SE`                             |
| Parent commit            | `e1989826771a1faf9fbb9e7e77a3c38e2256051d` |
| Source branch            | `main-VR`                                  |
| Source commit            | `f140c878138fe2783a6a4ad40420a3a2a996ccd0` |
| Source repository        | `ParticleTroned/skyrim-community-shaders`  |
| Recorded implementation  | `64229b11ea47436a98ba87ea55c5b4fefc43d686` |
| Audit date               | 2026-11-01                                 |
| Project release identity | Existing `CSX 3.18-SE`, unchanged          |

The source commits remain the provenance of the imported algorithms. Native
SE rendering, hooks and settings are retained where the branches require
different implementations. Feature package versions and the generated feature
registry reflect the port independently of the project release version.

Implementation and review fixes are present in the worktree. The complete
adversarial review and regression corrections are recorded in
[main-se-adversarial-review.md](main-se-adversarial-review.md).
Portable checks are recorded below.
The native `ALL` configure was attempted on 2026-11-01 and stopped because its
`Visual Studio 18 2026` generator is unavailable here. Windows plugin compilation,
native shader compilation and in-game SE/AE validation have **not** run in the
Linux development environment. This document does not
claim a release-ready binary, measured speedup or visual equivalence.

## Functional coverage

### Shared improvements

| Area                        | Implemented behavior                                                                                                                                                                                                                                                                                    | Main implementation                                                                                                                |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| Shader cache                | Managed committed packs, complete task identity, source/dependency/compiler/feature ABI provenance, compatibility-scoped records, deduplication, failed-request retention, active-scene clearing, bounded capture, publication recovery                                                                 | `src/ShaderCache.*`, `src/ShaderCache/`, `tools/build-shader-cache.py`, `config/shader-cache-abi.json`                             |
| Public services             | Versioned discovery, service registry, editor/feature/shader/weather/profiler/upscaling/screenshot services, accepted-draw observers, idempotent requests, bounded main-thread dispatch and diagnostic bridges                                                                                          | `include/CSXAPI/`, `src/Api/`                                                                                                      |
| Settings and presets        | Safe save/load/default restoration, dirty-baseline ownership, requested/runtime state separation, composable Adaptive Balance profiles, weather-variable baselines, stable preset serialization and overrides                                                                                           | `src/State.*`, `src/Settings*`, `src/Preset*`, `src/Weather*`, `src/SceneSettingsManager.*`                                        |
| Menu and editor             | Shared overview/detail pages, Home/General/Advanced layouts, desktop header/sidebar, pinned save/load/default footer and dirty/error feedback, feature status and constraints, profile navigation, editor transactions/undo, weather selection and preview safeguards, centralized global action guards | `src/Menu/`, `src/CSEditor/`, `src/Features/CSEditor.*`, `src/MenuDevBenchBridge.*`                                                |
| Adaptive Balance            | Composed location/time/weather appearance, point-light and fire response, clouds/fog/sky/aurora/eyes, bloom, water and godray appearance, scene/underwater DOF and autofocus ownership                                                                                                                  | `src/Features/AdaptiveBrightness.*`, `AdaptiveBalanceDepthOfField.*`, `AdaptiveBalanceGodraySettings.h`, shared appearance shaders |
| Materials                   | Registered PBR ownership, bounded landscape texture enumeration, safer material dispatch, multiscatter/reflection response, roughness/normalization guards, mesh terrain variation, parallax strength and landscape seams                                                                               | `src/TruePBR/`, `src/Features/ExtendedMaterials.*`, `TerrainVariation.*`, `TerrainSeams.*`, shared PBR/lighting shaders            |
| Cubemaps and IBL            | Transactional resources, complete mip/BC6H coverage, valid publication, capture rebasing/refresh, controlled handoff to diffuse/specular IBL                                                                                                                                                            | `src/Features/DynamicCubemaps.*`, `src/Features/IBL.*`                                                                             |
| Ambient and shadow lighting | Adaptive SSGI and AO-only resource profiles, selectable skylighting grids/cadence, staged shadow toggles, depth-aware volumetric clarity, robust shadow copy/moments and fallback resources                                                                                                             | `src/Features/ScreenSpaceGI.*`, `Skylighting.*`, `ScreenSpaceShadows.*`, `VolumetricLighting.*`, `VolumetricShadows.*`             |
| Sky and terrain             | Climate/worldspace controls, sunlight dimming, native celestial synchronization, terrain penumbra/heightmap readiness, runtime Terrain Helper and shared terrain depth                                                                                                                                  | `src/Features/SkySync.*`, `TerrainShadows.*`, `TerrainHelper.*`, `TerrainBlending.*`                                               |
| Water                       | Validated cache bounds/counts, transactional cache reads, child-worldspace and duplicate-water cleanup, flowmap ownership, centered parallax, appearance/LOD controls and optimized mesh option                                                                                                         | `src/Features/UnifiedWater/`, `UnifiedWater.*`, `WaterAppearance.*`, `Water Effects/Shaders/WaterEffects/WaterParallax.hlsli`      |
| Wetterness                  | Stateful rain accumulation/drying, puddles/film/shore response, material/skin/grass wetness and bounded procedural masks; explicit coexistence policy for legacy Wetness Effects                                                                                                                        | `src/Features/Wetterness/`, `Wetterness.*`, `features/Wetterness/`                                                                 |
| Grass and collision         | Cross-cell batches, retained capacity, source ownership and draw history, density/distance/simple-shading controls, optional LODs, current-frame HiZ, native fallback, authored PBR grass and finite collision strength                                                                                 | `src/Features/GrassOptimizations/`, `GrassOptimizations.*`, `GrassCollision.*`, `package/Shaders/RunGrass.hlsl`                    |
| Lights and culling          | Conservative native sun/torch/NPC/actor-part culling, immutable scene snapshots and hysteresis, safe LLF reads, inverse-depth contact rays, staged inverse-square/contact settings, particle configuration validation                                                                                   | `src/Features/LightLimitFix/`, `LightLimitFix.*`, `InverseSquareLighting.*`                                                        |
| Engine fixes                | SE/AE sun-receiver eligibility/repair, nonblocking sun-occlusion polling, LOD ring handling and cascade rasterizer behavior                                                                                                                                                                             | `src/EngineFixes/`, `src/EngineFix.*`                                                                                              |
| Texture streaming           | Default-off bounded streaming, category and menu protections, native DDS/source metadata, pressure hysteresis, deferred retirement/restoration and current camera demand                                                                                                                                | `src/Features/TextureStreaming/`, `TextureStreaming.*`                                                                             |
| Profiling and comparison    | Named CPU/GPU self/inclusive timing, bounded sessions and query pools, external NR GPU timings, target intervals, quick scan, reversible feature batches and mono upscaling sweep                                                                                                                       | `src/Profiler.*`, `src/SEFrameTiming.*`, `src/RenderTargetStats.*`, `src/Menu/PerformanceTuning*`                                  |
| Capture and diagnostics     | SDR/HDR single screenshots and ordinary timed sequences, SDR strict references/bursts, deadlines/cancellation, secure paths, atomic publication, manifests and exact-frame NR/exposure evidence, RenderDoc, engine/asset stutters, texture lifetime capture and optional Nsight Perf                    | `src/Features/Screenshot*`, `src/Features/RenderDoc.*`, `src/Diagnostics/`, `tools/screenshot_verify.py`                           |
| Build and packaging         | Updated pinned dependencies, separate SE upscaling/FG runtimes, verified skip-download mode, pre-install guards, ownership-based shader deployment, SE managed-cache AIO assembly and NR distribution guard                                                                                             | `cmake/`, `CMakeLists.txt`, `.github/workflows/`, `tools/`                                                                         |

New feature entries are Grass Optimizations, Texture Streaming, Neural
Rendering, Wetterness and Weather Picker. Landscape seams are integrated into
the existing terrain/material feature ownership. Legacy utility metadata and
DOF settings remain recognized during migration.

### New UI and desktop adaptation

The 2026-11-01 UI follow-up corrected an incomplete earlier checkpoint: shared
page infrastructure was present, but Home/General/Advanced and several feature
pages still used the old layout. Those pages and the remaining desktop shell
have now been adapted in this worktree.

| Area                        | Implemented UI behavior                                                                                                                                                                                                                                                                                                                                                                                                     |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Desktop shell               | Responsive branding/runtime header, built-in action glyphs or text, sidebar selection and dirty markers, native window resizing/docking and repair of off-screen or undersized saved geometry. The header-centering preference remains effective.                                                                                                                                                                           |
| Page actions                | Pinned saved/unsaved/error status with guarded Save, Load and Defaults actions. Feature defaults retain their weather-baseline ownership. Status details remain accessible when a narrow layout uses compact labels or icons.                                                                                                                                                                                               |
| Home, General and Advanced  | Overview/detail cards, accurate shader-cache actions, input and NR/reset/capture hotkeys, theme transactions and zoom, startup/shader/diagnostic/capture/testing pages, compile statistics and complete task details.                                                                                                                                                                                                       |
| Rendering feature pages     | Structured sections for Cloud Shadows, Extended Materials/Translucency, Foliage Lighting, Hair Specular, Inverse Square Lighting, LOD Blending, Light Limit Fix, Linear Lighting, Performance Overlay, SSGI, Screen Space Shadows, Subsurface Scattering, Terrain Blending/Shadows, Unified Water and Wetness Effects. Horizon Fix uses the shared automatic-feature About page. Existing structured pages remain in place. |
| Upscaling and NR            | Upscaling Mode/Tuning/Sharpening/Motion/Diagnostics and mono NR Mode/Blending/Look/Colour/Selection/Actors/Diagnostics pages. Native render/output dimensions, SE FG/Reflex controls and actual mono settings are retained. The NR hotkey and enabled control share save/load/capture/measurement and FG conflict guards.                                                                                                   |
| Screenshots                 | Source/Output/Framing/Sequence pages with actual service errors and sequence receipts, secure folder validation, and polling while navigating other sections. Ordinary HDR sequences and strict HDR reference/burst restrictions match the existing SE backend.                                                                                                                                                             |
| SE presentation preferences | Native font roles/catalog/previews, locale fallback, custom cursors, FG controls, HDR display and screenshot choices remain available. Theme dirty tracking includes SE fonts, palette and cursors.                                                                                                                                                                                                                         |

These are source implementations, with focused compile and headless interaction
checks documented in [main-se-validation.md](main-se-validation.md). A native
in-game visual/accessibility check remains necessary; no screenshot or GPU-rendered
UI validation is claimed. Headset attachment, wand pointers and controller
overlay facilities are excluded.

### FOV, render scale and neural rendering

| Source concern               | SE implementation                                                                                                                                                                                                           |
| ---------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Headset FOV and eye coverage | Native unjittered mono camera projection and actual viewport. No headset tangents, eye-width division or center-of-vision gate is used.                                                                                     |
| Render-scale authority       | Existing SE engine render/final-output resolution and temporal-upscaling paths remain authoritative. Requests apply at safe render boundaries and report real applied state. Legacy VR render-scale modes are rejected.     |
| Full-scene NR                | A mono native provider path uses validated source/depth/motion/exposure/guide inputs, bounded resources, model selection, temporal reset and failure fallback.                                                              |
| Peripheral compression       | Rectangular mono compression/reconstruction uses image coordinates and explicit dimensions. It does not depend on a physical headset's visible region.                                                                      |
| Character NR                 | Native scene categories and immutable depth/category snapshots select actors; up to four packed crops are admitted only with useful area savings and valid ownership/geometry.                                              |
| Provider interaction         | Canonical D3D device identity, validated shared textures/handles, FSR shared guides, bounded allocator waits, retained in-flight ownership and guarded provider shutdown.                                                   |
| Memory and recovery          | Complete replacement-peak budget admission, idle retirement, pressure response, bounded single-queue stall recovery and visible fallback reasons.                                                                           |
| Prewarm                      | Real menu-boundary preparation for an exact known mono layout. Adaptive or actor-dependent layouts remain explicit blockers until their real dimensions are available. Unused prepared resources have a bounded lease.      |
| Exposure                     | Ordinary builds capture the actual native HDR producer scope and constants. Exact-frame exposure identity follows the inference and screenshot receipts. Developer experiments are separately gated.                        |
| Load/save transitions        | Post-load resets are consumed on the rendering boundary. Load/new-game clears stale category/history state while preserving healthy provider/model ownership where safe. Ordinary saves use the existing SE recovery rules. |
| Presentation                 | Results feed the existing SE scene/HDR/desktop presentation graph. Existing frame generation retains presentation and pacing ownership.                                                                                     |
| Evidence                     | Actual provider execution/results/settings are retained in a bounded history. Optional D3D12 timestamps are published only after fence completion under the requesting profiler epoch.                                      |

**NR and frame generation remain alternatives.** Configured FG or a live DX12
FG presentation graph blocks NR, including paused FG. The port preserves FG
and its retained resources; it does not claim concurrent NR/FG operation.

NR requires a compatible separately supplied native provider at
`Data/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll`, version 310.8.0.0 or newer.
Explicit network selection is supported for the 310.9 family. Model assets are
resolved by the supplied provider; the adapter does not invent a fixed sidecar
filename. The repository's distribution guard deliberately rejects
provider binaries and stale/unsupported NR payloads in release packages.
Missing or ineligible native capabilities produce explicit unavailable/fallback
state. The source model/provider requirements are not replaced with simulated
inference.

Ordinary HDR screenshots and timed sequences remain supported through the native
HDR PNG encoder. Strict HDR reference capture and HDR bursts are explicitly
unsupported; strict reference/burst tooling uses SDR.

### Culling and streaming adaptation

Grass uses one native view, six frustum planes, three LOD slots and the native
352-byte geometry layout. Depth-pyramid allocation and dispatch reject
multi-view input. Conservative native shadow admission and actor hysteresis
operate on SE/AE ownership snapshots; no VR shadow-batch ABI is installed.

Streaming demand is captured after the complete mip-bias calculation for
eligible world frames. This includes native rendering and TAA, as well as
FSR/DLSS. DLSS demand therefore sees the same final bias as the scene shaders.
Streaming uses the actual engine render/final-output plan, not an NR inference
crop size.

## SE implementations preserved

-   **Frame generation:** native DX12 swap-chain integration, DLSS-G and FidelityFX
    frame-generation paths, eligibility, resource retention, synchronization and
    presentation ownership. The separate `StreamlineDX12` payload is preserved.
-   **HDR and display:** existing display detection, HDR mapping/encoding, white
    levels, framebuffer/UI composition, native preview behavior and SDR/HDR
    screenshot source selection.
-   **Timing:** genuine accepted Present intervals and whole-frame GPU timing.
    Named pass timings do not substitute for whole-frame duration. One shared
    D3D11 disjoint clock and the existing eight-slot retirement behavior remain.
-   **Performance protocol:** the original SE settling/block/restoration protocol,
    coverage/repeatability/frame-pacing diagnostics and frame-generation
    override/restore ownership remain authoritative.
-   **Materials:** SE Vanilla Fresnel, metal controls, skin/SSS classification and
    scatter/composite behavior, finite guards and legacy grass rain roughness.
-   **World behavior:** SE water model/coverage/reflection safeguards, celestial
    and terrain heightmap generation handoff, configurable moons, weather/time
    ownership, editor weather locks and vanity-camera behavior.
-   **UI and compatibility:** Full SE settings navigation, fonts/themes,
    localization, version identity, native shader indices and existing settings
    migrations. Live translations are retained; obsolete keys are pruned and
    missing translations fall back to English.

The 35 shared x64 FidelityFX descriptor layouts used by the SE upscaling/FG
providers are checked against a snapshot from the parent commit. This checks
structure size, alignment and member offsets; it does not establish runtime
provider compatibility by itself.

## Source facilities excluded

| Facility                                                                           | Reason                                                                                                                      |
| ---------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| OpenVR/OpenComposite headset submission and temporal submission leases             | SE presents through its native desktop swap chain.                                                                          |
| Stereo eye packing, per-eye geometry and headset reprojection                      | SE uses a single camera/view and native mono buffers.                                                                       |
| OCU foveation, headset calibration and periphery TAA fixtures                      | Their physical-eye/FOV contract has no direct SE equivalent. The applicable NR compression algorithm is adapted separately. |
| Controller beams, wand pointers, drag surfaces and headset overlays                | No SE controller/headset overlay owner exists.                                                                              |
| VR render-scale relatching, loading-menu headset clears and VR FPS Stabilizer      | Existing SE render/output and performance ownership are retained.                                                           |
| VR grass lifetime hooks, native VR shadow batches and temporal depth-culling hooks | Their native ABI and submission contract are VR-specific.                                                                   |
| Raw cull-pool exhaustion patch and global texture-list/FaceGen assignment offsets  | The source evidence establishes VR addresses, not a verified SE/AE ABI. No guessed SE hook is installed.                    |

Texture lifetime diagnostics retain real hooked D3D11 creation/final-release
counts. Owner enrichment instead walks bounded reachable SE/AE world/preview
materials and typed FaceGen fields under renderer ownership. It explicitly
reports incomplete native ownership coverage and unknown head-part assignment;
it does not claim a complete global texture inventory. See
[texture-lifetime-capture.md](texture-lifetime-capture.md).

Public compatibility structures retain reserved historical values where
necessary to keep their wire layout stable. These fields do not enable a VR
runtime, dependency, rendering path or shader permutation.

## Resource and failure behavior

Graphics resources are named and owned through the existing RAII utilities.
Replacement views/resources are published only when the complete candidate is
valid. The shadow-copy pass validates its device/context, shader, source views
and constant buffers before use, preserves constant-buffer windows, restores
compute bindings and predication, and leaves a valid fallback binding when
available. Fixed-resource allocation failures disable shadow consumers until
reset/recreation can establish a complete fallback.

Global save/load/default/cache actions recheck save/load and performance-comparison
ownership at execution. Cache actions additionally check active-shader-capture
ownership, including when confirmation commits the action. A failed settings restoration keeps visible
ownership/error state instead of silently overwriting later user changes.

Runtime packaging verifies pinned inputs before any destructive install step.
Skip-download development cannot produce an incomplete release package. Shader
deployment uses manifests and content identity, preserves modified/unowned
files, and preserves all three SE runtime roots when skip mode is active. See
[runtime-downloads.md](runtime-downloads.md) and
[prebuilt-shader-cache.md](prebuilt-shader-cache.md).

## Validation

### Executed in Linux

The repeatable portable suite is `tests/portable/CMakeLists.txt`. It compiles
policy code, public contracts, extracted production functions and several actual
production translation units. Fixtures provide engine/COM/transport boundaries
only where the Windows/game environment is unavailable.

The final test count and command results are recorded in the companion
`main-se-validation.md`. The suite covers:

-   Mono NR settings, exposure, color, compression, crops, prewarm, recovery,
    resource admission, provider configuration and API contracts.
-   Grass/culling/streaming policies and native-layout contracts.
-   Actual profiler/SE frame-timing implementations, accepted Present identity,
    query failures and settings restoration/measurement ownership.
-   Shader cache pack identity/publication/rollback, native SE captures and AIO
    packaging with byte-preserved HDR/FG fixtures.
-   Screenshot coordination, committed evidence/paths/hashes and verification.
-   Service discovery/thread affinity/dispatch, accepted-draw registration and
    shared native hook-target publication.
-   Global action guards, crop/settings migrations, feature metadata/payload
    guards and runtime-cache/deployment behavior.
-   Texture lifetime COM-sentinel accounting, status/command logic and bounded
    native inventory policy.

The preserved SE Catch2 inventory passed **42 cases / 25,132 assertions**:
DLSS-G retention (6), FG eligibility (2), shadow policy (2), performance
measurements (24), vanity camera (2), settings JSON (3), shader scheduling (3).
The tests and production SE measurement/JSON sources ran unchanged using genuine
Catch2 3.8.1 and nlohmann JSON 3.12.0.

Additional checks established:

| Check                             | Result and limit                                                                                       |
| --------------------------------- | ------------------------------------------------------------------------------------------------------ |
| Shared feature-buffer layout      | 25 records, 2,112 bytes and 353 matching CPU/HLSL fields                                               |
| Shared frame-buffer data          | 720 bytes and 31 matching fields                                                                       |
| SSGI constants                    | 224 bytes and 29 matching fields                                                                       |
| Shadow-copy stride                | 752 bytes and 12 matching fields                                                                       |
| Screen-space shadow constants     | All 12 shader fields match the 80-byte used prefix of the retained 96-byte CPU allocation              |
| FidelityFX compatibility          | 35 preserved x64 descriptors checked by compile-time size/alignment/offset assertions                  |
| Rendering preprocessing           | 398 flat shader cases; no helper-arity mismatch in expanded output                                     |
| Grass/depth/contact preprocessing | 30 grass variants and four culling/depth/contact helpers                                               |
| Offline cache inventory           | 3,425 captured tasks produce 4,182 unique tasks, retaining all 90 HDR cases; no bytecode compiled here |

The layout checks use actual production declarations and HLSL packing rules.
Preprocessing and structural bytecode fixtures are not substitutes for native
shader compilation, DXBC reflection or real rendered output.

### Repeating the portable checks

Use CMake 4.2+, a C++23 compiler, Python 3.10+, real nlohmann JSON 3.12 headers
(including `json_fwd.hpp`) and the repository's Python tool requirements. Install
Catch2 3 or explicitly opt into the pinned test dependency:

```sh
cmake -S tests/portable -B build/portable-se \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCSX_JSON_INCLUDE=/path/to/include \
  -DCSX_PORTABLE_SE_BASELINE=ON \
  -DCSX_BASELINE_FETCH_CATCH2=ON
cmake --build build/portable-se --parallel 4
ctest --test-dir build/portable-se --output-on-failure -L SEPort
python tools/extract-i18n.py --check
python tools/extract-i18n.py --orphans
python tools/sort-i18n.py --check
python tools/generate-feature-versions.py --check
```

For offline Catch2 sources, add
`-DFETCHCONTENT_SOURCE_DIR_CATCH2=/path/to/Catch2-3.8.1`. The Python shader-cache
inventory tests require the pinned hlslkit dependency; their explicit skip
messages must not be reported as passes. The recorded development run supplied
that dependency and exercised real CMake archive creation/extraction.

The original Windows `tests/host` runner also includes the portable tests.
The Windows-only `device_context_state_test` exercises real D3D11 WARP state
isolation and compute shader/SRV/UAV/constant-buffer-window/predication
restoration. Its native path has been added but **was not run here**.

## Required native validation

Run the normal and DevBench builds separately with the repository launchers:

```powershell
pwsh ./tools/setup-dev.ps1
pwsh ./tools/cmake.ps1 --preset ALL -DBUILD_HOST_TESTS=ON -DBUILD_SHADER_TESTS=ON -DAUTO_PLUGIN_DEPLOYMENT=OFF
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders run_host_tests prepare_shaders
pwsh ./tools/cmake.ps1 --preset DevBench-AIO-Release -DBUILD_HOST_TESTS=ON -DBUILD_SHADER_TESTS=ON -DAUTO_PLUGIN_DEPLOYMENT=OFF
pwsh ./tools/cmake.ps1 --build build/DevBench-AIO-Release --config Release --target CommunityShaders run_host_tests prepare_shaders
```

Optional Nsight Perf needs the separately supplied SDK and
`NVPERF_SUPPORT=ON` with `NVPERF_SDK_ROOT` in the DevBench build. It measures the
D3D11 scope only; NR/FG D3D12 queues and overlap are not inferred from it.

| SE/AE exercise                                                                                             | Expected evidence                                                                                                                               |
| ---------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| Start with normal and developer builds                                                                     | No missing symbol/relocation or service registration failures; unavailable optional tools report their actual state.                            |
| Native/TAA, FSR and DLSS; change viewport, output resolution and camera FOV                                | Single-view bounds remain correct; source/render/output dimensions agree; histories/resources rebuild at safe boundaries.                       |
| FG off/on, each supported FG provider, HDR/SDR and menu composition                                        | Native SE presentation/pacing and UI composition remain valid; no duplicated or stale frame submission.                                         |
| Main/loading menus, new game, repeated load, ordinary save, cell/worldspace transitions                    | No stale resources/categories/history; settings/capture transitions respect ownership; native fallbacks recover.                                |
| NR networks/qualities, rectangular compression, actor categories/crops, prewarm/cancel and budget pressure | Actual provider execution and diagnostics match the active layout; invalid/ineligible requests fail visibly; valid rendering survives fallback. |
| Grass/HiZ, native fallback, PBR/legacy materials, collision and shadow hysteresis                          | Correct depth/color coverage and shadows across camera cuts, cell changes and render-scale changes.                                             |
| Streaming enabled/disabled, memory pressure, menu preview and external ownership                           | Protected textures remain valid; restore/retirement follows fences/ownership; demand updates in native/TAA modes.                               |
| Water, terrain seams, weather/celestial changes, cubemaps, SSGI, volumetrics and Wetterness                | No buffer-layout artifacts, stale captures, duplicate lighting ownership or loss of retained SE effects.                                        |
| Screenshots/bursts during HDR/FG/NR, cancellation and filesystem failure                                   | Committed image/manifest receipts match actual frames, paths and hashes; no partial success or stale NR evidence.                               |
| Performance measurement, configuration edits, cancellation and restoration                                 | Original SE measurements stay valid; FG and user settings restore exactly or report an explicit recoverable error.                              |
| Save/load/default/cache controls and all menu layouts                                                      | Guard reasons, settings scope, dirty state, fonts and localized fallback labels remain usable.                                                  |
| Texture/stutter/RenderDoc/optional GPU diagnostics                                                         | Capture bounds and provenance match real operation; native locks and optional hook failures remain nonblocking and visible.                     |

Record native failures and measured/visual results against the pinned source and
base identities above before treating this branch as a release candidate.
