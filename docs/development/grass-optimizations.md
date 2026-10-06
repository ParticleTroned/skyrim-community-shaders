# Grass optimizations

The feature combines compatible native grass groups across resident cells,
then uses GPU instance tests and indirect draws. It retains the native
grass shader geometry contract, placement, native visible-group fades,
wind, collision and lighting. The master switch defaults to on.

## Implementation basis

The bucket, instance compaction, density reduction and middle/far mesh LOD
design follows [upstream PR2688](https://github.com/community-shaders/skyrim-community-shaders/pull/2688)
at [`dc11b1af40`](https://github.com/community-shaders/skyrim-community-shaders/commit/dc11b1af4082ff3200e0518cf463a8b07dc735ca)
by Anthony (DwemerEngineer). Ported contributions retain his verified
Git identity in commit co-author trailers.
The implementation adapts those techniques to CSX's native grass shader
and collision contracts. Open Shaders' separate Wind feature and its
replacement grass bending formulas are not included. CSX retains its
existing wind and collision behavior.

The stereo draw and view-facing normal contracts follow Open Shaders
[PR630](https://github.com/alandtse/open-shaders/pull/630).
[PR648](https://github.com/alandtse/open-shaders/pull/648) informs shared
per-instance work with separate eye outcomes. The lifecycle, runtime
switching and projection contracts were checked against Open Shaders
[PR815](https://github.com/alandtse/open-shaders/pull/815),
[PR816](https://github.com/alandtse/open-shaders/pull/816),
[PR822](https://github.com/alandtse/open-shaders/pull/822),
[PR817](https://github.com/alandtse/open-shaders/pull/817),
[PR752](https://github.com/alandtse/open-shaders/pull/752).
These are reviewed integration references, not wholesale ports. CSX uses
resident snapshots with retained material ownership, native
per-geometry matrices, explicit eye draws and bounded output capacity.
The Open Shaders VR placement change in PR817 assumes world-space instance
positions. CSX subtracts the representative origin in its batch offset,
so the existing per-geometry matrix remains the matching transform.

Open Shaders proposals
[PR653](https://github.com/alandtse/open-shaders/pull/653),
[PR812](https://github.com/alandtse/open-shaders/pull/812) and
[PR824](https://github.com/alandtse/open-shaders/pull/824) informed shared
padded CPU/GPU frusta, depth-source independence and the reusable SPD
builder. Those proposals were not merged at the time of this adaptation.
CSX uses the currently bound main depth target and a grass-owned max-depth
chain; SSR integration and shared scene-culling resources are not added.
The AMD SPD shader retains its copyright and license notice.

Universal CommonLib grass properties place their grass-specific fields
after a 0x160-byte lighting property on SE/AE and a 0x178-byte one on VR.
The native-tail accessor selects the matching layout before reading fades,
wind or light data. Reading the desktop fields on VR produces an empty
fade array and rejects otherwise valid visible groups.

## Controls

The master switch, combining cells, frustum culling, density reduction
and grass Hi-Z default to on, matching upstream. Mesh LOD defaults to off.
Existing saved choices are preserved; restoring defaults applies these
values, with grass Hi-Z disabled when scene Hi-Z is selected.
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
| Collision distance       | 2048    | Fade optimized grass collision to zero at this distance; zero disables collision.               |

Collision distance accepts 0–20,480 units, matching screen-space culling
controls. It requires Grass Collision and retains that feature's local
collision texture coverage. Increasing the distance changes the fade
radius within that coverage; it does not enlarge the texture. Native
fallback draws retain the existing 2,048-unit collision fade. The setting
applies in SE, AE and VR without recompiling shaders.

Projected quality size uses the model radius and current render height,
separately from the larger wind/collision bounds used for visibility.
Simpler shading retains the diffuse/complex-grass atlas and alpha rules;
it skips detail shadows, clustered lights and complex normal/specular
work. The PBR grass implementation from PR106 is now in the base. Its
simplified path retains authored diffuse/alpha layout, material scalar
values and coarse shadows, while skipping normal/RMAOS/subsurface texture
samples, detailed shadows, clustered lights and specular lobes. Full
shading remains the default.

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
multisampling, unsupported depth comparisons and invalid viewport
dimensions retain grass. The engine's equal-depth grass pass is accepted,
and a near-one viewport depth maximum adds its mapping difference to the
occlusion bias so visible grass stays conservative. It does
not consume scene Hi-Z resources or stale prepass snapshots. Conservative
max-depth reduction retains empty texels, padded edges and eye seams.

The reusable depth-pyramid builder uses AMD's single-pass downsampler
after the conservative source reduction. Devices below feature level 11.1,
unavailable SPD shaders and unsupported chain lengths use the existing
per-mip path. Both paths stop before packed stereo eyes can mix.
Grass still uses its own depth source and capture time; scene Hi-Z and
SSR do not consume this pyramid.

Grass Hi-Z defaults to on, matching upstream. A same-scene A/B determines
whether avoided drawing costs outweigh constructing and querying the
depth pyramid; turn it off where it is slower. Rejection counts alone do
not establish a performance benefit.

## Compatibility and failure behavior

Cross-cell buckets accept shared mesh buffers or the same recorded model
path with matching vertex/index buffer sizes and vertex counts. Both paths
also require the same material or equivalent non-PBR materials with the
same base texture, plus matching vertex layout, bounds, shader flags,
lights, wave period and render distance. PBR materials retain exact
identity because the native material comparison does not cover their
extended fields. They are limited to the
main scene target; other targets use
the native renderer. Unresolved model paths retain full meshes. Native
shape-level scene admission remains in place. Cached groups bypass native
per-group visibility tests while retaining native fades and complete
fallback submission. CPU group bounds and GPU instance tests share padded
per-eye frusta, preferring verified unjittered camera matrices. Entire
off-screen groups skip GPU work; unknown bounds remain candidates.
A geometry callback issues the combined draw before the native group
loop; successful buckets suppress member draws. Native submission remains
available for shader or resource failures. Grass outside loaded cells
cannot be created by these controls.

Current and previous native wind scalars are computed once per surviving
instance and shared by both eyes. Vertex shaders retain native bending
and per-vertex weights. Collision bounds expand only where the configured
collision distance can reach the grass. View-facing normal orientation
handles mirrored grass without changing authored spherical normals.

Snapshots retain GPU buffers, or copy pending CPU instance records, and
shader properties without later dereferencing captured shapes.
Destruction invalidates a generation
token, preventing address reuse from admitting old geometry. Generation,
group addition/removal and changed material contracts retire snapshots.
Unchanged membership retains CPU buckets and GPU records across frames;
changed buckets reuse compatible storage and upload their changed tail.
A source holds at most 262,144 instances and
residency holds at most 4,096 sources and 128 MiB of CPU record snapshots.
Oversized or unsupported work
retains native rendering. Oversized slice tables and unsupported native
vertex layouts reject only their bucket; they do not latch a session-wide
renderer failure.

All preparation precedes the first indirect draw. A failed bucket uses
native rendering for the entire pass; a completed bucket suppresses its
remaining native member draws. Graphics bindings and constant-buffer
windows are restored with RAII. SE, AE and VR use their respective native
relocations and VR virtual slot offsets.

The VR 1.4.15 draw call is at relocation `100847 + 0x75B`, calling
relocation `75479`. The `0x663` desktop offset lies inside another VR
instruction. Model tracking uses `15204 + 0x2F5`, `15205 + 0x62B` and
`15206 + 0x25C`; all three call the same model loader. Installation checks
the call opcode and destination before modifying code and retains native
rendering on a mismatch. Submitted counts must equal captured logical
counts; the VR native draw function expands stereo instances internally.

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
`reusedRecordBuckets`, `uploadedRecordBuckets` and `uploadedRecordBytes`
show whether persistent bucket records actually avoid per-frame copies.
Resident record buffers are capped at 128 MiB, including at most 16 MiB
of temporarily invisible buckets. Destroyed shapes cannot retain a dormant
bucket. `uncachedRecordBuckets` counts draws that use the bounded shared
scratch buffer instead.

`persistentBucketFrames`, `bucketRebuilds`, `cachedSources` and
`nativeVisibilityBypassed` identify CPU reuse. `coarseRejectedSlices` and
`coarseRejectedInstances` count logical instances excluded before the
per-eye GPU counters. Combine these separately when assessing culling.
Per-frame native wind timing does not invalidate persistent sources or
bucket compatibility. Current and previous wind values still come from
the native draw constants on each dispatch. Wave-period, light-list and
light-mask changes retain their compatibility checks; shape generation,
instance groups, meshes and materials retain their lifetime checks.
`hiZOutcomes` separates eligible eye instances with no pyramid, failed
near-plane projection, unusable footprints, invalid or uncovered depth,
and depth that does not hide the nearest bound. `wideFootprint` and
`sampledCells` measure depth lookup cost; a wide footprint is an
additional cost marker, not a rejection reason. The mutually exclusive
outcome counters plus `hiZRejected` account for eligible instances.
Readback is asynchronous; `gpuSamples` and
`droppedGpuSamples` expose coverage. `hiZBatches` and
`hiZDepthFallbacks` distinguish actual depth testing from unavailable
depth. `hiZBuildFailures` records which source, depth state or viewport
guard rejected the pyramid; `hiZFailureState` shows the observed state at
the failed depth and viewport guards. Counters arrive several frames after
their draws.
With diagnostics enabled, `captureAttempts`, `capturedSources` and
`admittedSources` show progress from native visibility into frame buckets.
`captureRejections` separates invalid geometry, sources, fades and buffers
from frame capacity, stale or destroyed captures. `drawAttempts` and
`drawRejections` identify shader, depth target, constant-buffer, viewport
and capacity fallbacks, including sources regenerated after capture,
before any replacement draw. These CPU counters
are cumulative and are compiled out of production. `sameModelPeers`,
`sameModelCompatible`, `sameModelMismatches` and `unidentifiedModels`
show why sources with a recorded model did or did not combine.

Profiler scopes separate preparation/culling, Hi-Z construction and
indirect drawing. Measure performance with diagnostic counters off;
collect a separate diagnostic window in the same scene. Keep time,
headset pose, settings and focus fixed, and compare grass Hi-Z on/off
under Advanced and Legacy separately.

Counter buffers, readbacks, diagnostic shaders and DevBench actions are
compiled only with `DEVBENCH_BRIDGE_ENABLED`. Production shader bytecode
has no diagnostic counter UAV, and grass dispatch saves and binds only
its three rendering UAV slots. User controls remain available in normal
builds. The upstream defaults are enabled; in-game quality, performance
and runtime compatibility require separate validation.

## Validation

The universal Release DLL builds with DevBench enabled and Tracy disabled.
Focused policy tests cover finite settings, threshold ordering, native
descriptor stride, stereo counts and scene Hi-Z exclusion. Shader tests
compile 16 vertex and eight pixel batching variants plus 64 PBR/native
and combined permutations with warnings treated as errors. Material and
native geometry/register reflection checks cover flat and VR paths.
Specialized simplified/full PBR shaders verify removal of detail textures
and clustered lighting resources. Flat/VR WARP dispatches verify packed-record preservation, sparse
fades, frustum and occlusion outcomes, LOD selection, distance/fade
cutoffs, simple shading flags, invalid/near-plane bounds, mono-eye
submission, odd depth dimensions and eye seams.

A completed VR Trace capture on 2026-10-05 finished 3,635 tasks with zero
failures. Its 3,580 distinct managed compilations include all eight grass
entries with `PBR_GRASS=1` and `GRASS_OPTIMIZATIONS`. The producing source
was `0d0c8855e` with local hook fixes and Build ID `866a1d02976c`; the compact runtime fixture
preserves the full producer identity and log hash. The updated 3,605-entry
inventory matches all captured release macro identities and retains 25
additional lighting variants from earlier captures. Standalone grass
and scene Hi-Z shaders compile separately, outside the managed cache.
This proves compilation and macro coverage, not cache-pack reuse or
in-game quality/performance. Native grass hooks installed successfully;
the draw and model-hook availability warnings are absent.

The cache builder applies `PBR_GRASS=1` and `GRASS_OPTIMIZATIONS` only to
`RunGrass.hlsl` in both shipped and Patka profiles, matching the bundled
native shader factory for either runtime. It preserves captured entry
identities and does not leak grass flags to other families. The old
capture retains coverage beyond the latest modlist. Grass shader ABI
`native-cell-buckets-v6` invalidates previous optimization bytecode
because instance extras now carry current/previous wind and the culling
shader uses shared frustum constants.

`tools/verify-shader-refactor.ps1` produces identical DXBC for 12
feature-absent flat/VR vertex permutations against landed commit
`0d0c8855e`. Pixel shaders intentionally
change normal orientation. The checker materializes feature include roots
from each revision. Production syntax and preprocessing passed for eight
translation units using the actual compiler options and forced header
with developer defines removed. Diagnostic resources and actions are
absent; shader reflection verifies that flat/VR production culling has
no diagnostic counter UAV. This is not a separately linked production
DLL test or a production runtime measurement.

In-game SE/AE validation, streaming/recovery and visual wind equivalence
remain untested. No middle/far LOD survivors were observed; authored LOD
assets are required to validate their appearance and performance.

Read-only inspection of loaded Skyrim VR 1.4.15 instructions and its
active address-library mapping verified the four native hook callsites
listed above and their destinations. The hook-fix universal Release DLL,
`GrassOptimizationPolicy` test and developer AIO archive verification
passed. The completed replacement-DLL startup capture confirms hook
activation; streaming and LOD quality remain separate checks.

## Latest VR comparison

The 2026-10-05 Western Watchtower comparison used Advanced scene culling,
the Play Game profile and clear weather. Every condition reset the
observed clock to noon, settled at least five seconds and measured eight
seconds with grass diagnostics and the CSX profiler off. Each core
condition ran twice, with the second sequence in reverse order. Player
position matched; headset pose was not recorded.

| Grass optimization condition | CPU ms | fpsVR reported FPS | FPS vs off |
| ---------------------------- | ------ | ------------------ | ---------- |
| Off                          | 7.05   | 90.8               | Baseline   |
| Full density, Hi-Z off       | 8.05   | 98.4               | +8.4%      |
| Full density, Hi-Z on        | 7.93   | 97.1               | +6.9%      |
| Density reduction, Hi-Z off  | 8.27   | 108.6              | +19.6%     |
| Density reduction, Hi-Z on   | 8.37   | 106.8              | +17.6%     |

Full-density batching increased reported FPS while costing approximately
1 ms more CPU time. Density reduction changes appearance. Grass Hi-Z
reduced reported FPS by 1.4% at full density and 1.7% with density
reduction; it is not a win in this scene. fpsVR GPU data were invalid,
and independent engine throughput was not captured. Separate CSX GPU
captures measured grass preparation/culling at 0.371 ms without Hi-Z
and 0.399 ms with it, plus 0.060 ms to build the pyramid. These are pass
timings, not whole-frame GPU times; the 120 Hz frame budget is 8.33 ms.

The separate wind-active ten-second diagnostic window recorded 1,127
persistent frames, 15,778 cached-source hits/native visibility bypasses,
zero bucket rebuilds, zero uploads and zero native fallbacks. No old/new
wind-guard timing A/B was measured. Grass Hi-Z rejected 5.81% of eligible
eye instances; most candidates failed its depth comparison. No depth
build failures or native fallbacks occurred. Supplementary controls
changed their intended outcomes, but optional mid/far LODs had zero
survivors and their appearance/performance remain unvalidated.

The measured source was `0d0c8855ed3b1b223ff1c092b71d7914fcb05643`
with local follow-up changes, using a universal Release DLL with
DevBench ON and Tracy OFF. Its producer Build ID was
`d89d9f1968ffa5852c869adb5d3683a42f46c18693ef05172e7f482a42964e8b`.
The later production-only UAV-slot cleanup was not in this measurement.
Raw captures, settings receipts and profiler comparisons remain local.
