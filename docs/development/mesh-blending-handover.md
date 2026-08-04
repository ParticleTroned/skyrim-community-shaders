# Mesh Blending — Implementation Handover

## Status

- The first conservative implementation now exists on `PR/nif-blending`, based on tag `RC173`.
- The original proposal was re-evaluated against RC173 before implementation; the decisions below supersede stale integration details in the handover.
- Static source checks pass. In accordance with the no-build instruction, the final source, shader permutations, C++ integration, and runtime behaviour remain unbuilt and require later validation.
- The design intentionally avoids NIF edits and new engine-address hooks.
- The initial implementation should target the normal world `BSLightingShader` path. Reflection, cubemap, shadow, water, effect, and other specialised passes are explicitly outside the MVP.

## RC173 implementation decisions

- Descriptor bit 8 is used because RC173 already mirrors `IsFemale` at bit 6 and uses bit 7 for external-emittance suppression.
- The descriptor is prepared before the original shared Lighting `SetupGeometry` call, matching the per-draw timing used by Extended Translucency and avoiding a possible dirty-state upload inside the original call.
- Static ownership is proven with `TESObjectREFR::Get3D()` plus a bounded ancestry walk. The nearest `BSFadeNode` is not treated as authoritative ownership.
- Model and node rules use an active `ExtraModelSwap` path when present, falling back to the base static model. The selected model and path identities participate in cache validation.
- The shipped default is strict automatic classification. Allow-list-only remains available for curated deployments, and deny rules always override automatic candidates.
- Allow rules bypass the sibling and bounds heuristic only. They do not bypass source render-state, material, static-owner, animation, pass, or distance safety gates; deny rules always win.
- Renderer submission remains the primary visibility/frustum/portal culling mechanism. A configurable 8192-unit camera/eye-centered bubble rejects distant candidates before owner lookup, cache lookup, string construction, or sibling traversal; zero disables the bubble.
- Classification uses a fixed 4096-entry, four-way set-associative cache with no owning references and a hard 256-object traversal cap. With no effective allow/deny rules, validated source/material/parent state can hit before owner resolution and is revalidated after a geometry-jittered 120–183 frames; configured policy retains the full model/owner signature lookup and 600–855-frame interval. Jitter avoids a cell-wide reclassification spike; distance rejects are deliberately not cached. Developer diagnostics separate pre-owner hits, full policy-signature hits, and owner-resolution attempts.
- Automatic receivers are initially required to be fully opaque and non-alpha-tested. This is stricter than the proposal because CPU flags cannot prove that an alpha-test cutoff exactly matches the prepass depth.
- Receiver classification is structural rather than tied to transient application-cull state, keeping cached results coherent when scripted nodes are hidden or revealed. Invalid or absent receiver depth still fails open in the shader.
- VR depth reads explicitly pass the current eye index. Raw depth near either 0 or 1 fails open, covering the far clear value and VR hidden-area/mask values.
- `MaximumGap` is sanitized to at least `DepthBias + BlendWidth`, preventing a discontinuous jump back to full opacity. Clearly negative signed gaps also fail open rather than fading against foreground geometry.
- The MVP continues to reuse slot `t17` and adds no depth copy, render target, draw pass, or geometry. The R16 Terrain Blending depth path should be compared with its existing R32 resource during later runtime quality testing before paying extra bandwidth globally.
- Known-ineligible Lighting permutations are removed at shader compile time. The remaining permutations take one uniform fast-path branch, and only CPU-qualified transition draws sample depth.
- If Light Limit Fix skips the shared Lighting hook for an unsafe directional-light slot, it explicitly clears the Mesh Blending descriptor so stale per-draw state cannot leak.
- The feature is intentionally non-core and its standalone package has `autoupload = false`; publication remains an explicit release decision.
- User sliders and toggles remain in `SettingsUser.json`, while generated allow/deny policy is isolated in the atomically written `Data/SKSE/Plugins/CommunityShaders/MeshBlendingRules.json`.
- The normal feature UI exposes a session-only **Discover Blendable Meshes** toggle. Discovery applies the same distance bubble and bounded strict classifier, records at most 1024 unique exact model/node candidates, performs no disk I/O while walking, and costs additional CPU time only while active.

## Executive summary

Implement a Community Shaders feature named **Mesh Blending**. It should soften the intersection between a transparent transition layer and an opaque surface behind it.

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

A fully opaque, non-alpha-tested, Z-tested, Z-writing sibling, or the underlying LAND surface already represented in the scene depth. Alpha-tested sibling support is deferred until cutoff/prepass equivalence can be proven.

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

The Mesh Blending descriptor must be cleared for every lighting geometry before optionally being set. A stale per-draw bit would affect unrelated meshes.

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
MeshBlending = 1u << 8
```

Do not reuse bit 6. RC173 mirrors `IsFemale` at bit 6 in both C++ and HLSL, and bit 7 is also occupied. Do not overlap Extended Translucency's bits 6–8 in `ExtraFeatureDescriptor`; use bit 8 of `ExtraShaderDescriptor` instead.

## Implemented feature structure

Feature identity:

- Display name: `Mesh Blending`
- Short name: `MeshBlending`
- Category: `Landscape & Textures`
- Shader define: `MESH_BLENDING`
- Shader type: Lighting only

Suggested files:

```text
src/Features/MeshBlending.h
src/Features/MeshBlending.cpp
features/Mesh Blending/Shaders/MeshBlending/MeshBlending.hlsli
docs/development/mesh-blending-handover.md
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
```

The existing CMake source and feature-package globs discover these files, so no explicit CMake entry is required.

## Implemented settings

### GPU settings

The implementation uses a 16-byte-aligned structure:

```cpp
struct alignas(16) PerFrame
{
    float BlendStrength = 1.0f;
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

std::uint32_t DetectionMode = 2; // StrictAutomatic
std::uint32_t RequireOverlappingBounds = 1;
float MaximumDistance = 8192.0f;
float BoundsExpansion = 32.0f;
std::uint32_t DeveloperLogging = 0;
```

Strict automatic is the product default. It remains bounded by static ownership, render-state, animation, material, sibling, bounds, traversal, cache, and distance gates; allow-list-only is available when a curated policy is preferred.

### CS-side settings and rule format

`Feature::Load` keys ordinary feature settings by display name, and this implementation serializes enum/bool-like values as integers. `SettingsUser.json` contains only the normal feature controls:

```json
{
  "Mesh Blending": {
    "Enabled": 1,
    "BlendStrength": 1.0,
    "DetectionMode": 2,
    "BlendWidth": 12.0,
    "DepthBias": 0.25,
    "MaximumGap": 64.0,
    "MaximumDistance": 8192.0,
    "BoundsExpansion": 32.0,
    "RequireOverlappingBounds": 1,
    "DeveloperLogging": 0
  }
}
```

Asset policy is intentionally isolated in `Data/SKSE/Plugins/CommunityShaders/MeshBlendingRules.json` so discovery does not rewrite unrelated CS settings. It is loaded once independently of whether a Mesh Blending section exists in `SettingsUser.json`, rather than reread during live settings or performance swaps. Save/Clear re-read and validate it immediately before mutation so in-session mod-author edits are merged rather than silently replaced. The file is replaced atomically and uses this versioned shape:

```json
{
  "SchemaVersion": 1,
  "AllowList": [
    {
      "Model": "meshes/landscape/mountains/mountaintrim01wet.nif",
      "NodePath": "mountaintrim01wet/rockskirt01wet[2]"
    }
  ],
  "DenyList": [
    {
      "Model": "meshes/example/glasswindow.nif",
      "NodePath": "windowroot/glass[1]"
    }
  ]
}
```

Detection modes are `0` disabled, `1` allow-list-only, and `2` strict automatic. Empty `Model` or `NodePath` fields wildcard that component; an entirely empty rule never matches. `*` and `?` wildcards are supported after ASCII lowercase/slash normalization.

An allow match should override automatic rejection only for safe render-state requirements. It must not force unsupported blend functions, disabled Z testing, water/effect shaders, or missing depth resources. A deny match always wins.

### Runtime discovery workflow

The normal Mesh Blending UI exposes one session-only toggle, **Discover Blendable Meshes**:

1. Enable discovery and walk through the locations to test.
2. Only rendered Lighting geometry inside `Culling Distance` is inspected.
3. A candidate is retained only after the strict source, static-owner, deny-rule, opaque-sibling, bounds, traversal, UTF-8, and path-safety checks pass.
4. **Save Detected Meshes** stops capture, confirms the promotion, merges exact candidates without partial overflow, and atomically updates `MeshBlendingRules.json`.
5. **Clear Saved Allow List** stops capture, clears the session, and atomically empties the saved `AllowList`; manually authored deny rules remain intact.

Discovery is not an installed-NIF inventory. It cannot see unvisited, unloaded, culled, or conditional variants. Starting or manually stopping it invalidates the bounded classification cache so stale normal-mode decisions cannot suppress capture or leak back into normal policy rendering. Capture automatically stops at 1024 unique candidates, before further cache misses can spend time on sibling traversal that cannot retain a result. No per-candidate file writes or log spam occur while walking.

Captured entries are candidates for visual review. The default Strict automatic mode previews them while discovery records them; Allow-list-only discovery records without changing unsaved candidates. Promoting one to an allow rule deliberately avoids repeating the automatic sibling traversal for that exact model/node pair, while all source render-state, static-owner, pass, animation, distance, and deny gates continue to apply. Feature-cost A/B measurement cannot start while discovery is active, so capture overhead cannot contaminate its baseline.

Malformed, unreadable, or newer-schema rule files fail closed: runtime blending and discovery are disabled, Save/Clear are locked, and the existing file is never silently replaced. The UI reports that the file must be fixed or removed followed by a restart.

## Runtime asset and shape identity

The allow/deny key must be stable across placed instances and must not depend solely on a raw pointer.

Preferred identity components:

1. Normalised model path from the owning reference's base object when it exposes `TESModel`.
2. Stable child path from the owning NIF root to the geometry, including child indices when names are duplicated.
3. Geometry name as a readable diagnostic field.
4. Optional material or texture signature as a validation field, not the primary key.

Suggested conceptual key:

```text
lowercase(normalised active model path) | root-relative named/indexed node path
```

Do not use only the material hash. Multiple placed cells and unrelated shapes can share materials, and systems such as True PBR can replace materials at runtime.

### Finding the owning asset

Starting from `pass->geometry`:

1. Read the geometry's `TESObjectREFR` user data and require its base form to be exactly `Static`.
2. Obtain that reference's authoritative `Get3D()` root and prove ownership with a bounded parent walk; do not infer ownership from the nearest `BSFadeNode`.
3. Prefer a valid `ExtraModelSwap` model and path when present.
4. Otherwise use the base static's `TESModel::GetModel()` path.
5. Permit an explicit node-only rule to use the root-relative node path when a model path cannot be resolved; strict automatic detection still fails closed without a stable model path.

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

Alpha-tested siblings are rejected in the MVP because the CPU-side flags cannot prove that their visible cutoff matches the depth prepass.

### Receiver plausibility

Traverse geometries only under the selected owning root. A plausible receiver should:

- not be the source geometry;
- use a supported fully opaque, non-alpha-tested lighting path;
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
void MeshBlending::PrepareLightingDraw(RE::BSRenderPass* a_pass);
```

It should:

1. Clear `ExtraShaderDescriptors::MeshBlending` unconditionally.
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
MeshBlending = 1u << 8
```

```hlsl
// package/Shaders/Common/Permutation.hlsli
static const uint MeshBlending = (1u << 8);
```

Clear and set only this bit. Do not rewrite the whole descriptor.

## Shader integration

### Shader helper

The implementation keeps the calculation in a small HLSL helper rather than embedding it directly in `Lighting.hlsl`:

```hlsl
namespace MeshBlending
{
    float ComputeFade(float2 screenUV, float fragmentRawDepth, uint eyeIndex)
    {
        float receiverRawDepth = SharedData::GetDepth(screenUV, eyeIndex);

        // Far clear and the VR hidden-area/mask value retain authored opacity.
        if (receiverRawDepth <= 1.0e-6 || receiverRawDepth >= 1.0 - 1.0e-6)
            return 1.0;

        float receiverDepth = SharedData::GetScreenDepth(receiverRawDepth);
        float fragmentDepth = SharedData::GetScreenDepth(fragmentRawDepth);
        float gap = receiverDepth - fragmentDepth;

        float width = max(SharedData::meshBlendingSettings.BlendWidth, 1.0e-4);
        float start = max(SharedData::meshBlendingSettings.DepthBias, 0.0);
        float maximumGap = max(SharedData::meshBlendingSettings.MaximumGap, start + width);

        // A receiver clearly in front is unrelated foreground; fail open.
        if (!(gap == gap) || gap < -max(start, 1.0e-4) || gap >= maximumGap)
            return 1.0;

        float fade = smoothstep(start, start + width, max(gap, 0.0));
        return lerp(1.0, saturate(fade), saturate(SharedData::meshBlendingSettings.BlendStrength));
    }
}
```

The sign of `gap` must be verified with an in-game debug visualisation before shipping. Do not change it to `abs` to hide a sign error; signed depth distinguishes a receiver behind the source from invalid ordering.

### Placement in `Lighting.hlsl`

The fade must compose with the final authored/material alpha:

1. Let vanilla and CS calculate texture alpha, material alpha, vertex alpha, alpha-test behaviour, and Extended Translucency adjustments first.
2. Calculate `meshBlendFade` only when the per-draw bit is set.
3. Multiply the final output alpha after feature-specific alpha overrides and before alpha is copied into motion-vector or deferred MRT channels.

Conceptually:

```hlsl
float meshBlendFade = 1.0;

#if defined(MESH_BLENDING_AVAILABLE)
[branch] if (MeshBlending::ShouldApply())
{
    meshBlendFade = MeshBlending::ComputeFade(screenUV, input.Position.z, eyeIndex);
}
#endif

// After normal alpha and Terrain Blending alpha selection:
psout.Diffuse.w *= meshBlendFade;
```

The exact placement matters. In the current shader, the deferred Terrain Blending block can replace `psout.Diffuse.w`. Applying the mesh-blending multiplier before that replacement would lose it. Apply the multiplier after any replacement and before the value fans out to `MotionVectors`, `Specular`, `Albedo`, `Reflectance`, `NormalGlossiness`, `Parameters`, `Masks`, and `Masks2`.

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
- ensure Terrain Blending's deferred alpha assignment is followed by, not subsequent to, the Mesh Blending multiplier.

If compatibility testing reveals incorrect depth gaps, add an explicit read-only accessor for the unmodified opaque prepass depth rather than duplicating or guessing Terrain Blending state. The desired receiver choice should then become a documented setting or fixed policy:

- ordinary opaque depth only; or
- nearest of opaque and true LAND depth.

Do not sample a resource while it is simultaneously bound for incompatible depth writes. Reuse the existing copied/readable depth path.

## Interaction with Extended Translucency

Extended Translucency already changes alpha for selected lighting materials. Mesh Blending must be a final multiplicative coverage adjustment:

```text
texture/material/vertex alpha
→ Extended Translucency shaping
→ other explicit alpha selection
→ Mesh Blending depth fade
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
- Alpha-tested receiver candidates are rejected and remain visually unchanged.

### Robustness

- Null pass, geometry, shader property, alpha property, parent, root, user data, or renderer data fails closed. A missing model path can match only an explicit node-only allow rule; strict automatic mode fails closed.
- Cache entries validate current live signatures before reuse.
- Cache size remains bounded during cell traversal and fast travel.
- Logging is rate limited.
- No persistent scenegraph ownership is introduced.

### Performance

- No extra full-screen pass in the MVP.
- No extra geometry pass in the MVP.
- No scenegraph traversal on every draw after cache warm-up.
- One depth read plus minimal ALU only for accepted source pixels.
- No string allocation on cache hits; path strings are built only on bounded cache misses when policy or diagnostics require them.

Because strict automatic is the default, use the built-in feature A/B toggle from a fixed camera after warming the classification cache. Record CPU classifier time, active draws, cache hit/miss/eviction counts, traversed objects, and GPU frame time for representative accepted assets and a deliberately dense worst-case view. Label zero-active results as idle-overhead samples rather than visual-effect cost. Repeat at 4096, 8192, and unlimited distance in flat-screen and VR. The inactive configuration should report no classifier work or depth samples; active cost should scale with accepted on-screen coverage rather than total loaded statics. The built-in toggle cannot measure package-install shader/hook overhead because those remain resident in both halves; compare separate loaded/unloaded captures for that question. Reconsider the default if these properties do not hold.

### VR-specific

- Each eye samples its own matching depth.
- No stereo fringe at the configured default width in representative scenes.
- No temporal or per-eye dither mismatch.
- MSAA edges do not produce a persistent halo.

## Suggested focused tests

Pure HLSL fade tests now cover endpoints, malformed-range sanitization, and fail-open gap handling. The CPU classifier cases and all renderer-state/runtime scenarios below remain pending; generic shader compilation is not evidence that they behave correctly in game.

### Classifier tests

- Standard alpha, Z-test on, Z-write off, opaque sibling: accept.
- Additive alpha: reject.
- Premultiplied alpha: reject in MVP.
- Alpha-tested receiver: reject in MVP.
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

## Remaining validation decisions

The implementation fixes the feature name/category, strict-automatic default, normalized model/node wildcard policy, strict `NiAlphaProperty` source gate, alpha-test exclusion, and initial VR code path. Runtime evidence is still needed to decide:

1. Final blend-width, bias, and maximum-gap defaults after depth visualization.
2. Whether Terrain Blending's R16 depth is adequate or its existing R32 resource is materially better on target assets.
3. Whether bounded acceptance logs are sufficient or an Advanced UI candidate browser is justified.
4. Whether VR should ship simultaneously after stereo/MSAA testing or remain disabled for the first release.

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
- bounded developer counters and accepted-candidate logging.

Shader mask/gap visualisations remain a deferred diagnostic enhancement. Do not add group-ID rendering, material-buffer blending, opaque-source replay, or support for two transparent layers until the MVP has been evaluated on real assets. This boundary preserves the original reason for the proposal: a cheap, CS-only improvement for authored terrain and rock transition skirts.
