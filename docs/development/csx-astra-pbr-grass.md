# Astra G1: PBR grass

This implements the PBR grass stage after the Hi-Z work in PR104. Grass
optimization and Reverse Z remain subsequent, separate PRs. The source
baseline is `main-VR` commit `47c7df09a`; this branch does not depend on
the Hi-Z implementation. The supplied Astra master and secondary handover
are technical guidance, not additional user instructions.

## Donor review and adaptation

Reviewed [upstream PR2709](https://github.com/community-shaders/skyrim-community-shaders/pull/2709)
at `2d68ed181ada895807ac87fdaaa9e1b83ceb01bd` and
[Open Shaders PR663](https://github.com/alandtse/open-shaders/pull/663)
at `a1915fc73b240abf802b7d2a7041b293d0427b2b`, cross-checking the current
Open Shaders source `fce2e305272e360ac7b917ae25d9ded0b234281e`.
The later PBR directional-shadow fix `c9e628dc5` was also reviewed.

The port retains fully populated material construction before interning,
the grass form's diffuse map, and distinct authored normal/RMAOS/subsurface
maps. It uses the existing PBR material registry and copy/hash ownership.
Missing normal maps bind the engine default normal. Missing RMAOS uses
roughness/specular parameters with nonmetal and unoccluded defaults;
the engine's white fallback must not be interpreted as authored metal.
Optional textures are rebound for every eligible material.

Native grass techniques and geometry remain in use. A combined
`PBR_GRASS` shader selects shading through per-draw flags, rather than
introducing donor technique IDs or optimizer hooks. This avoids importing
grass batching, material buckets, density/LOD changes, or new relocation
addresses. Existing constructor offsets are retained; the new hooks use
the established grass vtable, slots 2 and 4, for SE, AE and VR.

## Runtime and shader contract

PBR Grass defaults off. Authored materials use PBR shading only when True
PBR and Grass Lighting are both loaded and enabled and PBR Grass is on.
Turning those controls off retains native/basic shading. Authored PBR
diffuse maps use their full texture even when PBR shading is off; ordinary
basic and complex grass retain their existing selection.

Startup queues the existing grass descriptors when both feature packages
are loaded, including with disk caching off. Both shader stages must be
available together. Technique setup explicitly binds that pair and updates
the engine's reflected constant bookkeeping; an incomplete pair restores
the native stages. Shader failures retain the fallback. Runtime PBR Grass
switches change flags and do not request a different shader variant.

The pixel material buffer uses `b1`, size 32, with flags at byte 0,
roughness/specular parameters at byte 4 and subsurface parameters at byte 16. Normal, RMAOS and subsurface own `t2/s2`, `t3/s3` and `t4/s4`.
Material setup checks the buffer and exact reflected offsets before any
write; depth shaders have no PBR material buffer. Color shaders add
reflectance at target 5. VR World/PreviousWorld offsets, instancing,
collision, wind, motion vectors and alpha/depth behavior are retained.

Lighting uses CSX semantic color conversion, one ambient-balance
application, Wetterness, existing shadow-mask/world/screen shadow
ownership and eye-zero Skylighting coordinates. IBL uses its occluded
ambient path when Skylighting is enabled. Authored subsurface transmission
owns scattering when present; Foliage Lighting provides the alternative.
Water Effects caustics modify light color through its existing interface.

## Adversarial review fixes

-   Prevented material writes in depth shaders: unreflected constant-table
    entries default to zero, so a negative-offset check was insufficient.
-   Bound both custom stages explicitly when asynchronous compilation
    finishes during native setup, keeping shader pointers and actual bindings
    consistent. Restored both native stages for incomplete pairs.
-   Removed duplicate Skylighting treatment of IBL ambient light.
-   Kept authored PBR textures out of complex-grass atlas interpretation
    when PBR shading is disabled.
-   Preserved complete material construction before interning and neutral
    missing RMAOS handling, addressing the donor's shared-map failure.
-   Limited reflectance output to the combined PBR color shader, avoiding
    the donor's non-PBR compilation regression.
-   Clamped extra sampler address modes and material parameters at binding.
-   Covered all three existing immediate draw routes with optional DevBench
    profiler scopes; production has no new counters or measurement scopes.

## DevBench measurement

`communityshaders.menu` adds `set_pbr_grass_enabled` and
`set_pbr_grass_diagnostics_enabled`, both accepting boolean `enabled`.
They stage runtime values without saving settings. Status reports requested
configuration, parent feature availability and cumulative material-bind
counters. Availability and bind counts are not proof of completed draws.

Diagnostics enable `Grass::PBRRequested` and `Grass::Legacy` scopes through
the existing profiler. Requested shading may include a native fallback;
the separate unavailable/depth counter makes that limitation explicit.
Use a separate diagnostic capture, then turn diagnostics off for matched
whole-frame performance windows. All new measurement machinery is guarded
by `DEVBENCH_BRIDGE_ENABLED`, including state, actions and draw scopes.

## Validation and remaining runtime qualification

Focused tests compile strict flat/VR basic, enhanced and combined PBR
vertex/pixel shaders, including depth, alpha testing and feature-rich
variants. Reflection checks the material buffer, texture/sampler slots,
color/depth outputs and unchanged geometry offsets. Material tests execute
the production conversion against an engine-boundary harness, including
same-diffuse/different-map materials, reverse creation order, missing maps,
ordinary materials and absent True PBR. They do not replace a live engine
interning test. Technique tests execute the production hook against
asynchronous readiness and fallback cases. Persistence tests cover restart,
legacy defaults and input normalization; registry tests cover ownership.

The four basic grass PS and four basic grass VS flat/VR color/depth
permutations are byte-identical to the baseline. Enhanced combined PBR
shaders require runtime comparison, not a byte-identical claim.

Completed local checks:

-   `cmake --build build/astra-pbr-grass --config Release --target pbr_grass_shader_test --parallel 4`:
    passed; 32 strict permutations and reflection checks.
-   `ctest --test-dir build/astra-pbr-grass -C Release -R '^(PBRGrassShader|PBRGrassMaterial|TruePBRSettings|PBRMaterialRegistry)$' --output-on-failure`:
    4/4 passed, including both production-material and technique cases.
-   `tools/verify-shader-refactor.ps1` for `RunGrass.hlsl`, baseline
    `47c7df09a`, PS/VS flat/VR color/depth: all eight identical.
-   Production `/Zs` and `/P` checks using the real DLL compiler response
    files and generated forced header, with DevBench/Tracy undefined:
    Hooks, TruePBR, ShaderCache and MenuDevBenchBridge passed. All new
    diagnostic state, scopes and actions are absent. This is compiler-level
    isolation evidence, not a separately linked production DLL run.
-   Scoped whitespace, line-ending, clang-format, Prettier and diff checks:
    passed. Gersemi was skipped after it reformatted unrelated baseline
    CMake code; that churn was discarded and the new test blocks retained.

The generic DX12 shader-test framework was not run because its NuGet
download failed in this environment. The focused tests use the actual
DX11 grass shader compiler with warnings treated as errors.

Before qualification, run the new DevBench AIO with authored PBR grass and
complete a fresh compilation trace, preserving the existing cache through
the shader-cache controller. Exercise PBR on/off, parent feature on/off,
optional maps, alternate draw order, stereo motion and cell transitions.
Reset time to noon before matched performance windows and settle before
measuring. Generate a trace-derived permutation inventory only after the
compile counters reach zero; a partial grass trace must not replace the
complete tracked runtime inventory. In-game rendering, performance and the
new compilation trace are pending; SE/AE live testing is also pending.
