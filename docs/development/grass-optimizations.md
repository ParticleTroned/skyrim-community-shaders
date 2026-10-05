# Grass optimizations

The feature combines compatible native grass groups across resident cells,
then uses GPU instance tests and indirect draws. It retains the native
grass shader geometry contract, placement, native visible-group fades,
wind, collision and lighting. The master switch defaults to off.

## Implementation basis

The bucket, instance compaction, density reduction and middle/far mesh LOD
design follows [upstream grass optimizations](https://github.com/community-shaders/skyrim-community-shaders/commit/dc11b1af4082ff3200e0518cf463a8b07dc735ca)
by Anthony (DwemerEngineer). Ported contributions retain his verified
Git identity in commit co-author trailers.
The implementation adapts those techniques to CSX's native grass shader
and collision contracts. Open Shaders' separate Wind feature and its
replacement grass bending formulas are not included. CSX retains its
existing wind and collision behavior.

The lifecycle and VR contracts were checked against Open Shaders
[PR815](https://github.com/alandtse/open-shaders/pull/815),
[PR822](https://github.com/alandtse/open-shaders/pull/822),
[PR817](https://github.com/alandtse/open-shaders/pull/817),
[PR752](https://github.com/alandtse/open-shaders/pull/752) and
[PR812](https://github.com/alandtse/open-shaders/pull/812).
These are reviewed integration references, not wholesale ports. CSX uses
frame-local GPU snapshots with retained material ownership, native
per-geometry matrices, explicit eye draws and bounded output capacity.

## Controls

Combining cells and frustum culling default to on inside the disabled
master switch. Density reduction, mesh LOD and grass Hi-Z default to off.
Turning density reduction off disables projected-size thinning. Frustum,
Hi-Z, distance, fade and mesh cost controls remain independent.

Density controls use projected grass size: the default smallest size is
2 pixels, full-density size is 16 pixels and minimum density is 3%.
Below the smallest size, grass is removed. Between those thresholds a
stable instance hash thins the population with a smooth fade band. VR uses
the larger size from both eyes for matching density and LOD decisions.

Additional upstream controls are available without recompiling shaders:

| Control                  | Default | Behavior                                                                                        |
| ------------------------ | ------- | ----------------------------------------------------------------------------------------------- |
| Mesh cost bias           | 0.4     | Reduce expensive meshes' distant reach and increase their density size thresholds.              |
| Cost bias start distance | 6000    | Begin ramping in the mesh cost adjustment beyond this distance.                                 |
| Render distance override | 0       | Use the game's grass distance at zero; otherwise set a distance within resident cells.          |
| Edge fade start          | 0.85    | Begin fading at this fraction of the effective render distance.                                 |
| Invisible fade cutoff    | 0       | Skip grass whose combined fade is at or below the threshold.                                    |
| Simple shading size      | 0       | Keep full shading at zero; otherwise simplify lighting on grass smaller than this pixel radius. |

Projected quality size uses the model radius and current render height,
separately from the larger wind/collision bounds used for visibility.
Simpler shading retains the diffuse/complex-grass atlas and alpha rules;
it skips detail shadows, clustered lights and complex normal/specular
work. CSX's independent PBR grass implementation remains in its own PR.

Optional mesh LODs use the upstream asset convention:

-   `meshes/LOD/Grass/<source-stem>_LOD0.nif` for middle grass.
-   `meshes/LOD/Grass/<source-stem>_LOD1.nif` for far grass.

The default middle/far thresholds are 8/4 pixels, with a 3-pixel dithered
transition band. Density and LOD use independent hashes. Assets must use
the source vertex layout, diffuse texture and material UV contract, one
mesh and identity node transforms. Missing or
incompatible assets retain the full mesh; an available middle mesh can
also serve as the far fallback. The installer does not supply LOD assets.

## Grass Hi-Z A/B

Grass Hi-Z is an independent runtime switch. It can be compared on/off
without changing shaders or restarting Skyrim. It works with Advanced
and Legacy scene culling. Its activation is rejected while scene Hi-Z is
selected; selecting scene Hi-Z later automatically makes grass Hi-Z
inactive, retaining the requested setting for a later compatible mode.

The grass depth pyramid uses only the currently bound main depth target.
When another feature redirects the depth SRV, a private compatible view
of the bound main texture avoids a feature dependency. Unsupported views,
multisampling, nonstandard depth comparisons and invalid viewport
dimensions retain grass. It does
not consume scene Hi-Z resources or stale prepass snapshots. Conservative
max-depth reduction retains empty texels, padded edges and eye seams.

Hi-Z stays opt-in until a same-scene A/B shows that avoided drawing costs
more than constructing and querying the depth pyramid. Rejection counts
alone do not establish a performance benefit.

## Compatibility and failure behavior

Cross-cell buckets require matching mesh buffers, material identity,
vertex layout, bounds, shader flags, lights, wind timing and render
distance. They are limited to the main scene target; other targets use
the native renderer. Unresolved model paths retain full meshes. Native
shape-level scene admission and group visibility checks remain available
for fallback. Snapshots include resident groups excluded by native group
culling. A geometry callback issues the combined draw before the native
group loop; successful buckets suppress member draws. CSX retains native
CPU submission work instead of the donor queue-suppression patches. It
cannot revive shapes rejected before the visibility callback or create
grass outside loaded cells.

Snapshots retain GPU buffers, or copy pending CPU instance records, and
shader properties without later dereferencing captured shapes.
Destruction invalidates a generation
token, preventing address reuse from admitting old geometry. Stale-frame
captures are discarded. A source holds at most 262,144 instances and
frame capture holds at most 4,096 sources. Oversized or unsupported work
retains native rendering.

All preparation precedes the first indirect draw. A failed bucket uses
native rendering for the entire pass; a completed bucket suppresses its
remaining native member draws. Graphics bindings and constant-buffer
windows are restored with RAII. SE, AE and VR use their respective native
relocations and VR virtual slot offsets.

## DevBench

The `communityshaders.menu` action registry includes:

-   `set_grass_optimizations_enabled` with boolean `enabled`.
-   `set_grass_optimizations_settings` with a nonempty partial
    `grassOptimizations` object using the saved setting field names.
-   `set_grass_hiz_enabled` with boolean `enabled`.
-   `set_grass_optimizations_diagnostics_enabled` with boolean `enabled`.

Updates validate all fields atomically, including finite numeric ranges
and ordered thresholds. They apply at the next grass frame and do not
save settings. The Hi-Z action does not enable the master grass switch.

`status.grassOptimizations` reports requested settings, scene Hi-Z
availability, native fallback counts, combined sources and instances,
and cumulative GPU eye-instance outcomes. GPU outcomes distinguish
frustum, density, distance, fade and Hi-Z rejection, full/middle/far
survivors, and invalid bounds retained. VR mono passes do not duplicate
eye geometry.
Readback is asynchronous; `gpuSamples` and
`droppedGpuSamples` expose coverage. `hiZBatches` and
`hiZDepthFallbacks` distinguish actual depth testing from unavailable
depth. Counters arrive several frames after their draws.

Profiler scopes separate preparation/culling, Hi-Z construction and
indirect drawing. Measure performance with diagnostic counters off;
collect a separate diagnostic window in the same scene. Keep time,
headset pose, settings and focus fixed, and compare grass Hi-Z on/off
under Advanced and Legacy separately.

Counter buffers, readbacks, diagnostic shaders and DevBench actions are
compiled only with `DEVBENCH_BRIDGE_ENABLED`. Production shader bytecode
has no diagnostic counter UAV. User controls remain available in normal
builds. In-game quality, performance and runtime compatibility testing
remain required before enabling the feature by default.

## Validation

The universal Release DLL builds with DevBench enabled and Tracy disabled.
Focused policy tests cover finite settings, threshold ordering, native
descriptor stride, stereo counts and scene Hi-Z exclusion. Shader tests
compile 16 vertex and eight pixel variants with warnings treated as
errors. Flat/VR WARP dispatches verify packed-record preservation, sparse
fades, frustum and occlusion outcomes, LOD selection, distance/fade
cutoffs, simple shading flags, invalid/near-plane bounds, mono-eye
submission, odd depth dimensions and eye seams.

A VR Trace-level startup capture on 2026-10-05 reached zero remaining
tasks with no logged compilation failures. Its 3,605 distinct managed
entries match the previous VR inventory; the eight grass entries add
`GRASS_OPTIMIZATIONS`. The producing source was `516d643b9` and Build ID
`a885cbc559a256b533a96bbec2a079f1478cb9396ca68cb1906f3fd4443fd74f`.
The maintained VR inventory preserves that effective configuration.
Standalone grass-culling and Hi-Z shaders compiled separately and are
outside the managed inventory. The grass-culling shader emitted a
duplicate `VR` macro warning. PBR grass was absent from this build, so
combined PBR/optimization cache coverage remains unverified. Skyrim
exited after the queue reached zero; final live API verification was
unavailable. This capture establishes compilation coverage, not runtime
quality or performance.

`tools/verify-shader-refactor.ps1` produces identical DXBC against
`88f1b1a26` for feature-absent flat/VR color/depth vertex and pixel
permutations. Production syntax and preprocessing use the actual compiler
options and forced header with developer defines removed; diagnostic
resources and actions are absent. This is not a separately linked
production DLL test. In-game SE/AE/VR quality, streaming, recovery and
performance tests have not run; no measured speedup is claimed.
