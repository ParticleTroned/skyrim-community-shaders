# NIF Soft Mesh Transitions — Implementation Handover

## Status

- Design and implementation handover only.
- No implementation from this document is currently assumed to exist.
- Evaluated against the `cs-1.7-PL-SE` worktree on 2026-08-03.
- The design intentionally avoids NIF edits and new engine-address hooks.
- The initial implementation should target the normal world `BSLightingShader` path. Reflection, cubemap, shadow, water, effect, and other specialised passes are explicitly outside the MVP.
- No build or runtime tests were performed while preparing this handover.

## Executive summary

Implement a Community Shaders feature tentatively named **Soft Mesh Transitions**. It should soften the intersection between a transparent transition layer and an opaque surface behind it.

The principal target is a static NIF containing:

- one or more opaque, depth-writing `BSTriShape` receivers; and
- an overlapping `BSTriShape` transition layer using ordinary alpha blending, Z testing, and no Z writes.

Examples include rock skirts, mountain trim, ground overlays, moss or snow skirts, and similar terrain-like assets. A single NIF or `BSFadeNode` can contain several `BSTriShape` blocks; these are independent renderer draw calls even though artists commonly describe the complete NIF as one mesh.

The MVP should:

1. Inspect a lighting geometry at runtime through an existing CS lighting setup hook.
2. Walk to its owning static root and inspect sibling geometries.
3. Conservatively classify only standard alpha-blended, Z-tested, non-Z-writing lighting shapes with a plausible opaque sibling.
4. Set a per-draw CS descriptor bit for classified transition layers.
5. Sample CS's existing post-Z-prepass depth texture in the lighting pixel shader.
6. Multiply the shape's already-computed alpha by a narrow depth-gap fade.
7. Preserve the original blend, depth, sorting, alpha-test, and material states.

The inexpensive path adds no geometry pass. Its GPU cost is one depth load and a small amount of arithmetic only on affected pixels. It does not prove that the sampled opaque depth belongs to the same NIF. A later robust mode can add receiver depth and group IDs if real-world testing shows ownership mistakes.

## Problem statement

Terrain-like NIFs frequently combine visually different materials using separate shapes. A common arrangement is:

- an opaque rock or ground body;
- an alpha-blended skirt or overlay around the material boundary; and
- disabled Z writes on the skirt so the opaque surface remains the depth owner.

Conventional alpha blending can still reveal a hard intersection because the authored alpha does not account for the actual view-space distance between the layer and the opaque receiver. The seam becomes especially visible with different albedo, normal, roughness, wetness, or lighting responses.

The desired result is analogous to a soft-particle intersection fade, applied only to carefully selected static lighting geometry.

## Goals

- Require only Community Shaders; do not require an additional SKSE plugin.
- Do not require editing or repackaging NIF files.
- Detect high-confidence transition layers conservatively at runtime.
- Permit CS-side allow and deny overrides keyed to an asset and shape.
- Preserve authored texture alpha and vertex alpha.
- Preserve original renderer state, particularly Z writes, Z testing, sorting, and blend functions.
- Work with placed instances of replaced or modded meshes.
- Avoid full-screen processing and additional geometry passes in the MVP.
- Compose predictably with Extended Translucency and Terrain Blending.
- Keep a path open for a stricter receiver-ownership implementation later.

## Non-goals for the MVP

- Blending two opaque, depth-writing shapes into one another.
- Blending two transparent or two no-Z-write shapes.
- Reconstructing a hidden second surface from the normal scene depth.
- Resampling the actual material of the opaque receiver.
- Physically blending albedo, normal, roughness, metallic, wetness, or subsurface parameters from two materials.
- Changing geometry, silhouettes, collision, decals, or shadow geometry.
- Automatically processing glass, foliage, hair, skin, particles, water, refraction, decals, effects, or animated geometry.
- Applying to landscape layer transitions inside a `LANDSCAPE` draw. Skyrim LAND already has material blend weights; that is a separate height/weight-blending problem.
- Guaranteeing correct object ownership from a single scene-depth sample.

## Important terminology

### NIF root versus renderer mesh

One NIF root can contain several sibling `BSTriShape` blocks. Each shape has its own shader property, material, alpha property, render pass, and renderer state. “Part of the same NIF” is useful CPU-side grouping information, but it has no automatic meaning in a pixel shader.

### Source or transition layer

The alpha-blended, Z-tested, non-Z-writing shape whose opacity CS modifies.

### Receiver

An opaque or alpha-tested, Z-tested, Z-writing sibling, or the underlying LAND surface already represented in the scene depth.

### Soft intersection fade

A multiplier derived from the linear depth gap between the source fragment and the nearest opaque depth behind it. The source fades out as the gap approaches zero, allowing the receiver to show through at the intersection.

## Existing CS facilities to reuse

The implementation should reuse existing mechanisms instead of adding new low-level engine hooks.

### Lighting geometry setup

Relevant existing paths include:

- `src/Hooks.cpp`, `LightingExtensions::BSLightingShader_SetupGeometry`
- `src/Features/ExtendedTranslucency.cpp`, `ExtendedTranslucency::BSLightingShader_SetupGeometry`
- `src/Features/Skylighting.cpp`, which already inspects shader flags, alpha testing, parents, `BSFadeNode`, and BSX data

Use the shared lighting setup chain if practical. Do not add a new relocation merely to classify geometry.

The soft-transition descriptor must be cleared for every lighting geometry before optionally being set. A stale per-draw bit would affect unrelated meshes.

### Scene depth

CS already binds a scene-depth SRV to pixel-shader slot `t17`:

- `src/State.cpp`, `State::UpdateSharedData`
- `src/Utils/D3D.cpp`, `Util::GetCurrentSceneDepthSRV`
- `package/Shaders/Common/SharedData.hlsli`, `DepthTexture`, `GetDepth`, and `GetScreenDepth`

The MVP should reuse this resource and the existing screen/depth helpers. Do not introduce another depth copy until compatibility testing demonstrates a need.

### Scenegraph traversal

CommonLib exposes the required data and traversal helpers:

- `RE::NiAVObject::parent`
- `RE::BSVisit::TraverseScenegraphGeometries`
- `RE::NiAlphaProperty::GetAlphaBlending`
- `RE::NiAlphaProperty::GetAlphaTesting`
- `RE::NiAlphaProperty::GetSrcBlendMode`
- `RE::NiAlphaProperty::GetDestBlendMode`
- `RE::BSShaderProperty::flags`
- `RE::NiAVObject::GetUserData`
- `RE::TESModel::GetModel`

### Per-draw permutation data

CS already carries per-draw information through `State::PermutationCB` and `ExtraShaderDescriptor`:

- `src/State.h`
- `package/Shaders/Common/Permutation.hlsli`

Reserve a new matching bit in both C++ and HLSL, for example bit 8:

```cpp
SoftMeshTransition = 1u << 8
```

Do not reuse bit 6. HLSL already names it `IsFemale`, even though the current C++ enum does not mirror it. Do not overlap Extended Translucency's bits 6–8 in `ExtraFeatureDescriptor`; use `ExtraShaderDescriptor` instead.

## Proposed feature structure

Tentative feature identity:

- Display name: `Soft Mesh Transitions`
- Short name: `SoftMeshTransitions`
- Category: `Landscape & Textures` or `Materials`
- Shader define: `SOFT_MESH_TRANSITIONS`
- Shader type: Lighting only

Suggested files:

```text
src/Features/SoftMeshTransitions.h
src/Features/SoftMeshTransitions.cpp
features/Soft Mesh Transitions/Shaders/SoftMeshTransitions/SoftMeshTransitions.hlsli
docs/development/nif-soft-mesh-transitions-handover.md
```

Expected registrations and integration edits:

```text
src/Globals.h
src/Globals.cpp
src/Feature.cpp
src/FeatureBuffer.cpp
src/State.h
src/Hooks.cpp
package/Shaders/Common/Permutation.hlsli
package/Shaders/Common/SharedData.hlsli
package/Shaders/Lighting.hlsl
package/SKSE/Plugins/CommunityShaders/Translations/en.json
```

The build system normally discovers feature shader folders automatically, but confirm the existing feature-template conventions before adding explicit CMake entries.

## Proposed settings

### GPU settings

Use a 16-byte-aligned structure:

```cpp
struct alignas(16) Settings
{
    std::uint32_t Enabled = 0;
    float BlendWidth = 12.0f;
    float DepthBias = 0.25f;
    float MaximumGap = 64.0f;
};
```

The numeric defaults are starting values, not validated recommendations. They must be tuned in game. `BlendWidth` and `DepthBias` should be interpreted in the same linear view-depth units returned by `SharedData::GetScreenDepth`.

### CPU-only settings

Keep variable-size and classifier settings outside the GPU structure:

```cpp
enum class DetectionMode : std::uint32_t
{
    Disabled,
    AllowListOnly,
    StrictAutomatic
};

DetectionMode Mode = DetectionMode::AllowListOnly;
bool RequireOpaqueSibling = true;
bool RequireOverlappingBounds = true;
float BoundsExpansion = 32.0f;
bool DeveloperLogging = false;
```

The safest first release is disabled or allow-list-only. Strict automatic mode should not become the default until a representative group of mesh packs has been inspected.

### Illustrative CS-side override format

The exact JSON shape should follow existing CS settings and override conventions. The following is illustrative:

```json
{
  "SoftMeshTransitions": {
    "Enabled": true,
    "DetectionMode": "StrictAutomatic",
    "BlendWidth": 12.0,
    "DepthBias": 0.25,
    "Allow": [
      {
        "Model": "meshes/landscape/mountains/mountaintrim01wet.nif",
        "NodePath": "MountainTrim01Wet/RockSkirt01Wet"
      }
    ],
    "Deny": [
      {
        "Model": "meshes/example/glasswindow.nif",
        "NodePath": "WindowRoot/Glass"
      }
    ]
  }
}
```

An allow match should override automatic rejection only for safe render-state requirements. It must not force unsupported blend functions, disabled Z testing, water/effect shaders, or missing depth resources. A deny match always wins.

## Runtime asset and shape identity

The allow/deny key must be stable across placed instances and must not depend solely on a raw pointer.

Preferred identity components:

1. Normalised model path from the owning reference's base object when it exposes `TESModel`.
2. Stable child path from the owning NIF root to the geometry, including child indices when names are duplicated.
3. Geometry name as a readable diagnostic field.
4. Optional material or texture signature as a validation field, not the primary key.

Suggested conceptual key:

```text
lowercase(normalised model path) | root-relative node path | geometry name
```

Do not use only the material hash. Multiple placed cells and unrelated shapes can share materials, and systems such as True PBR can replace materials at runtime.

### Finding the owning asset

Starting from `pass->geometry`:

1. Walk `parent` pointers to the nearest owning `BSFadeNode` or reference root, not the global world root.
2. Prefer a root carrying `TESObjectREFR` user data.
3. Obtain its base object and attempt `As<RE::TESModel>()`.
4. Use `TESModel::GetModel()` when available.
5. Fall back to the root-relative node path plus geometry/material diagnostics when a model path cannot be resolved.

Do not attach persistent extra data to the scenegraph in the MVP. Runtime scenegraph mutation introduces cloning, threading, and lifetime questions that are unnecessary for the first implementation.

## Conservative classifier

Classification should have two stages: source eligibility and receiver plausibility.

### Required source conditions

All conditions below should be true unless an item is explicitly listed as optional:

| Check | Requirement | Rationale |
|---|---|---|
| Render pass | Non-null pass and geometry | Safety |
| Shader | `BSLightingShader`/`BSLightingShaderProperty` | Limit the shader implementation |
| World pass | Main world rendering | Avoid menus and specialised passes |
| Reflection | Not an active reflection/cubemap pass | Depth resource and projection may differ |
| Skinning | No skin instance and no `kSkinned` | Exclude actors and animated attachments |
| Alpha | `NiAlphaProperty::GetAlphaBlending()` or an explicitly supported implicit material alpha | The source must already be blendable |
| Source blend | `kSrcAlpha` | MVP supports conventional alpha only |
| Destination blend | `kInvSrcAlpha` | Reject additive, multiplicative, and unusual modes |
| Z test | `kZBufferTest` set | The source must respect opaque depth |
| Z write | `kZBufferWrite` clear | The opaque receiver must remain the depth owner |
| Refraction | No `kRefraction` or `kTempRefraction` | Separate renderer path and feedback risks |
| Decals | No `kDecal` or `kDynamicDecal` | Different depth and ownership semantics |
| Landscape | No `kMultiTextureLandscape` or `kLODLandscape` | LAND layer blending is a separate problem |
| Tree/billboard | No `kTreeAnim`, `kBillboard`, `kLODObjects`, or `kHDLODObjects` | Avoid foliage and distant-object behaviour |
| Character materials | Exclude face, hair, eye, and skin-like paths | Avoid changing actors |
| Root | Static owning root found | Required for sibling analysis |

Also reject effect, water, sky, grass, particle, and other non-lighting shader types before accessing lighting-specific material data.

### Alpha testing

A source that is both alpha tested and alpha blended is possible. Do not support it automatically in the first classifier. Alpha-test ordering can turn a smooth fade into a hard discard or coverage artifact. It can be considered later after explicit tests.

An opaque sibling may be alpha tested as long as it writes valid post-Z-prepass depth with the same cutoff used in its visible pass.

### Receiver plausibility

Traverse geometries only under the selected owning root. A plausible receiver should:

- not be the source geometry;
- use a supported opaque or alpha-tested lighting path;
- have `kZBufferTest` and `kZBufferWrite` set;
- not use true alpha blending;
- not be refraction, water, effect, decal, particle, or billboard geometry; and
- have a world bound that overlaps, contains, or lies within an expanded tolerance of the source bound.

Bounding-sphere overlap is intentionally coarse. It rejects clearly unrelated siblings but cannot prove per-pixel adjacency. More precise triangle or mesh-distance analysis would cost too much for routine draw setup and still would not establish visible depth ownership.

### Strict automatic decision

Enable a source automatically only when:

```text
source is safe
AND owning root is static
AND at least one plausible opaque sibling exists
AND source/receiver bounds plausibly overlap
AND asset/shape is not denied
```

An explicit CS allow entry may bypass the sibling/bounds heuristic but should not bypass fundamental renderer-safety checks.

## Cache and lifetime design

Do not traverse an entire NIF for every draw call.

Recommended cache entry:

```cpp
struct ClassificationSignature
{
    const RE::NiNode* root;
    const RE::BSShaderProperty* shaderProperty;
    const RE::NiAlphaProperty* alphaProperty;
    const void* rendererData;
    std::uint64_t relevantShaderFlags;
};

struct ClassificationCacheEntry
{
    ClassificationSignature signature;
    AssetShapeKey key;
    bool safeSource;
    bool hasPlausibleReceiver;
    bool enabledByPolicy;
    std::uint32_t lastSeenFrame;
};
```

Use the current geometry pointer as a lookup key only. Validate the signature from the current live render pass before reusing an entry. Never dereference a cached pointer unless it also arrives through the current pass.

Use a bounded cache with `lastSeenFrame` eviction, similar in principle to other renderer caches already present in the repository. Avoid holding `NiPointer` references indefinitely because that can retain unloaded cells or models.

Classification is expected on the render thread through geometry setup. If later model-load hooks prepopulate the cache from worker threads, guard shared state explicitly and keep render-thread lookups short.

Do not build strings or log on every draw. Compute and retain hashes for normal operation; retain readable strings only for configuration, diagnostics, or a developer candidate list.

## Hook integration

### Recommended flow

Add a function similar to:

```cpp
void SoftMeshTransitions::PrepareLightingDraw(RE::BSRenderPass* a_pass);
```

It should:

1. Clear `ExtraShaderDescriptors::SoftMeshTransition` unconditionally.
2. Return unless the feature and shader cache are active.
3. Return unless the pass is a safe main-world lighting pass.
4. Obtain or calculate the classification.
5. Set the descriptor bit only when policy enables that source.

Call it from the shared `LightingExtensions::BSLightingShader_SetupGeometry` chain before the underlying function selects or binds the final shader state. Verify ordering with the existing Extended Translucency, Skin, Linear Lighting, and Light Limit Fix hook chains.

The classifier must not install another independent vtable hook if the shared hook can carry the call. Reducing nested hook ordering is preferable.

### Descriptor definitions

Add matching definitions:

```cpp
// src/State.h
SoftMeshTransition = 1u << 8
```

```hlsl
// package/Shaders/Common/Permutation.hlsli
static const uint SoftMeshTransition = (1u << 8);
```

Clear and set only this bit. Do not rewrite the whole descriptor.

## Shader integration

### Suggested helper

Create a small HLSL helper rather than embedding the full calculation directly in `Lighting.hlsl`:

```hlsl
namespace SoftMeshTransitions
{
    float GetFade(float2 screenUV, float fragmentRawDepth)
    {
        float receiverRawDepth = SharedData::GetDepth(screenUV);

        // No opaque receiver: retain authored opacity.
        if (receiverRawDepth >= 1.0)
            return 1.0;

        float receiverDepth = SharedData::GetScreenDepth(receiverRawDepth);
        float fragmentDepth = SharedData::GetScreenDepth(fragmentRawDepth);
        float gap = receiverDepth - fragmentDepth;

        float width = max(SharedData::softMeshTransitionSettings.BlendWidth, 0.001);
        float start = SharedData::softMeshTransitionSettings.DepthBias;
        float fade = smoothstep(start, start + width, gap);

        // Treat gaps beyond the configured influence as ordinary authored alpha.
        if (gap >= SharedData::softMeshTransitionSettings.MaximumGap)
            fade = 1.0;

        return saturate(fade);
    }
}
```

The sign of `gap` must be verified with an in-game debug visualisation before shipping. Do not change it to `abs` to hide a sign error; signed depth distinguishes a receiver behind the source from invalid ordering.

### Placement in `Lighting.hlsl`

The fade must compose with the final authored/material alpha:

1. Let vanilla and CS calculate texture alpha, material alpha, vertex alpha, alpha-test behaviour, and Extended Translucency adjustments first.
2. Calculate `softMeshFade` only when the per-draw bit is set.
3. Multiply the final output alpha after feature-specific alpha overrides and before alpha is copied into motion-vector or deferred MRT channels.

Conceptually:

```hlsl
float softMeshFade = 1.0;

#if defined(SOFT_MESH_TRANSITIONS)
[branch] if (
    SharedData::softMeshTransitionSettings.Enabled &&
    (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::SoftMeshTransition))
{
    softMeshFade = SoftMeshTransitions::GetFade(screenUV, input.Position.z);
}
#endif

// After normal alpha and Terrain Blending alpha selection:
psout.Diffuse.w *= softMeshFade;
```

The exact placement matters. In the current shader, the deferred Terrain Blending block can replace `psout.Diffuse.w`. Applying the soft-mesh multiplier before that replacement would lose it. Apply the multiplier after any replacement and before the value fans out to `MotionVectors`, `Specular`, `Albedo`, `Reflectance`, `NormalGlossiness`, `Parameters`, `Masks`, and `Masks2`.

For the non-deferred path, ensure the final returned diffuse alpha receives the same multiplier.

### Blend functions

The MVP supports only `SrcAlpha / InvSrcAlpha` source-over blending.

- Do not modify RGB for ordinary non-premultiplied alpha.
- Reject premultiplied alpha initially. If added later, RGB and alpha must be attenuated consistently.
- Reject additive and multiplicative modes initially because reducing output alpha may not reduce their RGB contribution as expected.
- Do not force alpha blending on an opaque shape.
- Do not force Z writes on a no-Z-write shape.

### Depth coordinates

Reuse the existing `screenUV` and `SharedData` helpers in `Lighting.hlsl`. Do not invent independent viewport, dynamic-resolution, or VR eye calculations. The depth SRV and the lighting pass must use matching coordinates.

## Interaction with Terrain Blending

`Util::GetCurrentSceneDepthSRV` returns Terrain Blending's blended depth resource when Terrain Blending is active, and the post-Z-prepass copy otherwise.

For the MVP:

- reuse the normal `t17` resource;
- verify the target NIF with Terrain Blending both enabled and disabled;
- confirm that the terrain depth offset does not widen or invert the new fade; and
- ensure Terrain Blending's deferred alpha assignment is followed by, not subsequent to, the soft-transition multiplier.

If compatibility testing reveals incorrect depth gaps, add an explicit read-only accessor for the unmodified opaque prepass depth rather than duplicating or guessing Terrain Blending state. The desired receiver choice should then become a documented setting or fixed policy:

- ordinary opaque depth only; or
- nearest of opaque and true LAND depth.

Do not sample a resource while it is simultaneously bound for incompatible depth writes. Reuse the existing copied/readable depth path.

## Interaction with Extended Translucency

Extended Translucency already changes alpha for selected lighting materials. Soft Mesh Transitions must be a final multiplicative coverage adjustment:

```text
texture/material/vertex alpha
→ Extended Translucency shaping
→ other explicit alpha selection
→ Soft Mesh Transitions depth fade
→ output alpha/MRT propagation
```

The new descriptor belongs in `ExtraShaderDescriptor`, not Extended Translucency's `ExtraFeatureDescriptor` material-model field.

## SE and VR considerations

The feature should not require new runtime addresses. Its engine-side work should use the existing cross-runtime lighting setup and CommonLib scenegraph data.

For VR:

- sample the depth for the current eye using existing CS framebuffer/stereo helpers;
- never calculate a custom side-by-side eye offset from assumptions;
- keep the blend band narrow because view-ray depth gaps differ slightly between eyes;
- inspect both eyes for a stereo fringe at grazing angles;
- ensure any debug dither is world-stable and eye-consistent;
- avoid temporal screen-noise dependence when no suitable temporal reconstruction is active; and
- verify MSAA coverage edges, where copied depth may represent incomplete sample coverage.

The VR branch has additional Terrain Blending and depth-override logic. Port the feature by symbol and behaviour, not by blindly copying the SE file layout.

## Reflections, cubemaps, shadows, and specialised passes

Disable the effect outside the main world lighting pass in the MVP.

- A reflection or cubemap pass can have a different depth target and projection.
- Shadow and depth passes use Utility shaders and should preserve the source's existing depth behaviour.
- The source should not suddenly cast or write a depth shape that the original material did not.
- Water, effect, particle, sky, grass, and image-space shaders require separate implementations and should be rejected by classification.

Use existing `InWorld` and reflection state rather than inferring pass type from geometry names.

## Developer diagnostics

Implement diagnostics before enabling broad automatic selection.

Recommended developer-only information:

- candidates inspected;
- candidates accepted automatically;
- allow-list matches;
- deny-list matches;
- rejection reason counts;
- active transition draws per frame;
- cache hits, misses, and evictions; and
- resolved asset/shape keys.

Logs must be rate limited and disabled during normal play. Do not log every draw.

Useful visualisation modes:

1. **Candidate mask:** accepted source shapes in a solid debug colour.
2. **Fade heat map:** black at zero coverage, colour ramp through the blend band, white at full authored coverage.
3. **Depth gap:** signed linear gap mapped around zero, making sign and scale errors obvious.
4. **Receiver-missing mask:** highlight pixels whose depth is clear/far.

Visualisation must be gated by Developer Mode and must not be persisted as a normal user setting.

## Known limitations and expected artifacts

### Depth has no ownership

The copied scene depth contains the nearest opaque surface, not an object or NIF identity. CPU sibling discovery only establishes that a receiver is plausible. It does not prove that each sampled pixel belongs to that sibling.

An unrelated opaque object can influence the fade if it becomes the nearest depth behind the source. The source's normal Z test prevents many invalid cases, but it does not provide group identity.

### View-dependent width

The fade uses view-ray depth separation. A fixed depth width appears wider at grazing angles. Keep the default band narrow and clamp the maximum influenced gap.

### Transparent ordering

The MVP depends on the receiver being opaque/depth-writing and the source being rendered later through its existing transparent path. It does not solve general transparent sorting.

### Both shapes transparent or no-Z-write

Neither shape supplies reliable receiver depth. The MVP must reject this case. Supporting it requires a receiver-only private depth pass.

### Both shapes opaque

The nearest depth discards the hidden layer, and an opaque draw cannot be softly revealed merely by changing a later alpha value. Supporting this requires replay/deferred rendering, an additional depth layer, or an authored material blend.

### Material mismatch remains

Fading final coverage softens a seam but does not create a physical mixture of material properties. Strong differences in normals, roughness, wetness, or directional lighting may still reveal the boundary.

### Silhouettes and geometry

The feature changes opacity only. It does not alter geometry or silhouettes.

### MSAA and stochastic deferred coverage

Depth copies and alpha coverage may differ at partially covered samples. Inspect for halos. If deferred normal/roughness blending uses stochastic coverage, ensure the fade does not introduce VR shimmer. A stable alpha-to-coverage path may be preferable for a later VR-specific refinement.

## Robust receiver-ownership extension

If the MVP produces unacceptable false blends, the next stage can remain entirely inside CS but is more expensive.

### Required resources

- A receiver-only depth texture.
- A receiver group-ID texture, preferably integer.
- A transient group ID assigned to each visible owning root or instance.

### Required rendering

1. Replay eligible opaque siblings into receiver depth and group-ID targets.
2. Preserve their alpha-test cutoff when applicable.
3. Pass the owning group's ID to each transition source.
4. Sample both textures in the source pixel shader.
5. Apply the fade only when the sampled receiver ID matches the source group ID.

This guarantees ownership for visible receiver pixels but adds geometry work, render targets, state transitions, IDs, and lifetime management. It should not be part of the first implementation unless the cheap path proves inadequate.

A single global receiver mask without IDs is insufficient when unrelated eligible NIF instances overlap on screen.

## Material-aware extension

If coverage fading is insufficient, a later material-aware mode could capture or copy the opaque receiver's deferred attributes and blend:

- albedo;
- encoded/decoded normal;
- roughness or glossiness;
- reflectance/specular;
- AO; and
- wetness-related masks.

Normals must be decoded, blended with a suitable normal method, and re-encoded. Directly alpha-blending encoded normals is not correct. Reading and writing the same G-buffer resource is also invalid; copies or a staged pass would be required.

This is substantially more expensive and invasive than the soft-intersection MVP and should be treated as a separate phase.

## Implementation phases

### Phase 0 — Instrumentation

- Add no visual effect.
- Classify and count candidate shapes in Developer Mode.
- Record stable asset/shape keys and rejection reasons.
- Inspect representative mountain, rock, snow, moss, architecture, glass, foliage, and decal assets.

Exit condition: the strict classifier finds intended skirts without accepting obvious glass, foliage, characters, or effects.

### Phase 1 — Feature and policy plumbing

- Add the feature class and registration.
- Add settings serialization and restore defaults.
- Add CS-side allow and deny matching.
- Add bounded classification caching.
- Add developer candidate reporting.
- Reserve matching C++/HLSL descriptor bits.

Exit condition: the per-draw bit is correct and never leaks to the following draw.

### Phase 2 — Shader fade

- Add the HLSL helper.
- Reuse `t17` and existing depth linearisation.
- Integrate after final alpha selection.
- Propagate the result consistently to forward and deferred outputs.
- Add debug mask, gap, and fade visualisations.

Exit condition: the intended skirt fades only near the opaque intersection and preserves authored transparency elsewhere.

### Phase 3 — Compatibility hardening

- Test Terrain Blending enabled and disabled.
- Test Extended Translucency enabled and disabled.
- Test True PBR and standard materials.
- Test multiple placed instances, overlapping instances, and unrelated foreground/background geometry.
- Test interiors, exteriors, weather/wetness, camera distance, and grazing angles.
- Test SE/AE and then adapt to the VR branch using its existing depth path.

Exit condition: no state leaks, no shader-cache mismatch, and no unintended candidates in representative scenes.

### Phase 4 — Optional strict receiver mode

- Add receiver depth and group-ID rendering only if needed.
- Keep the cheap path available.
- Compare quality and cost before choosing defaults.

## Acceptance criteria

### Functional

- Ineligible meshes are bit-identical to the feature-disabled path apart from unavoidable shader permutation selection.
- The feature does not change Z-write, Z-test, blend, alpha-test, sorting, culling, or shadow state.
- An accepted transition layer retains authored alpha away from intersections.
- The transition approaches zero coverage smoothly at the receiver intersection.
- Clear/far depth does not make the source disappear.
- The descriptor is cleared before every unrelated lighting draw.
- Allow and deny rules are deterministic across multiple placed instances.
- Settings save, restore, and live enable/disable correctly.

### Compatibility

- Terrain Blending on/off does not invert or erase the fade.
- Extended Translucency output remains intact and is multiplied by the new fade.
- True PBR and vanilla lighting materials produce comparable transition placement.
- Reflections and specialised passes remain unchanged in the MVP.
- Alpha-tested receivers do not leak rectangular depth silhouettes.

### Robustness

- Null pass, geometry, shader property, alpha property, parent, root, user data, model, or renderer data fails closed.
- Cache entries validate current live signatures before reuse.
- Cache size remains bounded during cell traversal and fast travel.
- Logging is rate limited.
- No persistent scenegraph ownership is introduced.

### Performance

- No extra full-screen pass in the MVP.
- No extra geometry pass in the MVP.
- No scenegraph traversal on every draw after cache warm-up.
- One depth read plus minimal ALU only for accepted source pixels.
- No per-draw string allocation in normal mode.

### VR-specific

- Each eye samples its own matching depth.
- No stereo fringe at the configured default width in representative scenes.
- No temporal or per-eye dither mismatch.
- MSAA edges do not produce a persistent halo.

## Suggested focused tests

No tests were run for this handover. When implementation begins, use focused checks rather than assuming that generic shader compilation proves renderer-state correctness.

### Classifier tests

- Standard alpha, Z-test on, Z-write off, opaque sibling: accept.
- Additive alpha: reject.
- Premultiplied alpha: reject in MVP.
- Z-test off: reject.
- Z-write on: reject as source.
- Skinned/tree/decal/refraction/water/effect: reject.
- No opaque sibling in strict automatic mode: reject.
- Allow-list entry with safe state: accept.
- Deny-list entry: reject even if automatic rules pass.
- Duplicate geometry names: resolve via root-relative child path.

### Shader tests

- Depth clear value returns fade 1.
- Gap at/below bias returns fade 0.
- Gap at bias plus width returns fade 1.
- Large gap returns authored alpha.
- Disabled feature or absent descriptor returns authored alpha exactly.
- Deferred alpha propagates consistently across MRTs.

### Runtime scenarios

- Target mountain trim at several distances and angles.
- Several instances of the same NIF in view.
- Two eligible NIFs overlapping on screen.
- Actor or unrelated static moving between camera and target.
- Wet and dry material states.
- Terrain Blending, Extended Translucency, True PBR, and upscaling combinations.
- Cell transition, fast travel, save load, and menu open/close.
- Reflection/cubemap scenes to confirm the effect remains disabled there.

## Decisions to make before implementation

1. Final feature name and UI category.
2. Whether the initial user-visible mode is disabled or allow-list-only by default.
3. Exact stable key representation and wildcard policy.
4. Whether an Advanced UI candidate browser is required for the first version.
5. Initial blend-width, bias, and maximum-gap units after depth visualisation.
6. Whether Terrain Blending's blended depth or the unmodified opaque prepass is the preferred receiver source.
7. Whether alpha-tested plus alpha-blended sources remain excluded.
8. Whether implicit material alpha without `NiAlphaProperty` is supported initially.
9. Whether VR ships simultaneously or follows after SE validation.

## Recommended first implementation boundary

For the first working version, implement only:

- `BSLightingShader` static source geometry;
- standard `SrcAlpha / InvSrcAlpha` blending;
- Z testing enabled;
- Z writes disabled;
- no source alpha testing;
- a static owning root;
- an overlapping opaque/Z-writing sibling in strict automatic mode, or a CS allow-list match;
- main world pass only;
- the existing `t17` scene depth;
- one final alpha multiplier; and
- developer mask/gap visualisation.

Do not add group-ID rendering, material-buffer blending, opaque-source replay, or support for two transparent layers until the MVP has been evaluated on real assets. This boundary preserves the original reason for the proposal: a cheap, CS-only improvement for authored terrain and rock transition skirts.
