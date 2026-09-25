# Terrain Variation on meshes

Terrain Variation also reduces repetition on opaque meshes using landscape
textures, including dirt cliffs and ground slabs. **Apply to
Landscape-Textured Meshes** is enabled by default in the Terrain Variation
settings. It takes effect at runtime; save settings to retain the choice.
The existing LOD terrain option is independent. Disable at Boot disables
both terrain and mesh variation.

Eligibility requires a wrapping lighting material and either a diffuse path
under `landscape/` or a path referenced by a loaded landscape texture record
or its seasonal swap. Paths are case-insensitive and texture-relative.
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

`TerrainVariationMesh` exercises the production draw eligibility and runtime
disable functions against controlled engine stand-ins, plus the production
texture-path policy. It checks stale-bit removal, preservation of unrelated
descriptor bits, missing data, material exclusions, registered custom paths,
and tree-folder protection. It does not validate the engine ABI or GPU output.

The initial port has source and preprocessing checks only. C++ fixtures and
shader bytecode were not compiled, and SE/AE/VR runtime rendering was not
exercised. Visual acceptance should compare eligible ordinary, complex and
PBR meshes with mesh variation on/off, including parallax and shadows in both
VR eyes; excluded draws and the existing landscape path should retain their
appearance. Measure GPU cost separately in the same scene.
