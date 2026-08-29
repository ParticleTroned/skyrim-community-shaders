# Skyrim VR scene accumulation and culling static slice

## Scope

This slice narrows the static gap between world scene construction and the
already observed render-pass/draw path. It targets Skyrim VR 1.4.15.0 only. It
does not import names, addresses, layouts, or control flow from Special Edition
or Anniversary Edition merely because the broad architecture looks similar.

The exact target executable is SHA-256
`6961EFB4F4775A307B0FC9A3D637542C1E090BE207D3B09467EAB216B7F87971`.
Static disassembly uses the analysis-only reconstructed image SHA-256
`1E08D17CA4195C1D7296C076034577CC05C98C26A0D9B8F38AE5C6EDBBA4E8FE`.

## Confirmed target facts

| Boundary | Skyrim VR identity | Evidence and meaning |
|---|---|---|
| Base culling contract | `NiCullingProcess`, size `0x128`, vtable RVA `0x17F3720` | Pinned CommonLib VR layout defines camera/frustum state and virtual `Process1`, `Process2`, and `AppendVirtual` slots. |
| Engine culling contract | `BSCullingProcess`, size `0x301F8`, vtable RVA `0x1815B40` | Pinned layout exposes object array, 4096-entry cull queue, compound frustum, cull modes, alpha groups, and slots through `0x1C`. Exact target vtable inspection adds an optimized active-plane `NiBound` test at slot `0x1D`, RVA `0xD9A7B0`, which is absent from the pinned declaration. |
| Geometry-list culler | `BSGeometryListCullingProcess`, size `0x30200`, vtable RVA `0x1621068` | Exact derived type and its `AppendNonAccum` override are present in the VR layout. |
| Parabolic culler | `BSParabolicCullingProcess`, vtable RVA `0x190BEB0`; slot `0x19` RVA `0x136F4F0` | Exact target vtable identity. `AppendNonAccum` routes argument `1` to the inherited object array, `2` to the back-hemisphere accumulator at `+0x30200`, and `3` to both. The target deleting destructor releases `0x30248` bytes and other methods access state at `+0x30240`, so CommonLib's asserted `0x30230` layout is incomplete for this binary. |
| Per-object admission | `BSGeometryListCullingProcess::AppendNonAccum`, RVA `0xDA1860`, vfunc `0x19` | The exact VR vtable entry at RVA `0x1621130` points to `0xDA1860`. Disassembly reads `NiAVObject+0x128/+0x130`, applies the native delayed depth-culling result, admits the object, and assigns the current candidate result pointer/frame. |
| Traversal dispatch | `BSCullingProcess::Process1` `0xD99B60`, vfunc `0x16`; `Process2` `0xD99E90`, vfunc `0x17` | Exact VR vtable entries and disassembly establish the traversal/branch dispatcher and camera/frustum setup entry. `Process1` orders zero-bound/AlwaysDraw, AllPass/AllFail, preprocessed-node, compound-frustum, and ordinary active-plane routes before dispatching accepted objects through `AppendVirtual` or `NiAVObject::OnVisible`. `Process2` installs camera/frustum and visible-set state, then selects `Process1` or `NiAVObject::Cull` from `recurseToGeometry`. |
| Append dispatch | `BSCullingProcess::AppendVirtual` `0xD99F80`, vfunc `0x18`; base `AppendNonAccum` `0xD99F60`, vfunc `0x19` | The former handles alpha-group-aware dispatch and invokes slot `0x19`; the latter appends to the visible-object array. The geometry-list culler overrides slot `0x19` at `0xDA1860`. |
| Visibility/reset slots | `0x1A` `0xD9A010`; `0x1B` `0xD9A890`; `0x1C` `0xD9A870`; target-only `0x1D` `0xD9A7B0` | Slot addresses are exact. `0x1B` delegates object visibility testing, `0x1C` tests an `NiBound` through the camera/frustum contract, and `0x1D` performs the active-plane-mask test used by `Process1`, optionally clearing planes proven fully inside. CommonLib calls `0x1A` `TestBaseVisibility1`, but its VR body is dominated by visible-object/alpha-group cleanup, so that semantic name remains provisional. |
| Inherited OBB-occlusion bypass | `NiAVObject+0x122`; low byte of `BSCullingProcess+0x301D6` | `Process1` saves the process byte, replaces it with `saved OR object+0x122` for the recursive traversal, and restores it before return. `BSGeometryListCullingProcess::AppendNonAccum` checks the propagated byte at `0xDA18CC`; when set it takes ordinary object admission without consuming delayed OBB results or calling `RegisterGeometry`. The functional subtree-bypass role is exact; Bethesda's canonical field name is not. |
| Compound-frustum transaction | `BSCompoundFrustum::GetActivePlaneState` `0xDA4B90`; `Process` `0xDA33C0`; `SetActivePlaneState` `0xDA4BE0` | `Process1` saves active-plane state, performs the initial optimized world-bound test and compound object processing, then restores the state. This transaction occurs only when a nonempty compound frustum is active and cull mode is not `kIgnoreMultiBounds`. |
| Frustum installation | `NiCullingProcess::SetFrustum`, RVA `0xCBFAF0` | Target PDB coverage alias plus the pinned class contract. |
| Shared-object admission candidate | RVA `0xD9A5E0` | PDB coverage identifies `BSCullingProcess::AddShared`, although its derived-class prefix is inconsistent; the role is retained as high-confidence rather than confirmed identity. |
| Visible-object accumulator bridge | RVA `0xD9A190` | Exact disassembly drains `BSCullingProcess::objectArray` into a primary and optional secondary `BSShaderAccumulator`, then sends each alpha-group collection through `0xD9AF10`. Direct accumulator-admission calls occur at `0xD9A1F3` and `0xD9A221`. The exported target symbol name and indirect job owner remain unresolved. |
| Alpha-group accumulator drain | RVA `0xD9AF10` | Exact disassembly walks `BSCullingProcess::alphaGroups`, derives each group's auxiliary argument, calls the same single-geometry accumulator path at `0xD9B009`, and closes the group through accumulator virtual dispatch. |
| Direct scene-root accumulator entry | RVA `0x1308750`; recursive walker `0x130C0F0`; child helper `0x130C360` | The entry builds a two-field traversal context from an accumulator and filter flag. The walker resolves nodes and recurses through children. The child helper reads the node array at `+0x140` and count at `+0x14A`, rejects hidden or ineligible children, and calls single-geometry admission at `0x130C444`. |
| Scene-root registry lifecycle | rebuild RVA `0x5BC140`; append RVA `0x5C4770`; clear RVA `0x5C4C10` | The rebuild first releases the previous reference-counted entries, then appends `PlayerCharacter::Get3D1(false)` with rank `0.0` and accepted reference `Get3D2()` roots with a float ranking value. The PDB aliases the append helper to `BSFadeNode::sub_1405BC350`. Entries are exactly 16 bytes: rank at `+0x00`, root pointer at `+0x08`. |
| Main scene-root preparation owner | function RVA `0x5B9E90`, PDB alias `Main::sub_1405B2590`; candidate-builder callsite `0x5BA55D`; registry-rebuild callsite `0x5BA565` | The target Main helper first invokes `Main::sub_1405B46A0` (`0x5BC2E0`) to rebuild/sort the high-process actor candidate list, then reconstructs the scene-root registry. |
| Scene-root reference sources | `PlayerCharacter` singleton RVA `0x2FEB9F0`, relocation ID `517014`; `ProcessLists` singleton RVA `0x1F831B0`, relocation ID `514167`; `highActorHandles` at `ProcessLists+0x30`; ranked-candidate table RVA `0x2FEBA18`, count `0x2FEBA28` | CommonLib, the VR Address Library, and exact disassembly identify both reference sources. Exact target calls use `TESObjectREFR` slots `0x6F` (`Get3D1`, false) and `0x70` (`Get3D2`). Ranked records contain adjusted squared distance at `+0x00` and `ActorHandle` at `+0x04`. |
| High-process actor candidate ranking | builder `0x5BC2E0`; enumeration helper `0x5C1720`; extent accessor `0x2B9290`; default distance RVA `0x1ED6488` | The builder squares the default threshold `450.0`, filters `ProcessLists::highActorHandles` to resolved actors with present and eligible `Get3D2()` roots, and ranks accepted actors as `max(1.0, squaredDistance(player, actor) - square(max(extent.x, extent.y)))`. It accepts ranks strictly below the squared threshold and sorts ascending. The extent accessor returns `ExtraPrimitive` primitive data when present, otherwise relevant `MultiBoundMarkerData::halfExtents`, otherwise zero; the BGSPrimitive vector's canonical field name remains unresolved. |
| Depth scene-root producer | `Main::RenderDepth` callsite RVA `0x1323487` | Exact disassembly loops a global registry at `0x34232C0`, with count `0x34232D0`, 16-byte entries, and each scene-root pointer at entry `+0x08`, then calls the direct scene-root accumulator entry. |
| Directional-shadow scene-root producer | function RVA `0x134C220`, PDB alias `BSShadowDirectionalLight::BSShadowLight__Accumulate__focus__1arg`; callsite `0x134C343` | The path configures accumulator render mode `0x0E`, reads the same scene-root registry, and reuses `0x1308750`. This corroborates shared scene-root distribution without importing an SSE control-flow identity. |
| Main post-cull drain | callback RVA `0x1321830`, callsite `0x13218A5` | The callback passes the main culling-process global and primary accumulator global to `0xD9A190`, with no secondary accumulator. This is a concrete Main-owned consumer of the post-cull bridge. |
| Main list-drain jobs | `MainAccum` RVA `0x1322130`; callback RVA `0x13163F0`; bridge callsite `0x1316441` | `MainAccum` installs the same callback for two jobs at `0x13221E8` and `0x1322274`. The callback iterates a job-owned culling-process list and sends each entry to `0xD9A190`. |
| Mode-dependent culling worker | RVA `0x1316190`; bridge callsite `0x1316207` | Mode zero obtains a culling process, drains it to a primary and optional secondary accumulator, then finalizes it. Mode one follows the alternate cleanup route. A whole-image scan found no ordinary direct call, stored image VA, or RIP-relative address-take; only exception/unwind metadata names the body. It remains a possibly indirect or dormant worker, and its callback registration site is not invented. |
| Single-geometry accumulator admission | RVA `0x1308570` | Exact disassembly accepts accumulator, geometry, and auxiliary argument; obtains the geometry's shader property; either queues a deferred entry or indexes the 31-entry render-mode geometry-handler table at `0x13423E70`. This is the concrete handoff from culling output into render-pass construction. |
| Deferred-geometry drain | RVA `0x13087D0` | Exact disassembly walks the accumulator's 16-byte deferred entries at `+0xC0`, using counts at `+0xD0/+0xD8`, and invokes the same render-mode table for each geometry/property/auxiliary tuple. |
| Render-mode table initialization | RVA `0x12CC970`; selector `0x12CEB00`; table `0x13423E70` | The initializer installs `0x13089A0` as the default across 31 entries before specialized overrides. The selector chooses geometry and pre-resolve handlers with entry-zero fallback. `Main::MainLoop` calls the initializer at `0x5B80D1`; accumulator pre-resolve dispatch calls the selector at `0x1309305`. |
| Post-depth accumulator stage | `BSShaderAccumulator::FinishAccumulatingPostResolveDepth`, RVA `0x1309360`, vfunc `0x2B` | Header slot and exact VR vtable pointer agree with the target PDB alias. |
| Accumulation stage order | `BSShaderAccumulator::FinishAccumulating`, `0x1308710`, vfunc `0x26`; pre-resolve/dispatch `0x13092F0`, vfunc `0x2A` | The exact VR body invokes slot `0x2A` and then tail-calls slot `0x2B`; this establishes pre/post order without assigning the internal render-mode handlers prematurely. |
| Ordinary lighting pass selection | `BSLightingShaderProperty::GetRenderPasses`, `0x13045F0`, vfunc `0x2A` | Exact VR vtable pointer, header signature, and disassembly agree. It repeatedly invokes a lighting pass prepare helper at `0x12CA050`. |
| Geometry-to-pass registration | accumulator routine `0x13089A0` | Exact disassembly takes geometry and shader property, invokes property vfunc `0x2A`, walks `BSRenderPass::next`, classifies each pass by enum/hint, and routes it through the accumulator's batch renderer. It is the default handler for most render-mode entries, selected by both immediate and deferred geometry paths. |
| Render-pass identity | `BSRenderPass`, size `0x48` | Exact target layout joins shader, shader property, geometry, pass enum, accumulation hint, light array, list links, and cache-pool identity. Lighting uses an allocate-or-reuse helper; generic `BSShader::MakeRenderPass` (`0x13401E0`, relocation `100717`) uses a separate thread-local pool path and initializer at `0x1340110`. |
| Accumulator render stages | `RenderEffects` `0x130A240`; `RenderBatches` `0x130BA60`; `RenderPersistentPassList` `0x1347320` | Target relocation data and PDB coverage establish stable stage boundaries. |
| Batch renderer | `BSBatchRenderer`, size `0x108`, vtable RVA `0x1906E88` | Target layout includes technique-indexed pass groups, active-pass list, geometry groups, and alpha group. |
| Batch submission | `RenderActivePassRange` `0x1348B70`; `RenderBatches` `0x1349270`; `SetupAndDrawPass` `0x1349680` | Exact vtable/relocation/PDB evidence establishes the target-specific submission spine. |
| Batch registration | `RegisterPassSorted` `0x1348200`, vfunc `0x01`; `RegisterPass` `0x13482E0`, vfunc `0x02` | Exact vtable reads and disassembly show both resolve technique ID through `0x1349E00`, set the pass-group valid bit, and link the `BSRenderPass`; the sorted path additionally orders by shader-property state. |
| Geometry-group routing | selector `0x134A480`; group registration `0x1347150` | The selector indexes `BSBatchRenderer::geometryGroups` and tail-calls the registration routine, which links the pass into the group's simple list or grouped pass structure. |
| Depth producer | `Main::RenderDepth`, RVA `0x1323250`; downscale callsite `0x13235CF`; OBB callsite `+0x3B1` at `0x1323601` | At phase entry, the engine target state selects `RENDER_TARGET_DEPTHSTENCIL::kMAIN` and sets colour slots 0-2 to `NONE`. The phase later invokes `Main::DownscaleDepthBuffer` (`0x1322D80`) under the depth-downscale gate. Successful setup sets `0x1ED4180`; that flag gates `BSOBBOcclusionTestingShader::Draw` at `0x13561F0`, using the draw object from global `0x36F1870` and argument zero. This is the verified current-frame visibility-result producer boundary. |
| OBB visibility setup | `BSOBBOcclusionTestingShader::SetupTechnique`, RVA `0x1355A70`, vfunc `0x02` | The exact target body calls `BSShader::BeginTechnique` with vertex descriptor zero, pixel descriptor zero, and pixel skipping disabled, then configures renderer state and constants. This is a family-scoped `OBBOcclusionTesting` `0/0` technique, not the numerically identical ImageSpace `0/0` technique captured at the main menu. |
| OBB visibility registration | `RegisterGeometry` `0x13560A0`; record writer `0x1356320` | `BSGeometryListCullingProcess::AppendNonAccum` calls registration at `0xDA1957`, stores the returned result pointer in `NiAVObject+0x128`, and stamps `NiAVObject+0x130` with the current frame. Registration atomically reserves an index up to `0x1000`, invokes the record writer at `0x135618D`, writes one `0x40`-byte transformed-bound record into the `0x40000`-byte buffer at `+0xB8`, and clears the corresponding active result slot selected through `+0xC0` and `+0xD0/+0xD8`. |
| OBB visibility draw | `BSOBBOcclusionTestingShader::Draw`, RVA `0x13561F0`; submission helper `0xDBDF60`; `RestoreTechnique` `0x1355EA0` | The draw reads the registered count at `+0xB0` and nested resources owned from `+0xF0`, sets up the OBB technique, and submits one indexed-instanced triangle-list box: 12 triangles and 36 indices. `Main::RenderDepth` saves `BSGraphics::Renderer::GetDrawStereo()` at `0x3181708`, forces it true around this call, then restores it; the helper therefore submits exactly `2N` instances in this producer path. Draw then invokes virtual slot `0x03`; `RestoreTechnique` unbinds the OBB SRV/UAV and copies the GPU-written `+0x100` result buffer into its staging buffer at `0x1356057`. CSX wraps the draw call to mark the current-frame result ready and deliberately leaves the VR OBB vertex and pixel shaders vanilla. The Breezehome live join confirms capture-local D3D object/view identities, a typed immediate context, depth-only target state, 794 objects, 1,588 instances, and the primary-to-staging copy. Exact depth array-slice ownership, stable cross-run identities, accepted eye submissions, and engine completion/Map latency remain unresolved. |
| OBB resource construction | `BSLightingShader::CreateOBBOcclusionTestingShader` `0x1355770`; wrapper factory `0xDC0CA0` | The constructor publishes singleton `0x36F1870`, allocates and zeros 4096 CPU records (`+0xB8`, `0x40` bytes each) and two 4096-entry uint result arrays (`+0xD0/+0xD8`), then creates three structured buffers. `+0xF8` is dynamic, CPU-write, SRV-only, with 4096 `0x40`-byte elements. `+0x100` is a default SRV/UAV uint buffer with a read/write staging companion. `+0x108` is a default SRV-only uint buffer initialized from zeroed `+0xD0` and used as the result-reset source. The misleading PDB public `CreategBSOBBOcclusionTestingShader` at `0x13559F0` is the destructor body, not this constructor. |
| OBB result lifecycle | `CopyOcclusionTestResults` `0x1356270`; `MapOcclusionBox` `0x13562D0`; upload `0xDC0E90`; staging read `0xDC11B0`; reset copy `0xDC0F80` | At the tail of `Main::RenderFirstPersonView` (`0x13218C0`), the enabled path copies results at `0x132208B`, publishes the frame/result-buffer selection and atomically reset-swaps the pending and registered counts, clears the mapped-state byte, performs intervening view work, then maps again at `0x13220FE`. `Main::RenderDepth` has a second map call at `0x132336A`, before depth accumulation and the OBB draw. The prior `RestoreTechnique` copy has refreshed `+0x100` staging from its GPU primary. Exact disassembly of `0xDC11B0` proves an immediate-context `Map(staging, 0, D3D11_MAP_READ, 0, ...)`, followed by the `N x 4` copy into the active `+0xD0/+0xD8` CPU array and `Unmap`. Because `D3D11_MAP_FLAG_DO_NOT_WAIT` is absent, the D3D11 synchronization contract makes successful Map return the CPU-visibility boundary for commands affecting that staging allocation; it is not a whole-frame GPU-completion claim. The function then resets `+0x100` primary from the zero-initialized `+0x108` source. `MapOcclusionBox` maps `+0xF8` with `D3D11_MAP_WRITE_DISCARD`, uploads `N x 0x40` bytes from `+0xB8`, unmaps, and sets `+0xC9`. Resource classes, copy directions, ordering, and the per-allocation CPU completion boundary are exact; measured Map stall latency remains unresolved. |

### VR render-mode geometry-handler table

The table at `0x13423E70` has 31 entries. Initialization first fills every
entry with the ordinary geometry-to-pass handler at `0x13089A0`, then replaces
the following numeric modes. CommonLib names are included only where its pinned
VR header supplies one; unnamed numeric modes remain unnamed.

| Numeric mode(s) | Pinned CommonLib name(s) | Handler | Confirmed behavior |
|---|---|---|---|
| `12-17` | ShadowMask, ShadowMapPlain, ShadowMapClamped, ShadowMapPb, unnamed, ShadowMapCube | `0x13090A0` | Eligibility filters, property `GetRenderPasses_ShadowMapOrMask`, then accumulation-hint routing. |
| `18-19` | LocalMap, unnamed | `0x1308E50` | Eligibility filters, property `GetRenderPasses_LocalMap`, plus explicit geometry-group routing. |
| `20` | LodLandscapePass | `0x1308D90` | Add geometry to an accumulator-owned collection through `0x130C630`. |
| `21` | WaterReflectionPass | `0x1308DC0` | Return accepted without requesting or registering passes. |
| `23` | AlphaTransparencyShadowPass | `0x1308DD0` | Property `GetRenderPasses`; register only passes with nonzero pass enum through `BSBatchRenderer::RegisterPass`. |
| `24` | SunGlintRefractionPass | `0x130B1C0` | Require the relevant property flag, call `GetRenderPasses`, filter pass enum and accumulation hint, then route accepted passes to geometry group 16. |
| `25` | VolumetricLightingPass | `0x1309030` | When accumulator state permits, property `GetWaterFogPassList`, then sorted pass registration. |
| `26-27` | Occlusion, unnamed | `0x13091C0` | Property `GetRenderPasses`; retain nonzero pass enums with accumulation hint 6 or 7 and route to geometry group 0 or 1. |
| `29` | unnamed | `0x1309240` | Property `GetRenderPasses_Occlusion`; route every returned pass to geometry group 14. |

There is a target/header tension worth preserving: the pinned CommonLib enum
names numeric mode `28` as `kPrecipitationOcclusionMap`, but target mode `28`
retains the ordinary handler while unnamed mode `29` receives the specialized
`GetRenderPasses_Occlusion` route. The table indices and virtual calls are exact;
the semantic resolution is not.

### VR pre-resolve handler table

The parallel 31-entry table at `0x13423F70` is now structurally mapped. Its
default is `0x13092C0`; `FinishAccumulatingPreResolveDepth` selects a nonzero
mode from this table and falls back to entry zero, while mode zero directly
invokes `0x13093A0`.

| Numeric mode(s) | Handler | Confirmed boundary behavior |
|---|---|---|
| `12-17` | `0x130AF00` | Shared shadow-mode pre-resolve route; detailed batch/resource semantics remain to be decomposed. |
| `20` | `0x130AE90` | Invoke `0x130BEE0` when accumulator state is present, then fold a pending global condition into render flags. |
| `22` | `0x130A970` | Large BloodDecalPass-specific route; exact internal phases remain to be decomposed. |
| `23` | `0x130AED0` | No-op return. |
| `24` | `0x130B260` | Consume the batch-renderer group at `+0xF0` through persistent-pass rendering or `RenderBatches` with geometry-group index 16. |
| `25` | `0x130AEE0` | If accumulator state is present, tail-call `0x130BBB0` with a zero secondary argument. |
| `26-27` | `0x130B090` | Consume batch-renderer groups at `+0x70/+0x78` through persistent-pass rendering or `RenderBatches`, toggling the associated global phase flag. |
| `29` | `0x130B160` | Consume the batch-renderer group at `+0xE0` through persistent-pass rendering or `RenderBatches` with geometry-group index 14. |

This closes table membership and selection. It does not yet assign semantic
names to `0x130BEE0`, `0x130BBB0`, or every internal phase of the shared shadow
and blood-decal handlers.

These facts are recorded in engine-map revision 17. They establish a safe
instrumentation spine:

```text
Main scene preparation 0x5B9E90
  -> ranked-reference candidate builder 0x5BC2E0
  -> ProcessLists::highActorHandles enumeration 0x5C1720
  -> eligible actor Get3D2 roots ranked by adjusted squared player distance
  -> PlayerCharacter::Get3D1(false) plus ranked actor Get3D2 roots
  -> registry rebuild 0x5BC140 / append 0x5C4770 / clear 0x5C4C10
  -> target-global ranked scene-root registry 0x34232C0 / count 0x34232D0
  -> Main::RenderDepth or directional-shadow accumulation
  -> scene-root traversal entry 0x1308750
  -> recursive node walker 0x130C0F0 / child helper 0x130C360
  -> BSShaderAccumulator single-geometry admission 0x1308570

Ni/BSCullingProcess traversal
  -> BSGeometryListCullingProcess::AppendNonAccum
  -> BSOBBOcclusionTestingShader::RegisterGeometry
  -> OBB candidate result pointer at NiAVObject+0x128 / frame at +0x130
  -> BSCullingProcess objectArray / alphaGroups
  -> Main callback 0x1321830, MainAccum list callback 0x13163F0,
     or mode-dependent worker 0x1316190
  -> post-cull accumulator bridge 0xD9A190 / alpha-group drain 0xD9AF10
  -> BSShaderAccumulator single-geometry admission 0x1308570

BSParabolicCullingProcess::AppendNonAccum
  -> inherited object array and/or back-hemisphere accumulator
  -> BSShaderAccumulator single-geometry admission 0x1308570
  -> render-mode geometry-handler table 0x13423E70
  -> default geometry-to-pass handler 0x13089A0
  -> BSLightingShaderProperty::GetRenderPasses
  -> BSRenderPass creation/population
  -> BSBatchRenderer::RegisterPass[Sorted]
  -> BSShaderAccumulator accumulation completion
  -> BSShaderAccumulator render stage
  -> BSBatchRenderer pass range / SetupAndDrawPass
  -> family technique and geometry setup
  -> D3D11 Draw*
  -> accepted eye submission

Main::RenderDepth OBB producer
  -> engine depth-stencil target MAIN; colour target slots 0-2 NONE
  -> WRITE_DISCARD upload of N x 0x40 CPU records into dynamic +0xF8 SRV
  -> Main::DownscaleDepthBuffer
  -> downscaled-depth OBB readiness flag 0x1ED4180
  -> force Renderer draw-stereo true
  -> BSOBBOcclusionTestingShader::SetupTechnique (family-scoped 0/0)
  -> bind +0xF8 SRV and +0x100 UAV
  -> one 36-index indexed-instanced draw with 2N instances
  -> RestoreTechnique copies +0x100 primary GPU results to +0x100 staging
  -> restore prior Renderer draw-stereo value
  -> CSX current-frame visibility-result-ready boundary

Main::RenderFirstPersonView OBB result handoff
  -> READ-map +0x100 staging and copy N x uint32 into active +0xD0/+0xD8 CPU results
  -> reset +0x100 primary from zero-initialized +0x108 source
  -> publish result-buffer/frame selection and reset-swap pending/registered counts
  -> clear mapped-state byte
  -> perform intervening view work
  -> WRITE_DISCARD upload +0xB8 records into +0xF8 for the next epoch
```

The scene-root registry producer/lifecycle, direct depth/shadow scene-root route,
culling endpoint, Main-owned post-cull callbacks, visible-object and alpha-group
handoff, parabolic back-hemisphere route, ordinary render-mode dispatch, and
render-pass creation/registration/submission path are now target-specific facts.
The owners that feed the separate culling-process inputs, the canonical name of
the BGSPrimitive extent vector, the registration site for the remaining
mode-dependent worker, and specialized pre-resolve semantics remain gaps.

## Prior-art effect

The pinned `skyrim-rendering` 1.6.1170 reconstruction remains useful as a search
index, not a target map:

- Its world scene-list topology is still unverified on VR. The verified
  `AppendNonAccum` boundary gives us a concrete endpoint from which to trace
  callers, but it does not prove main/worker arrays or the same shadow-node,
  cell, portal, water, grass, and LOD distribution.
- Its culling class topology is now partially corroborated. The individual
  VR virtual-function addresses and broad traversal, frustum, append, and bound
  test roles are now mapped. The individual `AlwaysDraw`, preprocess,
  compound-frustum, plane-optimization, recursive traversal, and queueing
  branches still require exact instruction-level classification.
- The older `skyrimse-test` batch reconstruction is more strongly
  corroborated: the target contains the corresponding pass groups, active pass
  list, batch-range and setup/draw stages. Exact Begin/End-pass control flow is
  not yet promoted.

The prior-art catalogue records these distinctions and preserves each source's
runtime provenance.

## Remaining static work

1. Trace the owners that feed the separate culling-process inputs. Resolve the
   callback registration for the remaining mode-dependent worker at
   `0x1316190`; the Main-owned direct and list-drain bridge consumers and the
   high-process actor scene-root source/ranking path are already exact. Preserve
   the primitive-or-marker extent vector's unresolved canonical field name.
2. Resolve the canonical source name, if one exists, for the functionally
   mapped inherited OBB-occlusion bypass (`NiAVObject+0x122`, propagated through
   the low byte of `BSCullingProcess+0x301D6`) and the provisional semantic name
   of cleanup slot `0x1A`. The AlwaysDraw,
   AllPass/AllFail, preprocessed-node, compound-frustum, active-plane,
   AppendVirtual, OnVisible, and accumulated-flag branches are already exact.
3. Reconcile the exact `BSParabolicCullingProcess` target layout with the
   pinned CommonLib declaration, particularly target state at `+0x30240` and
   the deleting-destructor size `0x30248`.
4. Resolve the numeric-mode `28/29` semantic discrepancy and decompose the
   shared shadow (`0x130AF00`), blood-decal (`0x130A970`), `0x130BEE0`, and
   `0x130BBB0` pre-resolve phases; preserve numeric mode provenance wherever
   the pinned CommonLib enum and exact target behavior do not align.
5. Resolve the concrete D3D object/view and depth-view/array-slice identities,
   command-context identity, and qualify measured GPU completion/Map latency around the now-
   decomposed `BSOBBOcclusionTestingShader` lifecycle. Static evidence already
   fixes the structured-buffer descriptors, SRV/UAV/staging roles, copy
   directions, engine target selection (MAIN depth, no colour targets in slots
   0-2), family-scoped `0/0` setup, registration from `AppendNonAccum`,
   4096-instance store, first-person-view read/reset/upload handoff, depth-entry
   upload, 36-index submission, and forced `2N` stereo instance count.
   `resource-cpu-access-v1` now supplies the required Map/Unmap visibility and
   timing observations; loaded-scene live correlation remains pending.
6. Promote only independently corroborated functions and relations into the
   engine map.

## Live closure and next bounded observation

The Breezehome run documented in
[`obb-resource-live-capture-2026-08-29.md`](./obb-resource-live-capture-2026-08-29.md)
now supplies capture-local identities for the OBB engine shader and descriptor
pair, depth-only target state, `+0xF8` input buffer/SRV, `+0x100` result
buffer/UAV/staging, typed immediate context, 36-index `2N` draw, result version,
visibility consumers, result reset destination, and following-frame CPU
readback decisions. It confirms 794 registered objects, 1,588 submitted
instances, and 721 covered lighting-object decisions without relying on pointer
equality across captures.

The remaining bounded live slice should add, rather than repeat, evidence:

- one explicit scene epoch marker before culling traversal;
- bounded entry/exit observations at `0x1308750`, `0x130C360`, `0xD9A190`,
  `0x136F4F0`, `0x1308570`, and the selected render-mode handler, including
  producer route, numeric mode, parabolic routing argument, and
  immediate-versus-deferred path;
- `NiAVObject`, geometry, shader property/material, accumulator, and
  `BSRenderPass` observation IDs at verified boundaries;
- per-boundary accepted/rejected counts for the selected opaque candidate;
- a live ID for the `+0x108` reset source, measured engine Map stall latency,
  and accepted eye-submit events under both null-HMD and physical SteamVR
  routes; the Map call's synchronization semantics no longer require live
  rediscovery;
- capture segmentation or event-class filtering sufficient to retain complete
  command state and identity catalogues in a frame exceeding 20,000 events.

The scenario should remain a fixed, ordinary opaque `BSLightingShader` object
in the known Breezehome save. World scene-list distribution should be examined
after this object chain is complete, not by enabling all object families at
once.

The synchronization classification above uses the official
[`ID3D11DeviceContext::Map`](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)
and
[`D3D11_MAP_FLAG`](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_map_flag)
contracts. Windows' allocation-usage contract further specifies that a
synchronizing CPU access waits when `DO_NOT_WAIT` is absent. This platform
contract is paired with target-binary disassembly; it is not inferred from the
diagnostic readback ring.

## Timing-evidence contamination gate

An unexplained frame-rate throttle does not invalidate identity or command-order
observations, but it invalidates performance conclusions. A timing-sensitive
capture is not eligible for comparison unless its retained report records:

- observed FPS and frame-time distribution;
- Skyrim's configured cap and `iPresentInterval` state;
- HMD/compositor refresh and runtime route;
- reprojection, motion-smoothing, spacewarp, or frame-doubling mode;
- driver or external limiter state when known;
- GPU utilization and whether the run was CPU-, GPU-, compositor-, or
  limiter-bound;
- capture categories, screenshot/sequence load, profiler sampling, and measured
  collector overhead;
- whether menus, pause/fader state, shader compilation, or background asset
  loading were active.

If observed FPS is materially below both the configured cap and demonstrated
GPU capability and no limiter is identified, the run is labelled
`timing-contaminated-unexplained-throttle`. Its structural render-map evidence
may still be used; shader or feature cost deltas may not.

Until this context is emitted by the capture producer, it belongs in the
capture report and under `capture-manifest.json`'s namespaced
`extensions.csx.performanceContext`. Missing fields remain `unknown`; they are
never inferred from a screenshot overlay alone.
