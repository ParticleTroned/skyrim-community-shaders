# Terrain Variation on meshes

Terrain Variation also reduces repetition on opaque meshes using landscape
textures, including dirt cliffs and ground slabs. **Apply to
Landscape-Textured Meshes** is enabled by default in the Terrain Variation
settings. It takes effect at runtime; save settings to retain the choice.
The existing LOD terrain option is independent. Disable at Boot disables
both terrain and mesh variation.

Eligibility requires a wrapping lighting material and a diffuse path
referenced by a loaded landscape texture record or its seasonal swap.
When landscape records are unavailable, paths under `landscape/` provide a
fallback. An available but empty record list admits no mesh textures.
Directory membership alone does not qualify a texture when records are
available, protecting unrelated cliff-root and other authored mesh maps.
Paths are case-insensitive and texture-relative.
Tree textures under `landscape/trees/` are excluded even if a landscape record
references them. Alpha-tested or blended meshes, trees, decals, skinned and
character materials, LOD objects, projected UV materials and vanilla
multilayer parallax keep their existing sampling. Unsupported or missing
material data also retains ordinary sampling.

Diffuse, normal, packed material and auxiliary maps use one stochastic
lattice chosen from the original mesh UV before parallax. Normal maps use
CSX's normalized normal blending; packed material maps use its unweighted
channel blending. Parallax marching and soft shadows sample the same
shifted height maps. Existing terrain sampling, stereo transforms and
parallax quality controls are retained. The extra texture fetches on eligible
meshes can increase GPU cost; no performance improvement is claimed.

## DevBench

Use `communityshaders.menu` with:

```json
{
    "action": "set_terrain_variation_mesh_enabled",
    "enabled": false
}
```

`enabled` must be a boolean. Enabling requires Terrain Variation to be
loaded. Mutations run through the existing main-thread dispatcher and are
staged in memory until settings are saved. `status` reports
`terrainVariationMeshEnabled` (configured) and
`terrainVariationMeshActive` (loaded and enabled). Supply `expectedBuildId`
when a test requires a specific producer. The action is available in bridge
builds on SE, AE and VR.

## Validation

`TerrainVariationMesh` exercises production record loading, cached texture
lookup, draw eligibility and runtime disable against controlled engine
stand-ins, plus the production texture-path policy. It checks stale-bit
removal, preservation of unrelated descriptor bits, missing data, material
exclusions, registered custom and seasonal paths, and tree-folder
protection. Record reloads check that cached positive and negative decisions
are invalidated, including transitions between unavailable records, empty
records and populated records. It also checks rejection of unregistered
cliff-root textures and directory fallback when records are unavailable.
It does not validate the engine ABI or GPU output.

The initial port had source and preprocessing checks only. The texture
eligibility correction compiled and passed `TerrainVariationMesh` with MSVC
(`/std:c++latest /EHsc /W4 /WX /MD /UNDEBUG`). Shader bytecode and SE/AE/VR
runtime rendering remain unvalidated. Visual acceptance should
compare eligible ordinary, complex and PBR meshes with mesh variation
on/off, including parallax and shadows in both VR eyes; excluded draws and
the existing landscape path should retain their appearance. Measure GPU
cost separately in the same scene.
